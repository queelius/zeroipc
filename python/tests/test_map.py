"""
Test suite for lock-free hash map in shared memory.
"""

import os
import threading
import multiprocessing as mp
import pytest
import numpy as np

from zeroipc import Memory
from zeroipc.map import Map


class TestMapBasic:
    """Basic Map functionality tests."""

    def test_create_map(self):
        """Test creating a new map."""
        shm_name = f"/test_map_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create map with integer key/value
            map_obj = Map(memory, "int_map", capacity=100,
                         key_dtype=np.int32, value_dtype=np.int32)

            assert map_obj.capacity == 100
            assert map_obj.name == "int_map"

        finally:
            Memory.unlink(shm_name)

    def test_put_and_get(self):
        """Test basic put and get operations."""
        shm_name = f"/test_map_ops_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            map_obj = Map(memory, "test_map", capacity=100,
                         key_dtype=np.int32, value_dtype=np.int32)

            # Put some values
            assert map_obj.put(10, 100) == True
            assert map_obj.put(20, 200) == True
            assert map_obj.put(30, 300) == True

            # Get values
            assert map_obj.get(10) == 100
            assert map_obj.get(20) == 200
            assert map_obj.get(30) == 300

            # Non-existent key
            assert map_obj.get(40) is None

        finally:
            Memory.unlink(shm_name)

    def test_update_value(self):
        """Test updating existing values."""
        shm_name = f"/test_map_update_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            map_obj = Map(memory, "update_map", capacity=50,
                         key_dtype=np.int32, value_dtype=np.int32)

            # Initial value
            map_obj.put(42, 100)
            assert map_obj.get(42) == 100

            # Update value
            map_obj.put(42, 200)
            assert map_obj.get(42) == 200

            # Another update
            map_obj.put(42, 999)
            assert map_obj.get(42) == 999

        finally:
            Memory.unlink(shm_name)

    def test_remove(self):
        """Test removing entries."""
        shm_name = f"/test_map_remove_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            map_obj = Map(memory, "remove_map", capacity=50,
                         key_dtype=np.int32, value_dtype=np.int32)

            # Add and remove
            map_obj.put(10, 100)
            map_obj.put(20, 200)
            assert map_obj.get(10) == 100
            assert map_obj.get(20) == 200

            # Remove one
            assert map_obj.remove(10) == True
            assert map_obj.get(10) is None
            assert map_obj.get(20) == 200

            # Remove non-existent
            assert map_obj.remove(30) == False

            # Remove another
            assert map_obj.remove(20) == True
            assert map_obj.get(20) is None

        finally:
            Memory.unlink(shm_name)

    def test_contains(self):
        """Test contains/membership check."""
        shm_name = f"/test_map_contains_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            map_obj = Map(memory, "contains_map", capacity=50,
                         key_dtype=np.int32, value_dtype=np.int32)

            map_obj.put(10, 100)
            map_obj.put(20, 200)

            assert map_obj.contains(10) == True
            assert map_obj.contains(20) == True
            assert map_obj.contains(30) == False

            # After removal
            map_obj.remove(10)
            assert map_obj.contains(10) == False

        finally:
            Memory.unlink(shm_name)

    def test_size(self):
        """Test size tracking."""
        shm_name = f"/test_map_size_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            map_obj = Map(memory, "size_map", capacity=50,
                         key_dtype=np.int32, value_dtype=np.int32)

            assert map_obj.size() == 0

            map_obj.put(10, 100)
            assert map_obj.size() == 1

            map_obj.put(20, 200)
            map_obj.put(30, 300)
            assert map_obj.size() == 3

            map_obj.remove(20)
            assert map_obj.size() == 2

        finally:
            Memory.unlink(shm_name)

    def test_clear(self):
        """Test clearing the map."""
        shm_name = f"/test_map_clear_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            map_obj = Map(memory, "clear_map", capacity=50,
                         key_dtype=np.int32, value_dtype=np.int32)

            # Add some values
            for i in range(10):
                map_obj.put(i, i * 10)
            assert map_obj.size() == 10

            # Clear
            map_obj.clear()
            assert map_obj.size() == 0

            # Verify all gone
            for i in range(10):
                assert map_obj.get(i) is None

        finally:
            Memory.unlink(shm_name)


class TestMapDataTypes:
    """Test Map with different data types."""

    def test_float_values(self):
        """Test map with float values."""
        shm_name = f"/test_map_float_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            map_obj = Map(memory, "float_map", capacity=50,
                         key_dtype=np.int32, value_dtype=np.float32)

            map_obj.put(1, 3.14)
            map_obj.put(2, 2.718)
            map_obj.put(3, 1.618)

            assert abs(map_obj.get(1) - 3.14) < 0.001
            assert abs(map_obj.get(2) - 2.718) < 0.001
            assert abs(map_obj.get(3) - 1.618) < 0.001

        finally:
            Memory.unlink(shm_name)

    def test_int64_keys(self):
        """Test map with 64-bit integer keys."""
        shm_name = f"/test_map_int64_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            map_obj = Map(memory, "int64_map", capacity=50,
                         key_dtype=np.int64, value_dtype=np.int32)

            large_key = 10**15
            map_obj.put(large_key, 42)
            map_obj.put(large_key + 1, 43)

            assert map_obj.get(large_key) == 42
            assert map_obj.get(large_key + 1) == 43

        finally:
            Memory.unlink(shm_name)

    def test_double_values(self):
        """Test map with double precision values."""
        shm_name = f"/test_map_double_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            map_obj = Map(memory, "double_map", capacity=50,
                         key_dtype=np.int32, value_dtype=np.float64)

            pi = 3.141592653589793
            e = 2.718281828459045

            map_obj.put(1, pi)
            map_obj.put(2, e)

            assert map_obj.get(1) == pi
            assert map_obj.get(2) == e

        finally:
            Memory.unlink(shm_name)


class TestMapCollisions:
    """Test hash collision handling."""

    def test_linear_probing(self):
        """Test that linear probing handles collisions."""
        shm_name = f"/test_map_collision_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            # Small capacity to force collisions
            map_obj = Map(memory, "collision_map", capacity=10,
                         key_dtype=np.int32, value_dtype=np.int32)

            # Add values that might collide
            keys = [i * 10 for i in range(8)]  # 0, 10, 20, 30, ...
            for key in keys:
                map_obj.put(key, key * 100)

            # Verify all values are stored correctly
            for key in keys:
                assert map_obj.get(key) == key * 100

        finally:
            Memory.unlink(shm_name)

    def test_capacity_limit(self):
        """Test behavior at capacity limit."""
        shm_name = f"/test_map_capacity_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            capacity = 5
            map_obj = Map(memory, "capacity_map", capacity=capacity,
                         key_dtype=np.int32, value_dtype=np.int32)

            # Fill to capacity
            for i in range(capacity):
                assert map_obj.put(i, i * 10) == True

            # Try to add beyond capacity (should fail)
            assert map_obj.put(capacity, capacity * 10) == False

            # Remove one and add again
            map_obj.remove(0)
            assert map_obj.put(capacity, capacity * 10) == True

        finally:
            Memory.unlink(shm_name)


class TestMapConcurrency:
    """Test concurrent access to Map."""

    def test_concurrent_puts(self):
        """Test concurrent put operations."""
        shm_name = f"/test_map_concurrent_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            map_obj = Map(memory, "concurrent_map", capacity=1000,
                         key_dtype=np.int32, value_dtype=np.int32)

            def worker(start_key, n_items):
                for i in range(n_items):
                    key = start_key + i
                    map_obj.put(key, key * 10)

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

            # Verify all items
            expected_size = n_threads * items_per_thread
            assert map_obj.size() == expected_size

            for i in range(n_threads):
                for j in range(items_per_thread):
                    key = i * 100 + j
                    assert map_obj.get(key) == key * 10

        finally:
            Memory.unlink(shm_name)

    def test_concurrent_mixed_ops(self):
        """Test concurrent mixed operations."""
        shm_name = f"/test_map_mixed_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            map_obj = Map(memory, "mixed_map", capacity=500,
                         key_dtype=np.int32, value_dtype=np.int32)

            # Pre-populate
            for i in range(100):
                map_obj.put(i, i * 10)

            def reader():
                for _ in range(100):
                    key = os.getpid() % 100
                    _ = map_obj.get(key)

            def writer():
                for i in range(50):
                    key = 100 + i
                    map_obj.put(key, key * 10)

            def remover():
                for i in range(20):
                    map_obj.remove(i)

            # Mix of operations
            threads = []
            threads.extend([threading.Thread(target=reader) for _ in range(3)])
            threads.extend([threading.Thread(target=writer) for _ in range(2)])
            threads.append(threading.Thread(target=remover))

            for t in threads:
                t.start()

            for t in threads:
                t.join()

            # Basic sanity check
            assert map_obj.size() >= 80  # At least 100 + 50 - 20 - some overlap

        finally:
            Memory.unlink(shm_name)


class TestMapPersistence:
    """Test map persistence across processes."""

    def test_reopen_map(self):
        """Test reopening an existing map."""
        shm_name = f"/test_map_persist_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create and populate
            map1 = Map(memory, "persist_map", capacity=50,
                      key_dtype=np.int32, value_dtype=np.int32)
            map1.put(10, 100)
            map1.put(20, 200)

            # Open existing
            map2 = Map(memory, "persist_map",
                      key_dtype=np.int32, value_dtype=np.int32)

            # Should see same data
            assert map2.get(10) == 100
            assert map2.get(20) == 200
            assert map2.size() == 2

        finally:
            Memory.unlink(shm_name)


class TestMapEdgeCases:
    """Test edge cases and error conditions."""

    def test_empty_map(self):
        """Test operations on empty map."""
        shm_name = f"/test_map_empty_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            map_obj = Map(memory, "empty_map", capacity=50,
                         key_dtype=np.int32, value_dtype=np.int32)

            assert map_obj.size() == 0
            assert map_obj.get(0) is None
            assert map_obj.remove(0) == False
            assert map_obj.contains(0) == False

            # Clear empty map
            map_obj.clear()
            assert map_obj.size() == 0

        finally:
            Memory.unlink(shm_name)

    def test_zero_capacity_error(self):
        """Test that zero capacity raises error."""
        shm_name = f"/test_map_zero_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            with pytest.raises(ValueError, match="capacity must be greater than 0"):
                Map(memory, "zero_map", capacity=0,
                   key_dtype=np.int32, value_dtype=np.int32)

        finally:
            Memory.unlink(shm_name)

    def test_missing_dtype_error(self):
        """Test that missing data types raise error."""
        shm_name = f"/test_map_dtype_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Missing value_dtype
            with pytest.raises(TypeError):
                Map(memory, "bad_map", capacity=10, key_dtype=np.int32)

            # Missing key_dtype
            with pytest.raises(TypeError):
                Map(memory, "bad_map", capacity=10, value_dtype=np.int32)

            # Missing both
            with pytest.raises(TypeError):
                Map(memory, "bad_map", capacity=10)

        finally:
            Memory.unlink(shm_name)

    def test_name_too_long(self):
        """Test that long names raise error."""
        shm_name = f"/test_map_name_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            long_name = "x" * 32  # 32 chars, too long
            with pytest.raises(ValueError, match="Name too long"):
                Map(memory, long_name, capacity=10,
                   key_dtype=np.int32, value_dtype=np.int32)

        finally:
            Memory.unlink(shm_name)