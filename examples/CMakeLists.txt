add_executable(basic_usage
    basic_usage.cpp
)

target_include_directories(basic_usage PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(basic_usage
    rt  # For POSIX shared memory functions
    pthread  # For std::atomic
)

add_executable(custom_table_sizes
    custom_table_sizes.cpp
)

target_include_directories(custom_table_sizes PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(custom_table_sizes
    rt  # For POSIX shared memory functions
    pthread  # For std::atomic
)

add_executable(simulation_patterns
    simulation_patterns.cpp
)

target_include_directories(simulation_patterns PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(simulation_patterns
    rt  # For POSIX shared memory functions
    pthread  # For std::atomic
)

add_executable(benchmark_reads
    benchmark_reads.cpp
)

target_include_directories(benchmark_reads PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(benchmark_reads
    rt  # For POSIX shared memory functions
    pthread  # For std::atomic
)

# Enable optimizations for benchmark
target_compile_options(benchmark_reads PRIVATE -O3 -march=native)