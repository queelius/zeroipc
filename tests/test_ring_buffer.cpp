#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "zeroipc.h"
#include "ring.h"
#include <thread>
#include <vector>
#include <array>
#include <numeric>
#include <algorithm>

TEST_CASE("zeroipc::ring basic operations", "[zeroipc::ring]") {
    const std::string shm_name = "/test_ring_basic";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Create and use ring buffer") {
        zeroipc::ring<int> ring(shm, "test_ring", 100);
        
        REQUIRE(ring.capacity() == 100);
        REQUIRE(ring.size() == 0);
        REQUIRE(ring.empty());
        REQUIRE(!ring.full());
        REQUIRE(ring.available_space() == 100);
    }

    SECTION("Push and pop single elements") {
        zeroipc::ring<int> ring(shm, "single_ops", 10);
        
        // Push elements
        REQUIRE(ring.push(42));
        REQUIRE(ring.push(7));
        REQUIRE(ring.push(100));
        
        REQUIRE(ring.size() == 3);
        REQUIRE(!ring.empty());
        
        // Pop elements
        auto val1 = ring.pop();
        REQUIRE(val1.has_value());
        REQUIRE(*val1 == 42);
        
        auto val2 = ring.pop();
        REQUIRE(val2.has_value());
        REQUIRE(*val2 == 7);
        
        REQUIRE(ring.size() == 1);
        
        auto val3 = ring.pop();
        REQUIRE(val3.has_value());
        REQUIRE(*val3 == 100);
        
        REQUIRE(ring.empty());
        
        // Pop from empty
        auto val4 = ring.pop();
        REQUIRE(!val4.has_value());
    }

    SECTION("Fill to capacity") {
        zeroipc::ring<int> ring(shm, "fill_capacity", 5);
        
        for (int i = 0; i < 5; ++i) {
            REQUIRE(ring.push(i));
        }
        
        REQUIRE(ring.full());
        REQUIRE(ring.size() == 5);
        REQUIRE(ring.available_space() == 0);
        
        // Should fail when full
        REQUIRE(!ring.push(999));
        
        // Pop one to make room
        ring.pop();
        REQUIRE(!ring.full());
        REQUIRE(ring.push(999));
    }

    SECTION("Wraparound behavior") {
        zeroipc::ring<int> ring(shm, "wraparound", 3);
        
        // Fill buffer
        ring.push(1);
        ring.push(2);
        ring.push(3);
        
        // Pop and push to cause wraparound
        REQUIRE(*ring.pop() == 1);
        ring.push(4);
        
        REQUIRE(*ring.pop() == 2);
        ring.push(5);
        
        // Verify remaining elements
        REQUIRE(*ring.pop() == 3);
        REQUIRE(*ring.pop() == 4);
        REQUIRE(*ring.pop() == 5);
        REQUIRE(ring.empty());
    }

    shm.unlink();
}

TEST_CASE("zeroipc::ring bulk operations", "[zeroipc::ring]") {
    const std::string shm_name = "/test_ring_bulk";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Push bulk") {
        zeroipc::ring<int> ring(shm, "push_bulk", 100);
        
        std::vector<int> data(10);
        std::iota(data.begin(), data.end(), 0);  // 0,1,2,...,9
        
        size_t pushed = ring.push_bulk(data);
        REQUIRE(pushed == 10);
        REQUIRE(ring.size() == 10);
        
        // Verify data
        for (int i = 0; i < 10; ++i) {
            auto val = ring.pop();
            REQUIRE(val.has_value());
            REQUIRE(*val == i);
        }
    }

    SECTION("Push bulk partial when near full") {
        zeroipc::ring<int> ring(shm, "push_partial", 5);
        
        // Fill most of buffer
        ring.push(1);
        ring.push(2);
        ring.push(3);
        
        // Try to push more than available
        std::vector<int> data = {10, 20, 30, 40, 50};
        size_t pushed = ring.push_bulk(data);
        
        REQUIRE(pushed == 2);  // Only 2 slots available
        REQUIRE(ring.full());
        
        // Verify correct elements were pushed
        ring.pop();  // Remove 1
        ring.pop();  // Remove 2
        ring.pop();  // Remove 3
        REQUIRE(*ring.pop() == 10);
        REQUIRE(*ring.pop() == 20);
    }

    SECTION("Pop bulk") {
        zeroipc::ring<int> ring(shm, "pop_bulk", 100);
        
        // Push test data
        for (int i = 0; i < 20; ++i) {
            ring.push(i * 10);
        }
        
        // Pop bulk
        std::vector<int> output(10);
        size_t popped = ring.pop_bulk(output);
        
        REQUIRE(popped == 10);
        REQUIRE(ring.size() == 10);
        
        // Verify data
        for (int i = 0; i < 10; ++i) {
            REQUIRE(output[i] == i * 10);
        }
    }

    SECTION("Pop bulk partial when near empty") {
        zeroipc::ring<int> ring(shm, "pop_partial", 100);
        
        // Push only 3 elements
        ring.push(11);
        ring.push(22);
        ring.push(33);
        
        // Try to pop more than available
        std::vector<int> output(10);
        size_t popped = ring.pop_bulk(output);
        
        REQUIRE(popped == 3);
        REQUIRE(ring.empty());
        REQUIRE(output[0] == 11);
        REQUIRE(output[1] == 22);
        REQUIRE(output[2] == 33);
    }

    shm.unlink();
}

TEST_CASE("zeroipc::ring peek operations", "[zeroipc::ring]") {
    const std::string shm_name = "/test_ring_peek";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Peek without consuming") {
        zeroipc::ring<int> ring(shm, "peek_basic", 10);
        
        // Push test data
        for (int i = 0; i < 5; ++i) {
            ring.push(i);
        }
        
        // Peek at data
        std::vector<int> peeked(3);
        size_t count = ring.peek_bulk(0, peeked);
        
        REQUIRE(count == 3);
        REQUIRE(peeked[0] == 0);
        REQUIRE(peeked[1] == 1);
        REQUIRE(peeked[2] == 2);
        
        // Data should still be there
        REQUIRE(ring.size() == 5);
        
        // Peek with offset
        count = ring.peek_bulk(2, peeked);
        REQUIRE(count == 3);
        REQUIRE(peeked[0] == 2);
        REQUIRE(peeked[1] == 3);
        REQUIRE(peeked[2] == 4);
        
        // Original data still available for popping
        REQUIRE(*ring.pop() == 0);
        REQUIRE(*ring.pop() == 1);
    }

    SECTION("Get last N elements") {
        zeroipc::ring<double> ring(shm, "get_last", 100);
        
        // Simulate sensor data stream
        for (int i = 0; i < 50; ++i) {
            ring.push(i * 0.1);
        }
        
        // Get last 5 readings
        std::vector<double> last_readings(5);
        size_t got = ring.get_last_n(5, last_readings);
        
        REQUIRE(got == 5);
        REQUIRE(last_readings[0] == 45 * 0.1);
        REQUIRE(last_readings[1] == 46 * 0.1);
        REQUIRE(last_readings[2] == 47 * 0.1);
        REQUIRE(last_readings[3] == 48 * 0.1);
        REQUIRE(last_readings[4] == 49 * 0.1);
        
        // Data should still be in buffer
        REQUIRE(ring.size() == 50);
    }

    SECTION("Skip elements") {
        zeroipc::ring<int> ring(shm, "skip", 10);
        
        for (int i = 0; i < 8; ++i) {
            ring.push(i);
        }
        
        // Skip first 3 elements
        ring.skip(3);
        REQUIRE(ring.size() == 5);
        REQUIRE(*ring.pop() == 3);
        
        // Skip more than available
        ring.skip(100);
        REQUIRE(ring.empty());
    }

    shm.unlink();
}

TEST_CASE("zeroipc::ring overwrite mode", "[zeroipc::ring]") {
    const std::string shm_name = "/test_ring_overwrite";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Push overwrite when full") {
        zeroipc::ring<int> ring(shm, "overwrite", 3);
        
        // Fill buffer
        ring.push(1);
        ring.push(2);
        ring.push(3);
        REQUIRE(ring.full());
        
        // Overwrite oldest
        ring.push_overwrite(4);
        REQUIRE(ring.size() == 3);  // Still full
        
        // First element should now be 2 (1 was overwritten)
        REQUIRE(*ring.pop() == 2);
        REQUIRE(*ring.pop() == 3);
        REQUIRE(*ring.pop() == 4);
    }

    SECTION("Continuous overwrite simulating sensor stream") {
        zeroipc::ring<float> ring(shm, "sensor_stream", 5);
        
        // Simulate continuous sensor updates
        for (int i = 0; i < 20; ++i) {
            ring.push_overwrite(i * 0.5f);
        }
        
        // Should have last 5 values
        REQUIRE(ring.size() == 5);
        
        std::vector<float> values(5);
        ring.pop_bulk(values);
        
        // Should have values 15.0, 16.0, 17.0, 18.0, 19.0 (times 0.5)
        for (int i = 0; i < 5; ++i) {
            REQUIRE(values[i] == (15 + i) * 0.5f);
        }
    }

    shm.unlink();
}

TEST_CASE("zeroipc::ring statistics", "[zeroipc::ring]") {
    const std::string shm_name = "/test_ring_stats";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Track total read/written") {
        zeroipc::ring<int> ring(shm, "stats", 10);
        
        REQUIRE(ring.total_written() == 0);
        REQUIRE(ring.total_read() == 0);
        
        // Push and pop multiple rounds
        for (int round = 0; round < 3; ++round) {
            for (int i = 0; i < 5; ++i) {
                ring.push(i);
            }
            for (int i = 0; i < 5; ++i) {
                ring.pop();
            }
        }
        
        REQUIRE(ring.total_written() == 15);
        REQUIRE(ring.total_read() == 15);
        REQUIRE(ring.empty());
    }

    shm.unlink();
}

TEST_CASE("zeroipc::ring concurrent operations", "[zeroipc::ring][concurrent]") {
    const std::string shm_name = "/test_ring_concurrent";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Single producer single consumer") {
        zeroipc::ring<int> ring(shm, "spsc", 1000);
        const int total_items = 10000;
        std::atomic<bool> producer_done{false};
        
        // Producer thread
        std::thread producer([&ring, &producer_done, total_items]() {
            for (int i = 0; i < total_items; ++i) {
                while (!ring.push(i)) {
                    std::this_thread::yield();
                }
            }
            producer_done = true;
        });
        
        // Consumer thread
        std::thread consumer([&ring, &producer_done, total_items]() {
            int expected = 0;
            while (expected < total_items) {
                if (auto val = ring.pop()) {
                    REQUIRE(*val == expected);
                    expected++;
                } else if (producer_done && ring.empty()) {
                    break;
                } else {
                    std::this_thread::yield();
                }
            }
            REQUIRE(expected == total_items);
        });
        
        producer.join();
        consumer.join();
        
        REQUIRE(ring.empty());
        REQUIRE(ring.total_written() == total_items);
        REQUIRE(ring.total_read() == total_items);
    }

    SECTION("Multiple readers with peek") {
        zeroipc::ring<int> ring(shm, "multi_reader", 100);
        
        // Pre-fill with data
        for (int i = 0; i < 50; ++i) {
            ring.push(i);
        }
        
        const int num_readers = 4;
        std::vector<std::thread> readers;
        std::atomic<int> sum{0};
        
        for (int r = 0; r < num_readers; ++r) {
            readers.emplace_back([&ring, &sum]() {
                std::vector<int> buffer(10);
                
                // Each reader peeks at different offsets
                for (size_t offset = 0; offset < 40; offset += 10) {
                    size_t count = ring.peek_bulk(offset, buffer);
                    if (count > 0) {
                        int local_sum = 0;
                        for (size_t i = 0; i < count; ++i) {
                            local_sum += buffer[i];
                        }
                        sum += local_sum;
                    }
                }
            });
        }
        
        for (auto& t : readers) {
            t.join();
        }
        
        // Each reader should have peeked the same data
        // Sum of 0..39 = 780, times 4 readers = 3120
        REQUIRE(sum == 3120);
        
        // Original data should still be there
        REQUIRE(ring.size() == 50);
    }

    shm.unlink();
}

TEST_CASE("zeroipc::ring cross-process", "[zeroipc::ring][process]") {
    const std::string shm_name = "/test_ring_process";
    shm_unlink(shm_name.c_str());
    
    SECTION("Ring buffer persistence") {
        // Process 1: Create and populate
        {
            zeroipc::memory shm1(shm_name, 1024 * 1024);
            zeroipc::ring<uint32_t> ring(shm1, "persistent_ring", 100);
            
            for (uint32_t i = 0; i < 20; ++i) {
                ring.push(i * 100);
            }
            
            REQUIRE(ring.size() == 20);
            REQUIRE(ring.total_written() == 20);
        }
        
        // Process 2: Open and continue
        {
            zeroipc::memory shm2(shm_name, 0);
            zeroipc::ring<uint32_t> ring(shm2, "persistent_ring");
            
            REQUIRE(ring.size() == 20);
            REQUIRE(ring.capacity() == 100);
            REQUIRE(ring.total_written() == 20);
            REQUIRE(ring.total_read() == 0);
            
            // Pop some elements
            for (int i = 0; i < 5; ++i) {
                auto val = ring.pop();
                REQUIRE(val.has_value());
                REQUIRE(*val == i * 100);
            }
            
            // Push more
            ring.push(9999);
            ring.push(8888);
        }
        
        // Process 3: Verify state
        {
            zeroipc::memory shm3(shm_name, 0);
            zeroipc::ring<uint32_t> ring(shm3, "persistent_ring");
            
            REQUIRE(ring.size() == 17);  // 20 - 5 + 2
            REQUIRE(ring.total_written() == 22);
            REQUIRE(ring.total_read() == 5);
            
            // First element should be 500 (5 * 100)
            REQUIRE(*ring.pop() == 500);
        }
    }
    
    shm_unlink(shm_name.c_str());
}

TEST_CASE("zeroipc::ring with custom types", "[zeroipc::ring]") {
    const std::string shm_name = "/test_ring_custom";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    struct SensorData {
        uint64_t timestamp;
        float temperature;
        float pressure;
        uint32_t sensor_id;
    };

    SECTION("Sensor data streaming") {
        zeroipc::ring<SensorData> ring(shm, "sensor_ring", 1000);
        
        // Simulate sensor stream
        for (uint64_t t = 0; t < 100; ++t) {
            SensorData data{
                .timestamp = t * 1000,
                .temperature = 20.0f + t * 0.1f,
                .pressure = 1013.25f + t * 0.01f,
                .sensor_id = t % 4
            };
            ring.push(data);
        }
        
        // Get last 10 readings
        std::vector<SensorData> recent(10);
        size_t count = ring.get_last_n(10, recent);
        
        REQUIRE(count == 10);
        REQUIRE(recent[0].timestamp == 90000);
        REQUIRE(recent[9].timestamp == 99000);
        REQUIRE(recent[9].temperature == Catch::Approx(29.9f));
    }

    shm.unlink();
}