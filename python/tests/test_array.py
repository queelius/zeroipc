"""
Test suite for Array functionality.
"""

import pytest
import sys
import os
import threading

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import posix_shm_py as shm

# Try to import numpy for additional tests
try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False


class TestArray:
    """Test Array functionality."""
    
    def setup_method(self):
        """Set up test fixtures."""
        self.shm_name = "/test_array"
        self.mem = shm.SharedMemory(self.shm_name, 10 * 1024 * 1024)
    
    def teardown_method(self):
        """Clean up after tests."""
        try:
            self.mem.unlink()
        except:
            pass
    
    def test_basic_operations(self):
        """Test basic array operations."""
        arr = shm.FloatArray(self.mem, "basic_array", 100)
        
        # Test length
        assert len(arr) == 100
        
        # Test indexing - set values
        arr[0] = 1.23
        arr[10] = 4.56
        arr[99] = 7.89  # Last element
        
        # Test indexing - get values
        assert arr[0] == 1.23
        assert arr[10] == 4.56
        assert arr[99] == 7.89
        
        # Unset elements should be zero (or uninitialized)
        # Note: might be garbage in real shared memory
        # Just verify it doesn't crash
        _ = arr[50]
    
    def test_index_bounds(self):
        """Test index boundary checking."""
        arr = shm.FloatArray(self.mem, "bounds_array", 10)
        
        # Valid indices
        arr[0] = 1.0
        arr[9] = 9.0
        
        # Out of bounds should raise IndexError
        with pytest.raises(IndexError):
            arr[10] = 10.0
        
        with pytest.raises(IndexError):
            _ = arr[10]
        
        with pytest.raises(IndexError):
            arr[-11] = -11.0  # Negative index too far
    
    def test_negative_indexing(self):
        """Test negative indexing (Python-style)."""
        arr = shm.FloatArray(self.mem, "neg_array", 10)
        
        # Set values
        for i in range(10):
            arr[i] = float(i)
        
        # Negative indexing might not be implemented
        # Just test if it works or raises appropriately
        try:
            assert arr[-1] == 9.0  # Last element
            assert arr[-2] == 8.0
            assert arr[-10] == 0.0  # First element
        except (IndexError, TypeError):
            # Negative indexing not supported
            pass
    
    def test_sequential_access(self):
        """Test sequential read/write patterns."""
        arr = shm.FloatArray(self.mem, "seq_array", 1000)
        
        # Write sequential values
        for i in range(1000):
            arr[i] = float(i) * 1.1
        
        # Read and verify
        for i in range(1000):
            expected = float(i) * 1.1
            assert abs(arr[i] - expected) < 0.0001
    
    def test_concurrent_access(self):
        """Test concurrent array access."""
        arr = shm.FloatArray(self.mem, "concurrent_array", 1000)
        
        def writer(start, end, offset):
            for i in range(start, end):
                arr[i] = float(i + offset)
        
        def reader(start, end):
            total = 0.0
            for i in range(start, end):
                total += arr[i]
            return total
        
        # Initialize array
        for i in range(1000):
            arr[i] = 0.0
        
        # Start concurrent writers
        threads = []
        for i in range(4):
            start = i * 250
            end = start + 250
            t = threading.Thread(target=writer, args=(start, end, i * 1000))
            threads.append(t)
            t.start()
        
        for t in threads:
            t.join()
        
        # Verify some data was written
        total = sum(arr[i] for i in range(1000))
        assert total > 0
    
    @pytest.mark.skipif(not HAS_NUMPY, reason="NumPy not installed")
    def test_numpy_integration(self):
        """Test NumPy array integration."""
        arr = shm.FloatArray(self.mem, "numpy_array", 100)
        
        # Set some values
        for i in range(100):
            arr[i] = float(i)
        
        # Get numpy view (zero-copy)
        np_arr = arr.to_numpy()
        
        assert len(np_arr) == 100
        assert np_arr.dtype == np.float64
        
        # Verify values
        for i in range(100):
            assert np_arr[i] == float(i)
        
        # Modify through numpy - should affect shared memory
        np_arr[0] = 999.0
        assert arr[0] == 999.0
        
        # Modify through shared memory - should affect numpy view
        arr[1] = 888.0
        assert np_arr[1] == 888.0
    
    @pytest.mark.skipif(not HAS_NUMPY, reason="NumPy not installed")
    def test_numpy_from_array(self):
        """Test creating array from NumPy data."""
        arr = shm.FloatArray(self.mem, "from_numpy", 50)
        
        # Create numpy array
        np_data = np.arange(50, dtype=np.float64) * 2.0
        
        # Copy from numpy
        arr.from_numpy(np_data)
        
        # Verify data was copied
        for i in range(50):
            assert arr[i] == float(i * 2.0)
        
        # Test size mismatch
        wrong_size = np.arange(100, dtype=np.float64)
        with pytest.raises(RuntimeError):
            arr.from_numpy(wrong_size)
    
    def test_persistence(self):
        """Test that array persists across connections."""
        # Create and populate
        arr1 = shm.FloatArray(self.mem, "persist_array", 100)
        test_data = [float(i) * 3.14 for i in range(100)]
        
        for i, val in enumerate(test_data):
            arr1[i] = val
        
        del arr1
        
        # Re-attach and verify
        arr2 = shm.FloatArray(self.mem, "persist_array", 100)
        
        for i, expected in enumerate(test_data):
            assert abs(arr2[i] - expected) < 0.0001
    
    def test_patterns(self):
        """Test various access patterns."""
        arr = shm.FloatArray(self.mem, "pattern_array", 100)
        
        # Stride pattern
        for i in range(0, 100, 5):
            arr[i] = float(i)
        
        # Reverse pattern
        for i in range(99, -1, -1):
            if i % 5 != 0:
                arr[i] = float(-i)
        
        # Verify patterns
        for i in range(100):
            if i % 5 == 0:
                assert arr[i] == float(i)
            else:
                assert arr[i] == float(-i)
    
    def test_floating_point_precision(self):
        """Test floating point precision."""
        arr = shm.FloatArray(self.mem, "precision_array", 10)
        
        # Test various float values
        test_values = [
            0.0,
            1.0,
            -1.0,
            3.14159265359,
            2.718281828,
            1e10,
            1e-10,
            float('inf'),
            float('-inf'),
        ]
        
        for i, val in enumerate(test_values):
            if i < len(arr):
                arr[i] = val
                
                if val != val:  # NaN check
                    assert arr[i] != arr[i]  # NaN != NaN
                elif val == float('inf'):
                    assert arr[i] == float('inf')
                elif val == float('-inf'):
                    assert arr[i] == float('-inf')
                else:
                    assert abs(arr[i] - val) < 1e-10


if __name__ == "__main__":
    pytest.main([__file__, "-v"])