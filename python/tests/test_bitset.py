"""
Test suite for Bitset functionality.
"""

import pytest
import sys
import os
import threading

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import posix_shm_py as shm


class TestBitset:
    """Test Bitset functionality."""
    
    def setup_method(self):
        """Set up test fixtures."""
        self.shm_name = "/test_bitset"
        self.mem = shm.SharedMemory(self.shm_name, 1024 * 1024)
    
    def teardown_method(self):
        """Clean up after tests."""
        try:
            self.mem.unlink()
        except:
            pass
    
    def test_basic_operations(self):
        """Test basic bitset operations."""
        bits = shm.Bitset1024(self.mem, "basic_bits")
        
        # Initially all bits should be false
        assert bits.none()
        assert not bits.any()
        assert not bits.all()
        assert bits.count() == 0
        assert len(bits) == 1024
        
        # Set some bits
        bits.set(0)
        bits.set(10)
        bits.set(100)
        bits.set(1023)  # Last bit
        
        assert bits.any()
        assert not bits.none()
        assert not bits.all()
        assert bits.count() == 4
        
        # Test individual bits
        assert bits.test(0)
        assert bits.test(10)
        assert bits.test(100)
        assert bits.test(1023)
        assert not bits.test(1)
        assert not bits.test(500)
    
    def test_set_with_value(self):
        """Test setting bits with explicit value."""
        bits = shm.Bitset1024(self.mem, "value_bits")
        
        # Set to true
        bits.set(5, True)
        assert bits.test(5)
        
        # Set to false
        bits.set(5, False)
        assert not bits.test(5)
        
        # Set multiple
        for i in range(0, 100, 10):
            bits.set(i, True)
        
        assert bits.count() == 10
    
    def test_reset_operations(self):
        """Test reset (clear) operations."""
        bits = shm.Bitset1024(self.mem, "reset_bits")
        
        # Set some bits
        for i in range(10):
            bits.set(i)
        assert bits.count() == 10
        
        # Reset individual bits
        bits.reset(0)
        bits.reset(5)
        bits.reset(9)
        
        assert not bits.test(0)
        assert not bits.test(5)
        assert not bits.test(9)
        assert bits.count() == 7
        
        # Reset all
        bits.reset_all()
        assert bits.none()
        assert bits.count() == 0
    
    def test_flip_operations(self):
        """Test flip operations."""
        bits = shm.Bitset1024(self.mem, "flip_bits")
        
        # Flip individual bits
        bits.flip(10)
        assert bits.test(10)
        
        bits.flip(10)
        assert not bits.test(10)
        
        # Set pattern and flip all
        for i in range(0, 1024, 2):
            bits.set(i)  # Set even bits
        
        count_before = bits.count()
        bits.flip_all()
        
        # All even bits should be false, odd bits true
        for i in range(1024):
            if i % 2 == 0:
                assert not bits.test(i)
            else:
                assert bits.test(i)
        
        assert bits.count() == 1024 - count_before
    
    def test_set_reset_all(self):
        """Test set_all and reset_all operations."""
        bits = shm.Bitset1024(self.mem, "all_bits")
        
        # Set all bits
        bits.set_all()
        assert bits.all()
        assert bits.count() == 1024
        
        for i in range(0, 1024, 100):
            assert bits.test(i)
        
        # Reset all bits
        bits.reset_all()
        assert bits.none()
        assert bits.count() == 0
        
        for i in range(0, 1024, 100):
            assert not bits.test(i)
    
    def test_find_operations(self):
        """Test find_first and find_next operations."""
        bits = shm.Bitset1024(self.mem, "find_bits")
        
        # Empty bitset
        assert bits.find_first() == 1024  # Not found
        
        # Set some bits
        bits.set(10)
        bits.set(20)
        bits.set(30)
        bits.set(100)
        
        # Find first
        assert bits.find_first() == 10
        
        # Find next
        pos = bits.find_first()
        assert pos == 10
        
        pos = bits.find_next(pos)
        assert pos == 20
        
        pos = bits.find_next(pos)
        assert pos == 30
        
        pos = bits.find_next(pos)
        assert pos == 100
        
        pos = bits.find_next(pos)
        assert pos == 1024  # No more bits
    
    def test_indexing_interface(self):
        """Test __getitem__ and __setitem__ interface."""
        bits = shm.Bitset1024(self.mem, "index_bits")
        
        # Test __setitem__
        bits[42] = True
        bits[100] = True
        bits[500] = False
        
        # Test __getitem__
        assert bits[42] == True
        assert bits[100] == True
        assert bits[500] == False
        assert bits[999] == False
        
        # Test with boolean values
        bits[42] = False
        assert not bits[42]
    
    def test_repr(self):
        """Test string representation."""
        bits = shm.Bitset1024(self.mem, "repr_bits")
        
        bits.set(1)
        bits.set(2)
        bits.set(3)
        
        repr_str = repr(bits)
        assert "Bitset1024" in repr_str
        assert "3/1024" in repr_str  # Shows count/size
    
    def test_boundary_conditions(self):
        """Test boundary conditions."""
        bits = shm.Bitset1024(self.mem, "boundary_bits")
        
        # Test first bit
        bits.set(0)
        assert bits.test(0)
        assert bits.find_first() == 0
        
        # Test last bit
        bits.set(1023)
        assert bits.test(1023)
        
        # Test out of bounds (should be safe)
        bits.set(2000)  # Should do nothing
        bits.reset(2000)  # Should do nothing
        bits.flip(2000)  # Should do nothing
        assert not bits.test(2000)  # Should return false
    
    def test_concurrent_operations(self):
        """Test concurrent bit operations."""
        bits = shm.Bitset1024(self.mem, "concurrent_bits")
        
        def set_bits(start, end):
            for i in range(start, end):
                bits.set(i)
        
        def flip_bits(start, end):
            for i in range(start, end):
                bits.flip(i)
        
        threads = []
        
        # Set different ranges in parallel
        for i in range(4):
            start = i * 250
            end = start + 250
            t = threading.Thread(target=set_bits, args=(start, end))
            threads.append(t)
            t.start()
        
        for t in threads:
            t.join()
        
        # All 1000 bits should be set
        assert bits.count() == 1000
    
    def test_persistence(self):
        """Test that bitset persists across connections."""
        # Set pattern
        bits1 = shm.Bitset1024(self.mem, "persist_bits")
        for i in range(0, 1024, 3):
            bits1.set(i)
        
        count1 = bits1.count()
        del bits1
        
        # Re-attach and verify
        bits2 = shm.Bitset1024(self.mem, "persist_bits")
        assert bits2.count() == count1
        
        for i in range(1024):
            if i % 3 == 0:
                assert bits2.test(i)
            else:
                assert not bits2.test(i)


if __name__ == "__main__":
    pytest.main([__file__, "-v"])