cmake_minimum_required(VERSION 3.14)
project(zeroipc 
    VERSION 1.0.0
    DESCRIPTION "High-performance POSIX shared memory library for C++23"
    HOMEPAGE_URL "https://github.com/queelius/zeroipc"
    LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(ZEROIPC_INSTALL "Generate installation target" ON)

# Create interface library (header-only)
add_library(zeroipc INTERFACE)
add_library(zeroipc::zeroipc ALIAS zeroipc)

# Configure include directories
target_include_directories(zeroipc INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Set required C++ standard
target_compile_features(zeroipc INTERFACE cxx_std_23)

# Link against rt library on Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(zeroipc INTERFACE rt)
endif()

# Add subdirectories
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation
if(ZEROIPC_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    
    # Install headers
    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )
    
    # Install targets
    install(TARGETS zeroipc
        EXPORT zeroipc-targets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    
    # Install export
    install(EXPORT zeroipc-targets
        FILE zeroipc-targets.cmake
        NAMESPACE zeroipc::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zeroipc
    )
    
    # Create and install config file
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/zeroipc-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/zeroipc-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zeroipc
    )
    
    # Create and install version file
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/zeroipc-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/zeroipc-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/zeroipc-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zeroipc
    )
endif()