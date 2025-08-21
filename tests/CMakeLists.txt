# Fetch Catch2 v3 (header-only, modern C++)
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

# Create test executable
add_executable(posix_shm_tests
    test_main.cpp
    test_posix_shm.cpp
    test_shm_table.cpp
    test_shm_array.cpp
    test_shm_queue.cpp
    test_shm_stack.cpp
    test_shm_hash_map.cpp
    test_shm_set.cpp
    test_shm_bitset.cpp
    test_shm_ring_buffer.cpp
    test_shm_object_pool.cpp
    test_multi_process.cpp
    test_cpp_hashmap.cpp
    test_hash_map_multiple.cpp
    test_fragmentation.cpp
    test_integration_multi_structures.cpp
)

target_include_directories(posix_shm_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(posix_shm_tests PRIVATE
    Catch2::Catch2WithMain
    rt      # POSIX shared memory
    pthread # std::atomic
)

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(posix_shm_tests)