#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "posix_shm.h"
#include "shm_hash_map.h"
#include <thread>
#include <vector>
#include <unordered_set>

TEST_CASE("Multiple hash maps in same shared memory", "[shm_hash_map][multiple]") {
    const std::string shm_name = "/test_multi_hashmap";
    shm_unlink(shm_name.c_str());
    
    SECTION("Create multiple hash maps with different types") {
        posix_shm shm(shm_name, 10 * 1024 * 1024);
        
        // Create different hash maps
        shm_hash_map<int, int> map1(shm, "int_int_map", 100);
        shm_hash_map<int, double> map2(shm, "int_double_map", 100);
        shm_hash_map<uint64_t, float> map3(shm, "uint64_float_map", 100);
        
        // Insert into each map
        map1.insert(1, 100);
        map1.insert(2, 200);
        map1.insert(3, 300);
        
        map2.insert(10, 3.14);
        map2.insert(20, 2.718);
        map2.insert(30, 1.414);
        
        map3.insert(1000ULL, 1.23f);
        map3.insert(2000ULL, 4.56f);
        map3.insert(3000ULL, 7.89f);
        
        // Verify each map independently
        REQUIRE(map1.size() == 3);
        REQUIRE(map2.size() == 3);
        REQUIRE(map3.size() == 3);
        
        // Check values
        auto* val1 = map1.find(1);
        REQUIRE(val1 != nullptr);
        REQUIRE(*val1 == 100);
        
        auto* val2 = map1.find(2);
        REQUIRE(val2 != nullptr);
        REQUIRE(*val2 == 200);
        
        auto* val3 = map1.find(3);
        REQUIRE(val3 != nullptr);
        REQUIRE(*val3 == 300);
        
        auto* val10 = map2.find(10);
        REQUIRE(val10 != nullptr);
        REQUIRE(*val10 == Catch::Approx(3.14));
        
        auto* val20 = map2.find(20);
        REQUIRE(val20 != nullptr);
        REQUIRE(*val20 == Catch::Approx(2.718));
        
        auto* val30 = map2.find(30);
        REQUIRE(val30 != nullptr);
        REQUIRE(*val30 == Catch::Approx(1.414));
        
        auto* val1000 = map3.find(1000ULL);
        REQUIRE(val1000 != nullptr);
        REQUIRE(*val1000 == Catch::Approx(1.23f));
        
        auto* val2000 = map3.find(2000ULL);
        REQUIRE(val2000 != nullptr);
        REQUIRE(*val2000 == Catch::Approx(4.56f));
        
        auto* val3000 = map3.find(3000ULL);
        REQUIRE(val3000 != nullptr);
        REQUIRE(*val3000 == Catch::Approx(7.89f));
        
        shm.unlink();
    }
    
    SECTION("Create many hash maps of same type") {
        posix_shm shm(shm_name, 20 * 1024 * 1024);
        
        // Create 10 hash maps
        std::vector<std::unique_ptr<shm_hash_map<int, int>>> maps;
        for (int i = 0; i < 10; i++) {
            std::string name = "map_" + std::to_string(i);
            maps.push_back(std::make_unique<shm_hash_map<int, int>>(shm, name, 50));
        }
        
        // Insert different data into each map
        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 10; j++) {
                maps[i]->insert(j, i * 100 + j);
            }
        }
        
        // Verify each map has correct data
        for (int i = 0; i < 10; i++) {
            REQUIRE(maps[i]->size() == 10);
            for (int j = 0; j < 10; j++) {
                auto* val = maps[i]->find(j);
                REQUIRE(val != nullptr);
                REQUIRE(*val == i * 100 + j);
            }
        }
        
        shm.unlink();
    }
    
    SECTION("Persistence across processes for multiple maps") {
        {
            posix_shm shm(shm_name, 10 * 1024 * 1024);
            
            shm_hash_map<int, std::array<char, 16>> map1(shm, "config_map", 100);
            shm_hash_map<uint32_t, uint32_t> map2(shm, "id_map", 100);
            
            // Insert data
            std::array<char, 16> config1 = {"config_alpha"};
            std::array<char, 16> config2 = {"config_beta"};
            map1.insert(1, config1);
            map1.insert(2, config2);
            
            map2.insert(1001, 0xDEADBEEF);
            map2.insert(1002, 0xCAFEBABE);
        }
        
        // Reopen and verify
        {
            posix_shm shm(shm_name);
            
            shm_hash_map<int, std::array<char, 16>> map1(shm, "config_map");
            shm_hash_map<uint32_t, uint32_t> map2(shm, "id_map");
            
            REQUIRE(map1.size() == 2);
            REQUIRE(map2.size() == 2);
            
            auto* config1 = map1.find(1);
            REQUIRE(config1 != nullptr);
            REQUIRE(std::string(config1->data()) == "config_alpha");
            
            auto* val1001 = map2.find(1001);
            REQUIRE(val1001 != nullptr);
            REQUIRE(*val1001 == 0xDEADBEEF);
            
            auto* val1002 = map2.find(1002);
            REQUIRE(val1002 != nullptr);
            REQUIRE(*val1002 == 0xCAFEBABE);
            
            shm.unlink();
        }
    }
    
    SECTION("Collision handling across multiple maps") {
        posix_shm shm(shm_name, 10 * 1024 * 1024);
        
        // Create small maps to force collisions
        shm_hash_map<int, int> map1(shm, "collision_map1", 10);
        shm_hash_map<int, int> map2(shm, "collision_map2", 10);
        
        // Insert many items to force collisions
        for (int i = 0; i < 8; i++) {
            REQUIRE(map1.insert(i, i * 10));
            REQUIRE(map2.insert(i + 100, i * 20));
        }
        
        // Verify all values are correct despite collisions
        for (int i = 0; i < 8; i++) {
            auto* val1 = map1.find(i);
            REQUIRE(val1 != nullptr);
            REQUIRE(*val1 == i * 10);
            
            auto* val2 = map2.find(i + 100);
            REQUIRE(val2 != nullptr);
            REQUIRE(*val2 == i * 20);
        }
        
        // Ensure maps are independent
        REQUIRE(map1.find(100) == nullptr);
        REQUIRE(map2.find(0) == nullptr);
        
        shm.unlink();
    }
    
    SECTION("Concurrent operations on multiple maps") {
        posix_shm shm(shm_name, 10 * 1024 * 1024);
        
        shm_hash_map<int, int> map1(shm, "concurrent_map1", 1000);
        shm_hash_map<int, int> map2(shm, "concurrent_map2", 1000);
        shm_hash_map<int, int> map3(shm, "concurrent_map3", 1000);
        
        const int items_per_thread = 100;
        const int num_threads = 4;
        
        auto worker = [&](int thread_id, shm_hash_map<int, int>& map) {
            int start = thread_id * items_per_thread;
            int end = start + items_per_thread;
            
            for (int i = start; i < end; i++) {
                map.insert(i, i * i);
            }
        };
        
        // Launch threads for each map
        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; i++) {
            threads.emplace_back(worker, i, std::ref(map1));
            threads.emplace_back(worker, i, std::ref(map2));
            threads.emplace_back(worker, i, std::ref(map3));
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        // Verify all maps have correct data
        REQUIRE(map1.size() == items_per_thread * num_threads);
        REQUIRE(map2.size() == items_per_thread * num_threads);
        REQUIRE(map3.size() == items_per_thread * num_threads);
        
        for (int i = 0; i < items_per_thread * num_threads; i++) {
            auto* val1 = map1.find(i);
            REQUIRE(val1 != nullptr);
            REQUIRE(*val1 == i * i);
            
            auto* val2 = map2.find(i);
            REQUIRE(val2 != nullptr);
            REQUIRE(*val2 == i * i);
            
            auto* val3 = map3.find(i);
            REQUIRE(val3 != nullptr);
            REQUIRE(*val3 == i * i);
        }
        
        shm.unlink();
    }
}

TEST_CASE("Hash map stress tests", "[shm_hash_map][stress]") {
    const std::string shm_name = "/test_hashmap_stress";
    shm_unlink(shm_name.c_str());
    
    SECTION("Maximum capacity handling") {
        posix_shm shm(shm_name, 10 * 1024 * 1024);
        
        shm_hash_map<int, int> map(shm, "max_capacity", 100);
        
        // Fill to near capacity
        int inserted = 0;
        for (int i = 0; i < 100; i++) {
            if (map.insert(i, i * 2)) {
                inserted++;
            }
        }
        
        // Should insert at least 75 items (75% load factor)
        REQUIRE(inserted >= 75);
        REQUIRE(map.size() == inserted);
        
        // Verify all inserted items
        for (int i = 0; i < inserted; i++) {
            auto* val = map.find(i);
            if (val != nullptr) {
                REQUIRE(*val == i * 2);
            }
        }
        
        shm.unlink();
    }
    
    SECTION("Rapid insert/remove cycles") {
        posix_shm shm(shm_name, 10 * 1024 * 1024);
        
        shm_hash_map<int, int> map(shm, "rapid_cycles", 1000);
        
        // Perform many insert/remove cycles
        for (int cycle = 0; cycle < 10; cycle++) {
            // Insert batch
            for (int i = cycle * 50; i < (cycle + 1) * 50; i++) {
                REQUIRE(map.insert(i, i));
            }
            
            // Remove half
            for (int i = cycle * 50; i < cycle * 50 + 25; i++) {
                REQUIRE(map.erase(i));
            }
            
            // Verify remaining
            for (int i = cycle * 50 + 25; i < (cycle + 1) * 50; i++) {
                auto* val = map.find(i);
                REQUIRE(val != nullptr);
                REQUIRE(*val == i);
            }
        }
        
        shm.unlink();
    }
}