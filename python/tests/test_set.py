"""
Test suite for lock-free hash set in shared memory.
"""

import os
import threading
import pytest
import numpy as np

from zeroipc import Memory
from zeroipc.set import Set, HashSet


class TestSetBasic:
    """Basic Set functionality tests."""

    def test_create_set(self):
        """Test creating a new set."""
        shm_name = f"/test_set_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create set with integer elements
            set_obj = Set(memory, "int_set", capacity=100, dtype=np.int32)
            assert set_obj.name == "int_set"
            assert set_obj.size() == 0
            assert set_obj.empty() == True

            # Test HashSet alias
            set2 = HashSet(memory, "hash_set", capacity=50, dtype=np.int64)
            assert set2.name == "hash_set"

        finally:
            Memory.unlink(shm_name)

    def test_insert_and_contains(self):
        """Test basic insert and contains operations."""
        shm_name = f"/test_set_ops_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "test_set", capacity=100, dtype=np.int32)

            # Insert elements
            assert set_obj.insert(10) == True
            assert set_obj.insert(20) == True
            assert set_obj.insert(30) == True

            # Check contains
            assert set_obj.contains(10) == True
            assert set_obj.contains(20) == True
            assert set_obj.contains(30) == True
            assert set_obj.contains(40) == False

            # Size should be 3
            assert set_obj.size() == 3
            assert set_obj.empty() == False

        finally:
            Memory.unlink(shm_name)

    def test_add_alias(self):
        """Test that add() is an alias for insert()."""
        shm_name = f"/test_set_add_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "add_set", capacity=100, dtype=np.int32)

            # Use add() method
            assert set_obj.add(100) == True
            assert set_obj.add(200) == True

            assert set_obj.contains(100) == True
            assert set_obj.contains(200) == True
            assert set_obj.size() == 2

        finally:
            Memory.unlink(shm_name)

    def test_duplicate_elements(self):
        """Test that sets don't store duplicates."""
        shm_name = f"/test_set_dup_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "dup_set", capacity=100, dtype=np.int32)

            # Insert same element multiple times
            set_obj.insert(42)
            set_obj.insert(42)
            set_obj.insert(42)

            # Should only have one element
            assert set_obj.size() == 1
            assert set_obj.contains(42) == True

            # Insert different elements
            set_obj.insert(43)
            set_obj.insert(44)
            assert set_obj.size() == 3

        finally:
            Memory.unlink(shm_name)

    def test_erase(self):
        """Test removing elements with erase."""
        shm_name = f"/test_set_erase_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "erase_set", capacity=100, dtype=np.int32)

            # Add elements
            for i in range(10):
                set_obj.insert(i)
            assert set_obj.size() == 10

            # Erase some elements
            assert set_obj.erase(5) == True
            assert set_obj.contains(5) == False
            assert set_obj.size() == 9

            assert set_obj.erase(0) == True
            assert set_obj.erase(9) == True
            assert set_obj.size() == 7

            # Try to erase non-existent
            assert set_obj.erase(100) == False
            assert set_obj.size() == 7

        finally:
            Memory.unlink(shm_name)

    def test_remove_alias(self):
        """Test that remove() is an alias for erase()."""
        shm_name = f"/test_set_remove_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "remove_set", capacity=100, dtype=np.int32)

            set_obj.insert(10)
            set_obj.insert(20)

            # Use remove() method
            assert set_obj.remove(10) == True
            assert set_obj.contains(10) == False
            assert set_obj.size() == 1

        finally:
            Memory.unlink(shm_name)

    def test_discard(self):
        """Test discard method (no error on missing element)."""
        shm_name = f"/test_set_discard_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "discard_set", capacity=100, dtype=np.int32)

            set_obj.insert(10)
            set_obj.insert(20)

            # Discard existing element
            set_obj.discard(10)
            assert set_obj.contains(10) == False

            # Discard non-existent (should not raise error)
            set_obj.discard(100)  # No exception expected
            set_obj.discard(10)   # Already removed, no error

            assert set_obj.size() == 1

        finally:
            Memory.unlink(shm_name)


class TestSetMagicMethods:
    """Test Python magic methods and operator support."""

    def test_len(self):
        """Test __len__ for len() support."""
        shm_name = f"/test_set_len_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "len_set", capacity=100, dtype=np.int32)

            assert len(set_obj) == 0

            for i in range(5):
                set_obj.insert(i)

            assert len(set_obj) == 5

        finally:
            Memory.unlink(shm_name)

    def test_contains_operator(self):
        """Test __contains__ for 'in' operator."""
        shm_name = f"/test_set_in_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "in_set", capacity=100, dtype=np.int32)

            set_obj.insert(42)
            set_obj.insert(100)

            # Test 'in' operator
            assert 42 in set_obj
            assert 100 in set_obj
            assert 99 not in set_obj

        finally:
            Memory.unlink(shm_name)

    def test_bool(self):
        """Test __bool__ for truth value testing."""
        shm_name = f"/test_set_bool_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "bool_set", capacity=100, dtype=np.int32)

            # Empty set is False
            assert bool(set_obj) == False

            # Non-empty set is True
            set_obj.insert(1)
            assert bool(set_obj) == True

            # After removing all elements
            set_obj.remove(1)
            assert bool(set_obj) == False

        finally:
            Memory.unlink(shm_name)

    def test_str_repr(self):
        """Test string representations."""
        shm_name = f"/test_set_str_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "str_set", capacity=100, dtype=np.int32)

            set_obj.insert(1)
            set_obj.insert(2)

            str_rep = str(set_obj)
            assert "str_set" in str_rep
            assert "size=2" in str_rep
            assert "int32" in str_rep

            repr_rep = repr(set_obj)
            assert repr_rep == str_rep

        finally:
            Memory.unlink(shm_name)


class TestSetDataTypes:
    """Test Set with different data types."""

    def test_float_elements(self):
        """Test set with float elements."""
        shm_name = f"/test_set_float_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "float_set", capacity=100, dtype=np.float32)

            set_obj.insert(3.14)
            set_obj.insert(2.718)
            set_obj.insert(1.618)

            assert 3.14 in set_obj
            assert 2.718 in set_obj
            assert 1.618 in set_obj
            assert 0.0 not in set_obj

            assert set_obj.size() == 3

        finally:
            Memory.unlink(shm_name)

    def test_int64_elements(self):
        """Test set with 64-bit integer elements."""
        shm_name = f"/test_set_int64_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "int64_set", capacity=100, dtype=np.int64)

            large_val = 10**15
            set_obj.insert(large_val)
            set_obj.insert(large_val + 1)
            set_obj.insert(large_val + 2)

            assert large_val in set_obj
            assert (large_val + 1) in set_obj
            assert (large_val + 2) in set_obj
            assert (large_val - 1) not in set_obj

        finally:
            Memory.unlink(shm_name)


class TestSetConcurrency:
    """Test concurrent access to Set."""

    def test_concurrent_inserts(self):
        """Test concurrent insert operations."""
        shm_name = f"/test_set_concurrent_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "concurrent_set", capacity=1000, dtype=np.int32)

            def worker(start_val, n_items):
                for i in range(n_items):
                    set_obj.insert(start_val + i)

            # Create threads
            threads = []
            n_threads = 5
            items_per_thread = 50

            for i in range(n_threads):
                t = threading.Thread(target=worker,
                                    args=(i * 100, items_per_thread))
                threads.append(t)
                t.start()

            for t in threads:
                t.join()

            # Verify all unique items were added
            expected_size = n_threads * items_per_thread
            assert set_obj.size() == expected_size

            # Verify specific elements
            for i in range(n_threads):
                for j in range(items_per_thread):
                    val = i * 100 + j
                    assert val in set_obj

        finally:
            Memory.unlink(shm_name)

    def test_concurrent_mixed_ops(self):
        """Test concurrent mixed operations."""
        shm_name = f"/test_set_mixed_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "mixed_set", capacity=500, dtype=np.int32)

            # Pre-populate
            for i in range(100):
                set_obj.insert(i)

            def inserter():
                for i in range(50):
                    set_obj.insert(100 + i)

            def remover():
                for i in range(20):
                    set_obj.remove(i)

            def checker():
                for _ in range(100):
                    _ = 50 in set_obj

            # Mix of operations
            threads = []
            threads.append(threading.Thread(target=inserter))
            threads.append(threading.Thread(target=remover))
            threads.extend([threading.Thread(target=checker) for _ in range(3)])

            for t in threads:
                t.start()

            for t in threads:
                t.join()

            # Basic sanity check
            assert set_obj.size() >= 100  # At least 100 + 50 - 20 - overlap

        finally:
            Memory.unlink(shm_name)


class TestSetEdgeCases:
    """Test edge cases and error conditions."""

    def test_empty_set(self):
        """Test operations on empty set."""
        shm_name = f"/test_set_empty_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "empty_set", capacity=100, dtype=np.int32)

            assert set_obj.size() == 0
            assert len(set_obj) == 0
            assert set_obj.empty() == True
            assert bool(set_obj) == False

            assert 0 not in set_obj
            assert set_obj.erase(0) == False
            assert set_obj.remove(0) == False

            # Discard on empty set
            set_obj.discard(0)  # Should not raise

        finally:
            Memory.unlink(shm_name)

    def test_missing_dtype_error(self):
        """Test that missing dtype raises error."""
        shm_name = f"/test_set_dtype_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            with pytest.raises(TypeError, match="dtype is required"):
                Set(memory, "bad_set", capacity=10)

        finally:
            Memory.unlink(shm_name)

    def test_clear_not_implemented(self):
        """Test that clear() is not implemented."""
        shm_name = f"/test_set_clear_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            set_obj = Set(memory, "clear_set", capacity=100, dtype=np.int32)

            set_obj.insert(10)
            set_obj.insert(20)

            with pytest.raises(NotImplementedError):
                set_obj.clear()

        finally:
            Memory.unlink(shm_name)

    def test_capacity_limit(self):
        """Test behavior at capacity limit."""
        shm_name = f"/test_set_capacity_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            capacity = 5
            set_obj = Set(memory, "capacity_set", capacity=capacity, dtype=np.int32)

            # Fill to capacity
            for i in range(capacity):
                assert set_obj.insert(i) == True

            # Try to add beyond capacity
            assert set_obj.insert(capacity) == False
            assert set_obj.size() == capacity

            # Remove one and add again
            set_obj.remove(0)
            assert set_obj.insert(capacity) == True

        finally:
            Memory.unlink(shm_name)


class TestSetPersistence:
    """Test set persistence across processes."""

    def test_reopen_set(self):
        """Test reopening an existing set."""
        shm_name = f"/test_set_persist_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create and populate
            set1 = Set(memory, "persist_set", capacity=50, dtype=np.int32)
            set1.insert(10)
            set1.insert(20)
            set1.insert(30)

            # Open existing (should work through Map)
            set2 = Set(memory, "persist_set", dtype=np.int32)

            # Should see same data
            assert 10 in set2
            assert 20 in set2
            assert 30 in set2
            assert set2.size() == 3

        finally:
            Memory.unlink(shm_name)