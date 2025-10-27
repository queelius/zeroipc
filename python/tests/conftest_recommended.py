# Recommended pytest configuration for ZeroIPC Python tests
# Rename to conftest.py to activate

import pytest
import os
import tempfile
import multiprocessing
from pathlib import Path

# ============================================================================
# Test Markers
# ============================================================================

def pytest_configure(config):
    """Register custom markers."""
    config.addinivalue_line(
        "markers",
        "fast: Fast tests (<1 second) - run on every commit"
    )
    config.addinivalue_line(
        "markers",
        "medium: Medium tests (<10 seconds) - run in CI"
    )
    config.addinivalue_line(
        "markers",
        "slow: Slow tests (>10 seconds) - run on PR only"
    )
    config.addinivalue_line(
        "markers",
        "stress: Stress tests (>30 seconds) - run nightly"
    )
    config.addinivalue_line(
        "markers",
        "interop: Cross-language interoperability tests"
    )
    config.addinivalue_line(
        "markers",
        "lockfree: Lock-free data structure tests"
    )
    config.addinivalue_line(
        "markers",
        "sync: Synchronization primitive tests"
    )


# ============================================================================
# Test Configuration
# ============================================================================

class TestConfig:
    """Configuration for test parameters based on environment."""

    @staticmethod
    def test_mode():
        """Get test mode from environment."""
        return os.getenv("ZEROIPC_TEST_MODE", "FAST").upper()

    @staticmethod
    def is_ci():
        """Check if running in CI environment."""
        return os.getenv("CI") or os.getenv("CONTINUOUS_INTEGRATION")

    @classmethod
    def thread_count(cls):
        """Number of threads to use in tests."""
        mode = cls.test_mode()
        if mode == "FAST":
            return 4
        elif mode == "MEDIUM":
            return 8
        elif mode == "STRESS":
            return min(32, multiprocessing.cpu_count() * 2)
        else:
            return 4

    @classmethod
    def iteration_count(cls):
        """Number of iterations for stress tests."""
        mode = cls.test_mode()
        if mode == "FAST":
            return 100
        elif mode == "MEDIUM":
            return 1000
        elif mode == "STRESS":
            return 10000
        else:
            return 100

    @classmethod
    def timeout_multiplier(cls):
        """Timeout multiplier for CI environments."""
        return 3 if cls.is_ci() else 1


@pytest.fixture(scope="session")
def test_config():
    """Provide test configuration to all tests."""
    return TestConfig()


# ============================================================================
# Shared Memory Management
# ============================================================================

@pytest.fixture
def shm_name():
    """Generate unique shared memory name for test isolation."""
    pid = os.getpid()
    tid = id(pytest)  # Use pytest object id as unique identifier
    name = f"/zeroipc_test_{pid}_{tid}"
    yield name
    # Cleanup happens in Memory.__del__
    # But we can explicitly unlink if needed
    try:
        from zeroipc import Memory
        Memory.unlink(name)
    except:
        pass


@pytest.fixture
def temp_shm():
    """Create temporary shared memory that auto-cleans."""
    from zeroipc import Memory
    pid = os.getpid()
    name = f"/zeroipc_temp_{pid}_{id(pytest)}"
    mem = Memory(name, 10 * 1024 * 1024)  # 10MB
    yield mem
    Memory.unlink(name)


@pytest.fixture
def large_shm():
    """Create large shared memory for stress tests."""
    from zeroipc import Memory
    pid = os.getpid()
    name = f"/zeroipc_large_{pid}_{id(pytest)}"
    mem = Memory(name, 100 * 1024 * 1024)  # 100MB
    yield mem
    Memory.unlink(name)


# ============================================================================
# Test Data Fixtures
# ============================================================================

@pytest.fixture
def sample_array_data():
    """Sample data for array tests."""
    import numpy as np
    return np.arange(1000, dtype=np.int32)


@pytest.fixture
def sample_float_data():
    """Sample float data."""
    import numpy as np
    return np.random.randn(1000).astype(np.float32)


@pytest.fixture(params=[np.int32, np.int64, np.float32, np.float64])
def dtype_param(request):
    """Parameterized fixture for testing multiple dtypes."""
    import numpy as np
    return request.param


# ============================================================================
# Performance Fixtures
# ============================================================================

@pytest.fixture
def benchmark_memory():
    """Shared memory optimized for benchmarking."""
    from zeroipc import Memory
    # Use huge pages if available on Linux
    name = f"/zeroipc_bench_{os.getpid()}"
    mem = Memory(name, 1024 * 1024 * 1024)  # 1GB
    yield mem
    Memory.unlink(name)


# ============================================================================
# Pytest Hooks
# ============================================================================

def pytest_collection_modifyitems(config, items):
    """Modify test collection based on markers and environment."""

    # Skip slow/stress tests unless explicitly requested
    skip_slow = pytest.mark.skip(reason="Slow test - use -m slow to run")
    skip_stress = pytest.mark.skip(reason="Stress test - use -m stress to run")

    # Check if specific markers were requested
    markers = config.option.markexpr

    for item in items:
        # Auto-skip slow tests unless explicitly requested
        if "slow" in item.keywords and "slow" not in markers:
            if not markers:  # Only skip if no markers specified
                item.add_marker(skip_slow)

        # Auto-skip stress tests unless explicitly requested
        if "stress" in item.keywords and "stress" not in markers:
            if not markers:
                item.add_marker(skip_stress)


def pytest_report_header(config):
    """Add custom information to test report header."""
    mode = TestConfig.test_mode()
    ci = "Yes" if TestConfig.is_ci() else "No"
    threads = TestConfig.thread_count()
    iterations = TestConfig.iteration_count()

    return [
        f"ZeroIPC Test Configuration:",
        f"  Mode: {mode}",
        f"  CI Environment: {ci}",
        f"  Default Threads: {threads}",
        f"  Default Iterations: {iterations}",
    ]


# ============================================================================
# Interop Test Helpers
# ============================================================================

@pytest.fixture
def cpp_interop_path():
    """Path to C++ interop test binaries."""
    # Assume built in ../cpp/build
    root = Path(__file__).parent.parent.parent
    return root / "cpp" / "build"


@pytest.fixture
def interop_tools(cpp_interop_path):
    """Dictionary of available interop test tools."""
    import subprocess

    tools = {}

    # Check for C++ writer/reader
    cpp_writer = cpp_interop_path / "cpp_writer"
    if cpp_writer.exists():
        tools["cpp_writer"] = str(cpp_writer)

    cpp_reader = cpp_interop_path / "cpp_reader"
    if cpp_reader.exists():
        tools["cpp_reader"] = str(cpp_reader)

    return tools


# ============================================================================
# Cleanup Fixtures
# ============================================================================

@pytest.fixture(scope="session", autouse=True)
def cleanup_shared_memory():
    """Cleanup any leftover shared memory from previous test runs."""
    import subprocess
    import glob

    # At start of session
    pattern = f"/dev/shm/zeroipc_test_*"
    for shm_file in glob.glob(pattern):
        try:
            os.unlink(shm_file)
        except:
            pass

    yield

    # At end of session
    for shm_file in glob.glob(pattern):
        try:
            os.unlink(shm_file)
        except:
            pass


# ============================================================================
# Assertion Helpers
# ============================================================================

class Helpers:
    """Helper methods for common test assertions."""

    @staticmethod
    def assert_array_equal(arr1, arr2, msg="Arrays not equal"):
        """Assert numpy arrays are equal."""
        import numpy as np
        np.testing.assert_array_equal(arr1, arr2, err_msg=msg)

    @staticmethod
    def assert_eventually(condition_fn, timeout=1.0, interval=0.01):
        """Assert condition becomes true within timeout."""
        import time
        start = time.time()
        while time.time() - start < timeout:
            if condition_fn():
                return
            time.sleep(interval)
        raise AssertionError(
            f"Condition not met within {timeout} seconds"
        )

    @staticmethod
    def assert_concurrent_invariant(invariant_fn, duration=0.1):
        """Assert invariant holds during concurrent execution."""
        import time
        start = time.time()
        while time.time() - start < duration:
            assert invariant_fn(), "Invariant violated during concurrent execution"
            time.sleep(0.001)


@pytest.fixture
def helpers():
    """Provide helper methods to tests."""
    return Helpers()


# ============================================================================
# Example Usage in Tests
# ============================================================================

# Mark tests appropriately:
#
# @pytest.mark.fast
# def test_array_create(temp_shm):
#     from zeroipc import Array
#     arr = Array(temp_shm, "test", dtype=np.int32, shape=100)
#     assert arr.size == 100
#
# @pytest.mark.medium
# @pytest.mark.lockfree
# def test_queue_mpmc(temp_shm, test_config):
#     from zeroipc import Queue
#     import threading
#
#     q = Queue(temp_shm, "test", capacity=1000, dtype=np.int32)
#     threads = test_config.thread_count()
#     # ... MPMC test
#
# @pytest.mark.slow
# @pytest.mark.stress
# def test_sustained_load(large_shm, test_config):
#     # ... long-running stress test
#     iterations = test_config.iteration_count()
#
# @pytest.mark.interop
# def test_cpp_python_queue(temp_shm, interop_tools):
#     import subprocess
#     # ... run C++ writer, Python reader
