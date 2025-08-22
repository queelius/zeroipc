#include <catch2/catch_test_macros.hpp>
#include "table.h"
#include <string_view>

TEST_CASE("zeroipc::table operations", "[zeroipc::table]") {
    zeroipc::table table;

    SECTION("Initial state") {
        REQUIRE(table.get_entry_count() == 0);
        REQUIRE(table.get_total_allocated_size() == 0);
    }

    SECTION("Add and find entries") {
        bool added = table.add("test_array", 1024, 100, sizeof(int), 25);
        REQUIRE(added == true);
        REQUIRE(table.get_entry_count() == 1);

        auto* entry = table.find("test_array");
        REQUIRE(entry != nullptr);
        REQUIRE(entry->offset == 1024);
        REQUIRE(entry->size == 100);
        REQUIRE(entry->elem_size == sizeof(int));
        REQUIRE(entry->num_elem == 25);
        REQUIRE(entry->active == true);
    }

    SECTION("Duplicate names are rejected") {
        table.add("duplicate", 1024, 100);
        bool added = table.add("duplicate", 2048, 200);
        REQUIRE(added == false);
        REQUIRE(table.get_entry_count() == 1);
    }

    SECTION("Erase entries") {
        table.add("to_delete", 1024, 100);
        REQUIRE(table.get_entry_count() == 1);
        
        bool erased = table.erase("to_delete");
        REQUIRE(erased == true);
        REQUIRE(table.get_entry_count() == 0);
        REQUIRE(table.find("to_delete") == nullptr);
    }

    SECTION("string_view overloads") {
        std::string_view name = "sv_test";
        table.add(name, 2048, 512, sizeof(double), 64);
        
        auto* entry = table.find(name);
        REQUIRE(entry != nullptr);
        REQUIRE(entry->offset == 2048);
        
        bool erased = table.erase(name);
        REQUIRE(erased == true);
    }

    SECTION("Track total allocated size") {
        table.add("first", 100, 50);
        REQUIRE(table.get_total_allocated_size() == 150 - sizeof(zeroipc::table));
        
        table.add("second", 200, 100);
        REQUIRE(table.get_total_allocated_size() == 300 - sizeof(zeroipc::table));
    }

    SECTION("Clear all entries") {
        table.add("entry1", 100, 50);
        table.add("entry2", 200, 100);
        REQUIRE(table.get_entry_count() == 2);
        
        table.clear();
        REQUIRE(table.get_entry_count() == 0);
        REQUIRE(table.get_total_allocated_size() == 0);
    }
}

TEST_CASE("zeroipc::table with custom sizes", "[zeroipc::table][template]") {
    SECTION("Small table") {
        zeroipc::table16 small_table;
        REQUIRE(zeroipc::table16::MAX_NAME_SIZE == 32);
        REQUIRE(zeroipc::table16::MAX_ENTRIES == 16);
        
        // Test with a valid name
        small_table.add("test_entry", 100, 50);
        auto* entry = small_table.find("test_entry");
        REQUIRE(entry != nullptr);
    }

    SECTION("Large table") {
        zeroipc::table256 large_table;
        REQUIRE(zeroipc::table256::MAX_NAME_SIZE == 32);
        REQUIRE(zeroipc::table256::MAX_ENTRIES == 256);
        
        // Can store many more entries
        for (int i = 0; i < 100; ++i) {
            std::string name = "entry_" + std::to_string(i);
            large_table.add(name.c_str(), i * 100, 50);
        }
        REQUIRE(large_table.get_entry_count() == 100);
    }
}

TEST_CASE("zeroipc::table size calculations", "[zeroipc::table][template]") {
    SECTION("Compare table sizes") {
        constexpr size_t small_size = zeroipc::table16::size_bytes();
        constexpr size_t default_size = zeroipc::table::size_bytes();
        constexpr size_t large_size = zeroipc::table256::size_bytes();
        
        REQUIRE(small_size < default_size);
        REQUIRE(default_size < large_size);
        
        // Verify actual sizes match expectations
        REQUIRE(sizeof(zeroipc::table16) == small_size);
        REQUIRE(sizeof(zeroipc::table) == default_size);
        REQUIRE(sizeof(zeroipc::table256) == large_size);
    }
}