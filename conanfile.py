from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout
from conan.tools.files import copy


class PosixShmConan(ConanFile):
    name = "posix_shm"
    version = "1.0.0"
    license = "MIT"
    author = "Alex Towell"
    url = "https://github.com/queelius/posix_shm"
    description = "High-performance POSIX shared memory library for C++23"
    topics = ("shared-memory", "ipc", "posix", "lock-free", "header-only")
    
    # Settings and options
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "build_tests": [True, False],
        "build_examples": [True, False]
    }
    default_options = {
        "build_tests": False,
        "build_examples": False
    }
    
    # Header-only
    exports_sources = "include/*", "CMakeLists.txt", "cmake/*", "LICENSE", "README.md"
    no_copy_source = True
    
    def validate(self):
        # Check C++23 support
        if self.settings.compiler.get_safe("cppstd"):
            from conan.tools.build import check_min_cppstd
            check_min_cppstd(self, "23")
    
    def layout(self):
        cmake_layout(self, src_folder=".")
    
    def requirements(self):
        if self.options.build_tests:
            self.requires("catch2/3.5.2")
    
    def package(self):
        # Copy headers
        copy(self, "*.h", 
             src=self.source_folder, 
             dst=self.package_folder,
             keep_path=True)
        # Copy license
        copy(self, "LICENSE", 
             src=self.source_folder,
             dst=self.package_folder)
    
    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        
        # Header-only library
        self.cpp_info.includedirs = ["include"]
        
        # Link rt library on Linux
        if self.settings.os == "Linux":
            self.cpp_info.system_libs = ["rt"]
        
        # Require C++23
        self.cpp_info.set_property("cmake_target_name", "posix_shm::posix_shm")
        self.cpp_info.set_property("cmake_file_name", "posix_shm")
        
    def package_id(self):
        # Header-only library
        self.info.clear()