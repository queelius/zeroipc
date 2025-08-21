#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "posix_shm.h"
#include "shm_hash_map.h"
#include <thread>
#include <vector>
#include <unordered_set>
#include <random>

TEST_CASE("shm_hash_map basic operations", "[shm_hash_map]") {
    const std::string shm_name = "/test_hash_map_basic";
    shm_unlink(shm_name.c_str());
    posix_shm shm(shm_name, 10 * 1024 * 1024);

    SECTION("Create and use hash map") {
        shm_hash_map<int, int> map(shm, "test_map", 100);
        
        REQUIRE(map.empty());
        REQUIRE(map.size() == 0);
        REQUIRE(map.bucket_count() > 100);  // Power of 2 larger than capacity
    }

    SECTION("Insert and find") {
        shm_hash_map<uint32_t, double> map(shm, "number_map", 100);
        
        // Insert elements
        REQUIRE(map.insert(42, 3.14));
        REQUIRE(map.insert(7, 2.71));
        REQUIRE(map.insert(100, 1.41));
        
        REQUIRE(map.size() == 3);
        REQUIRE(!map.empty());
        
        // Find elements
        auto* val1 = map.find(42);
        REQUIRE(val1 != nullptr);
        REQUIRE(*val1 == Catch::Approx(3.14));
        
        auto* val2 = map.find(7);
        REQUIRE(val2 != nullptr);
        REQUIRE(*val2 == Catch::Approx(2.71));
        
        // Non-existent key
        auto* val3 = map.find(999);
        REQUIRE(val3 == nullptr);
    }

    SECTION("Duplicate key insertion") {
        shm_hash_map<int, int> map(shm, "dup_map", 10);
        
        REQUIRE(map.insert(5, 100));
        REQUIRE(!map.insert(5, 200));  // Should fail
        
        auto* val = map.find(5);
        REQUIRE(val != nullptr);
        REQUIRE(*val == 100);  // Original value unchanged
    }

    SECTION("Update existing value") {
        shm_hash_map<int, int> map(shm, "update_map", 10);
        
        map.insert(1, 10);
        REQUIRE(map.update(1, 20));
        
        auto* val = map.find(1);
        REQUIRE(val != nullptr);
        REQUIRE(*val == 20);
        
        // Update non-existent key
        REQUIRE(!map.update(999, 30));
    }

    SECTION("Insert or update") {
        shm_hash_map<int, int> map(shm, "upsert_map", 10);
        
        // Insert new
        map.insert_or_update(1, 10);
        REQUIRE(map.size() == 1);
        REQUIRE(*map.find(1) == 10);
        
        // Update existing
        map.insert_or_update(1, 20);
        REQUIRE(map.size() == 1);
        REQUIRE(*map.find(1) == 20);
    }

    SECTION("Erase elements") {
        shm_hash_map<int, int> map(shm, "erase_map", 10);
        
        map.insert(1, 10);
        map.insert(2, 20);
        map.insert(3, 30);
        REQUIRE(map.size() == 3);
        
        REQUIRE(map.erase(2));
        REQUIRE(map.size() == 2);
        REQUIRE(map.find(2) == nullptr);
        
        // Erase non-existent
        REQUIRE(!map.erase(999));
        REQUIRE(map.size() == 2);
    }

    SECTION("Contains check") {
        shm_hash_map<int, int> map(shm, "contains_map", 10);
        
        map.insert(42, 100);
        REQUIRE(map.contains(42));
        REQUIRE(!map.contains(43));
    }

    SECTION("Clear operation") {
        shm_hash_map<int, int> map(shm, "clear_map", 10);
        
        for (int i = 0; i < 5; ++i) {
            map.insert(i, i * 10);
        }
        REQUIRE(map.size() == 5);
        
        map.clear();
        REQUIRE(map.empty());
        REQUIRE(map.size() == 0);
        
        // Can insert after clear
        REQUIRE(map.insert(100, 1000));
    }

    SECTION("For each iteration") {
        shm_hash_map<int, int> map(shm, "foreach_map", 10);
        
        for (int i = 0; i < 5; ++i) {
            map.insert(i, i * 10);
        }
        
        int sum_keys = 0;
        int sum_values = 0;
        map.for_each([&](int key, int value) {
            sum_keys += key;
            sum_values += value;
        });
        
        REQUIRE(sum_keys == 10);   // 0+1+2+3+4
        REQUIRE(sum_values == 100); // 0+10+20+30+40
    }

    SECTION("Hash map discovery by name") {
        // Create and populate
        {
            shm_hash_map<int, double> map1(shm, "discoverable", 50);
            map1.insert(1, 1.1);
            map1.insert(2, 2.2);
            map1.insert(3, 3.3);
        }
        
        // Open existing
        shm_hash_map<int, double> map2(shm, "discoverable");
        REQUIRE(map2.size() == 3);
        REQUIRE(*map2.find(2) == Catch::Approx(2.2));
    }

    shm.unlink();
}

TEST_CASE("shm_hash_map collision handling", "[shm_hash_map]") {
    const std::string shm_name = "/test_hash_collision";
    shm_unlink(shm_name.c_str());
    posix_shm shm(shm_name, 10 * 1024 * 1024);

    SECTION("Linear probing") {
        // Small map to force collisions
        shm_hash_map<int, int> map(shm, "collision_map", 8);
        
        // Insert values that might collide
        for (int i = 0; i < 6; ++i) {
            REQUIRE(map.insert(i * 8, i));  // Same hash bucket
        }
        
        // Verify all can be found
        for (int i = 0; i < 6; ++i) {
            auto* val = map.find(i * 8);
            REQUIRE(val != nullptr);
            REQUIRE(*val == i);
        }
    }

    SECTION("Tombstone handling") {
        shm_hash_map<int, int> map(shm, "tombstone_map", 8);
        
        // Create collision chain
        map.insert(0, 100);
        map.insert(8, 200);  // Collides with 0
        map.insert(16, 300); // Also collides
        
        // Remove middle element (creates tombstone)
        REQUIRE(map.erase(8));
        
        // Can still find element after tombstone
        REQUIRE(*map.find(16) == 300);
        
        // Can reuse tombstone slot
        REQUIRE(map.insert(24, 400));
        REQUIRE(*map.find(24) == 400);
    }

    shm.unlink();
}

TEST_CASE("shm_hash_map concurrent operations", "[shm_hash_map][concurrent]") {
    const std::string shm_name = "/test_hash_concurrent";
    shm_unlink(shm_name.c_str());
    posix_shm shm(shm_name, 10 * 1024 * 1024);

    SECTION("Concurrent insertions") {
        shm_hash_map<int, int> map(shm, "concurrent_insert", 10000);
        const int num_threads = 4;
        const int items_per_thread = 1000;
        
        std::vector<std::thread> threads;
        std::atomic<int> success_count{0};
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&map, &success_count, t, items_per_thread]() {
                for (int i = 0; i < items_per_thread; ++i) {
                    int key = t * items_per_thread + i;
                    if (map.insert(key, key * 2)) {
                        success_count.fetch_add(1);
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        REQUIRE(success_count == num_threads * items_per_thread);
        REQUIRE(map.size() == num_threads * items_per_thread);
        
        // Verify all values
        for (int i = 0; i < num_threads * items_per_thread; ++i) {
            auto* val = map.find(i);
            REQUIRE(val != nullptr);
            REQUIRE(*val == i * 2);
        }
    }

    SECTION("Concurrent reads") {
        shm_hash_map<int, int> map(shm, "concurrent_read", 1000);
        
        // Pre-populate
        for (int i = 0; i < 100; ++i) {
            map.insert(i, i * 10);
        }
        
        std::vector<std::thread> threads;
        std::atomic<int> found_count{0};
        
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&map, &found_count]() {
                for (int iter = 0; iter < 1000; ++iter) {
                    for (int i = 0; i < 100; ++i) {
                        if (auto* val = map.find(i)) {
                            if (*val == i * 10) {
                                found_count++;
                            }
                        }
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        REQUIRE(found_count == 4 * 1000 * 100);
    }

    SECTION("Mixed operations") {
        shm_hash_map<int, int> map(shm, "concurrent_mixed", 1000);
        const int num_operations = 10000;
        
        std::thread inserter([&map, num_operations]() {
            for (int i = 0; i < num_operations; ++i) {
                map.insert(i, i);
            }
        });
        
        std::thread updater([&map, num_operations]() {
            for (int i = 0; i < num_operations; ++i) {
                map.update(i, i * 2);
            }
        });
        
        std::thread reader([&map, num_operations]() {
            int found = 0;
            while (found < num_operations / 2) {
                for (int i = 0; i < num_operations; ++i) {
                    if (map.find(i)) {
                        found++;
                    }
                }
            }
        });
        
        inserter.join();
        updater.join();
        reader.join();
        
        // Some elements should exist
        REQUIRE(map.size() > 0);
        REQUIRE(map.size() <= num_operations);
    }

    shm.unlink();
}

TEST_CASE("shm_hash_map with complex types", "[shm_hash_map]") {
    const std::string shm_name = "/test_hash_complex";
    shm_unlink(shm_name.c_str());
    posix_shm shm(shm_name, 10 * 1024 * 1024);

    struct Point {
        float x, y, z;
        bool operator==(const Point& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    SECTION("Struct values") {
        shm_hash_map<uint32_t, Point> map(shm, "point_map", 100);
        
        Point p1{1.0f, 2.0f, 3.0f};
        Point p2{4.0f, 5.0f, 6.0f};
        
        map.insert(1, p1);
        map.insert(2, p2);
        
        auto* found = map.find(1);
        REQUIRE(found != nullptr);
        REQUIRE(found->x == 1.0f);
        REQUIRE(found->y == 2.0f);
        REQUIRE(found->z == 3.0f);
    }

    SECTION("Load factor") {
        shm_hash_map<int, int> map(shm, "load_map", 100);
        
        REQUIRE(map.load_factor() == 0.0f);
        
        for (int i = 0; i < 50; ++i) {
            map.insert(i, i);
        }
        
        float lf = map.load_factor();
        REQUIRE(lf > 0.0f);
        REQUIRE(lf < 0.75f);  // Default max load factor
    }

    shm.unlink();
}

TEST_CASE("shm_hash_map cross-process", "[shm_hash_map][process]") {
    const std::string shm_name = "/test_hash_process";
    shm_unlink(shm_name.c_str());
    
    SECTION("Hash map persistence across processes") {
        // Process 1: Create and populate
        {
            posix_shm shm1(shm_name, 1024 * 1024);
            shm_hash_map<int, double> map(shm1, "persistent_map", 100);
            
            map.insert(1, 1.1);
            map.insert(2, 2.2);
            map.insert(3, 3.3);
            
            REQUIRE(map.size() == 3);
        }
        
        // Process 2: Open and verify
        {
            posix_shm shm2(shm_name, 0);  // Attach only
            shm_hash_map<int, double> map(shm2, "persistent_map");
            
            REQUIRE(map.size() == 3);
            REQUIRE(*map.find(1) == Catch::Approx(1.1));
            REQUIRE(*map.find(2) == Catch::Approx(2.2));
            REQUIRE(*map.find(3) == Catch::Approx(3.3));
            
            // Modify
            map.update(2, 4.4);
            map.insert(4, 5.5);
        }
        
        // Process 3: Verify modifications
        {
            posix_shm shm3(shm_name, 0);
            shm_hash_map<int, double> map(shm3, "persistent_map");
            
            REQUIRE(map.size() == 4);
            REQUIRE(*map.find(2) == Catch::Approx(4.4));
            REQUIRE(*map.find(4) == Catch::Approx(5.5));
        }
    }
    
    shm_unlink(shm_name.c_str());
}