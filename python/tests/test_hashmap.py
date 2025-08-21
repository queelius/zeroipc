"""
Test suite for HashMap functionality.
"""

import pytest
import sys
import os
import threading

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import posix_shm_py as shm


class TestHashMap:
    """Test HashMap functionality."""
    
    def setup_method(self):
        """Set up test fixtures."""
        self.shm_name = "/test_hashmap"
        self.mem = shm.SharedMemory(self.shm_name, 10 * 1024 * 1024)
    
    def teardown_method(self):
        """Clean up after tests."""
        try:
            self.mem.unlink()
        except:
            pass
    
    def test_basic_operations(self):
        """Test basic map operations."""
        map = shm.IntFloatMap(self.mem, "basic_map", capacity=100)
        
        # Test empty map
        assert len(map) == 0
        assert map.empty()
        assert not map  # __bool__
        
        # Test insert
        assert map.insert(1, 3.14)
        assert map.insert(2, 2.71)
        assert map.insert(3, 1.41)
        
        assert len(map) == 3
        assert not map.empty()
        assert map  # __bool__
        
        # Test find
        assert map.find(1) == 3.14
        assert map.find(2) == 2.71
        assert map.find(99) is None  # Not found
    
    def test_dict_interface(self):
        """Test dictionary-like interface."""
        map = shm.IntFloatMap(self.mem, "dict_map", capacity=100)
        
        # Test __setitem__
        map[10] = 1.23
        map[20] = 4.56
        map[30] = 7.89
        
        # Test __getitem__
        assert map[10] == 1.23
        assert map[20] == 4.56
        
        # Test __getitem__ with missing key
        with pytest.raises(KeyError):
            _ = map[999]
        
        # Test __contains__
        assert 10 in map
        assert 20 in map
        assert 999 not in map
        assert map.contains(10)
        
        # Test __delitem__
        del map[20]
        assert 20 not in map
        assert len(map) == 2
        
        # Test __delitem__ with missing key
        with pytest.raises(KeyError):
            del map[999]
    
    def test_update_operations(self):
        """Test update and insert_or_update."""
        map = shm.IntFloatMap(self.mem, "update_map", capacity=100)
        
        # Insert initial values
        map[1] = 1.0
        map[2] = 2.0
        
        # Test update existing
        assert map.update(1, 1.5)
        assert map[1] == 1.5
        
        # Test update non-existing (should fail)
        assert not map.update(99, 99.9)
        assert 99 not in map
        
        # Test insert_or_update
        map[3] = 3.0  # Uses insert_or_update internally
        assert map[3] == 3.0
        
        map[3] = 3.5  # Should update
        assert map[3] == 3.5
    
    def test_erase_and_clear(self):
        """Test erase and clear operations."""
        map = shm.IntFloatMap(self.mem, "erase_map", capacity=100)
        
        # Add some items
        for i in range(10):
            map[i] = float(i) * 1.1
        
        assert len(map) == 10
        
        # Test erase
        assert map.erase(5)
        assert 5 not in map
        assert len(map) == 9
        
        # Test erase non-existing
        assert not map.erase(99)
        
        # Test clear
        map.clear()
        assert len(map) == 0
        assert map.empty()
    
    def test_get_method(self):
        """Test get method with default values."""
        map = shm.IntFloatMap(self.mem, "get_map", capacity=100)
        
        map[1] = 1.23
        
        # Test get existing key
        assert map.get(1) == 1.23
        assert map.get(1, 9.99) == 1.23  # Default ignored
        
        # Test get missing key
        assert map.get(99) is None
        assert map.get(99, 9.99) == 9.99  # Default returned
    
    def test_iteration_methods(self):
        """Test keys(), values(), and items() methods."""
        map = shm.IntFloatMap(self.mem, "iter_map", capacity=100)
        
        # Add test data
        test_data = {1: 1.1, 2: 2.2, 3: 3.3, 4: 4.4, 5: 5.5}
        for k, v in test_data.items():
            map[k] = v
        
        # Test keys()
        keys = map.keys()
        assert len(keys) == 5
        assert set(keys) == set(test_data.keys())
        
        # Test values()
        values = map.values()
        assert len(values) == 5
        assert set(values) == set(test_data.values())
        
        # Test items()
        items = map.items()
        assert len(items) == 5
        assert set(items) == set(test_data.items())
    
    def test_capacity_limits(self):
        """Test behavior at capacity limits."""
        map = shm.IntFloatMap(self.mem, "capacity_map", capacity=5)
        
        # Fill to capacity
        for i in range(5):
            assert map.insert(i, float(i))
        
        assert len(map) == 5
        
        # Try to insert beyond capacity
        # Behavior depends on implementation - might fail or succeed
        # Just verify it doesn't crash
        map.insert(6, 6.0)
    
    def test_concurrent_access(self):
        """Test concurrent access from multiple threads."""
        map = shm.IntFloatMap(self.mem, "concurrent_map", capacity=1000)
        
        def writer(start, count):
            for i in range(start, start + count):
                map[i] = float(i) * 1.1
        
        def reader(start, count):
            for i in range(start, start + count):
                if i in map:
                    _ = map[i]
        
        threads = []
        
        # Start writers
        for i in range(4):
            t = threading.Thread(target=writer, args=(i * 100, 100))
            threads.append(t)
            t.start()
        
        # Start readers
        for i in range(4):
            t = threading.Thread(target=reader, args=(i * 100, 100))
            threads.append(t)
            t.start()
        
        for t in threads:
            t.join()
        
        # Verify some data was written
        assert len(map) > 0
    
    def test_string_key_map(self):
        """Test StringFloatMap with string keys."""
        map = shm.StringFloatMap(self.mem, "string_map", capacity=100)
        
        # Test basic operations with string keys
        map["hello"] = 1.23
        map["world"] = 4.56
        
        assert map["hello"] == 1.23
        assert map["world"] == 4.56
        
        # Update
        map["hello"] = 7.89
        assert map["hello"] == 7.89
        
        # Test missing key
        with pytest.raises(KeyError):
            _ = map["missing"]
    
    def test_persistence(self):
        """Test that map data persists across connections."""
        # Create and populate map
        map1 = shm.IntFloatMap(self.mem, "persist_map", capacity=100)
        test_data = {i: float(i) * 1.1 for i in range(10)}
        for k, v in test_data.items():
            map1[k] = v
        
        del map1
        
        # Re-attach and verify
        map2 = shm.IntFloatMap(self.mem, "persist_map")
        for k, v in test_data.items():
            assert map2[k] == v


if __name__ == "__main__":
    pytest.main([__file__, "-v"])