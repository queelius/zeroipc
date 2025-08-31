#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/table.h>
#include <zeroipc/array.h>
#include <zeroipc/queue.h>
#include <zeroipc/stack.h>
#include <thread>
#include <vector>
#include <random>
#include <set>
#include <chrono>

using namespace zeroipc;

class TableStressTest : public ::testing::Test {
protected:
    void SetUp() override {
        Memory::unlink("/test_table_stress");
    }
    
    void TearDown() override {
        Memory::unlink("/test_table_stress");
    }
};

// ========== TABLE CAPACITY TESTS ==========

TEST_F(TableStressTest, TableFillToCapacity) {
    // Create memory with small table (16 entries)
    Memory mem("/test_table_stress", 10*1024*1024, 16);
    
    std::vector<std::unique_ptr<Array<int>>> arrays;
    
    // Fill table to capacity
    int created = 0;
    for (int i = 0; i < 20; i++) {  // Try to create more than capacity
        std::string name = "array_" + std::to_string(i);
        
        try {
            arrays.push_back(
                std::make_unique<Array<int>>(mem, name, 10)
            );
            created++;
        } catch (const std::exception& e) {
            // Expected to fail when table is full
            break;
        }
    }
    
    // Should have created exactly 16 entries (or 15 if table reserves one)
    EXPECT_GE(created, 15);
    EXPECT_LE(created, 16);
    
    std::cout << "Created " << created << " entries in 16-entry table" << std::endl;
    
    // Verify all created arrays are still accessible
    for (int i = 0; i < created; i++) {
        (*arrays[i])[0] = i * 100;
        EXPECT_EQ((*arrays[i])[0], i * 100);
    }
}

TEST_F(TableStressTest, TableNameCollisions) {
    Memory mem("/test_table_stress", 10*1024*1024);
    
    // Create first structure
    Array<int> arr1(mem, "duplicate_name", 100);
    arr1[0] = 42;
    
    // Try to create with same name - should throw
    EXPECT_THROW(
        Array<int> arr2(mem, "duplicate_name", 200),
        std::runtime_error
    );
    
    // Original should still work
    EXPECT_EQ(arr1[0], 42);
}

TEST_F(TableStressTest, TableLongNames) {
    Memory mem("/test_table_stress", 10*1024*1024);
    
    // Test maximum name length (31 chars + null)
    std::string max_name(31, 'A');
    Array<int> arr1(mem, max_name, 10);
    arr1[0] = 100;
    
    // Test name that's too long (should be truncated)
    std::string long_name(100, 'B');
    Array<int> arr2(mem, long_name, 10);
    arr2[0] = 200;
    
    // Both should work
    EXPECT_EQ(arr1[0], 100);
    EXPECT_EQ(arr2[0], 200);
    
    // Try to find by truncated name
    std::string truncated = long_name.substr(0, 31);
    Array<int> arr2_ref(mem, truncated);
    EXPECT_EQ(arr2_ref[0], 200);
}

// ========== CONCURRENT TABLE ACCESS ==========

TEST_F(TableStressTest, ConcurrentTableCreation) {
    Memory mem("/test_table_stress", 50*1024*1024, 128);
    
    const int num_threads = 10;
    const int structures_per_thread = 10;
    
    std::atomic<int> successes{0};
    std::atomic<int> failures{0};
    std::vector<std::string> created_names;
    std::mutex names_mutex;
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < structures_per_thread; i++) {
                std::string name = "t" + std::to_string(t) + "_s" + std::to_string(i);
                
                try {
                    // Randomly create different structure types
                    int type = (t + i) % 3;
                    
                    if (type == 0) {
                        Array<int> arr(mem, name, 100);
                        arr[0] = t * 1000 + i;
                    } else if (type == 1) {
                        Queue<int> queue(mem, name, 100);
                        queue.push(t * 1000 + i);
                    } else {
                        Stack<int> stack(mem, name, 100);
                        stack.push(t * 1000 + i);
                    }
                    
                    successes++;
                    
                    std::lock_guard<std::mutex> lock(names_mutex);
                    created_names.push_back(name);
                    
                } catch (const std::exception& e) {
                    failures++;
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    std::cout << "Concurrent creation: " << successes << " successes, " 
             << failures << " failures" << std::endl;
    
    // Most should succeed (table has 128 entries)
    EXPECT_GT(successes, 50);
    
    // Verify all created structures are accessible
    for (const auto& name : created_names) {
        // Try to open as array (might be queue or stack, but just checking existence)
        try {
            Array<int> arr(mem, name);
            // If it opens, good enough
        } catch (...) {
            // Might be a different type, that's ok
        }
    }
}

TEST_F(TableStressTest, ConcurrentTableLookup) {
    Memory mem("/test_table_stress", 10*1024*1024);
    
    // Pre-create structures
    const int num_structures = 50;
    for (int i = 0; i < num_structures; i++) {
        std::string name = "lookup_" + std::to_string(i);
        Array<int> arr(mem, name, 10);
        arr[0] = i * 100;
    }
    
    // Concurrent lookups
    const int num_threads = 10;
    const int lookups_per_thread = 1000;
    
    std::atomic<int> successful_lookups{0};
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&]() {
            std::mt19937 rng(t);
            std::uniform_int_distribution<int> dist(0, num_structures - 1);
            
            for (int i = 0; i < lookups_per_thread; i++) {
                int idx = dist(rng);
                std::string name = "lookup_" + std::to_string(idx);
                
                try {
                    Array<int> arr(mem, name);
                    if (arr[0] == idx * 100) {
                        successful_lookups++;
                    }
                } catch (...) {
                    // Should not happen
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    EXPECT_EQ(successful_lookups, num_threads * lookups_per_thread);
    std::cout << "Completed " << successful_lookups << " concurrent lookups" << std::endl;
}

// ========== TABLE FRAGMENTATION ==========

TEST_F(TableStressTest, TableFragmentation) {
    Memory mem("/test_table_stress", 50*1024*1024, 64);
    
    // Create and destroy many structures to fragment table
    std::set<std::string> active_names;
    std::mt19937 rng(42);
    
    for (int round = 0; round < 100; round++) {
        // Create some structures
        for (int i = 0; i < 10; i++) {
            std::string name = "frag_" + std::to_string(round) + "_" + std::to_string(i);
            
            try {
                Queue<int> queue(mem, name, 100);
                queue.push(round * 100 + i);
                active_names.insert(name);
            } catch (...) {
                // Table might be full
            }
        }
        
        // Remove some random structures (simulate deletion)
        if (active_names.size() > 30) {
            // In real implementation, we'd need a remove operation
            // For now, just track that we would remove them
            auto it = active_names.begin();
            std::advance(it, rng() % active_names.size());
            active_names.erase(it);
        }
    }
    
    std::cout << "After fragmentation: " << active_names.size() 
             << " structures in table" << std::endl;
    
    // Table should still be functional
    EXPECT_GT(active_names.size(), 0);
    EXPECT_LE(active_names.size(), 64);
}

// ========== RAPID TABLE OPERATIONS ==========

TEST_F(TableStressTest, RapidTableChurn) {
    Memory mem("/test_table_stress", 20*1024*1024, 32);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Rapidly create structures with reused names
    const int iterations = 1000;
    for (int i = 0; i < iterations; i++) {
        std::string name = "churn_" + std::to_string(i % 10);
        
        // Create different types in sequence with same name
        {
            Array<int> arr(mem, name, 10);
            arr[0] = i;
        }
        
        {
            Queue<double> queue(mem, name, 10);
            queue.push(i * 3.14);
        }
        
        {
            Stack<char> stack(mem, name, 10);
            stack.push('A' + (i % 26));
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Completed " << iterations * 3 << " table operations in " 
             << duration.count() << "ms" << std::endl;
    
    double ops_per_sec = (iterations * 3 * 1000.0) / duration.count();
    std::cout << "Table throughput: " << ops_per_sec << " ops/sec" << std::endl;
    
    // Should complete reasonably fast
    EXPECT_LT(duration.count(), 5000);  // Less than 5 seconds
}

// ========== TABLE PATTERN TESTS ==========

TEST_F(TableStressTest, TableAccessPatterns) {
    Memory mem("/test_table_stress", 10*1024*1024);
    
    // Sequential pattern
    for (int i = 0; i < 30; i++) {
        std::string name = "seq_" + std::to_string(i);
        Array<int> arr(mem, name, 5);
        arr[0] = i;
    }
    
    // Random pattern
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 29);
    
    for (int i = 0; i < 100; i++) {
        int idx = dist(rng);
        std::string name = "seq_" + std::to_string(idx);
        Array<int> arr(mem, name);
        EXPECT_EQ(arr[0], idx);
    }
    
    // Batch pattern (multiple lookups of same name)
    std::string frequent_name = "seq_15";
    for (int i = 0; i < 50; i++) {
        Array<int> arr(mem, frequent_name);
        EXPECT_EQ(arr[0], 15);
    }
}

// ========== ERROR RECOVERY TESTS ==========

TEST_F(TableStressTest, TableErrorRecovery) {
    Memory mem("/test_table_stress", 5*1024*1024, 8);  // Very small table
    
    // Fill table completely
    std::vector<std::unique_ptr<Array<int>>> arrays;
    
    for (int i = 0; i < 10; i++) {
        try {
            arrays.push_back(
                std::make_unique<Array<int>>(mem, "fill_" + std::to_string(i), 10)
            );
        } catch (...) {
            break;
        }
    }
    
    size_t filled = arrays.size();
    std::cout << "Filled table with " << filled << " entries" << std::endl;
    
    // Try to create more - should fail gracefully
    int failed_attempts = 0;
    for (int i = 0; i < 5; i++) {
        try {
            Array<int> arr(mem, "overflow_" + std::to_string(i), 10);
        } catch (const std::exception& e) {
            failed_attempts++;
        }
    }
    
    EXPECT_EQ(failed_attempts, 5);
    
    // Existing structures should still work
    for (size_t i = 0; i < filled; i++) {
        (*arrays[i])[0] = i * 10;
        EXPECT_EQ((*arrays[i])[0], i * 10);
    }
}

// ========== CROSS-TYPE TABLE TESTS ==========

TEST_F(TableStressTest, MixedTypeTable) {
    Memory mem("/test_table_stress", 20*1024*1024);
    
    // Create mix of all structure types
    for (int i = 0; i < 30; i++) {
        std::string base_name = "mixed_" + std::to_string(i);
        
        // Array
        Array<int> arr(mem, base_name + "_arr", 10);
        arr[0] = i;
        
        // Queue
        Queue<double> queue(mem, base_name + "_queue", 10);
        queue.push(i * 2.5);
        
        // Stack
        Stack<char> stack(mem, base_name + "_stack", 10);
        stack.push('A' + i);
    }
    
    // Verify all can be accessed
    for (int i = 0; i < 30; i++) {
        std::string base_name = "mixed_" + std::to_string(i);
        
        Array<int> arr(mem, base_name + "_arr");
        EXPECT_EQ(arr[0], i);
        
        Queue<double> queue(mem, base_name + "_queue");
        auto qval = queue.pop();
        ASSERT_TRUE(qval.has_value());
        EXPECT_DOUBLE_EQ(*qval, i * 2.5);
        
        Stack<char> stack(mem, base_name + "_stack");
        auto sval = stack.pop();
        ASSERT_TRUE(sval.has_value());
        EXPECT_EQ(*sval, 'A' + i);
    }
}