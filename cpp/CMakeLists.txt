cmake_minimum_required(VERSION 3.20)
project(zeroipc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Find packages
find_package(Threads REQUIRED)

# Create header-only library
add_library(zeroipc INTERFACE)
target_include_directories(zeroipc INTERFACE include)
target_link_libraries(zeroipc INTERFACE Threads::Threads rt)

# Enable testing
enable_testing()

# Add subdirectories
add_subdirectory(benchmarks)

# Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Test executables
add_executable(test_table tests/test_table.cpp)
target_link_libraries(test_table gtest_main Threads::Threads)

add_executable(test_memory tests/test_memory.cpp)
target_link_libraries(test_memory gtest_main Threads::Threads rt)

add_executable(test_array tests/test_array.cpp)
target_link_libraries(test_array gtest_main Threads::Threads rt)

add_executable(test_queue tests/test_queue.cpp)
target_link_libraries(test_queue gtest_main Threads::Threads rt)

add_executable(test_stack tests/test_stack.cpp)
target_link_libraries(test_stack gtest_main Threads::Threads rt)

# Add tests
add_test(NAME table_test COMMAND test_table)
add_test(NAME memory_test COMMAND test_memory)
add_test(NAME array_test COMMAND test_array)
add_test(NAME queue_test COMMAND test_queue)
add_test(NAME stack_test COMMAND test_stack)

# Install headers
install(DIRECTORY include/zeroipc 
        DESTINATION include)