cmake_minimum_required(VERSION 3.20)
project(zeroipc 
    VERSION 2.0.0 
    DESCRIPTION "Lock-Free Shared Memory IPC with Codata Structures"
    HOMEPAGE_URL "https://github.com/queelius/zeroipc"
    LANGUAGES CXX)

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(ZEROIPC_BUILD_TESTS "Build tests" OFF)
option(ZEROIPC_BUILD_EXAMPLES "Build examples" OFF)
option(ZEROIPC_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(ZEROIPC_BUILD_TOOLS "Build CLI tools" OFF)
option(ZEROIPC_BUILD_DOCS "Build documentation" OFF)

# Include subdirectory for the main implementation
add_subdirectory(cpp)

# Install license and documentation (handled by Conan recipe for packaging)
# install(FILES LICENSE README.md SPECIFICATION.md DESTINATION share/doc/zeroipc)

# Package configuration
set(CPACK_PACKAGE_NAME "zeroipc")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VENDOR "Alex Towell")
set(CPACK_PACKAGE_CONTACT "atowell@siue.edu")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

include(CPack)