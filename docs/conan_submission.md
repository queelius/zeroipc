# Conan Center Index Submission Guide

## Steps to Submit posix_shm to Conan Center

1. **Fork the Conan Center Index repository**
   ```bash
   git clone https://github.com/conan-io/conan-center-index.git
   cd conan-center-index
   ```

2. **Create a new branch**
   ```bash
   git checkout -b posix_shm-1.0.0
   ```

3. **Create the recipe directory structure**
   ```bash
   mkdir -p recipes/posix_shm/all
   mkdir -p recipes/posix_shm/config.yml
   ```

4. **Copy our conanfile.py to the recipe directory**
   - Copy `/home/spinoza/github/repos/posix_shm/conanfile.py` to `recipes/posix_shm/all/conanfile.py`
   - Update the recipe to download from GitHub release instead of using local files

5. **Create config.yml**
   ```yaml
   versions:
     "1.0.0":
       folder: all
   ```

6. **Test the recipe locally**
   ```bash
   conan create recipes/posix_shm/all@ --version=1.0.0
   ```

7. **Create test package**
   ```bash
   mkdir -p recipes/posix_shm/all/test_package
   ```
   
   Create `test_package/conanfile.py`:
   ```python
   from conan import ConanFile
   from conan.tools.build import can_run
   from conan.tools.cmake import cmake_layout, CMake
   import os

   class TestPackageConan(ConanFile):
       settings = "os", "arch", "compiler", "build_type"
       generators = "CMakeDeps", "CMakeToolchain", "VirtualRunEnv"
       test_type = "explicit"

       def requirements(self):
           self.requires(self.tested_reference_str)

       def layout(self):
           cmake_layout(self)

       def build(self):
           cmake = CMake(self)
           cmake.configure()
           cmake.build()

       def test(self):
           if can_run(self):
               bin_path = os.path.join(self.cpp.build.bindir, "test_package")
               self.run(bin_path, env="conanrun")
   ```

8. **Submit Pull Request**
   - Commit changes
   - Push to your fork
   - Open PR to conan-io/conan-center-index
   - Title: "[posix_shm] Add posix_shm/1.0.0"
   - Follow the PR template

## Recipe Updates Needed

The current conanfile.py needs these modifications for Conan Center:

1. Change source to download from GitHub:
   ```python
   def source(self):
       get(self, **self.conan_data["sources"][self.version], strip_root=True)
   ```

2. Add conandata.yml:
   ```yaml
   sources:
     "1.0.0":
       url: "https://github.com/alextowell/posix_shm/archive/v1.0.0.tar.gz"
       sha256: "<calculate-sha256>"
   ```

3. Remove local file references