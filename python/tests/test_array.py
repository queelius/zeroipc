"""Tests for the Array class"""

import os
import unittest
import numpy as np
import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from zeroipc import Memory, Array


class TestArray(unittest.TestCase):
    
    def setUp(self):
        """Set up test fixtures"""
        self.test_name = f"/test_array_{os.getpid()}"
        self.memory = Memory(self.test_name, 10 * 1024 * 1024)  # 10MB
    
    def tearDown(self):
        """Clean up"""
        self.memory.close()
        Memory.unlink(self.test_name)
    
    def test_create_new_array(self):
        """Test creating a new array"""
        arr = Array(self.memory, "test_array", capacity=100, dtype=np.int32)
        
        self.assertEqual(len(arr), 100)
        self.assertEqual(arr.name, "test_array")
        self.assertEqual(arr.dtype, np.dtype('int32'))
    
    def test_access_elements(self):
        """Test reading and writing elements"""
        arr = Array(self.memory, "doubles", capacity=10, dtype=np.float64)
        
        # Write
        arr[0] = 3.14
        arr[5] = 2.718
        arr[9] = 1.414
        
        # Read
        self.assertAlmostEqual(arr[0], 3.14)
        self.assertAlmostEqual(arr[5], 2.718)
        self.assertAlmostEqual(arr[9], 1.414)
    
    def test_open_existing_array(self):
        """Test opening an existing array"""
        # Create array
        arr1 = Array(self.memory, "persistent", capacity=25, dtype=np.float32)
        arr1[0] = 1.5
        arr1[10] = 2.5
        arr1[24] = 3.5
        
        # Open existing
        arr2 = Array(self.memory, "persistent", dtype=np.float32)
        self.assertEqual(len(arr2), 25)
        self.assertAlmostEqual(arr2[0], 1.5, places=5)
        self.assertAlmostEqual(arr2[10], 2.5, places=5)
        self.assertAlmostEqual(arr2[24], 3.5, places=5)
    
    def test_numpy_operations(self):
        """Test NumPy operations on array"""
        arr = Array(self.memory, "numpy_ops", capacity=10, dtype=np.int32)
        
        # Fill with range
        arr.data[:] = np.arange(10)
        
        # Verify with iteration
        for i, val in enumerate(arr):
            self.assertEqual(val, i)
        
        # NumPy operations
        self.assertEqual(np.sum(arr.data), 45)
        self.assertTrue(np.all(arr.data == np.arange(10)))
    
    def test_fill_method(self):
        """Test fill method"""
        arr = Array(self.memory, "fillable", capacity=100, dtype=np.int32)
        
        arr.fill(42)
        
        for i in range(len(arr)):
            self.assertEqual(arr[i], 42)
    
    def test_structured_dtype(self):
        """Test array with structured dtype"""
        # Define a structured dtype
        point_dtype = np.dtype([
            ('x', 'f4'),
            ('y', 'f4'),
            ('z', 'f4')
        ])
        
        arr = Array(self.memory, "points", capacity=5, dtype=point_dtype)
        
        # Write structured data
        arr[0] = (1.0, 2.0, 3.0)
        arr[1] = (4.0, 5.0, 6.0)
        
        # Read structured data
        self.assertAlmostEqual(arr[0]['x'], 1.0, places=5)
        self.assertAlmostEqual(arr[0]['y'], 2.0, places=5)
        self.assertAlmostEqual(arr[0]['z'], 3.0, places=5)
    
    def test_multiple_arrays(self):
        """Test multiple arrays in same memory"""
        arr1 = Array(self.memory, "array1", capacity=10, dtype=np.int32)
        arr2 = Array(self.memory, "array2", capacity=20, dtype=np.float64)
        arr3 = Array(self.memory, "array3", capacity=30, dtype=np.uint8)
        
        self.assertEqual(self.memory.table.entry_count(), 3)
        
        arr1[0] = 100
        arr2[0] = 3.14
        arr3[0] = 255
        
        self.assertEqual(arr1[0], 100)
        self.assertAlmostEqual(arr2[0], 3.14)
        self.assertEqual(arr3[0], 255)
    
    def test_capacity_mismatch_error(self):
        """Test that capacity mismatch raises error"""
        arr1 = Array(self.memory, "sized", capacity=100, dtype=np.int32)
        
        # Opening with wrong capacity should raise
        with self.assertRaises(ValueError):
            Array(self.memory, "sized", capacity=50, dtype=np.int32)
    
    def test_missing_dtype_error(self):
        """Test that missing dtype raises error"""
        with self.assertRaises(TypeError):
            Array(self.memory, "no_dtype", capacity=10)
    
    def test_nonexistent_array_error(self):
        """Test opening nonexistent array raises error"""
        with self.assertRaises(ValueError):
            Array(self.memory, "nonexistent", dtype=np.int32)
    
    def test_long_name_error(self):
        """Test that long names raise error"""
        long_name = "x" * 32
        with self.assertRaises(ValueError):
            Array(self.memory, long_name, capacity=10, dtype=np.int32)
    
    def test_slicing(self):
        """Test array slicing"""
        arr = Array(self.memory, "sliceable", capacity=20, dtype=np.int32)
        arr.data[:] = np.arange(20)
        
        # Test various slices
        self.assertTrue(np.array_equal(arr[5:10], np.arange(5, 10)))
        self.assertTrue(np.array_equal(arr[::2], np.arange(0, 20, 2)))
        self.assertTrue(np.array_equal(arr[-5:], np.arange(15, 20)))


if __name__ == '__main__':
    unittest.main()