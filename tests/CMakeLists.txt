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
    test_multi_process.cpp
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