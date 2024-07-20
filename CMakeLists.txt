cmake_minimum_required(VERSION 3.10)
project(SharedMemoryProject VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the include directory to the include path
include_directories(${PROJECT_SOURCE_DIR}/include)

# We no longer need to add the src subdirectory since SharedMemoryManager is header-only now
# If you have other source files, you might still need this
# add_subdirectory(src)

# Option to build tests
option(BUILD_TESTS "Build the tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Option to build examples
option(BUILD_EXAMPLES "Build the examples" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# If you want to install the headers
install(FILES 
    include/SharedMemoryManager.h
    include/SharedMemoryAllocator.h
    DESTINATION include
)