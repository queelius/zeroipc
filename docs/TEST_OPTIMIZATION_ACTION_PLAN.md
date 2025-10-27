# ZeroIPC Test Optimization: Action Plan

## Immediate Actions (Can complete in 1-2 hours)

### 1. Apply Test Configuration Header

**File:** `/home/spinoza/github/beta/zeroipc/cpp/tests/test_config.h` (already created)

**Action:** Update existing slow tests to use parameterized timing:

#### test_semaphore.cpp
Replace all hardcoded delays:

```cpp
// Before:
std::this_thread::sleep_for(10ms);
std::this_thread::sleep_for(100ms);
std::this_thread::sleep_for(50ms);

// After:
#include "test_config.h"
using namespace zeroipc::test;

std::this_thread::sleep_for(TestTiming::CRITICAL_SECTION_DELAY);
std::this_thread::sleep_for(TestTiming::THREAD_START_DELAY);
std::this_thread::sleep_for(TestTiming::THREAD_SYNC_DELAY);
```

**Impact:** Reduces test_semaphore from 10+ minutes to <30 seconds

#### test_barrier.cpp
Same replacements as semaphore.

**Impact:** Reduces test_barrier from 10+ minutes to <30 seconds

### 2. Update CMakeLists.txt with Labels

**File:** `/home/spinoza/github/beta/zeroipc/cpp/CMakeLists.txt`

Add labels to all existing tests:

```cmake
# Example for fast tests
set_tests_properties(table_test PROPERTIES
    LABELS "fast;unit;core"
    TIMEOUT 5)

set_tests_properties(queue_test PROPERTIES
    LABELS "fast;unit;structures;lockfree"
    TIMEOUT 5)

# Example for medium tests
set_tests_properties(lockfree_comprehensive_test PROPERTIES
    LABELS "medium;integration;lockfree"
    TIMEOUT 10)

# Example for slow tests (increase timeout, mark as disabled)
set_tests_properties(semaphore_test PROPERTIES
    LABELS "slow;integration;sync"
    TIMEOUT 60)

set_tests_properties(barrier_test PROPERTIES
    LABELS "slow;integration;sync"
    TIMEOUT 60)

# Stress tests (mark as disabled by default)
set_tests_properties(stress_test PROPERTIES
    LABELS "stress;performance;concurrent"
    TIMEOUT 120
    DISABLED TRUE)

set_tests_properties(aba_problem_test PROPERTIES
    LABELS "stress;lockfree;concurrent"
    TIMEOUT 120
    DISABLED TRUE)

set_tests_properties(memory_boundaries_test PROPERTIES
    LABELS "stress;memory;edge-cases"
    TIMEOUT 120
    DISABLED TRUE)
```

**Impact:** Enables targeted test execution

### 3. Add Custom Test Targets

Append to CMakeLists.txt:

```cmake
# Fast tests only
add_custom_target(test_fast
    COMMAND ${CMAKE_CTEST_COMMAND} -L fast --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Default: fast + medium
add_custom_target(test_default
    COMMAND ${CMAKE_CTEST_COMMAND} -L "fast|medium" --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# CI: everything except stress
add_custom_target(test_ci
    COMMAND ${CMAKE_CTEST_COMMAND} -LE stress --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
```

**Impact:** Convenient test execution workflows

---

## Short-Term Actions (1-2 days)

### 4. Create Fast Variants of Synchronization Tests

Use `test_semaphore_optimized.cpp` as template for:
- `test_barrier_optimized.cpp`
- `test_channel_optimized.cpp` (if exists)

**Pattern:**
1. Keep all FAST tests (no delays)
2. Convert MEDIUM tests to use minimal delays
3. Move SLOW tests to separate `*SlowTest` fixture with `DISABLED_` prefix

**Build as separate executables:**

```cmake
add_executable(test_semaphore_fast tests/test_semaphore_optimized.cpp)
target_link_libraries(test_semaphore_fast gtest_main Threads::Threads rt)
add_test(NAME semaphore_test_fast COMMAND test_semaphore_fast)
set_tests_properties(semaphore_test_fast PROPERTIES
    LABELS "medium;unit;sync"
    TIMEOUT 10)

# Original semaphore test becomes slow variant
set_tests_properties(semaphore_test PROPERTIES
    LABELS "slow;integration;sync"
    TIMEOUT 300
    DISABLED TRUE)
```

**Impact:** Default test runs use fast variants

### 5. Fix Current Test Failures

From the timeout analysis, these tests need adjustment:

#### test_stress.cpp
Current: Times out at 1 second, needs ~2-3 seconds

```cmake
set_tests_properties(stress_test PROPERTIES
    LABELS "stress;performance;concurrent"
    TIMEOUT 120  # Increase from 1s
    DISABLED TRUE)  # Disable by default
```

#### test_aba_problem.cpp
Current: Times out at 1 second

```cmake
set_tests_properties(aba_problem_test PROPERTIES
    LABELS "stress;lockfree;concurrent"
    TIMEOUT 120
    DISABLED TRUE)
```

#### test_memory_boundaries.cpp
Current: Times out at 1 second

```cmake
set_tests_properties(memory_boundaries_test PROPERTIES
    LABELS "stress;memory;edge-cases"
    TIMEOUT 120
    DISABLED TRUE)
```

**Impact:** All tests pass when explicitly run

---

## Medium-Term Actions (1 week)

### 6. Python Test Organization

**Create test categories:**

```python
# python/tests/conftest.py
import pytest

def pytest_configure(config):
    config.addinivalue_line("markers", "fast: Fast tests (<1s)")
    config.addinivalue_line("markers", "medium: Medium tests (<10s)")
    config.addinivalue_line("markers", "slow: Slow tests (>10s)")
    config.addinivalue_line("markers", "stress: Stress tests")
    config.addinivalue_line("markers", "interop: Interop tests")

# Mark tests appropriately:
@pytest.mark.fast
def test_array_create():
    ...

@pytest.mark.medium
def test_queue_mpmc():
    ...

@pytest.mark.slow
@pytest.mark.stress
def test_sustained_load():
    ...
```

**Run targeted tests:**
```bash
pytest -m fast
pytest -m "fast or medium"
pytest -m "not slow"
```

**Impact:** Consistent test organization across languages

### 7. Interop Test Integration

**Create protocol-based interop tests:**

```python
# interop/test_interop_queue.py
import pytest
import subprocess
import json
from pathlib import Path

@pytest.mark.interop
class TestQueueInterop:
    def test_cpp_writes_python_reads(self, tmp_path):
        shm_name = f"/interop_test_{os.getpid()}"

        # Run C++ writer
        cpp_writer = subprocess.Popen(
            ["./cpp_queue_writer", shm_name],
            cwd=Path(__file__).parent
        )
        cpp_writer.wait(timeout=5)
        assert cpp_writer.returncode == 0

        # Run Python reader
        result = subprocess.run(
            ["python3", "python_queue_reader.py", shm_name],
            capture_output=True,
            timeout=5,
            cwd=Path(__file__).parent
        )
        assert result.returncode == 0

        # Validate output
        values = json.loads(result.stdout)
        assert values == list(range(100))

    def test_python_writes_cpp_reads(self, tmp_path):
        # Similar pattern, reversed
        pass
```

**Add to CTest:**

```cmake
add_test(
    NAME interop_queue
    COMMAND python3 -m pytest ${CMAKE_SOURCE_DIR}/interop/test_interop_queue.py
)
set_tests_properties(interop_queue PROPERTIES
    LABELS "medium;interop"
    TIMEOUT 30)
```

**Impact:** Proper interop test integration

### 8. Coverage Analysis

**C++ Coverage:**

```bash
# Build with coverage
cmake -B build -DCMAKE_CXX_FLAGS="--coverage -O0 -g" .
cmake --build build

# Run fast + medium tests
cd build
ctest -L "fast|medium" --output-on-failure

# Generate report
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/tests/*' '*/googletest/*' --output-file coverage_filtered.info
genhtml coverage_filtered.info --output-directory coverage_html

# Open in browser
xdg-open coverage_html/index.html
```

**Python Coverage:**

```bash
cd python
pytest --cov=zeroipc --cov-report=html --cov-report=term-missing
xdg-open htmlcov/index.html
```

**Set coverage targets:**
- Core structures: 90%+
- Lock-free algorithms: 85%+
- Error handling: 80%+

**Impact:** Identify untested code paths

---

## Long-Term Actions (Ongoing)

### 9. CI/CD Pipeline

**GitHub Actions workflow:**

```yaml
# .github/workflows/tests.yml
name: ZeroIPC Tests

on:
  push:
    branches: [ master ]
  pull_request:
    branches: [ master ]

jobs:
  fast-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake g++ lcov

      - name: Configure
        run: cmake -B build .

      - name: Build
        run: cmake --build build -j$(nproc)

      - name: Fast Tests
        run: cd build && ctest -L fast --output-on-failure -j$(nproc)
        timeout-minutes: 2

  medium-tests:
    runs-on: ubuntu-latest
    needs: fast-tests
    steps:
      - uses: actions/checkout@v3
      - name: Configure
        run: cmake -B build .
      - name: Build
        run: cmake --build build -j$(nproc)
      - name: Medium Tests
        run: cd build && ctest -L medium --output-on-failure -j$(nproc)
        timeout-minutes: 5

  python-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - uses: actions/setup-python@v4
        with:
          python-version: '3.11'
      - name: Install
        run: |
          cd python
          pip install -e ".[dev]"
      - name: Fast Tests
        run: cd python && pytest -m "not slow" --cov=zeroipc
        timeout-minutes: 3

  coverage:
    runs-on: ubuntu-latest
    if: github.event_name == 'pull_request'
    steps:
      - uses: actions/checkout@v3
      - name: Configure with coverage
        run: cmake -B build -DCMAKE_CXX_FLAGS="--coverage" .
      - name: Build
        run: cmake --build build
      - name: Test
        run: cd build && ctest -L "fast|medium" --output-on-failure
      - name: Coverage
        run: |
          lcov --capture --directory build --output-file coverage.info
          lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage_filtered.info
      - uses: codecov/codecov-action@v3
        with:
          files: ./coverage_filtered.info

  nightly-stress:
    runs-on: ubuntu-latest
    if: github.event_name == 'schedule'
    steps:
      - uses: actions/checkout@v3
      - name: Configure
        run: cmake -B build .
      - name: Build
        run: cmake --build build
      - name: All Tests
        run: cd build && ctest --output-on-failure -j$(nproc)
        timeout-minutes: 30
```

**Schedule nightly stress tests:**

```yaml
on:
  schedule:
    - cron: '0 2 * * *'  # 2 AM daily
```

**Impact:** Automated quality assurance

### 10. Performance Regression Testing

**Create baseline:**

```bash
cd cpp/benchmarks
./benchmark_queue --benchmark_out=baseline.json
git add baseline.json
git commit -m "Add performance baseline"
```

**CI comparison:**

```yaml
- name: Performance Regression Check
  run: |
    cd build
    ./benchmarks/benchmark_queue --benchmark_out=current.json
    python3 ../scripts/compare_benchmarks.py \
      ../cpp/benchmarks/baseline.json \
      current.json \
      --threshold=0.1  # Fail if 10% slower
```

**Impact:** Prevent performance regressions

---

## Validation Checklist

After implementing the action plan:

### Test Runtime
- [ ] Fast tests complete in <30 seconds
- [ ] Fast + Medium tests complete in <2 minutes
- [ ] Full suite (excluding stress) completes in <10 minutes
- [ ] Stress tests can complete in <30 minutes

### Test Organization
- [ ] All tests have appropriate labels
- [ ] CTest can run tests by category
- [ ] Custom targets work (`make test_fast`, etc.)
- [ ] Python tests have pytest markers

### Coverage
- [ ] C++ coverage >85% on core code
- [ ] Python coverage >85% on core code
- [ ] All lock-free algorithms have stress tests
- [ ] All synchronization primitives tested

### Interop
- [ ] C++ → Python tests passing
- [ ] Python → C++ tests passing
- [ ] Bidirectional tests passing
- [ ] All data types covered

### CI/CD
- [ ] Fast tests run on every push
- [ ] Full tests run on PR
- [ ] Coverage reports generated
- [ ] Nightly stress tests scheduled

### Documentation
- [ ] TESTING_STRATEGY.md complete
- [ ] Test examples for all categories
- [ ] CI/CD pipeline documented
- [ ] Coverage targets documented

---

## Expected Results

### Before Optimization
```
$ time ctest
...
100% tests passed, 0 tests failed out of 19

Total Test time (real) = 1247.32 sec  # 20+ minutes
```

### After Optimization (Default Suite)
```
$ time ctest -L "fast|medium"
...
100% tests passed, 0 tests failed out of 13

Total Test time (real) = 42.18 sec  # <1 minute
```

### After Optimization (CI Suite)
```
$ time ctest -LE stress
...
100% tests passed, 0 tests failed out of 16

Total Test time (real) = 123.45 sec  # ~2 minutes
```

### After Optimization (Full Suite)
```
$ time ctest
...
100% tests passed, 0 tests failed out of 19

Total Test time (real) = 487.22 sec  # ~8 minutes
```

---

## Priority Order

1. **Immediate (Today)**
   - Add test_config.h
   - Update semaphore/barrier timing
   - Add CTest labels
   - Fix timeout issues

2. **This Week**
   - Create fast test variants
   - Python test organization
   - Basic interop integration
   - Coverage analysis

3. **This Month**
   - Full CI/CD pipeline
   - Performance regression testing
   - Complete interop suite
   - Documentation finalization

4. **Ongoing**
   - Monitor test performance
   - Add missing test cases
   - Improve coverage
   - Refine test strategy

---

## Files to Modify

### Immediate Changes
1. `cpp/tests/test_semaphore.cpp` - Add test_config.h, reduce delays
2. `cpp/tests/test_barrier.cpp` - Add test_config.h, reduce delays
3. `cpp/CMakeLists.txt` - Add labels and timeouts
4. `cpp/tests/test_stress.cpp` - Reduce iterations for medium variant

### New Files to Create
1. `cpp/tests/test_config.h` - ✅ Created
2. `cpp/tests/test_semaphore_optimized.cpp` - ✅ Created
3. `cpp/tests/test_barrier_optimized.cpp` - TODO
4. `python/tests/conftest.py` - Add pytest markers
5. `interop/test_interop_queue.py` - TODO
6. `.github/workflows/tests.yml` - TODO

### Documentation
1. `docs/TESTING_STRATEGY.md` - ✅ Created
2. `docs/TEST_OPTIMIZATION_ACTION_PLAN.md` - ✅ This file
3. `README.md` - Update with new test commands

---

## Commands Quick Reference

```bash
# Fast tests only
ctest -L fast --output-on-failure

# Default suite
ctest -L "fast|medium" --output-on-failure

# CI suite
ctest -LE stress --output-on-failure

# Everything
ctest --output-on-failure

# Parallel
ctest -j$(nproc) -L fast

# Python fast
cd python && pytest -m "not slow"

# Coverage
ctest && lcov --capture ...
```

Ready to implement? Start with the Immediate Actions section and work through the checklist.
