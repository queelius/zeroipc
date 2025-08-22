#include <catch2/catch_test_macros.hpp>
#include "zeroipc.h"
#include "array.h"
#include <algorithm>
#include <numeric>

TEST_CASE("zeroipc::array basic operations", "[zeroipc::array]") {
    const std::string shm_name = "/test_array_basic";
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024); // 10MB

    SECTION("Create and access array") {
        zeroipc::array<int> arr(shm, "int_array", 100);
        
        REQUIRE(arr.size() == 100);
        REQUIRE(!arr.empty());
        REQUIRE(arr.name() == "int_array");
        
        // Write values
        for (size_t i = 0; i < arr.size(); ++i) {
            arr[i] = static_cast<int>(i * 2);
        }
        
        // Read values
        for (size_t i = 0; i < arr.size(); ++i) {
            REQUIRE(arr[i] == static_cast<int>(i * 2));
        }
    }

    SECTION("Array discovery by name") {
        // Create array
        {
            zeroipc::array<double> arr1(shm, "discoverable", 50);
            arr1[0] = 3.14159;
            arr1[49] = 2.71828;
        }
        
        // Discover existing array
        {
            zeroipc::array<double> arr2(shm, "discoverable");
            REQUIRE(arr2.size() == 50);
            REQUIRE(arr2[0] == 3.14159);
            REQUIRE(arr2[49] == 2.71828);
        }
    }

    SECTION("Bounds checking with at()") {
        zeroipc::array<int> arr(shm, "bounded", 10);
        
        REQUIRE_NOTHROW(arr.at(0) = 42);
        REQUIRE_NOTHROW(arr.at(9) = 99);
        REQUIRE_THROWS_AS(arr.at(10), std::out_of_range);
        REQUIRE_THROWS_AS(arr.at(100), std::out_of_range);
    }

    SECTION("Front and back access") {
        zeroipc::array<int> arr(shm, "endpoints", 5);
        arr.fill(0);
        
        arr.front() = 100;
        arr.back() = 200;
        
        REQUIRE(arr[0] == 100);
        REQUIRE(arr[4] == 200);
    }

    SECTION("Fill operation") {
        zeroipc::array<int> arr(shm, "fillable", 20);
        arr.fill(42);
        
        for (size_t i = 0; i < arr.size(); ++i) {
            REQUIRE(arr[i] == 42);
        }
    }

    SECTION("Iterator support") {
        zeroipc::array<int> arr(shm, "iterable", 10);
        std::iota(arr.begin(), arr.end(), 0);  // Fill with 0,1,2,...,9
        
        int sum = std::accumulate(arr.begin(), arr.end(), 0);
        REQUIRE(sum == 45);  // Sum of 0..9
        
        auto it = std::find(arr.begin(), arr.end(), 5);
        REQUIRE(it != arr.end());
        REQUIRE(*it == 5);
    }

    SECTION("std::span conversion") {
        zeroipc::array<int> arr(shm, "spannable", 15);
        std::iota(arr.begin(), arr.end(), 1);
        
        auto span = arr.as_span();
        REQUIRE(span.size() == 15);
        REQUIRE(span[0] == 1);
        REQUIRE(span[14] == 15);
        
        // Modify through span
        span[7] = 999;
        REQUIRE(arr[7] == 999);
    }
}

TEST_CASE("zeroipc::array with custom types", "[zeroipc::array]") {
    const std::string shm_name = "/test_array_custom";
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    struct Point {
        double x, y, z;
        bool operator==(const Point& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };
    static_assert(std::is_trivially_copyable_v<Point>);

    SECTION("Array of structs") {
        zeroipc::array<Point> points(shm, "3d_points", 100);
        
        points[0] = {1.0, 2.0, 3.0};
        points[50] = {10.5, 20.5, 30.5};
        
        REQUIRE(points[0] == Point{1.0, 2.0, 3.0});
        REQUIRE(points[50] == Point{10.5, 20.5, 30.5});
    }
}

TEST_CASE("zeroipc::array with custom table sizes", "[zeroipc::array][template]") {
    SECTION("Array with small table") {
        const std::string shm_name = "/test_array_small_table";
        zeroipc::memory16 shm(shm_name, 1024 * 1024);
        
        zeroipc::array<uint32_t, zeroipc::table16> arr(shm, "small_arr", 100);
        REQUIRE(arr.size() == 100);
        
        arr[0] = 0xDEADBEEF;
        REQUIRE(arr[0] == 0xDEADBEEF);
    }

    SECTION("Array with large table") {
        const std::string shm_name = "/test_array_large_table";
        zeroipc::memory256 shm(shm_name, 10 * 1024 * 1024);
        
        zeroipc::array<double, zeroipc::table256> arr(
            shm, "long_descriptive_array", 1000);
        REQUIRE(arr.size() == 1000);
        REQUIRE(arr.name() == "long_descriptive_array");
    }
}

TEST_CASE("zeroipc::array error handling", "[zeroipc::array][error]") {
    const std::string shm_name = "/test_array_errors";
    zeroipc::memory shm(shm_name, 1024); // Small shared memory

    SECTION("Not enough space") {
        REQUIRE_THROWS_AS(
            zeroipc::array<double>(shm, "too_big", 10000),
            std::runtime_error
        );
    }

    SECTION("Array not found") {
        REQUIRE_THROWS_AS(
            zeroipc::array<int>(shm, "nonexistent"),
            std::runtime_error
        );
    }

    SECTION("Size mismatch on open") {
        zeroipc::array<int> arr1(shm, "sized", 10);
        REQUIRE_THROWS_AS(
            zeroipc::array<int>(shm, "sized", 20),
            std::runtime_error
        );
    }
}