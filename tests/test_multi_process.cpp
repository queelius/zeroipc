#include <catch2/catch_test_macros.hpp>
#include "posix_shm.h"
#include "shm_array.h"
#include "shm_queue.h"
#include "shm_atomic.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <chrono>
#include <thread>

TEST_CASE("Multi-process shared memory", "[multi-process]") {
    const std::string shm_name = "/test_multiproc";
    const size_t shm_size = 10 * 1024 * 1024;
    
    // Clean up any leftover shared memory from previous runs
    shm_unlink(shm_name.c_str());

    SECTION("Parent-child data sharing") {
        pid_t pid = fork();
        REQUIRE(pid != -1);

        if (pid == 0) {  // Child process
            try {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                posix_shm shm(shm_name);  // Open existing
                
                shm_array<int> arr(shm, "shared_data");
                REQUIRE(arr.size() == 10);
                REQUIRE(arr[0] == 42);
                REQUIRE(arr[9] == 99);
                
                // Modify data
                arr[5] = 555;
                
                exit(0);
            } catch (...) {
                exit(1);
            }
        } else {  // Parent process
            posix_shm shm(shm_name, shm_size);
            shm_array<int> arr(shm, "shared_data", 10);
            
            arr[0] = 42;
            arr[9] = 99;
            
            int status;
            waitpid(pid, &status, 0);
            REQUIRE(WIFEXITED(status));
            REQUIRE(WEXITSTATUS(status) == 0);
            
            // Check child's modification
            REQUIRE(arr[5] == 555);
            
            // Clean up
            shm.unlink();
        }
    }

    SECTION("Producer-consumer with queue") {
        const int num_items = 100;
        
        pid_t pid = fork();
        REQUIRE(pid != -1);

        if (pid == 0) {  // Child - Consumer
            try {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                posix_shm shm(shm_name);
                shm_queue<int> queue(shm, "prod_cons_queue");
                
                int count = 0;
                auto start = std::chrono::steady_clock::now();
                while (count < num_items) {
                    auto val = queue.dequeue();
                    if (val.has_value()) {
                        REQUIRE(*val == count);
                        count++;
                    } else {
                        std::this_thread::yield();
                    }
                    
                    // Timeout protection
                    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
                        exit(2);  // Timeout
                    }
                }
                
                exit(0);
            } catch (...) {
                exit(1);
            }
        } else {  // Parent - Producer
            posix_shm shm(shm_name, shm_size);
            shm_queue<int> queue(shm, "prod_cons_queue", 10);
            
            for (int i = 0; i < num_items; ++i) {
                while (!queue.enqueue(i)) {
                    std::this_thread::yield();
                }
            }
            
            int status;
            waitpid(pid, &status, 0);
            REQUIRE(WIFEXITED(status));
            REQUIRE(WEXITSTATUS(status) == 0);
            
            // Clean up
            shm.unlink();
        }
    }

    SECTION("Atomic counter across processes") {
        const int num_children = 4;
        const int increments_per_child = 1000;
        
        // Create shared memory with atomic counter
        {
            posix_shm shm(shm_name, shm_size);
            shm_atomic_int counter(shm, "counter", 0);
        }
        
        // Fork multiple children
        for (int i = 0; i < num_children; ++i) {
            pid_t pid = fork();
            REQUIRE(pid != -1);
            
            if (pid == 0) {  // Child
                try {
                    posix_shm shm(shm_name, 0);  // Explicitly pass 0 to attach
                    shm_atomic_int counter(shm, "counter");
                    
                    for (int j = 0; j < increments_per_child; ++j) {
                        counter++;
                    }
                    
                    exit(0);
                } catch (...) {
                    exit(1);
                }
            }
        }
        
        // Parent waits for all children
        for (int i = 0; i < num_children; ++i) {
            int status;
            wait(&status);
            REQUIRE(WIFEXITED(status));
            REQUIRE(WEXITSTATUS(status) == 0);
        }
        
        // Check final counter value
        posix_shm shm(shm_name);
        shm_atomic_int counter(shm, "counter");
        REQUIRE(counter.load() == num_children * increments_per_child);
        
        // Clean up
        shm.unlink();
    }

    SECTION("Multiple data structures discovery") {
        pid_t pid = fork();
        REQUIRE(pid != -1);

        if (pid == 0) {  // Child
            try {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                posix_shm shm(shm_name);
                
                // Discover all structures by name
                shm_array<double> sensors(shm, "sensors");
                shm_queue<uint32_t> events(shm, "events");
                shm_atomic_bool flag(shm, "ready");
                
                REQUIRE(sensors.size() == 100);
                REQUIRE(events.capacity() == 50);
                REQUIRE(flag.load() == true);
                
                // Use the structures
                REQUIRE(sensors[0] == 3.14159);
                
                uint32_t event;
                REQUIRE(events.dequeue(event));
                REQUIRE(event == 0xDEADBEEF);
                
                exit(0);
            } catch (...) {
                exit(1);
            }
        } else {  // Parent
            posix_shm shm(shm_name, shm_size);
            
            // Create multiple structures
            shm_array<double> sensors(shm, "sensors", 100);
            sensors[0] = 3.14159;
            
            shm_queue<uint32_t> events(shm, "events", 50);
            events.enqueue(0xDEADBEEF);
            
            shm_atomic_bool flag(shm, "ready", true);
            
            int status;
            waitpid(pid, &status, 0);
            REQUIRE(WIFEXITED(status));
            REQUIRE(WEXITSTATUS(status) == 0);
            
            // Clean up
            shm.unlink();
        }
    }
}

TEST_CASE("Process crash resilience", "[multi-process][resilience]") {
    const std::string shm_name = "/test_resilience";
    
    SECTION("Shared memory survives process crash") {
        pid_t pid = fork();
        REQUIRE(pid != -1);
        
        if (pid == 0) {  // Child - will "crash"
            try {
                posix_shm shm(shm_name, 1024 * 1024);
                shm_array<int> arr(shm, "persistent", 5);
                arr[0] = 12345;
                
                // Simulate crash
                _exit(0);  // Immediate exit without cleanup
            } catch (...) {
                exit(1);
            }
        } else {  // Parent
            int status;
            waitpid(pid, &status, 0);
            
            // Now access the shared memory left by crashed child
            posix_shm shm(shm_name);
            shm_array<int> arr(shm, "persistent");
            REQUIRE(arr[0] == 12345);
            
            // Clean up
            shm.unlink();
        }
    }
}