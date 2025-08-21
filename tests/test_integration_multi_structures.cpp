#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "posix_shm.h"
#include "shm_array.h"
#include "shm_queue.h"
#include "shm_stack.h"
#include "shm_hash_map.h"
#include "shm_bitset.h"
#include "shm_ring_buffer.h"
#include "shm_object_pool.h"
#include "shm_set.h"
#include <thread>
#include <vector>
#include <random>

TEST_CASE("Multiple data structure types in same shared memory", "[integration][multi-type]") {
    const std::string shm_name = "/test_multi_structures";
    shm_unlink(shm_name.c_str());
    
    SECTION("Create one of each data structure type") {
        posix_shm shm(shm_name, 20 * 1024 * 1024);
        
        // Create various data structures
        shm_array<double> array(shm, "sensor_data", 1000);
        shm_queue<int> queue(shm, "event_queue", 500);
        shm_stack<float> stack(shm, "undo_stack", 200);
        shm_hash_map<int, double> map(shm, "config_map", 100);
        shm_hash_map<uint32_t, bool> set(shm, "id_set", 150);  // Use hash_map as a set
        shm_bitset<1024> bits(shm, "feature_flags");
        
        // Populate each structure
        for (size_t i = 0; i < 10; i++) {
            array[i] = i * 1.1;
        }
        
        for (int i = 0; i < 5; i++) {
            REQUIRE(queue.enqueue(i * 10));
        }
        
        for (int i = 0; i < 3; i++) {
            REQUIRE(stack.push(i * 0.5f));
        }
        
        map.insert(1, 3.14);
        map.insert(2, 2.718);
        map.insert(3, 1.414);
        
        set.insert(100, true);
        set.insert(200, true);
        set.insert(300, true);
        
        bits.set(10, true);
        bits.set(20, true);
        bits.set(30, true);
        
        // Verify all structures work correctly
        REQUIRE(array[5] == Catch::Approx(5.5));
        REQUIRE(queue.size() == 5);
        REQUIRE(stack.size() == 3);
        REQUIRE(map.size() == 3);
        REQUIRE(set.size() == 3);
        REQUIRE(bits.count() == 3);
        
        // Verify data integrity
        REQUIRE(queue.dequeue().value_or(-1) == 0);
        REQUIRE(stack.pop().value_or(-1.0f) == Catch::Approx(1.0f));
        
        auto* map_val = map.find(1);
        REQUIRE(map_val != nullptr);
        REQUIRE(*map_val == Catch::Approx(3.14));
        
        REQUIRE(set.contains(200) == true);
        REQUIRE(bits.test(20) == true);
        
        shm.unlink();
    }
    
    SECTION("Multiple instances of each type") {
        posix_shm shm(shm_name, 50 * 1024 * 1024);
        
        // Create multiple instances of each type
        std::vector<std::unique_ptr<shm_array<int>>> arrays;
        std::vector<std::unique_ptr<shm_queue<double>>> queues;
        std::vector<std::unique_ptr<shm_stack<uint32_t>>> stacks;
        std::vector<std::unique_ptr<shm_hash_map<int, int>>> maps;
        
        // Create 3 instances of each type
        for (int i = 0; i < 3; i++) {
            arrays.push_back(std::make_unique<shm_array<int>>(
                shm, "array_" + std::to_string(i), 100));
            queues.push_back(std::make_unique<shm_queue<double>>(
                shm, "queue_" + std::to_string(i), 50));
            stacks.push_back(std::make_unique<shm_stack<uint32_t>>(
                shm, "stack_" + std::to_string(i), 30));
            maps.push_back(std::make_unique<shm_hash_map<int, int>>(
                shm, "map_" + std::to_string(i), 40));
        }
        
        // Populate all structures
        for (int i = 0; i < 3; i++) {
            // Arrays
            for (int j = 0; j < 10; j++) {
                (*arrays[i])[j] = i * 100 + j;
            }
            
            // Queues
            for (int j = 0; j < 5; j++) {
                REQUIRE(queues[i]->enqueue(i + j * 0.1));
            }
            
            // Stacks
            for (int j = 0; j < 4; j++) {
                REQUIRE(stacks[i]->push(i * 1000 + j));
            }
            
            // Maps
            for (int j = 0; j < 6; j++) {
                REQUIRE(maps[i]->insert(j, i * 10 + j));
            }
        }
        
        // Verify all structures
        for (int i = 0; i < 3; i++) {
            REQUIRE((*arrays[i])[5] == i * 100 + 5);
            REQUIRE(queues[i]->size() == 5);
            REQUIRE(stacks[i]->size() == 4);
            REQUIRE(maps[i]->size() == 6);
            
            auto* val = maps[i]->find(3);
            REQUIRE(val != nullptr);
            REQUIRE(*val == i * 10 + 3);
        }
        
        shm.unlink();
    }
    
    SECTION("Producer-consumer with mixed structures") {
        posix_shm shm(shm_name, 10 * 1024 * 1024);
        
        shm_queue<int> work_queue(shm, "work_queue", 100);
        shm_stack<int> result_stack(shm, "results", 100);
        shm_hash_map<int, int> status_map(shm, "status", 100);
        shm_array<int> counters(shm, "counters", 10);
        
        // Initialize counters
        for (int i = 0; i < 10; i++) {
            counters[i] = 0;
        }
        
        const int num_items = 50;
        
        // Producer thread
        std::thread producer([&]() {
            for (int i = 0; i < num_items; i++) {
                while (!work_queue.enqueue(i)) {
                    std::this_thread::yield();
                }
                status_map.insert(i, 0);  // 0 = queued
                counters[0]++;  // Items produced
            }
        });
        
        // Consumer thread
        std::thread consumer([&]() {
            for (int i = 0; i < num_items; i++) {
                std::optional<int> item;
                while (!(item = work_queue.dequeue())) {
                    std::this_thread::yield();
                }
                
                // Process item
                int result = (*item) * 2;
                result_stack.push(result);
                status_map.update(*item, 1);  // 1 = processed
                counters[1]++;  // Items consumed
            }
        });
        
        producer.join();
        consumer.join();
        
        // Verify results
        REQUIRE(counters[0] == num_items);  // All produced
        REQUIRE(counters[1] == num_items);  // All consumed
        REQUIRE(work_queue.empty());
        REQUIRE(result_stack.size() == num_items);
        
        // Check all items were processed
        for (int i = 0; i < num_items; i++) {
            auto* status = status_map.find(i);
            REQUIRE(status != nullptr);
            REQUIRE(*status == 1);
        }
        
        shm.unlink();
    }
    
    SECTION("Persistence test with all structure types") {
        // First process: create and populate
        {
            posix_shm shm(shm_name, 10 * 1024 * 1024);
            
            shm_array<int> arr(shm, "persist_array", 100);
            shm_queue<double> queue(shm, "persist_queue", 50);
            shm_stack<uint64_t> stack(shm, "persist_stack", 30);
            shm_hash_map<int, float> map(shm, "persist_map", 40);
            shm_hash_map<int, bool> set(shm, "persist_set", 50);  // Use hash_map as a set
            shm_bitset<256> bits(shm, "persist_bits");
            
            // Populate
            arr[0] = 42;
            arr[99] = 999;
            
            queue.enqueue(3.14);
            queue.enqueue(2.718);
            
            stack.push(0xDEADBEEF);
            stack.push(0xCAFEBABE);
            
            map.insert(10, 1.23f);
            map.insert(20, 4.56f);
            
            set.insert(111, true);
            set.insert(222, true);
            
            bits.set(100, true);
            bits.set(200, true);
        }
        
        // Second process: open and verify
        {
            posix_shm shm(shm_name);
            
            shm_array<int> arr(shm, "persist_array");
            shm_queue<double> queue(shm, "persist_queue");
            shm_stack<uint64_t> stack(shm, "persist_stack");
            shm_hash_map<int, float> map(shm, "persist_map");
            shm_hash_map<int, bool> set(shm, "persist_set");  // Use hash_map as a set
            shm_bitset<256> bits(shm, "persist_bits");
            
            // Verify all data persisted correctly
            REQUIRE(arr[0] == 42);
            REQUIRE(arr[99] == 999);
            
            REQUIRE(queue.size() == 2);
            REQUIRE(queue.dequeue().value_or(0.0) == Catch::Approx(3.14));
            
            REQUIRE(stack.size() == 2);
            REQUIRE(stack.pop().value_or(0) == 0xCAFEBABE);
            
            auto* map_val1 = map.find(10);
            REQUIRE(map_val1 != nullptr);
            REQUIRE(*map_val1 == Catch::Approx(1.23f));
            
            auto* map_val2 = map.find(20);
            REQUIRE(map_val2 != nullptr);
            REQUIRE(*map_val2 == Catch::Approx(4.56f));
            
            REQUIRE(set.contains(111) == true);
            REQUIRE(set.contains(222) == true);
            
            REQUIRE(bits.test(100) == true);
            REQUIRE(bits.test(200) == true);
            
            shm.unlink();
        }
    }
    
    SECTION("Memory layout verification") {
        posix_shm shm(shm_name, 10 * 1024 * 1024);
        
        // Create structures in specific order
        shm_array<uint64_t> arr1(shm, "layout_arr1", 100);
        shm_queue<int> queue1(shm, "layout_queue1", 50);
        shm_stack<double> stack1(shm, "layout_stack1", 75);
        shm_hash_map<int, int> map1(shm, "layout_map1", 60);
        shm_array<float> arr2(shm, "layout_arr2", 200);
        shm_queue<uint32_t> queue2(shm, "layout_queue2", 100);
        
        // All structures should be properly aligned and not overlap
        // Fill with test data
        for (size_t i = 0; i < 100; i++) {
            arr1[i] = static_cast<uint64_t>(i) * 0xDEADBEEFULL;
        }
        
        for (int i = 0; i < 25; i++) {
            REQUIRE(queue1.enqueue(i * 111));
        }
        
        for (int i = 0; i < 20; i++) {
            REQUIRE(stack1.push(i * 1.414));
        }
        
        for (int i = 0; i < 30; i++) {
            REQUIRE(map1.insert(i, i * i));
        }
        
        for (size_t i = 0; i < 200; i++) {
            arr2[i] = i * 0.1f;
        }
        
        for (int i = 0; i < 40; i++) {
            REQUIRE(queue2.enqueue(i * 0xABCD));
        }
        
        // Verify all data is intact (no memory corruption)
        REQUIRE(arr1[50] == static_cast<uint64_t>(50) * 0xDEADBEEFULL);
        REQUIRE(queue1.size() == 25);
        REQUIRE(stack1.size() == 20);
        REQUIRE(map1.size() == 30);
        REQUIRE(arr2[100] == Catch::Approx(10.0f));
        REQUIRE(queue2.size() == 40);
        
        // Dequeue and verify values
        REQUIRE(queue1.dequeue().value_or(-1) == 0);
        REQUIRE(stack1.pop().value_or(-1.0) == Catch::Approx(19 * 1.414));
        auto* map_val = map1.find(15);
        REQUIRE(map_val != nullptr);
        REQUIRE(*map_val == 225);
        REQUIRE(queue2.dequeue().value_or(0) == 0);
        
        shm.unlink();
    }
}

TEST_CASE("Stress test with many structures", "[integration][stress]") {
    const std::string shm_name = "/test_stress_multi";
    shm_unlink(shm_name.c_str());
    
    SECTION("Maximum structures in limited memory") {
        // Use smaller shared memory to test limits
        posix_shm shm(shm_name, 5 * 1024 * 1024);
        
        std::vector<std::unique_ptr<shm_array<int>>> arrays;
        std::vector<std::unique_ptr<shm_queue<int>>> queues;
        std::vector<std::unique_ptr<shm_stack<int>>> stacks;
        
        // Try to create as many structures as possible
        int array_count = 0, queue_count = 0, stack_count = 0;
        
        // Create arrays until we run out of space
        for (int i = 0; i < 100; i++) {
            try {
                arrays.push_back(std::make_unique<shm_array<int>>(
                    shm, "arr_" + std::to_string(i), 100));
                array_count++;
            } catch (...) {
                break;
            }
        }
        
        // Create queues
        for (int i = 0; i < 100; i++) {
            try {
                queues.push_back(std::make_unique<shm_queue<int>>(
                    shm, "que_" + std::to_string(i), 50));
                queue_count++;
            } catch (...) {
                break;
            }
        }
        
        // Create stacks
        for (int i = 0; i < 100; i++) {
            try {
                stacks.push_back(std::make_unique<shm_stack<int>>(
                    shm, "stk_" + std::to_string(i), 30));
                stack_count++;
            } catch (...) {
                break;
            }
        }
        
        // Should have created at least some of each
        REQUIRE(array_count > 0);
        REQUIRE(queue_count > 0);
        REQUIRE(stack_count > 0);
        
        // Verify all created structures work
        for (int i = 0; i < array_count; i++) {
            (*arrays[i])[0] = i;
            REQUIRE((*arrays[i])[0] == i);
        }
        
        for (int i = 0; i < queue_count; i++) {
            REQUIRE(queues[i]->enqueue(i));
            REQUIRE(queues[i]->dequeue().value_or(-1) == i);
        }
        
        for (int i = 0; i < stack_count; i++) {
            REQUIRE(stacks[i]->push(i));
            REQUIRE(stacks[i]->pop().value_or(-1) == i);
        }
        
        shm.unlink();
    }
    
    SECTION("Concurrent access to many structures") {
        posix_shm shm(shm_name, 20 * 1024 * 1024);
        
        // Create multiple structures
        const int num_structures = 5;
        std::vector<shm_queue<int>> queues;
        std::vector<shm_stack<int>> stacks;
        std::vector<shm_hash_map<int, int>> maps;
        
        for (int i = 0; i < num_structures; i++) {
            queues.emplace_back(shm, "cq_" + std::to_string(i), 1000);
            stacks.emplace_back(shm, "cs_" + std::to_string(i), 1000);
            maps.emplace_back(shm, "cm_" + std::to_string(i), 1000);
        }
        
        const int operations_per_thread = 100;
        const int num_threads = 4;
        
        std::vector<std::thread> threads;
        
        // Worker function
        auto worker = [&](int thread_id) {
            std::mt19937 rng(thread_id);
            std::uniform_int_distribution<int> struct_dist(0, num_structures - 1);
            std::uniform_int_distribution<int> op_dist(0, 2);
            
            for (int i = 0; i < operations_per_thread; i++) {
                int struct_idx = struct_dist(rng);
                int op_type = op_dist(rng);
                int value = thread_id * 1000 + i;
                
                switch (op_type) {
                    case 0:  // Queue operation
                        if (i % 2 == 0) {
                            queues[struct_idx].enqueue(value);
                        } else {
                            queues[struct_idx].dequeue();
                        }
                        break;
                    case 1:  // Stack operation
                        if (i % 2 == 0) {
                            stacks[struct_idx].push(value);
                        } else {
                            stacks[struct_idx].pop();
                        }
                        break;
                    case 2:  // Map operation
                        maps[struct_idx].insert(value, value * 2);
                        break;
                }
            }
        };
        
        // Launch threads
        for (int i = 0; i < num_threads; i++) {
            threads.emplace_back(worker, i);
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        // Verify structures are not corrupted
        for (int i = 0; i < num_structures; i++) {
            // Just verify we can perform operations
            queues[i].enqueue(999999);
            REQUIRE(queues[i].dequeue().value_or(-1) == 999999);
            
            stacks[i].push(888888);
            REQUIRE(stacks[i].pop().value_or(-1) == 888888);
            
            maps[i].insert(777777, 666666);
            auto* val = maps[i].find(777777);
            REQUIRE(val != nullptr);
            REQUIRE(*val == 666666);
        }
        
        shm.unlink();
    }
}

TEST_CASE("All data structures together", "[integration][all-structures]") {
    const std::string shm_name = "/test_all_structures";
    shm_unlink(shm_name.c_str());
    
    SECTION("Create and use all structure types") {
        posix_shm shm(shm_name, 50 * 1024 * 1024);  // 50MB for all structures
        
        // Create all available data structures
        shm_array<double> array(shm, "array", 1000);
        shm_queue<int> queue(shm, "queue", 500);
        shm_stack<float> stack(shm, "stack", 200);
        shm_hash_map<int, double> map(shm, "map", 100);
        shm_set<uint32_t> set(shm, "set", 150);
        shm_bitset<2048> bits(shm, "bits");
        shm_ring_buffer<int> ring(shm, "ring", 100);
        shm_object_pool<uint64_t> pool(shm, "pool", 50);
        
        // Verify all were created successfully
        REQUIRE(array.size() == 1000);
        REQUIRE(queue.capacity() == 500);
        REQUIRE(stack.capacity() == 200);
        REQUIRE(map.bucket_count() > 0);
        REQUIRE(set.size() == 0);  // Empty initially
        REQUIRE(bits.size() == 2048);
        REQUIRE(ring.capacity() == 100);
        REQUIRE(pool.capacity() == 50);
        
        // Use each structure
        array[0] = 3.14159;
        REQUIRE(queue.enqueue(42));
        REQUIRE(stack.push(2.718f));
        REQUIRE(map.insert(1, 1.414));
        REQUIRE(set.insert(100));
        bits.set(1000);
        REQUIRE(ring.push(777));
        auto handle = pool.acquire();
        REQUIRE(handle != pool.invalid_handle);
        pool[handle] = 0xDEADBEEF;
        
        // Verify data
        REQUIRE(array[0] == Catch::Approx(3.14159));
        REQUIRE(queue.size() == 1);
        REQUIRE(stack.size() == 1);
        REQUIRE(map.size() == 1);
        REQUIRE(set.size() == 1);
        REQUIRE(bits.test(1000));
        REQUIRE(ring.size() == 1);
        REQUIRE(pool.num_allocated() == 1);
        
        // Clean up
        pool.release(handle);
        
        // Check table usage
        auto capacity = shm.get_capacity();
        INFO("Table entries used: " << (capacity.max_entries - capacity.available_entries) 
             << " / " << capacity.max_entries);
        REQUIRE(capacity.available_entries < capacity.max_entries);  // Some entries used
        
        shm.unlink();
    }
    
    SECTION("Cross-process with all structures") {
        // Process 1: Create and populate
        {
            posix_shm shm1(shm_name, 30 * 1024 * 1024);
            
            shm_array<int> array(shm1, "persistent_array", 100);
            shm_queue<double> queue(shm1, "persistent_queue", 50);
            shm_stack<uint32_t> stack(shm1, "persistent_stack", 30);
            shm_ring_buffer<float> ring(shm1, "persistent_ring", 20);
            shm_object_pool<int> pool(shm1, "persistent_pool", 10);
            
            // Populate
            for (int i = 0; i < 10; ++i) {
                array[i] = i * 100;
                queue.enqueue(i * 0.1);
                stack.push(i);
                ring.push(i * 1.5f);
            }
            
            auto h = pool.acquire();
            pool[h] = 999;
        }
        
        // Process 2: Verify persistence
        {
            posix_shm shm2(shm_name, 0);  // Attach only
            
            shm_array<int> array(shm2, "persistent_array");
            shm_queue<double> queue(shm2, "persistent_queue");
            shm_stack<uint32_t> stack(shm2, "persistent_stack");
            shm_ring_buffer<float> ring(shm2, "persistent_ring");
            shm_object_pool<int> pool(shm2, "persistent_pool");
            
            // Verify data
            REQUIRE(array[0] == 0);
            REQUIRE(array[9] == 900);
            REQUIRE(queue.size() == 10);
            REQUIRE(stack.size() == 10);
            REQUIRE(ring.size() == 10);
            REQUIRE(pool.num_allocated() == 1);
            
            // Pop from stack to verify LIFO order
            REQUIRE(*stack.pop() == 9);
            REQUIRE(*stack.pop() == 8);
        }
        
        shm_unlink(shm_name.c_str());
    }
}
