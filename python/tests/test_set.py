"""
Test suite for Set functionality.
"""

import pytest
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
import posix_shm_py as shm


class TestSet:
    """Test Set functionality."""
    
    def setup_method(self):
        """Set up test fixtures."""
        self.shm_name = "/test_set"
        self.mem = shm.SharedMemory(self.shm_name, 10 * 1024 * 1024)
    
    def teardown_method(self):
        """Clean up after tests."""
        try:
            self.mem.unlink()
        except:
            pass
    
    def test_basic_operations(self):
        """Test basic set operations."""
        s = shm.IntSet(self.mem, "basic_set", capacity=100)
        
        # Test empty set
        assert s.empty()
        assert len(s) == 0
        assert not s  # __bool__
        
        # Test insert
        assert s.insert(10)
        assert s.insert(20)
        assert s.insert(30)
        
        assert not s.empty()
        assert len(s) == 3
        assert s  # __bool__
        
        # Test duplicate insert
        assert not s.insert(10)  # Already exists
        assert len(s) == 3
        
        # Test contains
        assert s.contains(10)
        assert s.contains(20)
        assert not s.contains(99)
    
    def test_contains_operator(self):
        """Test __contains__ operator (in)."""
        s = shm.IntSet(self.mem, "contains_set", capacity=100)
        
        s.insert(1)
        s.insert(2)
        s.insert(3)
        
        assert 1 in s
        assert 2 in s
        assert 3 in s
        assert 4 not in s
        assert 99 not in s
    
    def test_add_remove_aliases(self):
        """Test add and remove method aliases."""
        s = shm.IntSet(self.mem, "alias_set", capacity=100)
        
        # Test add (alias for insert)
        s.add(5)
        s.add(10)
        assert 5 in s
        assert 10 in s
        
        # Test remove (alias for erase)
        s.remove(5)
        assert 5 not in s
        assert 10 in s
    
    def test_erase_and_clear(self):
        """Test erase and clear operations."""
        s = shm.IntSet(self.mem, "erase_set", capacity=100)
        
        # Add items
        for i in range(10):
            s.insert(i)
        
        assert len(s) == 10
        
        # Test erase
        assert s.erase(5)
        assert 5 not in s
        assert len(s) == 9
        
        # Test erase non-existing
        assert not s.erase(99)
        assert len(s) == 9
        
        # Test clear
        s.clear()
        assert s.empty()
        assert len(s) == 0
    
    def test_set_union(self):
        """Test set union operation."""
        s1 = shm.IntSet(self.mem, "set1", capacity=100)
        s2 = shm.IntSet(self.mem, "set2", capacity=100)
        
        # Populate sets
        for i in [1, 2, 3, 4, 5]:
            s1.insert(i)
        
        for i in [4, 5, 6, 7, 8]:
            s2.insert(i)
        
        # Create union
        result = s1.union(self.mem, "union_result", s2)
        
        # Union should contain all elements from both sets
        expected = {1, 2, 3, 4, 5, 6, 7, 8}
        for val in expected:
            assert val in result
        
        assert len(result) == len(expected)
    
    def test_set_intersection(self):
        """Test set intersection operation."""
        s1 = shm.IntSet(self.mem, "inter_set1", capacity=100)
        s2 = shm.IntSet(self.mem, "inter_set2", capacity=100)
        
        # Populate sets
        for i in [1, 2, 3, 4, 5]:
            s1.insert(i)
        
        for i in [3, 4, 5, 6, 7]:
            s2.insert(i)
        
        # Create intersection
        result = s1.intersection(self.mem, "inter_result", s2)
        
        # Intersection should contain only common elements
        expected = {3, 4, 5}
        for val in expected:
            assert val in result
        
        assert len(result) == len(expected)
        
        # Non-common elements should not be in result
        assert 1 not in result
        assert 2 not in result
        assert 6 not in result
        assert 7 not in result
    
    def test_set_difference(self):
        """Test set difference operation."""
        s1 = shm.IntSet(self.mem, "diff_set1", capacity=100)
        s2 = shm.IntSet(self.mem, "diff_set2", capacity=100)
        
        # Populate sets
        for i in [1, 2, 3, 4, 5]:
            s1.insert(i)
        
        for i in [3, 4, 5, 6, 7]:
            s2.insert(i)
        
        # Create difference (s1 - s2)
        result = s1.difference(self.mem, "diff_result", s2)
        
        # Difference should contain elements in s1 but not in s2
        expected = {1, 2}
        for val in expected:
            assert val in result
        
        assert len(result) == len(expected)
        
        # Common elements should not be in result
        assert 3 not in result
        assert 4 not in result
        assert 5 not in result
    
    def test_subset_superset(self):
        """Test subset and superset relationships."""
        s1 = shm.IntSet(self.mem, "subset1", capacity=100)
        s2 = shm.IntSet(self.mem, "subset2", capacity=100)
        s3 = shm.IntSet(self.mem, "subset3", capacity=100)
        
        # s1 = {1, 2, 3}
        for i in [1, 2, 3]:
            s1.insert(i)
        
        # s2 = {1, 2, 3, 4, 5}
        for i in [1, 2, 3, 4, 5]:
            s2.insert(i)
        
        # s3 = {4, 5, 6}
        for i in [4, 5, 6]:
            s3.insert(i)
        
        # Test subset
        assert s1.is_subset(s2)  # s1 ⊆ s2
        assert not s2.is_subset(s1)  # s2 ⊄ s1
        assert not s1.is_subset(s3)  # s1 ⊄ s3
        
        # Test superset
        assert s2.is_superset(s1)  # s2 ⊇ s1
        assert not s1.is_superset(s2)  # s1 ⊉ s2
        assert not s2.is_superset(s3)  # s2 ⊉ s3
        
        # A set is subset/superset of itself
        assert s1.is_subset(s1)
        assert s1.is_superset(s1)
    
    def test_disjoint_sets(self):
        """Test disjoint set checking."""
        s1 = shm.IntSet(self.mem, "disj_set1", capacity=100)
        s2 = shm.IntSet(self.mem, "disj_set2", capacity=100)
        s3 = shm.IntSet(self.mem, "disj_set3", capacity=100)
        
        # s1 = {1, 2, 3}
        for i in [1, 2, 3]:
            s1.insert(i)
        
        # s2 = {4, 5, 6}
        for i in [4, 5, 6]:
            s2.insert(i)
        
        # s3 = {3, 4, 5}
        for i in [3, 4, 5]:
            s3.insert(i)
        
        # s1 and s2 are disjoint
        assert s1.is_disjoint(s2)
        assert s2.is_disjoint(s1)
        
        # s1 and s3 are not disjoint (share 3)
        assert not s1.is_disjoint(s3)
        assert not s3.is_disjoint(s1)
        
        # s2 and s3 are not disjoint (share 4, 5)
        assert not s2.is_disjoint(s3)
        assert not s3.is_disjoint(s2)
        
        # Empty set is disjoint with everything
        s4 = shm.IntSet(self.mem, "empty_set", capacity=100)
        assert s4.is_disjoint(s1)
        assert s1.is_disjoint(s4)
    
    def test_persistence(self):
        """Test that set persists across connections."""
        # Create and populate
        s1 = shm.IntSet(self.mem, "persist_set", capacity=100)
        test_data = {10, 20, 30, 40, 50}
        for val in test_data:
            s1.insert(val)
        
        size1 = len(s1)
        del s1
        
        # Re-attach and verify
        s2 = shm.IntSet(self.mem, "persist_set")
        assert len(s2) == size1
        
        for val in test_data:
            assert val in s2
        
        # Should not contain other values
        assert 99 not in s2
        assert 0 not in s2


if __name__ == "__main__":
    pytest.main([__file__, "-v"])