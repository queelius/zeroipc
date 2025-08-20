#include <catch2/catch_test_macros.hpp>
#include "posix_shm.h"
#include <cstring>
#include <thread>
#include <chrono>

TEST_CASE("posix_shm basic operations", "[posix_shm]") {
    const std::string shm_name = "/test_basic_ops";
    const size_t size = 1024 * 1024; // 1MB

    SECTION("Create and destroy shared memory") {
        {
            posix_shm shm(shm_name, size);
            REQUIRE(shm.get_total_size() == size);
            REQUIRE(shm.get_ref_count() == 1);
            REQUIRE(shm.get_base_addr() != nullptr);
        }
        // Destructor should clean up
    }

    SECTION("Write and read data") {
        posix_shm shm(shm_name, size);
        auto* data = static_cast<char*>(shm.get_base_addr());
        
        const char* test_string = "Hello, shared memory!";
        std::strcpy(data, test_string);
        
        REQUIRE(std::strcmp(data, test_string) == 0);
    }

    SECTION("Reference counting") {
        posix_shm shm1(shm_name, size);
        REQUIRE(shm1.get_ref_count() == 1);
        
        {
            posix_shm shm2(shm_name); // Open existing
            REQUIRE(shm2.get_ref_count() == 2);
            REQUIRE(shm1.get_ref_count() == 2);
        }
        
        REQUIRE(shm1.get_ref_count() == 1);
    }

    SECTION("Table is properly initialized") {
        posix_shm shm(shm_name, size);
        auto* table = shm.get_table();
        REQUIRE(table != nullptr);
        REQUIRE(table->get_entry_count() == 0);
    }
}

TEST_CASE("posix_shm with custom table sizes", "[posix_shm][template]") {
    SECTION("Small table configuration") {
        const std::string shm_name = "/test_small_table";
        posix_shm_small shm(shm_name, 64 * 1024);
        
        REQUIRE(shm.get_table() != nullptr);
        REQUIRE(shm_table_small::MAX_NAME_SIZE == 16);
        REQUIRE(shm_table_small::MAX_ENTRIES == 16);
    }

    SECTION("Large table configuration") {
        const std::string shm_name = "/test_large_table";
        posix_shm_large shm(shm_name, 10 * 1024 * 1024);
        
        REQUIRE(shm.get_table() != nullptr);
        REQUIRE(shm_table_large::MAX_NAME_SIZE == 64);
        REQUIRE(shm_table_large::MAX_ENTRIES == 256);
    }
}

TEST_CASE("posix_shm error handling", "[posix_shm][error]") {
    SECTION("Opening non-existent shared memory without size fails") {
        REQUIRE_THROWS_AS(
            posix_shm("/nonexistent_shm", 0),
            std::runtime_error
        );
    }
}