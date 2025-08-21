#include <catch2/catch_test_macros.hpp>
#include "posix_shm.h"
#include "shm_set.h"
#include <thread>
#include <vector>
#include <set>

TEST_CASE("shm_set basic operations", "[shm_set]") {
    const std::string shm_name = "/test_set_basic";
    shm_unlink(shm_name.c_str());
    posix_shm shm(shm_name, 10 * 1024 * 1024);

    SECTION("Create and use set") {
        shm_set<int> set(shm, "test_set", 100);
        
        REQUIRE(set.empty());
        REQUIRE(set.size() == 0);
    }

    SECTION("Insert and contains") {
        shm_set<uint32_t> set(shm, "number_set", 100);
        
        // Insert elements
        REQUIRE(set.insert(42));
        REQUIRE(set.insert(7));
        REQUIRE(set.insert(100));
        
        REQUIRE(set.size() == 3);
        REQUIRE(!set.empty());
        
        // Check membership
        REQUIRE(set.contains(42));
        REQUIRE(set.contains(7));
        REQUIRE(set.contains(100));
        REQUIRE(!set.contains(999));
    }

    SECTION("Duplicate insertion") {
        shm_set<int> set(shm, "dup_set", 10);
        
        REQUIRE(set.insert(5));
        REQUIRE(!set.insert(5));  // Should fail
        REQUIRE(set.size() == 1);
    }

    SECTION("Erase elements") {
        shm_set<int> set(shm, "erase_set", 10);
        
        set.insert(1);
        set.insert(2);
        set.insert(3);
        REQUIRE(set.size() == 3);
        
        REQUIRE(set.erase(2));
        REQUIRE(set.size() == 2);
        REQUIRE(!set.contains(2));
        
        // Erase non-existent
        REQUIRE(!set.erase(999));
        REQUIRE(set.size() == 2);
    }

    SECTION("Clear operation") {
        shm_set<int> set(shm, "clear_set", 10);
        
        for (int i = 0; i < 5; ++i) {
            set.insert(i);
        }
        REQUIRE(set.size() == 5);
        
        set.clear();
        REQUIRE(set.empty());
        REQUIRE(set.size() == 0);
        
        // Can insert after clear
        REQUIRE(set.insert(100));
    }

    SECTION("For each iteration") {
        shm_set<int> set(shm, "foreach_set", 10);
        
        for (int i = 0; i < 5; ++i) {
            set.insert(i * 2);
        }
        
        int sum = 0;
        int count = 0;
        set.for_each([&](int value) {
            sum += value;
            count++;
        });
        
        REQUIRE(count == 5);
        REQUIRE(sum == 20);  // 0+2+4+6+8
    }

    shm.unlink();
}

TEST_CASE("shm_set set operations", "[shm_set]") {
    const std::string shm_name = "/test_set_ops";
    shm_unlink(shm_name.c_str());
    posix_shm shm(shm_name, 10 * 1024 * 1024);

    shm_set<int> set1(shm, "set1", 10);
    shm_set<int> set2(shm, "set2", 10);
    
    // Populate sets
    for (int i = 0; i < 5; ++i) {
        set1.insert(i);
    }
    for (int i = 3; i < 8; ++i) {
        set2.insert(i);
    }

    SECTION("Union") {
        auto result = set1.set_union(shm, "union_result", set2);
        
        // Should contain 0,1,2,3,4,5,6,7
        REQUIRE(result.size() == 8);
        for (int i = 0; i < 8; ++i) {
            REQUIRE(result.contains(i));
        }
    }

    SECTION("Intersection") {
        auto result = set1.set_intersection(shm, "intersect_result", set2);
        
        // Should contain 3,4
        REQUIRE(result.size() == 2);
        REQUIRE(result.contains(3));
        REQUIRE(result.contains(4));
        REQUIRE(!result.contains(0));
        REQUIRE(!result.contains(7));
    }

    SECTION("Difference") {
        auto result = set1.set_difference(shm, "diff_result", set2);
        
        // Should contain 0,1,2
        REQUIRE(result.size() == 3);
        REQUIRE(result.contains(0));
        REQUIRE(result.contains(1));
        REQUIRE(result.contains(2));
        REQUIRE(!result.contains(3));
    }

    SECTION("Subset/Superset") {
        shm_set<int> small_set(shm, "small", 10);
        shm_set<int> large_set(shm, "large", 10);
        
        small_set.insert(1);
        small_set.insert(2);
        
        large_set.insert(1);
        large_set.insert(2);
        large_set.insert(3);
        
        REQUIRE(small_set.is_subset_of(large_set));
        REQUIRE(!large_set.is_subset_of(small_set));
        
        REQUIRE(large_set.is_superset_of(small_set));
        REQUIRE(!small_set.is_superset_of(large_set));
        
        // Set is subset/superset of itself
        REQUIRE(small_set.is_subset_of(small_set));
        REQUIRE(small_set.is_superset_of(small_set));
    }

    SECTION("Disjoint sets") {
        shm_set<int> set_a(shm, "set_a", 10);
        shm_set<int> set_b(shm, "set_b", 10);
        shm_set<int> set_c(shm, "set_c", 10);
        
        set_a.insert(1);
        set_a.insert(2);
        
        set_b.insert(3);
        set_b.insert(4);
        
        set_c.insert(2);
        set_c.insert(3);
        
        REQUIRE(set_a.is_disjoint(set_b));
        REQUIRE(set_b.is_disjoint(set_a));
        
        REQUIRE(!set_a.is_disjoint(set_c));
        REQUIRE(!set_b.is_disjoint(set_c));
    }

    shm.unlink();
}

TEST_CASE("shm_set concurrent operations", "[shm_set][concurrent]") {
    const std::string shm_name = "/test_set_concurrent";
    shm_unlink(shm_name.c_str());
    posix_shm shm(shm_name, 10 * 1024 * 1024);

    SECTION("Concurrent insertions") {
        shm_set<int> set(shm, "concurrent_insert", 10000);
        const int num_threads = 4;
        const int items_per_thread = 1000;
        
        std::vector<std::thread> threads;
        std::atomic<int> success_count{0};
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&set, &success_count, t, items_per_thread]() {
                for (int i = 0; i < items_per_thread; ++i) {
                    int value = t * items_per_thread + i;
                    if (set.insert(value)) {
                        success_count.fetch_add(1);
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        REQUIRE(success_count == num_threads * items_per_thread);
        REQUIRE(set.size() == num_threads * items_per_thread);
    }

    SECTION("Concurrent membership tests") {
        shm_set<int> set(shm, "concurrent_contains", 1000);
        
        // Pre-populate
        for (int i = 0; i < 100; ++i) {
            set.insert(i * 2);  // Even numbers
        }
        
        std::vector<std::thread> threads;
        std::atomic<int> found_count{0};
        std::atomic<int> not_found_count{0};
        
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&set, &found_count, &not_found_count]() {
                for (int iter = 0; iter < 1000; ++iter) {
                    for (int i = 0; i < 200; ++i) {
                        if (set.contains(i)) {
                            if (i % 2 == 0) {
                                found_count++;
                            }
                        } else {
                            if (i % 2 == 1) {
                                not_found_count++;
                            }
                        }
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        REQUIRE(found_count == 4 * 1000 * 100);      // Even numbers found
        REQUIRE(not_found_count == 4 * 1000 * 100);  // Odd numbers not found
    }

    shm.unlink();
}

TEST_CASE("shm_set cross-process", "[shm_set][process]") {
    const std::string shm_name = "/test_set_process";
    shm_unlink(shm_name.c_str());
    
    SECTION("Set persistence across processes") {
        // Process 1: Create and populate
        {
            posix_shm shm1(shm_name, 1024 * 1024);
            shm_set<int> set(shm1, "persistent_set", 100);
            
            set.insert(10);
            set.insert(20);
            set.insert(30);
            
            REQUIRE(set.size() == 3);
        }
        
        // Process 2: Open and verify
        {
            posix_shm shm2(shm_name, 0);  // Attach only
            shm_set<int> set(shm2, "persistent_set");
            
            REQUIRE(set.size() == 3);
            REQUIRE(set.contains(10));
            REQUIRE(set.contains(20));
            REQUIRE(set.contains(30));
            
            // Modify
            set.erase(20);
            set.insert(40);
        }
        
        // Process 3: Verify modifications
        {
            posix_shm shm3(shm_name, 0);
            shm_set<int> set(shm3, "persistent_set");
            
            REQUIRE(set.size() == 3);
            REQUIRE(!set.contains(20));
            REQUIRE(set.contains(40));
        }
    }
    
    shm_unlink(shm_name.c_str());
}