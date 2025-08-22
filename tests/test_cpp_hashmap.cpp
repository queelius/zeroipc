#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "zeroipc.h"
#include "map.h"
#include <thread>
#include <vector>

TEST_CASE("zeroipc::map core functionality", "[zeroipc::map]") {
    const std::string shm_name = "/test_hashmap_basic";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Create and use hash map") {
        zeroipc::map<int, double> map(shm, "test_map", 100);
        
        REQUIRE(map.size() == 0);
        REQUIRE(map.empty());
        
        // Test insert
        REQUIRE(map.insert(1, 3.14));
        REQUIRE(map.insert(2, 2.71));
        REQUIRE(map.size() == 2);
        
        // Test find
        auto* val1 = map.find(1);
        REQUIRE(val1 != nullptr);
        REQUIRE(*val1 == 3.14);
        
        auto* val2 = map.find(2);
        REQUIRE(val2 != nullptr);
        REQUIRE(*val2 == 2.71);
        
        // Test non-existent key
        auto* val_none = map.find(99);
        REQUIRE(val_none == nullptr);
    }

    SECTION("Test contains") {
        zeroipc::map<int, double> map(shm, "contains_map", 100);
        
        map.insert(10, 1.0);
        REQUIRE(map.contains(10));
        REQUIRE(!map.contains(20));
    }

    SECTION("Test update") {
        zeroipc::map<int, double> map(shm, "update_map", 100);
        
        map.insert(1, 1.0);
        REQUIRE(map.update(1, 2.0));
        
        auto* val = map.find(1);
        REQUIRE(val != nullptr);
        REQUIRE(*val == 2.0);
        
        // Update non-existing should fail
        REQUIRE(!map.update(99, 99.0));
    }

    SECTION("Test erase") {
        zeroipc::map<int, double> map(shm, "erase_map", 100);
        
        map.insert(1, 1.0);
        map.insert(2, 2.0);
        REQUIRE(map.size() == 2);
        
        REQUIRE(map.erase(1));
        REQUIRE(map.size() == 1);
        REQUIRE(!map.contains(1));
        REQUIRE(map.contains(2));
        
        // Erase non-existing
        REQUIRE(!map.erase(99));
    }

    SECTION("Test insert_or_update") {
        zeroipc::map<int, double> map(shm, "insert_update_map", 100);
        
        // Should insert
        map.insert_or_update(1, 1.0);
        REQUIRE(map.contains(1));
        REQUIRE(*map.find(1) == 1.0);
        
        // Should update
        map.insert_or_update(1, 2.0);
        REQUIRE(*map.find(1) == 2.0);
    }

    SECTION("Test clear") {
        zeroipc::map<int, double> map(shm, "clear_map", 100);
        
        for (int i = 0; i < 10; ++i) {
            map.insert(i, double(i));
        }
        REQUIRE(map.size() == 10);
        
        map.clear();
        REQUIRE(map.size() == 0);
        REQUIRE(map.empty());
    }

    SECTION("Test for_each") {
        zeroipc::map<int, double> map(shm, "foreach_map", 100);
        
        for (int i = 0; i < 5; ++i) {
            map.insert(i, double(i) * 1.1);
        }
        
        int count = 0;
        double sum = 0;
        map.for_each([&count, &sum](int key, double value) {
            count++;
            sum += value;
        });
        
        REQUIRE(count == 5);
        REQUIRE(sum == Catch::Approx(11.0));
    }

    shm.unlink();
}

TEST_CASE("zeroipc::map collision resolution", "[zeroipc::map]") {
    const std::string shm_name = "/test_hashmap_collision";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    zeroipc::map<int, int> map(shm, "collision_map", 1000);
    
    // Insert many items to force collisions
    for (int i = 0; i < 100; ++i) {
        REQUIRE(map.insert(i, i * 10));
    }
    
    // Verify all can be found
    for (int i = 0; i < 100; ++i) {
        auto* val = map.find(i);
        REQUIRE(val != nullptr);
        REQUIRE(*val == i * 10);
    }
    
    shm.unlink();
}

TEST_CASE("zeroipc::map persistence basics", "[zeroipc::map]") {
    const std::string shm_name = "/test_hashmap_persist";
    shm_unlink(shm_name.c_str());
    
    // Process 1: Create and populate
    {
        zeroipc::memory shm1(shm_name, 10 * 1024 * 1024);
        zeroipc::map<int, double> map(shm1, "persist_map", 100);
        
        map.insert(1, 1.23);
        map.insert(2, 4.56);
        map.insert(3, 7.89);
        
        REQUIRE(map.size() == 3);
    }
    
    // Process 2: Attach and verify
    {
        zeroipc::memory shm2(shm_name, 0);  // Attach only
        zeroipc::map<int, double> map(shm2, "persist_map");
        
        REQUIRE(map.size() == 3);
        REQUIRE(*map.find(1) == 1.23);
        REQUIRE(*map.find(2) == 4.56);
        REQUIRE(*map.find(3) == 7.89);
    }
    
    shm_unlink(shm_name.c_str());
}

TEST_CASE("zeroipc::map concurrent access", "[zeroipc::map][concurrent]") {
    const std::string shm_name = "/test_hashmap_concurrent";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    zeroipc::map<int, int> map(shm, "concurrent_map", 10000);
    
    const int num_threads = 4;
    const int items_per_thread = 1000;
    
    std::vector<std::thread> threads;
    
    // Concurrent inserts
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
    
    // Verify all items
    REQUIRE(map.size() == num_threads * items_per_thread);
    
    for (int i = 0; i < num_threads * items_per_thread; ++i) {
        auto* val = map.find(i);
        REQUIRE(val != nullptr);
        REQUIRE(*val == i * 2);
    }
    
    shm.unlink();
}

TEST_CASE("zeroipc::map with string keys", "[zeroipc::map]") {
    const std::string shm_name = "/test_hashmap_string";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);
    
    // Use hash of string as key
    zeroipc::map<uint64_t, double> map(shm, "string_map", 100);
    
    auto hash_str = [](const std::string& s) {
        return std::hash<std::string>{}(s);
    };
    
    map.insert(hash_str("hello"), 1.23);
    map.insert(hash_str("world"), 4.56);
    
    REQUIRE(*map.find(hash_str("hello")) == 1.23);
    REQUIRE(*map.find(hash_str("world")) == 4.56);
    
    shm.unlink();
}