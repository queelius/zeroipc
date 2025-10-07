#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/map.h>
#include <zeroipc/set.h>
#include <zeroipc/pool.h>
#include <zeroipc/ring.h>
#include <thread>
#include <vector>
#include <unistd.h>

using namespace zeroipc;

class NewStructuresTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create unique shared memory name for each test
        shm_name_ = "/test_new_" + std::to_string(getpid()) + "_" + 
                    std::to_string(test_counter_++);
    }
    
    void TearDown() override {
        // Cleanup
        Memory::unlink(shm_name_);
    }
    
    std::string shm_name_;
    static int test_counter_;
};

int NewStructuresTest::test_counter_ = 0;

// Map Tests
TEST_F(NewStructuresTest, MapBasicOperations) {
    Memory mem(shm_name_, 1024 * 1024);
    Map<int, double> map(mem, "test_map", 100);
    
    // Insert
    EXPECT_TRUE(map.insert(42, 3.14));
    EXPECT_TRUE(map.insert(10, 2.71));
    
    // Find
    auto val = map.find(42);
    ASSERT_TRUE(val.has_value());
    EXPECT_DOUBLE_EQ(*val, 3.14);
    
    // Contains
    EXPECT_TRUE(map.contains(42));
    EXPECT_FALSE(map.contains(999));
    
    // Update
    EXPECT_TRUE(map.insert(42, 6.28));
    val = map.find(42);
    ASSERT_TRUE(val.has_value());
    EXPECT_DOUBLE_EQ(*val, 6.28);
    
    // Size
    EXPECT_EQ(map.size(), 2);
    
    // Erase
    EXPECT_TRUE(map.erase(10));
    EXPECT_FALSE(map.contains(10));
    EXPECT_EQ(map.size(), 1);
}

TEST_F(NewStructuresTest, MapOpenExisting) {
    Memory mem(shm_name_, 1024 * 1024);
    {
        Map<int, int> map1(mem, "shared_map", 50);
        map1.insert(1, 100);
        map1.insert(2, 200);
    }
    
    // Open existing map
    Map<int, int> map2(mem, "shared_map");
    EXPECT_EQ(map2.size(), 2);
    
    auto val = map2.find(1);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 100);
}

// Set Tests
TEST_F(NewStructuresTest, SetBasicOperations) {
    Memory mem(shm_name_, 1024 * 1024);
    Set<int> set(mem, "test_set", 100);
    
    // Insert
    EXPECT_TRUE(set.insert(42));
    EXPECT_TRUE(set.insert(10));
    EXPECT_FALSE(set.insert(42));  // Duplicate
    
    // Contains
    EXPECT_TRUE(set.contains(42));
    EXPECT_FALSE(set.contains(999));
    
    // Size
    EXPECT_EQ(set.size(), 2);
    
    // Erase
    EXPECT_TRUE(set.erase(10));
    EXPECT_FALSE(set.contains(10));
    EXPECT_EQ(set.size(), 1);
}

// Pool Tests
TEST_F(NewStructuresTest, PoolBasicOperations) {
    Memory mem(shm_name_, 1024 * 1024);
    
    struct TestObject {
        int value;
        double data;
    };
    
    Pool<TestObject> pool(mem, "test_pool", 10);
    
    EXPECT_EQ(pool.capacity(), 10);
    EXPECT_EQ(pool.allocated(), 0);
    EXPECT_EQ(pool.available(), 10);
    
    // Allocate objects
    auto obj1 = pool.allocate();
    ASSERT_TRUE(obj1.has_value());
    (*obj1)->value = 42;
    (*obj1)->data = 3.14;
    
    auto obj2 = pool.allocate();
    ASSERT_TRUE(obj2.has_value());
    
    EXPECT_EQ(pool.allocated(), 2);
    EXPECT_EQ(pool.available(), 8);
    
    // Deallocate
    pool.deallocate(*obj1);
    EXPECT_EQ(pool.allocated(), 1);
    
    // Allocate again - should reuse freed slot
    auto obj3 = pool.allocate();
    ASSERT_TRUE(obj3.has_value());
    EXPECT_EQ(pool.allocated(), 2);
}

TEST_F(NewStructuresTest, PoolConstruct) {
    Memory mem(shm_name_, 1024 * 1024);
    
    struct Point {
        int x, y;
        Point(int x_, int y_) : x(x_), y(y_) {}
    };
    
    Pool<Point> pool(mem, "point_pool", 5);
    
    auto p1 = pool.construct(10, 20);
    ASSERT_TRUE(p1.has_value());
    EXPECT_EQ((*p1)->x, 10);
    EXPECT_EQ((*p1)->y, 20);
    
    pool.destroy(*p1);
    EXPECT_EQ(pool.allocated(), 0);
}

// Ring Buffer Tests
TEST_F(NewStructuresTest, RingBasicOperations) {
    Memory mem(shm_name_, 1024 * 1024);
    Ring<int> ring(mem, "test_ring", 100 * sizeof(int));
    
    EXPECT_TRUE(ring.empty());
    EXPECT_FALSE(ring.full());
    
    // Write data
    EXPECT_TRUE(ring.write(42));
    EXPECT_TRUE(ring.write(43));
    
    EXPECT_FALSE(ring.empty());
    EXPECT_EQ(ring.available(), 2);
    
    // Read data
    auto val = ring.read();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 42);
    
    val = ring.read();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 43);
    
    EXPECT_TRUE(ring.empty());
}

TEST_F(NewStructuresTest, RingBulkOperations) {
    Memory mem(shm_name_, 1024 * 1024);
    Ring<int> ring(mem, "bulk_ring", 100 * sizeof(int));
    
    // Write bulk
    int data[] = {1, 2, 3, 4, 5};
    size_t written = ring.write_bulk(data, 5);
    EXPECT_EQ(written, 5);
    EXPECT_EQ(ring.available(), 5);
    
    // Read bulk
    int read_data[5];
    size_t read_count = ring.read_bulk(read_data, 5);
    EXPECT_EQ(read_count, 5);
    
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(read_data[i], i + 1);
    }
    
    EXPECT_TRUE(ring.empty());
}

TEST_F(NewStructuresTest, RingWrapAround) {
    Memory mem(shm_name_, 1024 * 1024);
    Ring<int> ring(mem, "wrap_ring", 5 * sizeof(int));  // Small ring for testing wrap
    
    // Fill the ring
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(ring.write(i));
    }
    
    EXPECT_TRUE(ring.full());
    EXPECT_FALSE(ring.write(99));  // Should fail when full
    
    // Read some elements
    for (int i = 0; i < 3; ++i) {
        auto val = ring.read();
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, i);
    }
    
    // Write more (will wrap around)
    EXPECT_TRUE(ring.write(10));
    EXPECT_TRUE(ring.write(11));
    EXPECT_TRUE(ring.write(12));
    
    // Read all remaining
    std::vector<int> values;
    while (!ring.empty()) {
        auto val = ring.read();
        if (val) values.push_back(*val);
    }
    
    EXPECT_EQ(values.size(), 5);
    EXPECT_EQ(values[0], 3);
    EXPECT_EQ(values[1], 4);
    EXPECT_EQ(values[2], 10);
    EXPECT_EQ(values[3], 11);
    EXPECT_EQ(values[4], 12);
}

// Concurrent Tests
TEST_F(NewStructuresTest, MapConcurrentInsert) {
    Memory mem(shm_name_, 10 * 1024 * 1024);
    Map<int, int> map(mem, "concurrent_map", 1000);
    
    const int num_threads = 4;
    const int items_per_thread = 100;
    
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, items_per_thread]() {
            for (int i = 0; i < items_per_thread; ++i) {
                int key = t * items_per_thread + i;
                map.insert(key, key * 2);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify all items are present
    EXPECT_EQ(map.size(), num_threads * items_per_thread);
    
    for (int i = 0; i < num_threads * items_per_thread; ++i) {
        auto val = map.find(i);
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, i * 2);
    }
}

TEST_F(NewStructuresTest, PoolConcurrentAllocate) {
    Memory mem(shm_name_, 10 * 1024 * 1024);
    Pool<int> pool(mem, "concurrent_pool", 100);
    
    const int num_threads = 4;
    const int allocs_per_thread = 20;
    
    std::vector<std::thread> threads;
    std::vector<std::vector<int*>> thread_allocations(num_threads);
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&pool, &thread_allocations, t, allocs_per_thread]() {
            for (int i = 0; i < allocs_per_thread; ++i) {
                auto ptr = pool.allocate();
                if (ptr) {
                    **ptr = t * 1000 + i;
                    thread_allocations[t].push_back(*ptr);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify total allocations
    size_t total_allocated = 0;
    for (const auto& allocs : thread_allocations) {
        total_allocated += allocs.size();
    }
    
    EXPECT_EQ(pool.allocated(), total_allocated);
    EXPECT_LE(total_allocated, 100);  // Should not exceed pool capacity
    
    // Deallocate all
    for (auto& allocs : thread_allocations) {
        for (auto* ptr : allocs) {
            pool.deallocate(ptr);
        }
    }
    
    EXPECT_EQ(pool.allocated(), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}