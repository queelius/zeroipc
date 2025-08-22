#include <catch2/catch_test_macros.hpp>
#include "zeroipc.h"
#include <cstring>
#include <thread>
#include <chrono>

TEST_CASE("zeroipc::memory basic operations", "[zeroipc::memory]") {
    const std::string shm_name = "/test_basic_ops";
    const size_t size = 1024 * 1024; // 1MB

    SECTION("Create and destroy shared memory") {
        {
            zeroipc::memory shm(shm_name, size);
            REQUIRE(shm.get_total_size() == size);
            REQUIRE(shm.get_ref_count() == 1);
            REQUIRE(shm.get_base_addr() != nullptr);
        }
        // Destructor should clean up
    }

    SECTION("Write and read data") {
        zeroipc::memory shm(shm_name, size);
        auto* data = static_cast<char*>(shm.get_base_addr());
        
        const char* test_string = "Hello, shared memory!";
        std::strcpy(data, test_string);
        
        REQUIRE(std::strcmp(data, test_string) == 0);
    }

    SECTION("Reference counting") {
        zeroipc::memory shm1(shm_name, size);
        REQUIRE(shm1.get_ref_count() == 1);
        
        {
            zeroipc::memory shm2(shm_name); // Open existing
            REQUIRE(shm2.get_ref_count() == 2);
            REQUIRE(shm1.get_ref_count() == 2);
        }
        
        REQUIRE(shm1.get_ref_count() == 1);
    }

    SECTION("Table is properly initialized") {
        zeroipc::memory shm(shm_name, size);
        auto* table = shm.get_table();
        REQUIRE(table != nullptr);
        REQUIRE(table->get_entry_count() == 0);
    }
}

TEST_CASE("zeroipc::memory with custom table sizes", "[zeroipc::memory][template]") {
    SECTION("Small table configuration") {
        const std::string shm_name = "/test_small_table";
        zeroipc::memory_small shm(shm_name, 64 * 1024);
        
        REQUIRE(shm.get_table() != nullptr);
        REQUIRE(zeroipc::table_small::MAX_NAME_SIZE == 16);
        REQUIRE(zeroipc::table_small::MAX_ENTRIES == 16);
    }

    SECTION("Large table configuration") {
        const std::string shm_name = "/test_large_table";
        zeroipc::memory_large shm(shm_name, 10 * 1024 * 1024);
        
        REQUIRE(shm.get_table() != nullptr);
        REQUIRE(zeroipc::table_large::MAX_NAME_SIZE == 64);
        REQUIRE(zeroipc::table_large::MAX_ENTRIES == 256);
    }
}

TEST_CASE("zeroipc::memory error handling", "[zeroipc::memory][error]") {
    SECTION("Opening non-existent shared memory without size fails") {
        REQUIRE_THROWS_AS(
            zeroipc::memory("/nonexistent_shm", 0),
            std::runtime_error
        );
    }
}