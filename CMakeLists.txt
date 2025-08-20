cmake_minimum_required(VERSION 3.14)
project(posix_shm 
    VERSION 1.0.0
    DESCRIPTION "High-performance POSIX shared memory library for C++23"
    HOMEPAGE_URL "https://github.com/alextowell/posix_shm"
    LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(POSIX_SHM_INSTALL "Generate installation target" ON)

# Create interface library (header-only)
add_library(posix_shm INTERFACE)
add_library(posix_shm::posix_shm ALIAS posix_shm)

# Configure include directories
target_include_directories(posix_shm INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Set required C++ standard
target_compile_features(posix_shm INTERFACE cxx_std_23)

# Link against rt library on Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(posix_shm INTERFACE rt)
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
if(POSIX_SHM_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    
    # Install headers
    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )
    
    # Install targets
    install(TARGETS posix_shm
        EXPORT posix_shm-targets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    
    # Install export
    install(EXPORT posix_shm-targets
        FILE posix_shm-targets.cmake
        NAMESPACE posix_shm::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/posix_shm
    )
    
    # Create and install config file
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/posix_shm-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/posix_shm-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/posix_shm
    )
    
    # Create and install version file
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/posix_shm-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/posix_shm-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/posix_shm-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/posix_shm
    )
endif()