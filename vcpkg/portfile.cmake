vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO queelius/posix_shm
    REF v1.0.1
    SHA512 c52d1f09e9cda9d39cacbb6d13e9fa68f48a93c885a5c82eb219dee004826a73acdba6c7cf47b7f1f4b38e8e302f81ea9fbdcd4f105ae8b112f02dd67426318e
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTS=OFF
        -DBUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/posix_shm)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")

file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)