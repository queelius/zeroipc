#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "zeroipc.h"
#include "stack.h"
#include <thread>
#include <vector>
#include <atomic>
#include <sys/mman.h>

TEST_CASE("zeroipc::stack basic operations", "[zeroipc::stack]") {
    const std::string shm_name = "/test_stack_basic";
    // Clean up any leftover shared memory from previous runs
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Create and use stack") {
        zeroipc::stack<int> stack(shm, "int_stack", 10);
        
        REQUIRE(stack.empty());
        REQUIRE(!stack.full());
        REQUIRE(stack.size() == 0);
        REQUIRE(stack.capacity() == 10);
    }

    SECTION("Push and pop") {
        zeroipc::stack<int> stack(shm, "ops_stack", 5);
        
        // Push elements
        REQUIRE(stack.push(10));
        REQUIRE(stack.push(20));
        REQUIRE(stack.push(30));
        
        REQUIRE(stack.size() == 3);
        REQUIRE(!stack.empty());
        REQUIRE(!stack.full());
        
        // Pop elements (LIFO order)
        auto val1 = stack.pop();
        REQUIRE(val1.has_value());
        REQUIRE(*val1 == 30);  // Last in, first out
        
        auto val2 = stack.pop();
        REQUIRE(val2.has_value());
        REQUIRE(*val2 == 20);
        
        REQUIRE(stack.size() == 1);
        
        auto val3 = stack.pop();
        REQUIRE(val3.has_value());
        REQUIRE(*val3 == 10);
        
        REQUIRE(stack.empty());
    }

    SECTION("Stack full behavior") {
        zeroipc::stack<int> stack(shm, "full_stack", 3);
        
        REQUIRE(stack.push(1));
        REQUIRE(stack.push(2));
        REQUIRE(stack.push(3));
        REQUIRE(stack.full());
        
        // Cannot push when full
        REQUIRE(!stack.push(4));
        REQUIRE(stack.size() == 3);
        
        // After pop, can push again
        stack.pop();
        REQUIRE(!stack.full());
        REQUIRE(stack.push(4));
    }

    SECTION("Stack empty behavior") {
        zeroipc::stack<int> stack(shm, "empty_stack", 5);
        
        REQUIRE(stack.empty());
        auto val = stack.pop();
        REQUIRE(!val.has_value());
        
        // Top on empty stack
        auto top_val = stack.top();
        REQUIRE(!top_val.has_value());
    }

    SECTION("Top operation") {
        zeroipc::stack<int> stack(shm, "top_stack", 5);
        
        stack.push(10);
        stack.push(20);
        stack.push(30);
        
        // Top should return last pushed without removing
        auto top1 = stack.top();
        REQUIRE(top1.has_value());
        REQUIRE(*top1 == 30);
        REQUIRE(stack.size() == 3);  // Size unchanged
        
        // Pop and check top again
        stack.pop();
        auto top2 = stack.top();
        REQUIRE(top2.has_value());
        REQUIRE(*top2 == 20);
    }

    SECTION("Clear operation") {
        zeroipc::stack<int> stack(shm, "clear_stack", 5);
        
        stack.push(1);
        stack.push(2);
        stack.push(3);
        REQUIRE(stack.size() == 3);
        
        stack.clear();
        REQUIRE(stack.empty());
        REQUIRE(stack.size() == 0);
        
        // Can push after clear
        REQUIRE(stack.push(10));
        REQUIRE(stack.size() == 1);
    }

    SECTION("Stack discovery by name") {
        // Create and populate stack
        {
            zeroipc::stack<double> s1(shm, "discoverable_stack", 10);
            s1.push(3.14);
            s1.push(2.71);
            s1.push(1.41);
        }
        
        // Open existing stack
        zeroipc::stack<double> s2(shm, "discoverable_stack");
        REQUIRE(s2.size() == 3);
        REQUIRE(s2.capacity() == 10);
        
        // Verify contents (LIFO order)
        auto val = s2.pop();
        REQUIRE(val.has_value());
        REQUIRE(*val == Catch::Approx(1.41));
    }

    SECTION("Multiple stacks in same segment") {
        zeroipc::stack<int> s1(shm, "stack1", 5);
        zeroipc::stack<double> s2(shm, "stack2", 10);
        zeroipc::stack<char> s3(shm, "stack3", 20);
        
        s1.push(100);
        s2.push(3.14);
        s3.push('A');
        
        REQUIRE(s1.size() == 1);
        REQUIRE(s2.size() == 1);
        REQUIRE(s3.size() == 1);
        
        REQUIRE(*s1.pop() == 100);
        REQUIRE(*s2.pop() == Catch::Approx(3.14));
        REQUIRE(*s3.pop() == 'A');
    }

    shm.unlink();
}

TEST_CASE("zeroipc::stack struct types", "[zeroipc::stack]") {
    const std::string shm_name = "/test_stack_struct";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    struct Point {
        int x, y;
        bool operator==(const Point& other) const {
            return x == other.x && y == other.y;
        }
    };

    SECTION("Stack of structs") {
        zeroipc::stack<Point> stack(shm, "point_stack", 5);
        
        Point p1{10, 20};
        Point p2{30, 40};
        Point p3{50, 60};
        
        REQUIRE(stack.push(p1));
        REQUIRE(stack.push(p2));
        REQUIRE(stack.push(p3));
        
        auto val3 = stack.pop();
        REQUIRE(val3.has_value());
        REQUIRE(*val3 == p3);
        
        auto val2 = stack.pop();
        REQUIRE(val2.has_value());
        REQUIRE(*val2 == p2);
        
        auto val1 = stack.pop();
        REQUIRE(val1.has_value());
        REQUIRE(*val1 == p1);
    }

    shm.unlink();
}

TEST_CASE("zeroipc::stack concurrent operations", "[zeroipc::stack][concurrent]") {
    const std::string shm_name = "/test_stack_concurrent";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Concurrent push operations") {
        zeroipc::stack<int> stack(shm, "concurrent_push", 1000);
        const int num_threads = 4;
        const int items_per_thread = 100;
        
        std::vector<std::thread> threads;
        std::atomic<int> success_count{0};
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&stack, &success_count, t, items_per_thread]() {
                for (int i = 0; i < items_per_thread; ++i) {
                    if (stack.push(t * 1000 + i)) {
                        success_count.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        // All pushes should succeed
        REQUIRE(success_count == num_threads * items_per_thread);
        REQUIRE(stack.size() == num_threads * items_per_thread);
    }

    SECTION("Concurrent pop operations") {
        zeroipc::stack<int> stack(shm, "concurrent_pop", 1000);
        
        // Pre-fill stack
        const int total_items = 400;
        for (int i = 0; i < total_items; ++i) {
            stack.push(i);
        }
        
        const int num_threads = 4;
        std::vector<std::thread> threads;
        std::atomic<int> pop_count{0};
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&stack, &pop_count]() {
                while (true) {
                    auto val = stack.pop();
                    if (!val.has_value()) {
                        break;
                    }
                    pop_count.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        // All items should be popped
        REQUIRE(pop_count == total_items);
        REQUIRE(stack.empty());
    }

    SECTION("Mixed push/pop operations") {
        zeroipc::stack<int> stack(shm, "concurrent_mixed", 100);
        const int num_operations = 1000;
        
        std::thread pusher([&stack, num_operations]() {
            for (int i = 0; i < num_operations; ++i) {
                while (!stack.push(i)) {
                    std::this_thread::yield();
                }
            }
        });
        
        std::thread popper([&stack, num_operations]() {
            int count = 0;
            while (count < num_operations) {
                auto val = stack.pop();
                if (val.has_value()) {
                    count++;
                } else {
                    std::this_thread::yield();
                }
            }
        });
        
        pusher.join();
        popper.join();
        
        // Stack should be empty after equal push/pop
        REQUIRE(stack.empty());
    }

    shm.unlink();
}

TEST_CASE("zeroipc::stack cross-process", "[zeroipc::stack][process]") {
    const std::string shm_name = "/test_stack_process";
    shm_unlink(shm_name.c_str());
    
    SECTION("Stack persistence across processes") {
        // Process 1: Create and populate
        {
            zeroipc::memory shm1(shm_name, 1024 * 1024);
            zeroipc::stack<int> stack(shm1, "persistent_stack", 10);
            
            stack.push(100);
            stack.push(200);
            stack.push(300);
            
            REQUIRE(stack.size() == 3);
        }
        
        // Process 2: Open and verify
        {
            zeroipc::memory shm2(shm_name, 0);  // Attach only
            zeroipc::stack<int> stack(shm2, "persistent_stack");
            
            REQUIRE(stack.size() == 3);
            
            // Verify LIFO order
            REQUIRE(*stack.pop() == 300);
            REQUIRE(*stack.pop() == 200);
            REQUIRE(*stack.pop() == 100);
            REQUIRE(stack.empty());
        }
    }
    
    shm_unlink(shm_name.c_str());
}