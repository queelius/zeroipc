#include <catch2/catch_test_macros.hpp>
#include "zeroipc.h"
#include "queue.h"
#include <thread>
#include <vector>
#include <sys/mman.h>

TEST_CASE("zeroipc::queue basic operations", "[zeroipc::queue]") {
    const std::string shm_name = "/test_queue_basic";
    // Clean up any leftover shared memory from previous runs
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Create and use queue") {
        zeroipc::queue<int> queue(shm, "int_queue", 10);
        
        REQUIRE(queue.empty());
        REQUIRE(!queue.full());
        REQUIRE(queue.size() == 0);
        REQUIRE(queue.capacity() == 10);
        REQUIRE(queue.name() == "int_queue");
    }

    SECTION("Enqueue and dequeue") {
        zeroipc::queue<int> queue(shm, "ops_queue", 5);
        
        // Enqueue elements
        REQUIRE(queue.enqueue(10));
        REQUIRE(queue.enqueue(20));
        REQUIRE(queue.enqueue(30));
        
        REQUIRE(queue.size() == 3);
        REQUIRE(!queue.empty());
        REQUIRE(!queue.full());
        
        // Dequeue elements
        auto val1 = queue.dequeue();
        REQUIRE(val1.has_value());
        REQUIRE(*val1 == 10);
        
        auto val2 = queue.dequeue();
        REQUIRE(val2.has_value());
        REQUIRE(*val2 == 20);
        
        REQUIRE(queue.size() == 1);
    }

    SECTION("Queue full behavior") {
        zeroipc::queue<int> queue(shm, "full_queue", 3);
        
        REQUIRE(queue.enqueue(1));
        REQUIRE(queue.enqueue(2));
        REQUIRE(queue.enqueue(3));
        REQUIRE(queue.full());
        
        // Cannot enqueue when full
        REQUIRE(!queue.enqueue(4));
        REQUIRE(queue.size() == 3);
        
        // After dequeue, can enqueue again
        queue.dequeue();
        REQUIRE(!queue.full());
        REQUIRE(queue.enqueue(4));
    }

    SECTION("Queue empty behavior") {
        zeroipc::queue<int> queue(shm, "empty_queue", 5);
        
        REQUIRE(queue.empty());
        auto val = queue.dequeue();
        REQUIRE(!val.has_value());
        
        // Using output parameter version
        int output;
        REQUIRE(!queue.dequeue(output));
    }

    SECTION("Circular buffer wraparound") {
        zeroipc::queue<int> queue(shm, "circular", 3);
        
        // Fill and empty multiple times
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (int i = 0; i < 3; ++i) {
                REQUIRE(queue.enqueue(cycle * 10 + i));
            }
            REQUIRE(queue.full());
            
            for (int i = 0; i < 3; ++i) {
                auto val = queue.dequeue();
                REQUIRE(val.has_value());
                REQUIRE(*val == cycle * 10 + i);
            }
            REQUIRE(queue.empty());
        }
    }

    SECTION("Queue discovery by name") {
        // Create and populate queue
        {
            zeroipc::queue<double> q1(shm, "discoverable_q", 10);
            q1.enqueue(3.14);
            q1.enqueue(2.718);
        }
        
        // Discover and use existing queue
        {
            zeroipc::queue<double> q2(shm, "discoverable_q");
            REQUIRE(q2.size() == 2);
            REQUIRE(q2.capacity() == 10);
            
            auto val = q2.dequeue();
            REQUIRE(val.has_value());
            REQUIRE(*val == 3.14);
        }
    }
}

TEST_CASE("zeroipc::queue with custom types", "[zeroipc::queue]") {
    const std::string shm_name = "/test_queue_custom";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    struct Message {
        uint64_t timestamp;
        uint32_t type;
        float value;
    };
    static_assert(std::is_trivially_copyable_v<Message>);

    SECTION("Queue of structs") {
        zeroipc::queue<Message> msg_queue(shm, "messages", 100);
        
        Message m1{1000, 1, 42.5f};
        Message m2{2000, 2, 99.9f};
        
        REQUIRE(msg_queue.enqueue(m1));
        REQUIRE(msg_queue.enqueue(m2));
        
        auto received = msg_queue.dequeue();
        REQUIRE(received.has_value());
        REQUIRE(received->timestamp == 1000);
        REQUIRE(received->type == 1);
        REQUIRE(received->value == 42.5f);
    }
}

TEST_CASE("zeroipc::queue lock-free concurrency", "[zeroipc::queue][concurrency]") {
    const std::string shm_name = "/test_queue_concurrent";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Multiple producers") {
        zeroipc::queue<int> queue(shm, "multi_prod", 1000);
        const int num_producers = 4;
        const int items_per_producer = 100;
        
        std::vector<std::thread> producers;
        for (int p = 0; p < num_producers; ++p) {
            producers.emplace_back([&queue, p, items_per_producer]() {
                for (int i = 0; i < items_per_producer; ++i) {
                    while (!queue.enqueue(p * 1000 + i)) {
                        std::this_thread::yield();
                    }
                }
            });
        }
        
        for (auto& t : producers) {
            t.join();
        }
        
        REQUIRE(queue.size() == num_producers * items_per_producer);
    }

    SECTION("Producer-consumer pattern") {
        zeroipc::queue<int> queue(shm, "prod_cons", 10);
        const int num_items = 100;
        
        std::thread producer([&queue, num_items]() {
            for (int i = 0; i < num_items; ++i) {
                while (!queue.enqueue(i)) {
                    std::this_thread::yield();
                }
            }
        });
        
        std::thread consumer([&queue, num_items]() {
            int count = 0;
            while (count < num_items) {
                auto val = queue.dequeue();
                if (val.has_value()) {
                    REQUIRE(*val == count);
                    count++;
                } else {
                    std::this_thread::yield();
                }
            }
        });
        
        producer.join();
        consumer.join();
        
        REQUIRE(queue.empty());
    }
}

TEST_CASE("zeroipc::queue with custom table sizes", "[zeroipc::queue][template]") {
    SECTION("Queue with small table") {
        const std::string shm_name = "/test_queue_small_table";
        zeroipc::memory_small shm(shm_name, 1024 * 1024);
        
        zeroipc::queue<uint32_t, zeroipc::table_small> queue(shm, "small_q", 50);
        REQUIRE(queue.capacity() == 50);
        
        queue.enqueue(0xCAFEBABE);
        auto val = queue.dequeue();
        REQUIRE(val.has_value());
        REQUIRE(*val == 0xCAFEBABE);
    }

    SECTION("Queue with large table") {
        const std::string shm_name = "/test_queue_large_table";
        zeroipc::memory_large shm(shm_name, 10 * 1024 * 1024);
        
        zeroipc::queue<double, zeroipc::table_large> queue(
            shm, "queue_with_very_long_descriptive_name", 500);
        REQUIRE(queue.capacity() == 500);
        REQUIRE(queue.name() == "queue_with_very_long_descriptive_name");
    }
}

TEST_CASE("zeroipc::queue error handling", "[zeroipc::queue][error]") {
    const std::string shm_name = "/test_queue_errors";
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Queue not found") {
        REQUIRE_THROWS_AS(
            zeroipc::queue<int>(shm, "nonexistent"),
            std::runtime_error
        );
    }

    SECTION("Capacity mismatch on open") {
        zeroipc::queue<int> q1(shm, "sized_q", 10);
        REQUIRE_THROWS_AS(
            zeroipc::queue<int>(shm, "sized_q", 20),
            std::runtime_error
        );
    }
}