"""
Test suite for Lazy codata structure in shared memory.
"""

import os
import threading
import time
import multiprocessing as mp
import pytest
import numpy as np

from zeroipc import Memory
from zeroipc.lazy import Lazy, LazyState, lazy_constant, lazy_function


class TestLazyBasic:
    """Basic Lazy functionality tests."""

    def test_create_lazy_with_immediate_value(self):
        """Test creating a lazy value with immediate value."""
        shm_name = f"/test_lazy_immediate_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            lazy_val = Lazy(memory, "immediate", value=42, dtype=np.int32)

            assert lazy_val.is_evaluated()
            assert lazy_val.get_state() == LazyState.EVALUATED
            assert lazy_val.force() == 42

            # Multiple forces should return same value
            assert lazy_val.force() == 42
            assert lazy_val.force() == 42

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_create_lazy_with_computation(self):
        """Test creating a lazy value with deferred computation."""
        shm_name = f"/test_lazy_compute_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            computation_count = [0]  # Track how many times computed

            def expensive_computation():
                computation_count[0] += 1
                time.sleep(0.01)  # Simulate expensive computation
                return 3.14159

            lazy_val = Lazy(memory, "pi", dtype=np.float64,
                          computation=expensive_computation)

            assert not lazy_val.is_evaluated()
            assert lazy_val.get_state() == LazyState.UNEVALUATED

            # Force evaluation
            result = lazy_val.force()
            assert abs(result - 3.14159) < 0.00001
            assert lazy_val.is_evaluated()
            assert computation_count[0] == 1

            # Subsequent forces should not recompute
            result2 = lazy_val.force()
            assert abs(result2 - 3.14159) < 0.00001
            assert computation_count[0] == 1  # Still 1

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_lazy_error_handling(self):
        """Test lazy evaluation with errors."""
        shm_name = f"/test_lazy_error_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            def failing_computation():
                raise ValueError("Computation failed")

            lazy_val = Lazy(memory, "fail", dtype=np.int32,
                          computation=failing_computation)

            assert not lazy_val.is_evaluated()

            # Force should handle error
            with pytest.raises(RuntimeError, match="Computation failed"):
                lazy_val.force()

            # Check error state
            assert lazy_val.get_state() == LazyState.ERROR
            assert lazy_val.is_error()

            # Subsequent forces should also raise (with the original error message)
            with pytest.raises(RuntimeError, match="Computation failed"):
                lazy_val.force()

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_lazy_try_get(self):
        """Test trying to get lazy value without forcing."""
        shm_name = f"/test_lazy_peek_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            lazy_val = Lazy(memory, "maybe", dtype=np.int32,
                          computation=lambda: 100)

            # Try get before evaluation
            result = lazy_val.try_get()
            assert result is None
            assert not lazy_val.is_evaluated()

            # Force evaluation
            lazy_val.force()

            # Try get after evaluation
            result = lazy_val.try_get()
            assert result == 100
            assert lazy_val.is_evaluated()

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")




    def test_lazy_reset(self):
        """Test resetting lazy value."""
        shm_name = f"/test_lazy_reset_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            lazy_val = Lazy(memory, "value", dtype=np.int32,
                          value=100)

            # Check initial value
            assert lazy_val.force() == 100
            assert lazy_val.is_evaluated()

            # Reset should clear the state
            lazy_val.reset()
            assert not lazy_val.is_evaluated()

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_lazy_different_dtypes(self):
        """Test lazy values with different data types."""
        shm_name = f"/test_lazy_dtypes_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            # Float64
            lazy_float = Lazy(memory, "float", value=3.14, dtype=np.float64)
            assert abs(lazy_float.force() - 3.14) < 0.01

            # Int64
            lazy_int = Lazy(memory, "int", value=1000000, dtype=np.int64)
            assert lazy_int.force() == 1000000

            # Bool
            lazy_bool = Lazy(memory, "bool", value=True, dtype=np.bool_)
            assert lazy_bool.force() == True

            # UInt32
            lazy_uint = Lazy(memory, "uint", value=4294967295, dtype=np.uint32)
            assert lazy_uint.force() == 4294967295

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")


class TestLazyConcurrency:
    """Concurrent access tests for Lazy."""

    def test_concurrent_force(self):
        """Test concurrent forcing of lazy value."""
        shm_name = f"/test_lazy_concurrent_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            computation_count = mp.Value('i', 0)

            def expensive_computation():
                with computation_count.get_lock():
                    computation_count.value += 1
                time.sleep(0.1)  # Simulate expensive work
                return 42

            lazy_val = Lazy(memory, "shared", dtype=np.int32,
                          computation=expensive_computation)

            def worker():
                result = lazy_val.force()
                assert result == 42

            threads = [threading.Thread(target=worker) for _ in range(10)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            # Should only compute once despite multiple threads
            assert computation_count.value == 1

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_multiprocess_lazy(self):
        """Test lazy value across multiple processes."""
        shm_name = f"/test_lazy_multiproc_{os.getpid()}"

        def producer_process(shm_name):
            memory = Memory(shm_name)
            lazy_val = Lazy(memory, "shared", dtype=np.float32, open_existing=True)
            lazy_val.reset(3.14159)

        def consumer_process(shm_name, results_queue):
            memory = Memory(shm_name)
            lazy_val = Lazy(memory, "shared", dtype=np.float32, open_existing=True)
            value = lazy_val.force()
            results_queue.put(value)

        try:
            memory = Memory(shm_name, 1024 * 1024)

            # Create lazy value
            lazy_val = Lazy(memory, "shared", dtype=np.float32)

            # Producer sets value
            producer = mp.Process(target=producer_process, args=(shm_name,))
            producer.start()
            producer.join()

            # Multiple consumers read value
            results_queue = mp.Queue()
            consumers = []
            for _ in range(3):
                p = mp.Process(target=consumer_process, args=(shm_name, results_queue))
                p.start()
                consumers.append(p)

            for p in consumers:
                p.join()

            # Check all consumers got the same value
            results = []
            while not results_queue.empty():
                results.append(results_queue.get())

            assert len(results) == 3
            for r in results:
                assert abs(r - 3.14159) < 0.00001

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")


class TestLazyEdgeCases:
    """Edge case tests for Lazy."""

    def test_lazy_name_limits(self):
        """Test lazy name length limits."""
        shm_name = f"/test_lazy_name_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            # Maximum valid name (31 chars)
            max_name = "a" * 31
            lazy_val = Lazy(memory, max_name, value=1, dtype=np.int32)
            assert lazy_val.force() == 1

            # Name too long should fail
            with pytest.raises(ValueError, match="Name too long"):
                Lazy(memory, "a" * 32, value=2, dtype=np.int32)

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_lazy_no_dtype(self):
        """Test that dtype is required."""
        shm_name = f"/test_lazy_no_dtype_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            with pytest.raises(TypeError, match="dtype is required"):
                Lazy(memory, "nodtype", value=42)

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_lazy_open_nonexistent(self):
        """Test opening non-existent lazy value."""
        shm_name = f"/test_lazy_open_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            with pytest.raises(RuntimeError, match="Lazy not found"):
                Lazy(memory, "nonexistent", dtype=np.int32, open_existing=True)

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_lazy_force_without_computation(self):
        """Test forcing lazy without computation set."""
        shm_name = f"/test_lazy_no_comp_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            lazy_val = Lazy(memory, "nocomp", dtype=np.int32)

            # Should raise error when forcing without computation
            with pytest.raises(RuntimeError, match="No computation"):
                lazy_val.force()

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_lazy_multiple_evaluations(self):
        """Test that computation only happens once."""
        shm_name = f"/test_lazy_once_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            evaluations = []

            def track_evaluation():
                evaluations.append(time.time())
                return len(evaluations)

            lazy_val = Lazy(memory, "once", dtype=np.int32,
                          computation=track_evaluation)

            # Force multiple times from multiple threads
            def force_many():
                for _ in range(5):
                    lazy_val.force()

            threads = [threading.Thread(target=force_many) for _ in range(3)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            # Should have evaluated exactly once
            assert len(evaluations) == 1
            assert lazy_val.force() == 1

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_lazy_set_computation(self):
        """Test setting computation after creation."""
        shm_name = f"/test_lazy_set_comp_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            lazy_val = Lazy(memory, "delayed", dtype=np.int32)

            # Set computation later
            lazy_val.set_computation(lambda: 42)

            assert lazy_val.force() == 42

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_lazy_computation_types(self):
        """Test various computation return types."""
        shm_name = f"/test_lazy_comp_types_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            # Computation returning numpy scalar
            lazy_np = Lazy(memory, "numpy", dtype=np.float32,
                         computation=lambda: np.float32(2.5))
            assert abs(lazy_np.force() - 2.5) < 0.001

            # Computation returning Python int
            lazy_py = Lazy(memory, "python", dtype=np.int64,
                         computation=lambda: 1234567890)
            assert lazy_py.force() == 1234567890

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")


class TestLazyAdvanced:
    """Advanced Lazy functionality tests for better coverage."""

    def test_lazy_init_method(self):
        """Test the init() method (alias for set_computation)."""
        shm_name = f"/test_lazy_init_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            lazy_val = Lazy(memory, "init_test", dtype=np.int32)

            # Use init() method instead of set_computation()
            lazy_val.init(lambda: 123)

            assert lazy_val.force() == 123

        finally:
            Memory.unlink(shm_name)

    def test_lazy_callable_interface(self):
        """Test Lazy as callable (calls force())."""
        shm_name = f"/test_lazy_call_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            lazy_val = Lazy(memory, "callable", value=42, dtype=np.int32)

            # Call like a function
            result = lazy_val()
            assert result == 42

        finally:
            Memory.unlink(shm_name)

    def test_lazy_boolean_conversion(self):
        """Test Lazy boolean conversion."""
        shm_name = f"/test_lazy_bool_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            # Unevaluated lazy should be falsy
            lazy_val = Lazy(memory, "bool_test", dtype=np.int32, computation=lambda: 42)
            assert not bool(lazy_val)

            # Evaluated lazy should be truthy
            lazy_val.force()
            assert bool(lazy_val)

            # Error state lazy should be falsy
            lazy_error = Lazy(memory, "error_test", dtype=np.int32, computation=lambda: 1/0)
            try:
                lazy_error.force()
            except RuntimeError:
                pass
            assert not bool(lazy_error)

        finally:
            Memory.unlink(shm_name)

    def test_lazy_string_representation(self):
        """Test Lazy string representation."""
        shm_name = f"/test_lazy_str_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            # Unevaluated lazy
            lazy_val = Lazy(memory, "str_test", dtype=np.int32, computation=lambda: 42)
            str_repr = str(lazy_val)
            assert "str_test" in str_repr
            assert "UNEVALUATED" in str_repr
            assert "int32" in str_repr
            assert repr(lazy_val) == str(lazy_val)

            # Evaluated lazy
            lazy_val.force()
            str_repr = str(lazy_val)
            assert "EVALUATED" in str_repr
            assert "value=42" in str_repr

        finally:
            Memory.unlink(shm_name)

    def test_lazy_compute_fn_alias(self):
        """Test that compute_fn is kept in sync with computation."""
        shm_name = f"/test_lazy_compute_fn_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            lazy_val = Lazy(memory, "compute_fn_test", dtype=np.int32)

            # Set computation via set_computation
            lazy_val.set_computation(lambda: 999)

            # compute_fn should be set as an alias
            assert hasattr(lazy_val, 'compute_fn')
            assert lazy_val.compute_fn is not None

            # Should work
            result = lazy_val.force()
            assert result == 999

        finally:
            Memory.unlink(shm_name)

    def test_lazy_reset_with_value(self):
        """Test resetting lazy with a new value."""
        shm_name = f"/test_lazy_reset_val_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            lazy_val = Lazy(memory, "reset_val", dtype=np.int32, computation=lambda: 100)

            # Force initial evaluation
            assert lazy_val.force() == 100
            assert lazy_val.is_evaluated()

            # Reset with new value
            lazy_val.reset(value=200)

            # Should have new value without recomputation
            assert lazy_val.force() == 200
            assert lazy_val.is_evaluated()

        finally:
            Memory.unlink(shm_name)

    def test_lazy_error_try_get(self):
        """Test try_get when lazy is in error state."""
        shm_name = f"/test_lazy_error_try_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            lazy_val = Lazy(memory, "error_try", dtype=np.int32,
                          computation=lambda: 1/0)

            # Force error
            try:
                lazy_val.force()
            except RuntimeError:
                pass

            # try_get should raise on error state
            with pytest.raises(RuntimeError, match="Lazy computation failed"):
                lazy_val.try_get()

        finally:
            Memory.unlink(shm_name)


class TestLazyUtilityFunctions:
    """Test utility functions for Lazy creation."""

    def test_lazy_constant(self):
        """Test lazy_constant utility function."""
        shm_name = f"/test_lazy_constant_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            # With inferred dtype
            lazy_int = lazy_constant(memory, "const_int", 42)
            assert lazy_int.dtype == np.int64
            assert lazy_int.force() == 42
            assert lazy_int.is_evaluated()

            # With explicit dtype
            lazy_float = lazy_constant(memory, "const_float", 3.14, dtype=np.float32)
            assert lazy_float.dtype == np.float32
            assert abs(lazy_float.force() - 3.14) < 0.001

        finally:
            Memory.unlink(shm_name)

    def test_lazy_function(self):
        """Test lazy_function utility function."""
        shm_name = f"/test_lazy_function_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            def compute():
                return 123.456

            lazy_func = lazy_function(memory, "func_test", compute, dtype=np.float64)
            assert lazy_func.dtype == np.float64
            assert not lazy_func.is_evaluated()
            assert abs(lazy_func.force() - 123.456) < 0.001

        finally:
            Memory.unlink(shm_name)


class TestLazyComplexTypes:
    """Test Lazy with more complex data types."""

    def test_lazy_complex_dtypes(self):
        """Test Lazy with structured array types."""
        shm_name = f"/test_lazy_complex_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            # Structured array type
            dt = np.dtype([('x', 'i4'), ('y', 'f8')])

            def compute():
                return np.array([(10, 3.14)], dtype=dt)[0]

            lazy_struct = Lazy(memory, "struct_test", dtype=dt, computation=compute)
            result = lazy_struct.force()

            assert result['x'] == 10
            assert abs(result['y'] - 3.14) < 1e-10

        finally:
            Memory.unlink(shm_name)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])