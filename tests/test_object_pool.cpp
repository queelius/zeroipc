#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "zeroipc.h"
#include "pool.h"
#include <thread>
#include <vector>
#include <set>
#include <unordered_set>
#include <atomic>

TEST_CASE("zeroipc::pool basic operations", "[zeroipc::pool]") {
    const std::string shm_name = "/test_pool_basic";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Create and use pool") {
        zeroipc::pool<int> pool(shm, "test_pool", 100);
        
        REQUIRE(pool.capacity() == 100);
        REQUIRE(pool.num_allocated() == 0);
        REQUIRE(pool.num_available() == 100);
        REQUIRE(pool.empty());
        REQUIRE(!pool.full());
    }

    SECTION("Acquire and release single objects") {
        zeroipc::pool<int> pool(shm, "single_pool", 10);
        
        // Acquire objects
        auto h1 = pool.acquire();
        REQUIRE(h1 != pool.invalid_handle);
        REQUIRE(pool.is_valid(h1));
        REQUIRE(pool.num_allocated() == 1);
        
        auto h2 = pool.acquire();
        REQUIRE(h2 != pool.invalid_handle);
        REQUIRE(h2 != h1);  // Different handles
        REQUIRE(pool.num_allocated() == 2);
        
        // Use objects
        pool[h1] = 42;
        pool[h2] = 100;
        
        REQUIRE(pool[h1] == 42);
        REQUIRE(pool[h2] == 100);
        
        // Release objects
        pool.release(h1);
        REQUIRE(pool.num_allocated() == 1);
        
        pool.release(h2);
        REQUIRE(pool.num_allocated() == 0);
        REQUIRE(pool.empty());
    }

    SECTION("Acquire all objects") {
        zeroipc::pool<int> pool(shm, "exhaust_pool", 5);
        
        std::vector<zeroipc::pool<int>::handle_type> handles;
        
        // Acquire all objects
        for (int i = 0; i < 5; ++i) {
            auto h = pool.acquire();
            REQUIRE(h != pool.invalid_handle);
            handles.push_back(h);
            pool[h] = i * 10;
        }
        
        REQUIRE(pool.full());
        REQUIRE(pool.num_allocated() == 5);
        REQUIRE(pool.num_available() == 0);
        
        // Should fail when pool is exhausted
        auto h_fail = pool.acquire();
        REQUIRE(h_fail == pool.invalid_handle);
        
        // Release one and acquire again
        pool.release(handles[2]);
        auto h_new = pool.acquire();
        REQUIRE(h_new != pool.invalid_handle);
        REQUIRE(h_new == handles[2]);  // Should reuse the same slot
    }

    SECTION("Get pointer access") {
        zeroipc::pool<double> pool(shm, "ptr_pool", 10);
        
        auto h = pool.acquire();
        REQUIRE(h != pool.invalid_handle);
        
        double* ptr = pool.get(h);
        REQUIRE(ptr != nullptr);
        
        *ptr = 3.14159;
        REQUIRE(pool[h] == Catch::Approx(3.14159));
        
        // Invalid handle
        auto invalid_ptr = pool.get(pool.invalid_handle);
        REQUIRE(invalid_ptr == nullptr);
    }

    SECTION("Acquire and construct") {
        struct Point {
            float x, y, z;
            Point() : x(0), y(0), z(0) {}
            Point(float a, float b, float c) : x(a), y(b), z(c) {}
        };
        
        zeroipc::pool<Point> pool(shm, "point_pool", 10);
        
        // Acquire and construct with arguments
        auto h1 = pool.acquire_construct(1.0f, 2.0f, 3.0f);
        REQUIRE(h1.has_value());
        REQUIRE(pool[*h1].x == 1.0f);
        REQUIRE(pool[*h1].y == 2.0f);
        REQUIRE(pool[*h1].z == 3.0f);
        
        // Acquire and default construct
        auto h2 = pool.acquire_construct();
        REQUIRE(h2.has_value());
        REQUIRE(pool[*h2].x == 0.0f);
        REQUIRE(pool[*h2].y == 0.0f);
        REQUIRE(pool[*h2].z == 0.0f);
    }

    shm.unlink();
}

TEST_CASE("zeroipc::pool batch operations", "[zeroipc::pool]") {
    const std::string shm_name = "/test_pool_batch";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Batch acquire") {
        zeroipc::pool<int> pool(shm, "batch_acquire", 100);
        
        zeroipc::pool<int>::handle_type handles[20];
        size_t acquired = pool.acquire_batch(20, handles);
        
        REQUIRE(acquired == 20);
        REQUIRE(pool.num_allocated() == 20);
        
        // Verify all handles are unique
        std::set<zeroipc::pool<int>::handle_type> unique_handles(handles, handles + 20);
        REQUIRE(unique_handles.size() == 20);
        
        // Use the objects
        for (size_t i = 0; i < 20; ++i) {
            pool[handles[i]] = i * 100;
        }
    }

    SECTION("Batch acquire partial when near full") {
        zeroipc::pool<int> pool(shm, "batch_partial", 10);
        
        // Manually acquire some objects
        for (int i = 0; i < 7; ++i) {
            pool.acquire();
        }
        
        // Try to acquire more than available
        zeroipc::pool<int>::handle_type handles[10];
        size_t acquired = pool.acquire_batch(10, handles);
        
        REQUIRE(acquired == 3);  // Only 3 available
        REQUIRE(pool.full());
    }

    SECTION("Batch release") {
        zeroipc::pool<int> pool(shm, "batch_release", 50);
        
        // Acquire some objects
        std::vector<zeroipc::pool<int>::handle_type> handles;
        for (int i = 0; i < 25; ++i) {
            handles.push_back(pool.acquire());
        }
        
        REQUIRE(pool.num_allocated() == 25);
        
        // Batch release
        pool.release_batch(handles);
        
        REQUIRE(pool.num_allocated() == 0);
        REQUIRE(pool.empty());
    }

    shm.unlink();
}

TEST_CASE("zeroipc::pool reuse patterns", "[zeroipc::pool]") {
    const std::string shm_name = "/test_pool_reuse";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("LIFO reuse pattern") {
        zeroipc::pool<int> pool(shm, "lifo_pool", 5);
        
        // Acquire all
        std::vector<zeroipc::pool<int>::handle_type> handles;
        for (int i = 0; i < 5; ++i) {
            handles.push_back(pool.acquire());
        }
        
        // Release in reverse order
        for (int i = 4; i >= 0; --i) {
            pool.release(handles[i]);
        }
        
        // Re-acquire should get them in LIFO order (stack behavior)
        auto h0 = pool.acquire();
        REQUIRE(h0 == handles[0]);  // Last released, first acquired
        
        auto h1 = pool.acquire();
        REQUIRE(h1 == handles[1]);
    }

    SECTION("Rapid acquire/release cycles") {
        zeroipc::pool<int> pool(shm, "cycle_pool", 3);
        
        for (int cycle = 0; cycle < 100; ++cycle) {
            std::vector<zeroipc::pool<int>::handle_type> handles;
            
            // Acquire all
            for (int i = 0; i < 3; ++i) {
                auto h = pool.acquire();
                REQUIRE(h != pool.invalid_handle);
                handles.push_back(h);
                pool[h] = cycle * 100 + i;
            }
            
            // Verify full
            REQUIRE(pool.full());
            REQUIRE(pool.acquire() == pool.invalid_handle);
            
            // Release all
            for (auto h : handles) {
                pool.release(h);
            }
            
            // Verify empty
            REQUIRE(pool.empty());
        }
        
        // Pool should still be functional
        REQUIRE(pool.num_allocated() == 0);
        REQUIRE(pool.capacity() == 3);
    }

    shm.unlink();
}

TEST_CASE("zeroipc::pool unsafe access", "[zeroipc::pool]") {
    const std::string shm_name = "/test_pool_unsafe";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Access all objects view") {
        zeroipc::pool<int> pool(shm, "unsafe_pool", 10);
        
        // Get view of all objects
        auto all_objects = pool.unsafe_all_objects();
        REQUIRE(all_objects.size() == 10);
        
        // Acquire some objects and set values
        auto h1 = pool.acquire();
        auto h2 = pool.acquire();
        auto h3 = pool.acquire();
        
        pool[h1] = 111;
        pool[h2] = 222;
        pool[h3] = 333;
        
        // Can access via unsafe view
        REQUIRE(all_objects[h1] == 111);
        REQUIRE(all_objects[h2] == 222);
        REQUIRE(all_objects[h3] == 333);
        
        // Note: Other indices contain uninitialized data
    }

    shm.unlink();
}

TEST_CASE("zeroipc::pool concurrent operations", "[zeroipc::pool][concurrent]") {
    const std::string shm_name = "/test_pool_concurrent";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Multiple threads acquiring and releasing") {
        zeroipc::pool<int> pool(shm, "concurrent_pool", 1000);
        const int num_threads = 4;
        const int ops_per_thread = 10000;
        
        std::atomic<int> total_acquired{0};
        std::atomic<int> total_released{0};
        
        std::vector<std::thread> threads;
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&pool, &total_acquired, &total_released, t, ops_per_thread]() {
                std::vector<zeroipc::pool<int>::handle_type> my_handles;
                
                for (int op = 0; op < ops_per_thread; ++op) {
                    // Acquire phase
                    if (op % 2 == 0) {
                        auto h = pool.acquire();
                        if (h != pool.invalid_handle) {
                            pool[h] = t * 1000 + op;
                            my_handles.push_back(h);
                            total_acquired++;
                        }
                    }
                    // Release phase
                    else if (!my_handles.empty()) {
                        pool.release(my_handles.back());
                        my_handles.pop_back();
                        total_released++;
                    }
                }
                
                // Release all remaining
                for (auto h : my_handles) {
                    pool.release(h);
                    total_released++;
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        // All acquired objects should be released
        REQUIRE(total_acquired == total_released);
        REQUIRE(pool.empty());
    }

    SECTION("Stress test with rapid cycling") {
        zeroipc::pool<uint64_t> pool(shm, "stress_pool", 100);
        const int num_threads = 8;
        const int cycles = 1000;
        
        std::atomic<bool> start{false};
        std::atomic<int> successful_cycles{0};
        
        std::vector<std::thread> threads;
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&pool, &start, &successful_cycles, cycles]() {
                // Wait for start signal
                while (!start) {
                    std::this_thread::yield();
                }
                
                for (int c = 0; c < cycles; ++c) {
                    auto h = pool.acquire();
                    if (h != pool.invalid_handle) {
                        // Do some work
                        pool[h] = std::hash<std::thread::id>{}(std::this_thread::get_id());
                        
                        // Small delay
                        std::this_thread::yield();
                        
                        pool.release(h);
                        successful_cycles++;
                    }
                }
            });
        }
        
        // Start all threads simultaneously
        start = true;
        
        for (auto& t : threads) {
            t.join();
        }
        
        // Should have completed many cycles
        REQUIRE(successful_cycles > 0);
        REQUIRE(pool.empty());
    }

    shm.unlink();
}

TEST_CASE("zeroipc::pool cross-process", "[zeroipc::pool][process]") {
    const std::string shm_name = "/test_pool_process";
    shm_unlink(shm_name.c_str());
    
    SECTION("Pool persistence across processes") {
        struct Entity {
            uint32_t id;
            float x, y, z;
            uint32_t health;
        };
        
        // Process 1: Create pool and allocate some entities
        std::vector<zeroipc::pool<Entity>::handle_type> persisted_handles;
        {
            zeroipc::memory shm1(shm_name, 1024 * 1024);
            zeroipc::pool<Entity> pool(shm1, "entity_pool", 100);
            
            // Create some entities
            for (uint32_t i = 0; i < 10; ++i) {
                auto h = pool.acquire();
                REQUIRE(h != pool.invalid_handle);
                
                pool[h] = Entity{
                    .id = i,
                    .x = i * 1.0f,
                    .y = i * 2.0f,
                    .z = i * 3.0f,
                    .health = 100
                };
                
                persisted_handles.push_back(h);
            }
            
            REQUIRE(pool.num_allocated() == 10);
        }
        
        // Process 2: Open pool and verify entities
        {
            zeroipc::memory shm2(shm_name, 0);
            zeroipc::pool<Entity> pool(shm2, "entity_pool");
            
            REQUIRE(pool.capacity() == 100);
            REQUIRE(pool.num_allocated() == 10);
            
            // Verify entities are intact
            for (size_t i = 0; i < persisted_handles.size(); ++i) {
                auto h = persisted_handles[i];
                REQUIRE(pool.is_valid(h));
                
                const Entity& e = pool[h];
                REQUIRE(e.id == i);
                REQUIRE(e.x == Catch::Approx(i * 1.0f));
                REQUIRE(e.y == Catch::Approx(i * 2.0f));
                REQUIRE(e.z == Catch::Approx(i * 3.0f));
                REQUIRE(e.health == 100);
            }
            
            // Allocate more entities
            for (int i = 0; i < 5; ++i) {
                auto h = pool.acquire();
                REQUIRE(h != pool.invalid_handle);
            }
            
            REQUIRE(pool.num_allocated() == 15);
        }
        
        // Process 3: Verify final state
        {
            zeroipc::memory shm3(shm_name, 0);
            zeroipc::pool<Entity> pool(shm3, "entity_pool");
            
            REQUIRE(pool.num_allocated() == 15);
            REQUIRE(pool.num_available() == 85);
        }
    }
    
    shm_unlink(shm_name.c_str());
}

TEST_CASE("zeroipc::pool edge cases", "[zeroipc::pool]") {
    const std::string shm_name = "/test_pool_edge";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Invalid handle operations") {
        zeroipc::pool<int> pool(shm, "edge_pool", 10);
        
        // Release invalid handle (should be safe)
        pool.release(pool.invalid_handle);
        pool.release(999999);  // Out of range
        
        REQUIRE(pool.num_allocated() == 0);
        
        // Check invalid handle
        REQUIRE(!pool.is_valid(pool.invalid_handle));
        REQUIRE(!pool.is_valid(999));
        
        // Get with invalid handle
        REQUIRE(pool.get(pool.invalid_handle) == nullptr);
        REQUIRE(pool.get(999) == nullptr);
    }

    SECTION("Double release") {
        zeroipc::pool<int> pool(shm, "double_release", 5);
        
        auto h = pool.acquire();
        pool[h] = 42;
        
        pool.release(h);
        REQUIRE(pool.num_allocated() == 0);
        
        // Double release (undefined behavior, but shouldn't crash)
        pool.release(h);
        
        // Pool should still be functional
        auto h2 = pool.acquire();
        REQUIRE(h2 != pool.invalid_handle);
    }

    SECTION("Empty pool operations") {
        zeroipc::pool<int> pool(shm, "empty_ops", 10);
        
        REQUIRE(pool.empty());
        
        // Batch acquire on empty pool
        zeroipc::pool<int>::handle_type handles[5];
        size_t acquired = pool.acquire_batch(5, handles);
        REQUIRE(acquired == 5);
        
        // Release batch to return to empty
        pool.release_batch(std::span(handles, 5));
        REQUIRE(pool.empty());
    }

    shm.unlink();
}