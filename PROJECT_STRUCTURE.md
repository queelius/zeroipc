# Repository Structure

This repository follows modern best practices for a hybrid C++/Python project.

```
posix_shm/
├── include/                    # C++ headers (public API)
│   ├── posix_shm.h
│   ├── shm_queue.h
│   ├── shm_stack.h
│   └── ...
│
├── src/                        # C++ implementation (if needed for non-header-only parts)
│   └── (currently empty - header-only library)
│
├── tests/                      # C++ tests
│   ├── CMakeLists.txt
│   ├── test_main.cpp
│   ├── test_shm_queue.cpp
│   └── ...
│
├── benchmarks/                 # Performance benchmarks
│   ├── CMakeLists.txt
│   ├── bench_queue.cpp
│   └── ...
│
├── examples/                   # C++ examples
│   ├── CMakeLists.txt
│   ├── basic_usage.cpp
│   ├── n_body_simulation.cpp
│   └── ...
│
├── python/                     # Python package
│   ├── setup.py               # Python package setup
│   ├── pyproject.toml         # Modern Python build config
│   ├── MANIFEST.in            # Include C++ headers in package
│   ├── README.md              # Python-specific readme
│   │
│   ├── src/                   # C++ extension source
│   │   └── python_bindings.cpp
│   │
│   ├── posix_shm/             # Pure Python code
│   │   ├── __init__.py        # Package initialization
│   │   ├── helpers.py         # Python helper functions
│   │   └── numpy_utils.py     # NumPy integration utilities
│   │
│   └── tests/                 # Python tests
│       ├── test_queue.py
│       ├── test_interop.py    # Test C++/Python interop
│       └── ...
│
├── docs/                       # Documentation
│   ├── Doxyfile               # C++ API docs
│   ├── mkdocs.yml             # MkDocs config for Python docs
│   ├── index.md               # Main documentation
│   ├── cpp_api/               # C++ documentation
│   ├── python_api/            # Python documentation
│   └── data_structures/       # Conceptual docs
│       ├── README.md
│       ├── shm_queue.md
│       └── ...
│
├── cmake/                      # CMake modules
│   ├── FindCatch2.cmake
│   └── ...
│
├── conan/                      # Conan packaging
│   ├── conanfile.py
│   └── test_package/
│
├── vcpkg/                      # vcpkg packaging
│   ├── portfile.cmake
│   └── vcpkg.json
│
├── .github/                    # GitHub specific
│   └── workflows/
│       ├── cpp_ci.yml         # C++ CI/CD
│       ├── python_ci.yml      # Python CI/CD
│       └── docs.yml           # Documentation deployment
│
├── CMakeLists.txt             # Root CMake configuration
├── Makefile                   # Convenience makefile
├── README.md                  # Main project readme
├── LICENSE                    # MIT License
├── CHANGELOG.md               # Version history
├── CONTRIBUTING.md            # Contribution guidelines
└── CLAUDE.md                  # AI assistant context
```

## Build Systems

### C++ Build
```bash
# Traditional CMake
cmake -B build .
cmake --build build

# Or convenience Makefile
make
make test
make install
```

### Python Build
```bash
# Development install
cd python
pip install -e .

# Build wheel
python -m build

# Upload to PyPI
python -m twine upload dist/*
```

## Package Distribution

### C++ Distribution
- **Conan**: `conan create .`
- **vcpkg**: Via vcpkg registry
- **CMake**: FetchContent or git submodule
- **System**: Header-only, just copy includes

### Python Distribution
- **PyPI**: `pip install posix-shm`
- **Conda**: `conda install -c conda-forge posix-shm`
- **Source**: `pip install git+https://github.com/queelius/posix_shm.git#subdirectory=python`

## Testing Strategy

### C++ Tests
- Unit tests with Catch2
- Integration tests for IPC
- Benchmarks with Google Benchmark
- Sanitizers (ASAN, TSAN, UBSAN)

### Python Tests
- pytest for unit tests
- Integration tests with multiprocessing
- Interop tests (Python creates, C++ reads and vice versa)
- Type checking with mypy

## Documentation

### C++ Documentation
- Doxygen for API reference
- Markdown for tutorials
- Examples in examples/

### Python Documentation
- MkDocs with mkdocstrings for API reference
- Jupyter notebooks for tutorials
- Type hints in code
- Material theme for modern look

## CI/CD Pipeline

### C++ Pipeline
1. Build on multiple platforms (Linux, macOS, Windows WSL)
2. Run tests with sanitizers
3. Generate coverage reports
4. Build documentation
5. Package for Conan/vcpkg

### Python Pipeline
1. Test on Python 3.8-3.12
2. Build wheels for multiple platforms
3. Run integration tests
4. Type checking
5. Upload to PyPI on tags

## Development Workflow

### C++ Development
```bash
make dev        # Debug build with sanitizers
make test       # Run tests
make bench      # Run benchmarks
make format     # Format code
make tidy       # Run clang-tidy
```

### Python Development
```bash
cd python
pip install -e ".[dev]"  # Install with dev dependencies
pytest                    # Run tests
black .                   # Format code
mypy posix_shm           # Type check
```

## Best Practices

### C++
- C++23 standard
- Header-only when possible
- Concepts for type constraints
- [[nodiscard]] for API safety
- Lock-free algorithms
- Zero-copy operations

### Python
- Type hints everywhere
- Property decorators for getters/setters
- Context managers for RAII
- NumPy integration for arrays
- Pickle support for serialization
- Pythonic naming (snake_case)

### Both
- Semantic versioning
- Comprehensive tests
- Clear documentation
- Examples for common use cases
- Benchmarks for performance claims