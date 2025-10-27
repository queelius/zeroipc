cmake_minimum_required(VERSION 3.14)
project(zeroipc_examples)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find ZeroIPC headers
set(ZEROIPC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../cpp/include")

# Common compile options
add_compile_options(-Wall -Wextra -O2)

# Include directories
include_directories(${ZEROIPC_INCLUDE_DIR})

# Link libraries
set(LINK_LIBS pthread rt)

# Define examples
set(EXAMPLES
    01_hello_world
    12_reactive_streams
)

# Build each example
foreach(example ${EXAMPLES})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${example}.cpp")
        add_executable(${example} ${example}.cpp)
        target_link_libraries(${example} ${LINK_LIBS})
    endif()
endforeach()

# Additional examples (to be created)
# 02_producer_consumer
# 03_pub_sub
# 10_futures
# 11_lazy_config
# 13_channels
# 20_worker_pool
# 21_event_sourcing
# 22_saga_pattern
# 23_circuit_breaker
# 30_sensor_pipeline
# 31_trading_system
# 32_ml_pipeline
# 33_game_server

# Print build information
message(STATUS "Building ZeroIPC Examples")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Include Directory: ${ZEROIPC_INCLUDE_DIR}")
message(STATUS "Examples: ${EXAMPLES}")