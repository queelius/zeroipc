cmake_minimum_required(VERSION 3.20)

# Benchmark executables
add_executable(benchmark_queue benchmark_queue.cpp)
target_link_libraries(benchmark_queue PRIVATE zeroipc)

add_executable(benchmark_stack benchmark_stack.cpp)
target_link_libraries(benchmark_stack PRIVATE zeroipc)

add_executable(benchmark_array benchmark_array.cpp)
target_link_libraries(benchmark_array PRIVATE zeroipc)

# Set optimization flags for benchmarks
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_compile_options(benchmark_queue PRIVATE -O3 -march=native)
    target_compile_options(benchmark_stack PRIVATE -O3 -march=native)
    target_compile_options(benchmark_array PRIVATE -O3 -march=native)
endif()