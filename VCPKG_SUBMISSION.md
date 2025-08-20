# vcpkg Submission Guide

## Steps to Submit posix_shm to vcpkg

1. **Fork the vcpkg repository**
   ```bash
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   ```

2. **Create a new branch**
   ```bash
   git checkout -b add-posix-shm
   ```

3. **Create port directory**
   ```bash
   mkdir ports/posix-shm
   ```

4. **Update portfile.cmake**
   
   Update the SHA512 hash in `vcpkg/portfile.cmake`:
   ```bash
   # Download the release tarball and calculate SHA512
   wget https://github.com/alextowell/posix_shm/archive/v1.0.0.tar.gz
   sha512sum v1.0.0.tar.gz
   ```
   
   Then update the portfile:
   ```cmake
   vcpkg_from_github(
       OUT_SOURCE_PATH SOURCE_PATH
       REPO alextowell/posix_shm
       REF v1.0.0
       SHA512 <paste-calculated-sha512>
       HEAD_REF master
   )
   ```

5. **Copy port files**
   - Copy updated `portfile.cmake` to `ports/posix-shm/`
   - Copy `vcpkg.json` to `ports/posix-shm/`

6. **Test the port**
   ```bash
   ./vcpkg install posix-shm
   ```

7. **Update versions database**
   ```bash
   # Add to versions/p-/posix-shm.json
   {
     "versions": [
       {
         "version": "1.0.0",
         "git-tree": "<git-tree-hash>",
         "port-version": 0
       }
     ]
   }
   ```

   Calculate git-tree hash:
   ```bash
   git add ports/posix-shm
   git commit -m "temp"
   git rev-parse HEAD:ports/posix-shm
   ```

8. **Update baseline**
   
   Add to `versions/baseline.json`:
   ```json
   "posix-shm": {
     "baseline": "1.0.0",
     "port-version": 0
   }
   ```

9. **Submit Pull Request**
   - Commit all changes
   - Push to your fork
   - Open PR to Microsoft/vcpkg
   - Title: "[posix-shm] Add new port"
   - Follow the PR checklist

## Important Notes

- vcpkg requires all dependencies to be available in vcpkg
- The port must build on all supported platforms (or explicitly mark unsupported ones)
- Header-only libraries should set `vcpkg_cmake_config_fixup()` appropriately
- Tests are not built by default in vcpkg ports