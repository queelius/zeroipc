# ZeroIPC Artifact - OSDI 2025

This artifact contains the complete ZeroIPC implementation and evaluation scripts for reproducing the results in our OSDI 2025 submission.

## System Requirements

### Hardware
- x86_64 processor with AVX2 support
- Minimum 8GB RAM (16GB+ recommended)
- 10GB free disk space

### Software
- Ubuntu 20.04 LTS or newer (tested on 20.04, 22.04, 24.04)
- GCC 11+ or Clang 14+ with C++23 support
- CMake 3.20+
- Python 3.8+
- Git

## Quick Start (10 minutes)

```bash
# 1. Extract artifact
tar xzf zeroipc-osdi25-artifact.tar.gz
cd zeroipc-osdi25-artifact

# 2. Install dependencies
./scripts/install_deps.sh

# 3. Build everything
./scripts/build_all.sh

# 4. Run basic tests
./scripts/run_basic_tests.sh

# 5. Verify functionality
./scripts/verify_artifact.sh
```

## Detailed Installation (20 minutes)

### Step 1: System Dependencies

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    python3-pip \
    python3-numpy \
    libnuma-dev \
    linux-tools-common \
    linux-tools-generic \
    linux-tools-`uname -r`

# Python dependencies
pip3 install --user numpy pytest pytest-cov matplotlib pandas
```

### Step 2: Build ZeroIPC

```bash
# C++ implementation
cd cpp
mkdir build && cd build
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release
ninja
ninja test
cd ../..

# Python implementation  
cd python
pip3 install -e .
pytest tests/
cd ..

# C implementation
cd c
make all test
cd ..
```

### Step 3: Verify Installation

```bash
# Should print "ZeroIPC artifact ready"
./scripts/verify_artifact.sh
```

## Reproducing Paper Results

### Figure 3: Scalability (30 minutes)

```bash
cd experiments
./run_scalability.sh
python3 plot_scalability.py
# Output: figures/scalability.pdf
```

### Table 1: Single-Thread Performance (15 minutes)

```bash
cd experiments
./run_microbenchmarks.sh
python3 analyze_microbenchmarks.py
# Output: tables/single_thread.tex
```

### Table 2: Comparison with Other Systems (20 minutes)

```bash
cd experiments
./run_comparison.sh
python3 analyze_comparison.py
# Output: tables/comparison.tex
```

### Figure 4: Contention Analysis (45 minutes)

```bash
cd experiments
./run_contention.sh --threads 1,2,4,8,16,32,48
python3 plot_contention.py
# Output: figures/contention.pdf
```

### All Experiments (2-3 hours)

```bash
cd experiments
./run_all_experiments.sh
./generate_all_figures.sh
# Output: figures/ and tables/ directories
```

## Key Claims Validation

### Claim 1: Lock-Free Correctness

```bash
# Run stress tests with thread sanitizer
cd cpp/build
cmake .. -DENABLE_SANITIZERS=ON
make test_stress_tsan
./tests/test_stress_tsan --gtest_repeat=100
```

### Claim 2: 8M+ ops/sec Throughput

```bash
cd cpp/build
./benchmarks/benchmark_queue --benchmark_repetitions=10
# Should show >8M ops/sec for single thread
```

### Claim 3: Cross-Language Interoperability

```bash
cd interop
./test_interop.sh
./test_reverse_interop.sh
# All tests should pass
```

### Claim 4: 85% Test Coverage

```bash
cd cpp/build
ninja coverage
# Should show >=85% line coverage
```

## Repository Structure

```
zeroipc-osdi25-artifact/
├── README.md                 # This file
├── LICENSE                   # MIT license
├── ARTIFACT.md              # Artifact metadata
├── cpp/                     # C++23 implementation
│   ├── include/zeroipc/    # Header files
│   ├── tests/               # Unit tests
│   ├── benchmarks/          # Performance benchmarks
│   └── CMakeLists.txt      
├── python/                  # Python implementation
│   ├── zeroipc/            # Python package
│   └── tests/              # Python tests
├── c/                       # C99 implementation
│   ├── include/            # C headers
│   ├── src/                # C sources
│   └── tests/              # C tests
├── interop/                 # Cross-language tests
├── experiments/             # Reproduction scripts
│   ├── run_*.sh            # Individual experiments
│   ├── plot_*.py           # Figure generation
│   └── data/               # Raw results
├── figures/                 # Generated figures
├── tables/                  # Generated tables
└── scripts/                 # Utility scripts
```

## Customization

### Different Thread Counts

Edit `experiments/config.sh`:
```bash
THREAD_COUNTS="1 2 4 8 16"  # Modify as needed
```

### Different Workloads

```bash
cd experiments
./run_custom.sh --workload producer_consumer --threads 8
```

### Performance Profiling

```bash
cd cpp/build
perf record ./benchmarks/benchmark_queue
perf report
```

## Troubleshooting

### Issue: CMake version too old
**Solution**: Install newer CMake from https://cmake.org/download/

### Issue: C++23 not supported
**Solution**: Upgrade compiler or use:
```bash
export CXX=g++-13  # or clang++-15
```

### Issue: Permission denied on /dev/shm
**Solution**: 
```bash
sudo chmod 1777 /dev/shm
```

### Issue: Tests fail with "Resource temporarily unavailable"
**Solution**: Increase shared memory limits:
```bash
sudo sysctl kernel.shmmax=68719476736
sudo sysctl kernel.shmall=4294967296
```

## Expected Results

All experiments should complete successfully and produce results within 10% of those reported in the paper. Minor variations are expected due to:
- Hardware differences
- System load
- CPU frequency scaling
- NUMA effects

## Support

For artifact-related issues, please contact: [artifact-support-email]

## Citation

If you use this artifact in your research, please cite:

```bibtex
@inproceedings{zeroipc-osdi25,
  title={ZeroIPC: Lock-Free Codata Structures for Zero-Copy Inter-Process Communication},
  author={[Authors]},
  booktitle={19th USENIX Symposium on Operating Systems Design and Implementation (OSDI 25)},
  year={2025}
}
```