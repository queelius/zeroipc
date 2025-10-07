from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps
from conan.tools.files import copy, save, load
from conan.tools.scm import Git
from conan.errors import ConanInvalidConfiguration
import os


class ZeroipcConan(ConanFile):
    name = "zeroipc"
    version = "2.0.0"
    
    # Package metadata
    description = "Lock-Free Shared Memory IPC with Codata Structures"
    author = "Alex Towell <atowell@siue.edu>"
    topics = ("shared-memory", "ipc", "lock-free", "codata", "cpp23", "header-only")
    homepage = "https://github.com/queelius/zeroipc"
    url = "https://github.com/queelius/zeroipc"
    license = "MIT"
    
    # Package configuration
    package_type = "header-library"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "build_tests": [True, False],
        "build_examples": [True, False], 
        "build_benchmarks": [True, False],
        "build_tools": [True, False]
    }
    default_options = {
        "build_tests": False,
        "build_examples": False,
        "build_benchmarks": False,
        "build_tools": False
    }
    
    # Exports
    exports = "LICENSE", "README.md", "SPECIFICATION.md"
    exports_sources = "cpp/*", "CMakeLists.txt", "LICENSE", "README.md"
    
    def configure(self):
        # Header-only library, minimal settings for consumption
        if self.package_type == "header-library":
            self.settings.rm_safe("compiler.libcxx")

    def validate(self):
        # Ensure C++23 support
        from conan.tools.env import VirtualBuildEnv
        from conan import __version__ as conan_version
        
        # Import Version for proper version comparison
        try:
            from conan.tools.scm import Version
        except ImportError:
            from conans.model.version import Version
            
        if self.settings.compiler == "gcc":
            if Version(str(self.settings.compiler.version)) < Version("13"):
                raise ConanInvalidConfiguration("zeroipc requires GCC >= 13 for C++23 support")
        elif self.settings.compiler == "clang":
            if Version(str(self.settings.compiler.version)) < Version("16"):
                raise ConanInvalidConfiguration("zeroipc requires Clang >= 16 for C++23 support")
        elif self.settings.compiler == "msvc":
            if Version(str(self.settings.compiler.version)) < Version("193"):
                raise ConanInvalidConfiguration("zeroipc requires MSVC >= 19.30 (VS 2022) for C++23 support")
    
    def layout(self):
        cmake_layout(self, src_folder=".")
    
    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        
        tc = CMakeToolchain(self)
        tc.variables["CMAKE_CXX_STANDARD"] = "23"
        tc.variables["CMAKE_CXX_STANDARD_REQUIRED"] = True
        tc.variables["ZEROIPC_BUILD_TESTS"] = self.options.build_tests
        tc.variables["ZEROIPC_BUILD_EXAMPLES"] = self.options.build_examples  
        tc.variables["ZEROIPC_BUILD_BENCHMARKS"] = self.options.build_benchmarks
        tc.variables["ZEROIPC_BUILD_TOOLS"] = self.options.build_tools
        tc.generate()
    
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        
        if self.options.build_tests:
            cmake.test()
    
    def package(self):
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        copy(self, "README.md", src=self.source_folder, dst=os.path.join(self.package_folder, "share/doc/zeroipc"))
        copy(self, "SPECIFICATION.md", src=self.source_folder, dst=os.path.join(self.package_folder, "share/doc/zeroipc"))
        
        cmake = CMake(self)
        cmake.install()
    
    def package_info(self):
        # Header-only library
        self.cpp_info.set_property("cmake_file_name", "zeroipc")
        self.cpp_info.set_property("cmake_target_name", "zeroipc::zeroipc")
        
        # Include directories
        self.cpp_info.includedirs = ["include"]
        
        # System libraries
        if self.settings.os in ("Linux", "FreeBSD"):
            self.cpp_info.system_libs.append("rt")
            self.cpp_info.system_libs.append("pthread")
        elif self.settings.os == "Windows":
            pass  # No special system libs needed on Windows
        
        # Compiler requirements
        self.cpp_info.cppstd = "23"
        
        # CMake variables for consumers
        self.cpp_info.set_property("cmake_target_aliases", ["zeroipc"])

    def test(self):
        if self.options.build_tests and not self.conf.get("tools.build:skip_test", default=False):
            cmake = CMake(self)
            cmake.test()