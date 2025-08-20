# posix_shm v1.0.1 Release Notes

## 🎉 Clean Release

This is a clean release with all build artifacts and experimental code removed.

## What's Changed

### 🧹 Cleanup
- Removed build directory from repository (build artifacts shouldn't be in version control)
- Removed old experimental code directories (`old-queue/`, `shm_c_code/`)
- Fixed all repository URLs to use correct GitHub organization

### 📚 Documentation
- Documentation deployed to GitHub Pages: https://queelius.github.io/posix_shm/
- Added custom Doxygen styling for better readability
- Fixed all broken links and references

### 📦 Packaging
- Updated Conan recipe with correct SHA256
- Updated vcpkg portfile with correct SHA512
- Ready for submission to package managers

## Features

### Data Structures
- `shm_array<T>` - Fixed-size arrays with O(1) access
- `shm_queue<T>` - Lock-free circular queue
- `shm_atomic<T>` - Atomic types for synchronization
- `shm_ring_buffer<T>` - Ring buffer for streaming
- `shm_object_pool<T>` - Object pool for allocation patterns
- SIMD utilities for vectorized operations

### Performance
- Lock-free algorithms where possible
- Zero-copy IPC
- Cache-friendly memory layout
- SIMD optimizations with AVX2

## Installation

### CMake FetchContent
```cmake
include(FetchContent)
FetchContent_Declare(
    posix_shm
    GIT_REPOSITORY https://github.com/queelius/posix_shm.git
    GIT_TAG        v1.0.1
)
FetchContent_MakeAvailable(posix_shm)
```

## Checksums

- SHA256: `b24f1b6dc0fca6dd56706a68591e68638434bac2a13686806c9588adf89bbad6`
- SHA512: `c52d1f09e9cda9d39cacbb6d13e9fa68f48a93c885a5c82eb219dee004826a73acdba6c7cf47b7f1f4b38e8e302f81ea9fbdcd4f105ae8b112f02dd67426318e`

## License

MIT License - See [LICENSE](LICENSE) file

---

**Full Changelog**: https://github.com/queelius/posix_shm/compare/v1.0.0...v1.0.1