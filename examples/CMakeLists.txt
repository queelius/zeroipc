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