#include <gtest/gtest.h>
#include "zeroipc/memory.h"
#include "zeroipc/table.h"
#include "zeroipc/queue.h"
#include "zeroipc/stack.h"
#include "zeroipc/array.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

class CoverageTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up any existing shared memory
        shm_unlink("/test_coverage");
    }
    
    void TearDown() override {
        shm_unlink("/test_coverage");
    }
};

// Test Memory edge cases
TEST_F(CoverageTest, MemoryEdgeCases) {
    zeroipc::Memory mem("/test_coverage", 64 * 1024);
    
    // Test allocation with very long name (should throw)
    std::string long_name(100, 'x');
    EXPECT_THROW(mem.allocate(long_name, 100), std::invalid_argument);
    
    // Test finding non-existent entry
    size_t offset, size;
    EXPECT_FALSE(mem.find("nonexistent", offset, size));
    
    // Test successful allocation and find
    size_t alloc_offset = mem.allocate("test_struct", 256);
    EXPECT_TRUE(mem.find("test_struct", offset, size));
    EXPECT_EQ(offset, alloc_offset);
    EXPECT_EQ(size, 256);
    
    // Test duplicate name allocation (should throw)
    EXPECT_THROW(mem.allocate("test_struct", 128), std::invalid_argument);
    
    // Test at() method with valid offset
    void* ptr = mem.at(alloc_offset);
    EXPECT_NE(ptr, nullptr);
    
    // Test at() method with invalid offset
    EXPECT_THROW(mem.at(100 * 1024), std::out_of_range);
}

// Test Table edge cases
TEST_F(CoverageTest, TableEdgeCases) {
    zeroipc::Memory mem("/test_coverage", 64 * 1024);
    auto* table = mem.table();
    
    // Test entry count
    size_t initial_count = table->entry_count();
    
    // Add entries until table is full
    size_t max_entries = table->max_entries();
    for (size_t i = initial_count; i < max_entries - 1; i++) {
        std::string name = "entry_" + std::to_string(i);
        uint64_t offset = 1000 + i * 100;
        EXPECT_TRUE(table->add(name, offset, 100));
    }
    
    // Try to add one more (should succeed for the last slot)
    EXPECT_TRUE(table->add("last_entry", 99000, 100));
    
    // Try to add beyond capacity (should return false)
    EXPECT_FALSE(table->add("overflow", 100000, 100));
    
    // Verify we can find entries
    auto* entry = table->find("entry_0");
    if (entry) {
        EXPECT_EQ(entry->offset, 1000);
        EXPECT_EQ(entry->size, 100);
    }
    
    // Test finding non-existent entry
    EXPECT_EQ(table->find("nonexistent"), nullptr);
}

// Test Queue edge cases and all methods
TEST_F(CoverageTest, QueueComprehensive) {
    zeroipc::Memory mem("/test_coverage", 64 * 1024);
    
    // Test zero capacity (should throw)
    EXPECT_THROW(zeroipc::Queue<int>(mem, "zero_queue", 0), std::invalid_argument);
    
    // Test normal queue
    zeroipc::Queue<int> q(mem, "queue", 10);
    
    // Test empty state
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());
    EXPECT_EQ(q.size(), 0);
    EXPECT_EQ(q.capacity(), 10);
    EXPECT_FALSE(q.pop().has_value());
    
    // Note: Circular queue reserves one slot to distinguish full from empty
    // So a queue with capacity 10 can hold at most 9 elements
    
    // Fill queue to actual capacity (9 elements for capacity 10)
    for (int i = 0; i < 9; i++) {
        EXPECT_TRUE(q.push(i));
    }
    
    EXPECT_FALSE(q.empty());
    EXPECT_TRUE(q.full());
    EXPECT_EQ(q.size(), 9);
    
    // Try to push when full
    EXPECT_FALSE(q.push(9));
    
    // Pop all 9 elements and verify order
    for (int i = 0; i < 9; i++) {
        auto val = q.pop();
        EXPECT_TRUE(val.has_value());
        EXPECT_EQ(*val, i);
    }
    
    // Queue should be empty again
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.pop().has_value());
    
    // Test wrap-around behavior
    for (int cycle = 0; cycle < 3; cycle++) {
        // Push 5 elements
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(q.push(cycle * 100 + i));
        }
        // Pop 3 elements
        for (int i = 0; i < 3; i++) {
            auto val = q.pop();
            EXPECT_TRUE(val.has_value());
            EXPECT_EQ(*val, cycle * 100 + i);
        }
        // Push 3 more
        for (int i = 5; i < 8; i++) {
            EXPECT_TRUE(q.push(cycle * 100 + i));
        }
        // Pop all remaining
        while (!q.empty()) {
            [[maybe_unused]] auto val = q.pop();
        }
    }
}

// Test Stack edge cases and all methods
TEST_F(CoverageTest, StackComprehensive) {
    zeroipc::Memory mem("/test_coverage", 64 * 1024);
    
    // Create stack
    zeroipc::Stack<int> stack(mem, "stack", 10);
    
    // Test empty state
    EXPECT_TRUE(stack.empty());
    EXPECT_FALSE(stack.full());
    EXPECT_EQ(stack.size(), 0);
    EXPECT_EQ(stack.capacity(), 10);
    EXPECT_FALSE(stack.pop().has_value());
    EXPECT_FALSE(stack.top().has_value());
    
    // Push elements
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(stack.push(i));
        auto top = stack.top();
        EXPECT_TRUE(top.has_value());
        EXPECT_EQ(*top, i);
    }
    
    EXPECT_TRUE(stack.full());
    EXPECT_FALSE(stack.push(11)); // Should fail when full
    
    // Pop elements (LIFO order)
    for (int i = 9; i >= 0; i--) {
        auto val = stack.pop();
        EXPECT_TRUE(val.has_value());
        EXPECT_EQ(*val, i);
    }
    
    EXPECT_TRUE(stack.empty());
}

// Test Array edge cases and all methods
TEST_F(CoverageTest, ArrayComprehensive) {
    zeroipc::Memory mem("/test_coverage", 64 * 1024);
    
    // Test zero capacity (should throw)
    EXPECT_THROW(zeroipc::Array<int>(mem, "zero_array", 0), std::invalid_argument);
    
    // Create array
    zeroipc::Array<double> arr(mem, "array", 100);
    
    // Test capacity
    EXPECT_EQ(arr.capacity(), 100);
    
    // Test operator[]
    for (size_t i = 0; i < 100; i++) {
        arr[i] = i * 3.14;
    }
    
    for (size_t i = 0; i < 100; i++) {
        EXPECT_DOUBLE_EQ(arr[i], i * 3.14);
    }
    
    // Test at() with bounds checking
    EXPECT_DOUBLE_EQ(arr.at(0), 0.0);
    EXPECT_DOUBLE_EQ(arr.at(99), 99 * 3.14);
    EXPECT_THROW(arr.at(100), std::out_of_range);
    EXPECT_THROW(arr.at(1000), std::out_of_range);
    
    // Test direct access with bounds checking
    arr[50] = 123.456;
    EXPECT_DOUBLE_EQ(arr[50], 123.456);
    EXPECT_THROW(arr[100], std::out_of_range);
    EXPECT_THROW(arr[1000] = 0.0, std::out_of_range);
    
    // Test data() pointer
    double* data = arr.data();
    EXPECT_NE(data, nullptr);
    EXPECT_DOUBLE_EQ(data[0], 0.0);
    
    // Test fill()
    arr.fill(42.0);
    for (size_t i = 0; i < 100; i++) {
        EXPECT_DOUBLE_EQ(arr[i], 42.0);
    }
    
    // Test iterator access
    auto* begin_ptr = arr.begin();
    auto* end_ptr = arr.end();
    EXPECT_EQ(end_ptr - begin_ptr, 100);
    EXPECT_DOUBLE_EQ(*begin_ptr, 42.0);
}

// Test Memory move semantics
TEST_F(CoverageTest, MemoryMoveSemantics) {
    // Test move constructor
    {
        zeroipc::Memory mem1("/test_coverage", 64 * 1024);
        size_t offset1 = mem1.allocate("struct1", 256);
        
        zeroipc::Memory mem2(std::move(mem1));
        
        // mem2 should have the data
        size_t offset, size;
        EXPECT_TRUE(mem2.find("struct1", offset, size));
        EXPECT_EQ(offset, offset1);
        EXPECT_EQ(size, 256);
    }
    
    // Test move assignment
    {
        zeroipc::Memory mem1("/test_coverage", 64 * 1024);
        mem1.allocate("struct1", 256);
        
        zeroipc::Memory mem2("/test_coverage");  // Open existing
        mem2 = std::move(mem1);
        
        // mem2 should have the data
        size_t offset, size;
        EXPECT_TRUE(mem2.find("struct1", offset, size));
        EXPECT_EQ(size, 256);
    }
}

// Test concurrent Queue operations
TEST_F(CoverageTest, QueueConcurrent) {
    zeroipc::Memory mem("/test_coverage", 256 * 1024);
    zeroipc::Queue<int> queue(mem, "concurrent_queue", 1000);
    
    const int num_producers = 4;
    const int num_consumers = 4;
    const int items_per_producer = 100;
    
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::vector<std::thread> threads;
    
    // Start producers
    for (int p = 0; p < num_producers; p++) {
        threads.emplace_back([&, p]() {
            for (int i = 0; i < items_per_producer; i++) {
                int value = p * 1000 + i;
                while (!queue.push(value)) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
                produced.fetch_add(1);
            }
        });
    }
    
    // Start consumers
    for (int c = 0; c < num_consumers; c++) {
        threads.emplace_back([&]() {
            while (consumed.load() < num_producers * items_per_producer) {
                auto val = queue.pop();
                if (val.has_value()) {
                    consumed.fetch_add(1);
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(produced.load(), num_producers * items_per_producer);
    EXPECT_EQ(consumed.load(), num_producers * items_per_producer);
    EXPECT_TRUE(queue.empty());
}

// Test concurrent Stack operations
TEST_F(CoverageTest, StackConcurrent) {
    zeroipc::Memory mem("/test_coverage", 256 * 1024);
    zeroipc::Stack<int> stack(mem, "concurrent_stack", 1000);
    
    const int num_threads = 4;
    const int operations_per_thread = 100;
    
    std::atomic<int> push_count{0};
    std::atomic<int> pop_count{0};
    std::vector<std::thread> threads;
    
    // Mixed push/pop operations
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < operations_per_thread; i++) {
                if (i % 2 == 0) {
                    // Push
                    if (stack.push(t * 1000 + i)) {
                        push_count.fetch_add(1);
                    }
                } else {
                    // Pop
                    if (stack.pop().has_value()) {
                        pop_count.fetch_add(1);
                    }
                }
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Drain remaining items
    while (stack.pop().has_value()) {
        pop_count.fetch_add(1);
    }
    
    EXPECT_EQ(push_count.load(), pop_count.load());
    EXPECT_TRUE(stack.empty());
}

// Test opening existing structures
TEST_F(CoverageTest, OpenExistingStructures) {
    // Create structures
    {
        zeroipc::Memory mem("/test_coverage", 64 * 1024);
        zeroipc::Queue<int> queue(mem, "persist_queue", 50);
        zeroipc::Stack<double> stack(mem, "persist_stack", 30);
        zeroipc::Array<float> array(mem, "persist_array", 100);
        
        // Add some data
        queue.push(42);
        queue.push(43);
        stack.push(3.14);
        stack.push(2.71);
        array[0] = 1.23f;
        array[99] = 4.56f;
    }
    
    // Open and verify
    {
        zeroipc::Memory mem("/test_coverage");
        
        // Open queue
        zeroipc::Queue<int> queue(mem, "persist_queue");
        EXPECT_EQ(queue.size(), 2);
        auto q1 = queue.pop();
        EXPECT_TRUE(q1.has_value());
        EXPECT_EQ(*q1, 42);
        
        // Open stack
        zeroipc::Stack<double> stack(mem, "persist_stack");
        EXPECT_EQ(stack.size(), 2);
        auto s1 = stack.pop();
        EXPECT_TRUE(s1.has_value());
        EXPECT_DOUBLE_EQ(*s1, 2.71);
        
        // Open array
        zeroipc::Array<float> array(mem, "persist_array");
        EXPECT_EQ(array.capacity(), 100);
        EXPECT_FLOAT_EQ(array[0], 1.23f);
        EXPECT_FLOAT_EQ(array[99], 4.56f);
    }
}

// Test type mismatch detection
TEST_F(CoverageTest, TypeMismatchDetection) {
    // Create with one type
    {
        zeroipc::Memory mem("/test_coverage", 64 * 1024);
        zeroipc::Queue<int> queue(mem, "type_test", 10);
        queue.push(42);
    }
    
    // Try to open with different type (should throw)
    {
        zeroipc::Memory mem("/test_coverage");
        EXPECT_THROW(zeroipc::Queue<double>(mem, "type_test"), std::runtime_error);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}