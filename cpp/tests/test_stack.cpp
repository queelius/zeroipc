#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <zeroipc/stack.h>
#include <thread>
#include <vector>

using namespace zeroipc;

class StackTest : public ::testing::Test {
protected:
    void SetUp() override {
        Memory::unlink("/test_stack");
    }
    
    void TearDown() override {
        Memory::unlink("/test_stack");
    }
};

TEST_F(StackTest, CreateAndBasicOps) {
    Memory mem("/test_stack", 1024*1024);
    Stack<int> stack(mem, "int_stack", 100);
    
    EXPECT_TRUE(stack.empty());
    EXPECT_FALSE(stack.full());
    EXPECT_EQ(stack.size(), 0);
    EXPECT_EQ(stack.capacity(), 100);
    
    // Push some values
    EXPECT_TRUE(stack.push(10));
    EXPECT_TRUE(stack.push(20));
    EXPECT_TRUE(stack.push(30));
    
    EXPECT_FALSE(stack.empty());
    EXPECT_EQ(stack.size(), 3);
    
    // Check top
    auto top = stack.top();
    ASSERT_TRUE(top.has_value());
    EXPECT_EQ(*top, 30);
    
    // Pop values (LIFO order)
    auto val = stack.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 30);
    
    val = stack.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 20);
    
    val = stack.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 10);
    
    EXPECT_TRUE(stack.empty());
    EXPECT_FALSE(stack.pop().has_value());
    EXPECT_FALSE(stack.top().has_value());
}

TEST_F(StackTest, FullStack) {
    Memory mem("/test_stack", 1024*1024);
    Stack<int> stack(mem, "small_stack", 3);
    
    EXPECT_TRUE(stack.push(1));
    EXPECT_TRUE(stack.push(2));
    EXPECT_TRUE(stack.push(3));
    EXPECT_FALSE(stack.push(4)); // Should fail - stack full
    
    EXPECT_TRUE(stack.full());
    
    stack.pop();
    EXPECT_FALSE(stack.full());
    EXPECT_TRUE(stack.push(4));
}

TEST_F(StackTest, OpenExisting) {
    Memory mem("/test_stack", 1024*1024);
    
    {
        Stack<double> stack1(mem, "double_stack", 50);
        stack1.push(3.14);
        stack1.push(2.71);
        stack1.push(1.41);
    }
    
    Stack<double> stack2(mem, "double_stack");
    EXPECT_EQ(stack2.capacity(), 50);
    EXPECT_EQ(stack2.size(), 3);
    
    EXPECT_DOUBLE_EQ(*stack2.pop(), 1.41);
    EXPECT_DOUBLE_EQ(*stack2.pop(), 2.71);
    EXPECT_DOUBLE_EQ(*stack2.pop(), 3.14);
}

TEST_F(StackTest, StructType) {
    struct Point {
        float x, y, z;
    };
    
    Memory mem("/test_stack", 1024*1024);
    Stack<Point> stack(mem, "point_stack", 10);
    
    Point p1{1.0f, 2.0f, 3.0f};
    Point p2{4.0f, 5.0f, 6.0f};
    
    EXPECT_TRUE(stack.push(p1));
    EXPECT_TRUE(stack.push(p2));
    
    auto p = stack.pop();
    ASSERT_TRUE(p.has_value());
    EXPECT_FLOAT_EQ(p->x, 4.0f);
    EXPECT_FLOAT_EQ(p->y, 5.0f);
    EXPECT_FLOAT_EQ(p->z, 6.0f);
}

TEST_F(StackTest, ConcurrentPushPop) {
    Memory mem("/test_stack", 10*1024*1024);
    Stack<int> stack(mem, "concurrent_stack", 10000);
    
    const int num_threads = 4;
    const int items_per_thread = 1000;
    std::vector<std::thread> threads;
    
    // Multiple threads pushing and popping
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&, t]() {
            // Push phase
            for (int i = 0; i < items_per_thread; i++) {
                int value = t * items_per_thread + i;
                while (!stack.push(value)) {
                    std::this_thread::yield();
                }
            }
            
            // Pop phase
            for (int i = 0; i < items_per_thread / 2; i++) {
                while (!stack.pop()) {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    for (auto& t : threads) t.join();
    
    // Should have half the items left
    EXPECT_EQ(stack.size(), num_threads * items_per_thread / 2);
    
    // Pop remaining
    int count = 0;
    while (stack.pop()) {
        count++;
    }
    EXPECT_EQ(count, num_threads * items_per_thread / 2);
    EXPECT_TRUE(stack.empty());
}