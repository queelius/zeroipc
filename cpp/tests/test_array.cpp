#include <gtest/gtest.h>
#include <zeroipc/array.h>
#include <algorithm>
#include <numeric>

using namespace zeroipc;

class ArrayTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_name = "/test_array_" + std::to_string(getpid());
        mem = std::make_unique<Memory>(test_name, 10 * 1024 * 1024); // 10MB
    }
    
    void TearDown() override {
        if (mem) {
            mem->unlink();
        }
    }
    
    std::string test_name;
    std::unique_ptr<Memory> mem;
};

TEST_F(ArrayTest, CreateNewArray) {
    Array<int> arr(*mem, "test_array", 100);
    
    EXPECT_EQ(arr.capacity(), 100);
    EXPECT_EQ(arr.name(), "test_array");
    EXPECT_NE(arr.data(), nullptr);
}

TEST_F(ArrayTest, AccessElements) {
    Array<double> arr(*mem, "doubles", 10);
    
    // Write
    arr[0] = 3.14;
    arr[5] = 2.718;
    arr.at(9) = 1.414;
    
    // Read
    EXPECT_DOUBLE_EQ(arr[0], 3.14);
    EXPECT_DOUBLE_EQ(arr[5], 2.718);
    EXPECT_DOUBLE_EQ(arr.at(9), 1.414);
}

TEST_F(ArrayTest, BoundsChecking) {
    Array<int> arr(*mem, "bounded", 50);
    
    // Valid access
    EXPECT_NO_THROW(arr[49] = 100);
    EXPECT_NO_THROW(arr.at(0) = 200);
    
    // Out of bounds
    EXPECT_THROW(arr.at(50), std::out_of_range);
    EXPECT_THROW(arr.at(100), std::out_of_range);
}

TEST_F(ArrayTest, OpenExistingArray) {
    // Create array
    {
        Array<float> arr1(*mem, "persistent", 25);
        arr1[0] = 1.5f;
        arr1[10] = 2.5f;
        arr1[24] = 3.5f;
    }
    
    // Open existing
    {
        Array<float> arr2(*mem, "persistent");
        EXPECT_EQ(arr2.capacity(), 25);
        EXPECT_FLOAT_EQ(arr2[0], 1.5f);
        EXPECT_FLOAT_EQ(arr2[10], 2.5f);
        EXPECT_FLOAT_EQ(arr2[24], 3.5f);
    }
}

TEST_F(ArrayTest, IteratorSupport) {
    Array<int> arr(*mem, "iterable", 10);
    
    // Fill with values
    std::iota(arr.begin(), arr.end(), 0);
    
    // Verify with range-based for
    int expected = 0;
    for (int val : arr) {
        EXPECT_EQ(val, expected++);
    }
    
    // STL algorithms
    EXPECT_EQ(std::accumulate(arr.begin(), arr.end(), 0), 45);
    EXPECT_TRUE(std::is_sorted(arr.begin(), arr.end()));
}

TEST_F(ArrayTest, FillMethod) {
    Array<int> arr(*mem, "fillable", 100);
    
    arr.fill(42);
    
    for (size_t i = 0; i < arr.capacity(); ++i) {
        EXPECT_EQ(arr[i], 42);
    }
}

TEST_F(ArrayTest, CustomTypes) {
    struct Point {
        float x, y, z;
    };
    
    Array<Point> points(*mem, "points", 5);
    
    points[0] = {1.0f, 2.0f, 3.0f};
    points[1] = {4.0f, 5.0f, 6.0f};
    
    EXPECT_FLOAT_EQ(points[0].x, 1.0f);
    EXPECT_FLOAT_EQ(points[0].y, 2.0f);
    EXPECT_FLOAT_EQ(points[0].z, 3.0f);
    
    EXPECT_FLOAT_EQ(points[1].x, 4.0f);
    EXPECT_FLOAT_EQ(points[1].y, 5.0f);
    EXPECT_FLOAT_EQ(points[1].z, 6.0f);
}

TEST_F(ArrayTest, MultipleArrays) {
    Array<int> arr1(*mem, "array1", 10);
    Array<double> arr2(*mem, "array2", 20);
    Array<char> arr3(*mem, "array3", 30);
    
    EXPECT_EQ(mem->table()->entry_count(), 3);
    
    arr1[0] = 100;
    arr2[0] = 3.14;
    arr3[0] = 'A';
    
    EXPECT_EQ(arr1[0], 100);
    EXPECT_DOUBLE_EQ(arr2[0], 3.14);
    EXPECT_EQ(arr3[0], 'A');
}

TEST_F(ArrayTest, CapacityMismatchThrows) {
    Array<int> arr1(*mem, "sized", 100);
    
    // Opening with wrong capacity should throw
    EXPECT_THROW(Array<int>(*mem, "sized", 50), std::runtime_error);
    
    // Opening without specifying capacity should work
    EXPECT_NO_THROW(Array<int> arr2(*mem, "sized"));
}

TEST_F(ArrayTest, NonExistentArrayThrows) {
    EXPECT_THROW(Array<int>(*mem, "nonexistent"), std::invalid_argument);
}

TEST_F(ArrayTest, LongNameThrows) {
    std::string long_name(32, 'x');
    EXPECT_THROW(Array<int>(*mem, long_name, 10), std::invalid_argument);
}

TEST_F(ArrayTest, ZeroInitialization) {
    Array<int> arr(*mem, "zeroed", 50);
    
    // All elements should be zero-initialized
    for (size_t i = 0; i < arr.capacity(); ++i) {
        EXPECT_EQ(arr[i], 0);
    }
}

TEST_F(ArrayTest, DataPointer) {
    Array<int> arr(*mem, "direct", 10);
    
    // Direct pointer access
    int* ptr = arr.data();
    ptr[0] = 111;
    ptr[5] = 555;
    
    EXPECT_EQ(arr[0], 111);
    EXPECT_EQ(arr[5], 555);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}