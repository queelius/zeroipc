find_package(GTest REQUIRED)
include(GoogleTest)

# Find all .cpp files in the current directory and subdirectories
file(GLOB_RECURSE TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# Create an executable for the tests
add_executable(shared_memory_tests ${TEST_SOURCES})

# Include the project's include directory
target_include_directories(shared_memory_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# Link the test executable with necessary libraries
target_link_libraries(shared_memory_tests
    GTest::GTest
    GTest::Main
    rt  # For POSIX shared memory functions
    pthread  # For std::atomic
)

# Discover and register the tests
gtest_discover_tests(shared_memory_tests)
