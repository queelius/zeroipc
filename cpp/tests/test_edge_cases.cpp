#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/array.h>
#include <zeroipc/queue.h>
#include <zeroipc/stack.h>
#include <limits>
#include <thread>
#include <chrono>

using namespace zeroipc;

class EdgeCaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        Memory::unlink("/test_edge");
    }
    
    void TearDown() override {
        Memory::unlink("/test_edge");
    }
};

// ========== QUEUE EDGE CASES ==========

TEST_F(EdgeCaseTest, QueueSingleElement) {
    Memory mem("/test_edge", 1024*1024);
    Queue<int> queue(mem, "single", 2); // Minimum useful size (circular buffer needs n+1)
    
    // Single element operations
    EXPECT_TRUE(queue.empty());
    EXPECT_TRUE(queue.push(42));
    EXPECT_FALSE(queue.empty());
    EXPECT_FALSE(queue.full());
    EXPECT_EQ(queue.size(), 1);
    
    auto val = queue.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 42);
    EXPECT_TRUE(queue.empty());
}

TEST_F(EdgeCaseTest, QueueFullBehavior) {
    Memory mem("/test_edge", 1024*1024);
    Queue<int> queue(mem, "full", 3); // Can hold 2 elements
    
    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    EXPECT_FALSE(queue.push(3)); // Should fail when full
    
    // After pop, should be able to push again
    queue.pop();
    EXPECT_TRUE(queue.push(3));
    EXPECT_FALSE(queue.push(4)); // Full again
}

TEST_F(EdgeCaseTest, QueueEmptyPop) {
    Memory mem("/test_edge", 1024*1024);
    Queue<int> queue(mem, "empty", 10);
    
    // Pop from empty queue
    auto val = queue.pop();
    EXPECT_FALSE(val.has_value());
    
    // Multiple pops from empty
    for (int i = 0; i < 5; i++) {
        EXPECT_FALSE(queue.pop().has_value());
    }
    
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
}

TEST_F(EdgeCaseTest, QueueWrapAroundStress) {
    Memory mem("/test_edge", 1024*1024);
    Queue<int> queue(mem, "wrap", 5); // Small queue to force wrapping
    
    // Repeatedly fill and empty to test wrap-around
    for (int round = 0; round < 100; round++) {
        // Fill to capacity
        for (int i = 0; i < 4; i++) {
            EXPECT_TRUE(queue.push(round * 100 + i));
        }
        EXPECT_TRUE(queue.full());
        
        // Empty completely
        for (int i = 0; i < 4; i++) {
            auto val = queue.pop();
            ASSERT_TRUE(val.has_value());
            EXPECT_EQ(*val, round * 100 + i);
        }
        EXPECT_TRUE(queue.empty());
    }
}

// ========== STACK EDGE CASES ==========

TEST_F(EdgeCaseTest, StackSingleElement) {
    Memory mem("/test_edge", 1024*1024);
    Stack<int> stack(mem, "single", 1);
    
    EXPECT_TRUE(stack.empty());
    EXPECT_TRUE(stack.push(42));
    EXPECT_TRUE(stack.full()); // Single element capacity
    EXPECT_FALSE(stack.push(43)); // Can't push when full
    
    EXPECT_EQ(*stack.top(), 42);
    EXPECT_EQ(*stack.pop(), 42);
    EXPECT_TRUE(stack.empty());
}

TEST_F(EdgeCaseTest, StackUnderflow) {
    Memory mem("/test_edge", 1024*1024);
    Stack<int> stack(mem, "underflow", 10);
    
    // Pop from empty
    EXPECT_FALSE(stack.pop().has_value());
    EXPECT_FALSE(stack.top().has_value());
    
    // Push one, pop twice
    stack.push(1);
    EXPECT_TRUE(stack.pop().has_value());
    EXPECT_FALSE(stack.pop().has_value());
}

TEST_F(EdgeCaseTest, StackOverflow) {
    Memory mem("/test_edge", 1024*1024);
    Stack<int> stack(mem, "overflow", 3);
    
    EXPECT_TRUE(stack.push(1));
    EXPECT_TRUE(stack.push(2));
    EXPECT_TRUE(stack.push(3));
    EXPECT_TRUE(stack.full());
    EXPECT_FALSE(stack.push(4)); // Overflow
    
    // Verify data integrity after overflow attempt
    EXPECT_EQ(*stack.pop(), 3);
    EXPECT_EQ(*stack.pop(), 2);
    EXPECT_EQ(*stack.pop(), 1);
}

// ========== ARRAY EDGE CASES ==========

TEST_F(EdgeCaseTest, ArrayZeroSizeRejection) {
    Memory mem("/test_edge", 1024*1024);
    
    // Should throw or handle gracefully
    EXPECT_THROW(
        Array<int> arr(mem, "zero", 0),
        std::runtime_error
    );
}

TEST_F(EdgeCaseTest, ArraySingleElement) {
    Memory mem("/test_edge", 1024*1024);
    Array<int> arr(mem, "single", 1);
    
    EXPECT_EQ(arr.size(), 1);
    arr[0] = 42;
    EXPECT_EQ(arr[0], 42);
    
    // Test bounds
    EXPECT_THROW(arr.at(1), std::out_of_range);
}

TEST_F(EdgeCaseTest, ArrayBoundsChecking) {
    Memory mem("/test_edge", 1024*1024);
    Array<int> arr(mem, "bounds", 10);
    
    // Valid access
    arr[0] = 1;
    arr[9] = 10;
    
    // at() should throw
    EXPECT_THROW(arr.at(10), std::out_of_range);
    EXPECT_THROW(arr.at(-1), std::out_of_range);
    EXPECT_THROW(arr.at(SIZE_MAX), std::out_of_range);
}

// ========== MEMORY EDGE CASES ==========

TEST_F(EdgeCaseTest, MemoryMinimumSize) {
    // Minimum size to hold table header
    size_t min_size = 1024; // Should be enough for table
    Memory mem("/test_edge", min_size);
    
    // Should be able to create at least one small structure
    Array<char> arr(mem, "tiny", 1);
    arr[0] = 'x';
    EXPECT_EQ(arr[0], 'x');
}

TEST_F(EdgeCaseTest, TableNameCollision) {
    Memory mem("/test_edge", 10*1024*1024);
    
    // Create first array
    Array<int> arr1(mem, "duplicate", 10);
    
    // Try to create with same name - should throw
    EXPECT_THROW(
        Array<int> arr2(mem, "duplicate", 20),
        std::runtime_error
    );
}

TEST_F(EdgeCaseTest, TableNameBoundary) {
    Memory mem("/test_edge", 10*1024*1024);
    
    // Maximum name length (31 chars + null terminator)
    std::string long_name(31, 'A');
    Array<int> arr1(mem, long_name, 10);
    EXPECT_NO_THROW(arr1[0] = 42);
    
    // Name too long - should be truncated or handled
    std::string too_long(100, 'B');
    Array<int> arr2(mem, too_long, 10);
    EXPECT_NO_THROW(arr2[0] = 42);
}

// ========== CONCURRENT EDGE CASES ==========

TEST_F(EdgeCaseTest, QueueConcurrentEmpty) {
    Memory mem("/test_edge", 1024*1024);
    Queue<int> queue(mem, "concurrent_empty", 100);
    
    // Multiple threads trying to pop from empty queue
    std::vector<std::thread> threads;
    std::atomic<int> pop_count{0};
    
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 100; j++) {
                if (queue.pop().has_value()) {
                    pop_count++;
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    EXPECT_EQ(pop_count, 0); // No successful pops from empty queue
    EXPECT_TRUE(queue.empty());
}

TEST_F(EdgeCaseTest, StackConcurrentFull) {
    Memory mem("/test_edge", 1024*1024);
    Stack<int> stack(mem, "concurrent_full", 10);
    
    // Fill the stack
    for (int i = 0; i < 10; i++) {
        stack.push(i);
    }
    EXPECT_TRUE(stack.full());
    
    // Multiple threads trying to push to full stack
    std::vector<std::thread> threads;
    std::atomic<int> push_count{0};
    
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 100; j++) {
                if (stack.push(j)) {
                    push_count++;
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    EXPECT_EQ(push_count, 0); // No successful pushes to full stack
    EXPECT_TRUE(stack.full());
}

// ========== DATA TYPE EDGE CASES ==========

TEST_F(EdgeCaseTest, LargeStructType) {
    Memory mem("/test_edge", 10*1024*1024);
    
    struct LargeStruct {
        char data[1024];
        int id;
    };
    
    Queue<LargeStruct> queue(mem, "large_struct", 10);
    
    LargeStruct ls;
    std::fill(std::begin(ls.data), std::end(ls.data), 'A');
    ls.id = 42;
    
    EXPECT_TRUE(queue.push(ls));
    
    auto val = queue.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->id, 42);
    EXPECT_EQ(val->data[0], 'A');
    EXPECT_EQ(val->data[1023], 'A');
}

TEST_F(EdgeCaseTest, MinMaxValues) {
    Memory mem("/test_edge", 1024*1024);
    
    // Test with extreme values
    Queue<int64_t> queue(mem, "minmax", 10);
    
    queue.push(std::numeric_limits<int64_t>::min());
    queue.push(std::numeric_limits<int64_t>::max());
    queue.push(0);
    
    EXPECT_EQ(*queue.pop(), std::numeric_limits<int64_t>::min());
    EXPECT_EQ(*queue.pop(), std::numeric_limits<int64_t>::max());
    EXPECT_EQ(*queue.pop(), 0);
}

// ========== RAPID OPERATIONS ==========

TEST_F(EdgeCaseTest, RapidPushPop) {
    Memory mem("/test_edge", 1024*1024);
    Queue<int> queue(mem, "rapid", 10);
    
    // Rapid alternating push/pop
    for (int i = 0; i < 10000; i++) {
        EXPECT_TRUE(queue.push(i));
        auto val = queue.pop();
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(*val, i);
    }
    
    EXPECT_TRUE(queue.empty());
}

TEST_F(EdgeCaseTest, AlternatingFullEmpty) {
    Memory mem("/test_edge", 1024*1024);
    Stack<int> stack(mem, "alternate", 5);
    
    for (int round = 0; round < 100; round++) {
        // Fill completely
        for (int i = 0; i < 5; i++) {
            EXPECT_TRUE(stack.push(i));
        }
        EXPECT_TRUE(stack.full());
        
        // Empty completely
        for (int i = 4; i >= 0; i--) {
            EXPECT_EQ(*stack.pop(), i);
        }
        EXPECT_TRUE(stack.empty());
    }
}