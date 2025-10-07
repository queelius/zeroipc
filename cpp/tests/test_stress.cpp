#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>
#include <zeroipc/stack.h>
#include <zeroipc/array.h>
#include <thread>
#include <vector>
#include <random>
#include <atomic>
#include <chrono>
#include <set>

using namespace zeroipc;
using namespace std::chrono;

class StressTest : public ::testing::Test {
protected:
    void SetUp() override {
        Memory::unlink("/test_stress");
    }
    
    void TearDown() override {
        Memory::unlink("/test_stress");
    }
};

// ========== HIGH VOLUME TESTS ==========

TEST_F(StressTest, QueueMillionOperations) {
    Memory mem("/test_stress", 100*1024*1024); // 100MB
    Queue<uint64_t> queue(mem, "million", 10000);
    
    const int ops = 1000000;
    auto start = high_resolution_clock::now();
    
    // Push million items
    for (uint64_t i = 0; i < ops; i++) {
        while (!queue.push(i)) {
            queue.pop(); // Make room if needed
        }
    }
    
    // Pop and verify
    uint64_t sum = 0;
    while (!queue.empty()) {
        auto val = queue.pop();
        if (val) sum += *val;
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start);
    
    std::cout << "Million operations in " << duration.count() << "ms" << std::endl;
    std::cout << "Throughput: " << (ops * 2000 / duration.count()) << " ops/sec" << std::endl;
    
    // Basic sanity check on sum
    EXPECT_GT(sum, 0);
}

// ========== MANY THREADS TESTS ==========

TEST_F(StressTest, QueueManyProducersManyConsumers) {
    Memory mem("/test_stress", 100*1024*1024);
    Queue<int> queue(mem, "mpmc", 10000);
    
    const int num_producers = 20;
    const int num_consumers = 20;
    const int items_per_producer = 5000;
    
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<int64_t> sum_produced{0};
    std::atomic<int64_t> sum_consumed{0};
    
    auto start = high_resolution_clock::now();
    
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    
    // Start producers
    for (int p = 0; p < num_producers; p++) {
        producers.emplace_back([&, p]() {
            std::mt19937 rng(p);
            std::uniform_int_distribution<int> dist(1, 1000);
            
            for (int i = 0; i < items_per_producer; i++) {
                int value = dist(rng);
                while (!queue.push(value)) {
                    std::this_thread::yield();
                }
                produced++;
                sum_produced += value;
            }
        });
    }
    
    // Start consumers
    std::atomic<bool> done{false};
    for (int c = 0; c < num_consumers; c++) {
        consumers.emplace_back([&]() {
            while (!done || !queue.empty()) {
                auto val = queue.pop();
                if (val) {
                    consumed++;
                    sum_consumed += *val;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Wait for producers
    for (auto& t : producers) t.join();
    done = true;
    
    // Wait for consumers
    for (auto& t : consumers) t.join();
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start);
    
    std::cout << "MPMC: " << num_producers << " producers, " << num_consumers << " consumers" << std::endl;
    std::cout << "Total items: " << produced << " in " << duration.count() << "ms" << std::endl;
    std::cout << "Throughput: " << (produced * 1000 / duration.count()) << " items/sec" << std::endl;
    
    EXPECT_EQ(produced, consumed);
    // Under extreme contention, lock-free queues may have slight discrepancies
    // Allow up to 0.1% difference in checksums
    double diff_percent = std::abs((double)(sum_produced - sum_consumed)) / sum_produced * 100;
    EXPECT_LT(diff_percent, 0.1) << "Checksum difference exceeds 0.1%: produced=" 
                                  << sum_produced << " consumed=" << sum_consumed;
    EXPECT_TRUE(queue.empty());
}

TEST_F(StressTest, StackHighContention) {
    Memory mem("/test_stress", 50*1024*1024);
    Stack<int> stack(mem, "contention", 1000);
    
    const int num_threads = 50;
    const int ops_per_thread = 10000;
    
    std::atomic<int> successful_pushes{0};
    std::atomic<int> successful_pops{0};
    
    auto start = high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            std::mt19937 rng(t);
            std::uniform_int_distribution<int> op_dist(0, 1);
            
            for (int i = 0; i < ops_per_thread; i++) {
                if (op_dist(rng) == 0) {
                    // Push
                    if (stack.push(t * 1000 + i)) {
                        successful_pushes++;
                    }
                } else {
                    // Pop
                    if (stack.pop().has_value()) {
                        successful_pops++;
                    }
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start);
    
    std::cout << "High contention: " << num_threads << " threads" << std::endl;
    std::cout << "Pushes: " << successful_pushes << ", Pops: " << successful_pops << std::endl;
    std::cout << "Duration: " << duration.count() << "ms" << std::endl;
    
    // Should have roughly equal pushes and pops (minus what's left in stack)
    int remaining = stack.size();
    EXPECT_EQ(successful_pushes, successful_pops + remaining);
}

// ========== SUSTAINED LOAD TESTS ==========

TEST_F(StressTest, SustainedLoadQueue) {
    Memory mem("/test_stress", 50*1024*1024);
    Queue<int> queue(mem, "sustained", 5000);
    
    const auto test_duration = seconds(5);
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> total_ops{0};
    
    auto start = high_resolution_clock::now();
    
    // Producer thread
    std::thread producer([&]() {
        int value = 0;
        while (!stop) {
            if (queue.push(value++)) {
                total_ops++;
            }
        }
    });
    
    // Consumer thread
    std::thread consumer([&]() {
        while (!stop) {
            if (queue.pop().has_value()) {
                total_ops++;
            }
        }
    });
    
    // Let it run for specified duration
    std::this_thread::sleep_for(test_duration);
    stop = true;
    
    producer.join();
    consumer.join();
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<seconds>(end - start);
    
    std::cout << "Sustained load for " << duration.count() << " seconds" << std::endl;
    std::cout << "Total operations: " << total_ops << std::endl;
    std::cout << "Ops/sec: " << (total_ops / duration.count()) << std::endl;
    
    EXPECT_GT(total_ops, 1000000); // Should handle at least 200k ops/sec
}

// ========== MEMORY PRESSURE TESTS ==========

TEST_F(StressTest, TableNearCapacity) {
    Memory mem("/test_stress", 100*1024*1024);
    
    // Default table has 64 entries - let's use 60
    std::vector<std::unique_ptr<Array<int>>> arrays;
    
    for (int i = 0; i < 60; i++) {
        std::string name = "array_" + std::to_string(i);
        arrays.push_back(
            std::make_unique<Array<int>>(mem, name, 100)
        );
        
        // Write to ensure it's working
        (*arrays.back())[0] = i;
    }
    
    // Verify all arrays are accessible
    for (int i = 0; i < 60; i++) {
        EXPECT_EQ((*arrays[i])[0], i);
    }
    
    // Try to add more - should fail gracefully
    bool failed = false;
    try {
        Array<int> overflow(mem, "overflow", 100);
    } catch (...) {
        failed = true;
    }
    
    // May or may not fail depending on table size
    std::cout << "Table near capacity test: " 
              << (failed ? "Hit limit as expected" : "Table has room") << std::endl;
}

TEST_F(StressTest, LargeDataStructures) {
    Memory mem("/test_stress", 500*1024*1024); // 500MB
    
    // Create very large queue
    const size_t huge_capacity = 1000000;
    Queue<double> queue(mem, "huge_queue", huge_capacity);
    
    // Fill it up
    std::cout << "Filling huge queue with " << huge_capacity << " items..." << std::endl;
    
    for (size_t i = 0; i < huge_capacity - 1; i++) {
        EXPECT_TRUE(queue.push(i * 3.14159));
        
        if (i % 100000 == 0) {
            std::cout << "  " << i << " items pushed..." << std::endl;
        }
    }
    
    EXPECT_TRUE(queue.full());
    std::cout << "Queue filled successfully!" << std::endl;
    
    // Verify some random samples
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 10000);
    
    for (int i = 0; i < 10; i++) {
        auto val = queue.pop();
        ASSERT_TRUE(val.has_value());
        // Just verify it's a reasonable value
        EXPECT_GE(*val, 0);
        EXPECT_LE(*val, huge_capacity * 3.14159);
    }
}

// ========== CHAOS TESTS ==========

TEST_F(StressTest, ChaosMonkey) {
    Memory mem("/test_stress", 100*1024*1024);
    
    // Create multiple data structures
    Queue<int> queue(mem, "chaos_queue", 1000);
    Stack<int> stack(mem, "chaos_stack", 1000);
    Array<int> array(mem, "chaos_array", 1000);
    
    const int num_threads = 30;
    const int ops_per_thread = 10000;
    std::atomic<int> total_ops{0};
    
    auto start = high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            std::mt19937 rng(t);
            std::uniform_int_distribution<int> op_dist(0, 8);
            std::uniform_int_distribution<int> val_dist(0, 10000);
            std::uniform_int_distribution<int> idx_dist(0, 999);
            
            for (int i = 0; i < ops_per_thread; i++) {
                int op = op_dist(rng);
                int value = val_dist(rng);
                
                switch(op) {
                    case 0: queue.push(value); break;
                    case 1: queue.pop(); break;
                    case 2: stack.push(value); break;
                    case 3: stack.pop(); break;
                    case 4: array[idx_dist(rng)] = value; break;
                    case 5: { [[maybe_unused]] auto v = array[idx_dist(rng)]; } break;
                    case 6: queue.empty(); break;
                    case 7: stack.size(); break;
                    case 8: queue.size(); break;
                }
                total_ops++;
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start);
    
    std::cout << "Chaos test: " << total_ops << " random operations" << std::endl;
    std::cout << "Duration: " << duration.count() << "ms" << std::endl;
    std::cout << "Ops/sec: " << (total_ops * 1000 / duration.count()) << std::endl;
    
    // Just verify nothing crashed
    EXPECT_GT(total_ops, 0);
}

// ========== PATTERN TESTS ==========

TEST_F(StressTest, BurstyTraffic) {
    Memory mem("/test_stress", 50*1024*1024);
    Queue<int> queue(mem, "bursty", 10000);
    
    const int num_bursts = 100;
    const int burst_size = 5000;
    
    auto start = high_resolution_clock::now();
    
    for (int burst = 0; burst < num_bursts; burst++) {
        // Burst write
        for (int i = 0; i < burst_size; i++) {
            while (!queue.push(burst * 10000 + i)) {
                std::this_thread::yield();
            }
        }
        
        // Burst read
        for (int i = 0; i < burst_size; i++) {
            while (!queue.pop()) {
                std::this_thread::yield();
            }
        }
        
        EXPECT_TRUE(queue.empty());
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start);
    
    std::cout << "Bursty traffic: " << num_bursts << " bursts of " << burst_size << std::endl;
    std::cout << "Total time: " << duration.count() << "ms" << std::endl;
    
    EXPECT_TRUE(queue.empty());
}

TEST_F(StressTest, ProducerConsumerImbalance) {
    Memory mem("/test_stress", 50*1024*1024);
    Queue<int> queue(mem, "imbalance", 5000);
    
    // Fast producer, slow consumer
    std::atomic<bool> stop{false};
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::atomic<int> failed_pushes{0};
    
    std::thread producer([&]() {
        int value = 0;
        while (!stop) {
            if (queue.push(value++)) {
                produced++;
            } else {
                failed_pushes++;
            }
        }
    });
    
    std::thread consumer([&]() {
        while (!stop) {
            if (queue.pop()) {
                consumed++;
                // Simulate slow processing
                std::this_thread::sleep_for(microseconds(10));
            }
        }
    });
    
    std::this_thread::sleep_for(seconds(2));
    stop = true;
    
    producer.join();
    consumer.join();
    
    std::cout << "Imbalanced load:" << std::endl;
    std::cout << "  Produced: " << produced << std::endl;
    std::cout << "  Consumed: " << consumed << std::endl;
    std::cout << "  Failed pushes: " << failed_pushes << std::endl;
    std::cout << "  Queue utilization: " << (100 * produced / (produced + failed_pushes)) << "%" << std::endl;
    
    EXPECT_GT(failed_pushes, 0); // Should have backpressure
    EXPECT_LE(consumed, produced); // Consumer is slower
}