cmake_minimum_required(VERSION 3.20)
project(zeroipc VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Find packages
find_package(Threads REQUIRED)

# Create header-only library
add_library(libzeroipc INTERFACE)
target_include_directories(libzeroipc INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(libzeroipc INTERFACE Threads::Threads)

# Link rt library on Linux/Unix systems
if(UNIX AND NOT APPLE)
    target_link_libraries(libzeroipc INTERFACE rt)
endif()

# Set up installation
install(TARGETS libzeroipc
    EXPORT zeroipcTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include)

install(EXPORT zeroipcTargets
    FILE zeroipcTargets.cmake
    NAMESPACE zeroipc::
    DESTINATION lib/cmake/zeroipc)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "zeroipcConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/zeroipcConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/zeroipcConfig.cmake"
    INSTALL_DESTINATION lib/cmake/zeroipc)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/zeroipcConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/zeroipcConfigVersion.cmake"
    DESTINATION lib/cmake/zeroipc)

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

add_executable(test_lockfree_comprehensive tests/test_lockfree_comprehensive.cpp)
target_link_libraries(test_lockfree_comprehensive gtest_main Threads::Threads rt)

add_executable(test_stress tests/test_stress.cpp)
target_link_libraries(test_stress gtest_main Threads::Threads rt)

add_executable(test_edge_cases tests/test_edge_cases.cpp)
target_link_libraries(test_edge_cases gtest_main Threads::Threads rt)

add_executable(test_aba_problem tests/test_aba_problem.cpp)
target_link_libraries(test_aba_problem gtest_main Threads::Threads rt)

add_executable(test_table_stress tests/test_table_stress.cpp)
target_link_libraries(test_table_stress gtest_main Threads::Threads rt)

add_executable(test_memory_boundaries tests/test_memory_boundaries.cpp)
target_link_libraries(test_memory_boundaries gtest_main Threads::Threads rt)

add_executable(test_failure_recovery tests/test_failure_recovery.cpp)
target_link_libraries(test_failure_recovery gtest_main Threads::Threads rt)

add_executable(test_new_structures tests/test_new_structures.cpp)
target_link_libraries(test_new_structures gtest_main Threads::Threads rt)

add_executable(test_codata tests/test_codata.cpp)
target_link_libraries(test_codata gtest_main Threads::Threads rt)

add_executable(test_cli_tool tests/test_cli_tool.cpp)
target_link_libraries(test_cli_tool gtest_main Threads::Threads rt)

add_executable(test_coverage_improvement tests/test_coverage_improvement.cpp)
target_link_libraries(test_coverage_improvement gtest_main Threads::Threads rt)

add_executable(test_semaphore tests/test_semaphore.cpp)
target_link_libraries(test_semaphore gtest_main Threads::Threads rt)

add_executable(test_barrier tests/test_barrier.cpp)
target_link_libraries(test_barrier gtest_main Threads::Threads rt)

add_executable(test_latch tests/test_latch.cpp)
target_link_libraries(test_latch gtest_main Threads::Threads rt)

# Add tests with proper categorization
# FAST tests: <100ms, core functionality
add_test(NAME table_test COMMAND test_table)
set_tests_properties(table_test PROPERTIES
    LABELS "fast;unit"
    TIMEOUT 5)

add_test(NAME memory_test COMMAND test_memory)
set_tests_properties(memory_test PROPERTIES
    LABELS "fast;unit"
    TIMEOUT 5)

add_test(NAME array_test COMMAND test_array)
set_tests_properties(array_test PROPERTIES
    LABELS "fast;unit;lockfree"
    TIMEOUT 5)

add_test(NAME queue_test COMMAND test_queue)
set_tests_properties(queue_test PROPERTIES
    LABELS "fast;unit;lockfree"
    TIMEOUT 5)

add_test(NAME stack_test COMMAND test_stack)
set_tests_properties(stack_test PROPERTIES
    LABELS "fast;unit;lockfree"
    TIMEOUT 5)

add_test(NAME edge_cases_test COMMAND test_edge_cases)
set_tests_properties(edge_cases_test PROPERTIES
    LABELS "fast;unit"
    TIMEOUT 5)

add_test(NAME new_structures_test COMMAND test_new_structures)
set_tests_properties(new_structures_test PROPERTIES
    LABELS "fast;unit"
    TIMEOUT 10)

# MEDIUM tests: <5s, multi-threaded with minimal delays
add_test(NAME lockfree_comprehensive_test COMMAND test_lockfree_comprehensive)
set_tests_properties(lockfree_comprehensive_test PROPERTIES
    LABELS "medium;integration;lockfree"
    TIMEOUT 5)

add_test(NAME aba_problem_test COMMAND test_aba_problem)
set_tests_properties(aba_problem_test PROPERTIES
    LABELS "medium;integration;lockfree"
    TIMEOUT 10)

add_test(NAME memory_boundaries_test COMMAND test_memory_boundaries)
set_tests_properties(memory_boundaries_test PROPERTIES
    LABELS "medium;integration"
    TIMEOUT 10)

add_test(NAME failure_recovery_test COMMAND test_failure_recovery)
set_tests_properties(failure_recovery_test PROPERTIES
    LABELS "medium;integration"
    TIMEOUT 15)

add_test(NAME codata_test COMMAND test_codata)
set_tests_properties(codata_test PROPERTIES
    LABELS "medium;unit"
    TIMEOUT 10)

add_test(NAME cli_tool_test COMMAND test_cli_tool)
set_tests_properties(cli_tool_test PROPERTIES
    LABELS "medium;integration"
    TIMEOUT 10)

add_test(NAME coverage_improvement_test COMMAND test_coverage_improvement)
set_tests_properties(coverage_improvement_test PROPERTIES
    LABELS "medium;unit"
    TIMEOUT 10)

add_test(NAME latch_test COMMAND test_latch)
set_tests_properties(latch_test PROPERTIES
    LABELS "medium;unit;sync"
    TIMEOUT 10)

# SLOW tests: >5s, full synchronization tests with threading delays
add_test(NAME semaphore_test COMMAND test_semaphore)
set_tests_properties(semaphore_test PROPERTIES
    LABELS "slow;integration;sync"
    TIMEOUT 600
    DISABLED FALSE)

add_test(NAME barrier_test COMMAND test_barrier)
set_tests_properties(barrier_test PROPERTIES
    LABELS "slow;integration;sync"
    TIMEOUT 600
    DISABLED FALSE)

# STRESS tests: >30s, exhaustive stress testing (disabled by default)
add_test(NAME stress_test COMMAND test_stress)
set_tests_properties(stress_test PROPERTIES
    LABELS "stress;integration;lockfree"
    TIMEOUT 1800
    DISABLED TRUE)

add_test(NAME table_stress_test COMMAND test_table_stress)
set_tests_properties(table_stress_test PROPERTIES
    LABELS "stress;integration"
    TIMEOUT 1800
    DISABLED TRUE)

# CLI Tools
add_executable(zeroipc tools/zeroipc.cpp)
target_link_libraries(zeroipc Threads::Threads rt)

# Install headers
install(DIRECTORY include/zeroipc
        DESTINATION include)

# Install CLI tools
install(TARGETS zeroipc
        RUNTIME DESTINATION bin)

# Custom test targets for convenient execution
add_custom_target(test_fast
    COMMAND ${CMAKE_CTEST_COMMAND} -L fast --output-on-failure
    COMMENT "Running fast tests only (~30 seconds)")

add_custom_target(test_default
    COMMAND ${CMAKE_CTEST_COMMAND} -L "fast|medium" --output-on-failure
    COMMENT "Running default test suite (fast + medium, ~2 minutes)")

add_custom_target(test_ci
    COMMAND ${CMAKE_CTEST_COMMAND} -LE stress --output-on-failure
    COMMENT "Running CI test suite (all except stress tests, ~10 minutes)")

add_custom_target(test_all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running complete test suite including stress tests (~30 minutes)")