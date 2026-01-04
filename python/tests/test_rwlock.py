"""Tests for RWLock implementation."""

import os
import time
import threading
import pytest
import numpy as np

from zeroipc import Memory, RWLock, Array


class TestRWLockBasic:
    """Basic RWLock functionality tests."""

    def test_create_rwlock(self):
        """Test creating a new RWLock."""
        shm_name = f"/test_rwlock_create_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test_rwlock")

            assert rwlock.name == "test_rwlock"
            assert rwlock.readers == 0
            assert rwlock.writer_active == False

        finally:
            Memory.unlink(shm_name)

    def test_open_existing_rwlock(self):
        """Test opening an existing RWLock."""
        shm_name = f"/test_rwlock_open_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            # Create rwlock
            rwlock1 = RWLock(memory, "existing")
            assert rwlock1.readers == 0

            # Open existing rwlock
            rwlock2 = RWLock(memory, "existing", create_if_missing=False)
            assert rwlock2.name == "existing"

        finally:
            Memory.unlink(shm_name)

    def test_reader_lock_unlock(self):
        """Test basic reader lock and unlock operations."""
        shm_name = f"/test_rwlock_reader_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")

            # Lock for reading
            rwlock.reader_lock()
            assert rwlock.readers == 1

            # Unlock
            rwlock.reader_unlock()
            assert rwlock.readers == 0

        finally:
            Memory.unlink(shm_name)

    def test_writer_lock_unlock(self):
        """Test basic writer lock and unlock operations."""
        shm_name = f"/test_rwlock_writer_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")

            # Lock for writing
            assert rwlock.writer_lock() == True
            assert rwlock.writer_active == True

            # Unlock
            rwlock.writer_unlock()
            assert rwlock.writer_active == False

        finally:
            Memory.unlink(shm_name)

    def test_reader_context_manager(self):
        """Test reader context manager."""
        shm_name = f"/test_rwlock_reader_ctx_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")

            assert rwlock.readers == 0

            with rwlock.reader():
                assert rwlock.readers == 1

            assert rwlock.readers == 0

        finally:
            Memory.unlink(shm_name)

    def test_writer_context_manager(self):
        """Test writer context manager."""
        shm_name = f"/test_rwlock_writer_ctx_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")

            assert not rwlock.writer_active

            with rwlock.writer():
                assert rwlock.writer_active

            assert not rwlock.writer_active

        finally:
            Memory.unlink(shm_name)

    def test_repr(self):
        """Test string representation."""
        shm_name = f"/test_rwlock_repr_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test_rwlock")

            repr_str = repr(rwlock)
            assert "RWLock" in repr_str
            assert "test_rwlock" in repr_str
            assert "readers=0" in repr_str
            assert "writer_active=no" in repr_str

        finally:
            Memory.unlink(shm_name)


class TestRWLockConcurrency:
    """Concurrency tests for RWLock."""

    def test_multiple_readers_concurrent(self):
        """Test that multiple readers can hold lock simultaneously."""
        shm_name = f"/test_rwlock_multi_readers_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")
            data = Array(memory, "data", dtype=np.int32, capacity=1)
            data[0] = 42
            results = []

            def reader(tid):
                with rwlock.reader():
                    # All readers should be able to access simultaneously
                    results.append((tid, data[0], rwlock.readers))
                    time.sleep(0.01)  # Hold lock briefly

            threads = [threading.Thread(target=reader, args=(i,)) for i in range(5)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            # All readers should have read the value
            assert len(results) == 5
            # At some point, multiple readers should have been active
            max_readers = max(r[2] for r in results)
            assert max_readers > 1

        finally:
            Memory.unlink(shm_name)

    def test_writer_exclusivity(self):
        """Test that only one writer can hold lock at a time."""
        shm_name = f"/test_rwlock_writer_excl_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")
            counter = Array(memory, "counter", dtype=np.int32, capacity=1)
            counter[0] = 0

            def writer():
                with rwlock.writer():
                    val = counter[0]
                    time.sleep(0.001)  # Force context switch
                    counter[0] = val + 1

            threads = [threading.Thread(target=writer) for _ in range(10)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            # With proper exclusion, should be exactly 10
            assert counter[0] == 10

        finally:
            Memory.unlink(shm_name)

    def test_readers_block_writer(self):
        """Test that active readers block writer."""
        shm_name = f"/test_rwlock_readers_block_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")
            results = []

            def reader():
                rwlock.reader_lock()
                results.append("reader_locked")
                time.sleep(0.05)
                results.append("reader_unlocking")
                rwlock.reader_unlock()

            def writer():
                time.sleep(0.01)  # Let reader start
                results.append("writer_waiting")
                acquired = rwlock.writer_lock(timeout=0.2)
                if acquired:
                    results.append("writer_locked")
                    rwlock.writer_unlock()
                else:
                    results.append("writer_timeout")

            reader_thread = threading.Thread(target=reader)
            writer_thread = threading.Thread(target=writer)

            reader_thread.start()
            writer_thread.start()

            reader_thread.join()
            writer_thread.join()

            # Writer should have been blocked by reader
            assert "reader_locked" in results
            assert "writer_waiting" in results
            # Writer should eventually acquire after reader releases
            assert "writer_locked" in results

        finally:
            Memory.unlink(shm_name)

    def test_writer_blocks_readers(self):
        """Test that active writer blocks readers."""
        shm_name = f"/test_rwlock_writer_blocks_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")
            results = []

            def writer():
                rwlock.writer_lock()
                results.append("writer_locked")
                time.sleep(0.05)
                results.append("writer_unlocking")
                rwlock.writer_unlock()

            def reader():
                time.sleep(0.01)  # Let writer start
                results.append("reader_waiting")
                rwlock.reader_lock()
                results.append("reader_locked")
                rwlock.reader_unlock()

            writer_thread = threading.Thread(target=writer)
            reader_thread = threading.Thread(target=reader)

            writer_thread.start()
            reader_thread.start()

            writer_thread.join()
            reader_thread.join()

            # Reader should have been blocked by writer
            assert "writer_locked" in results
            assert "reader_waiting" in results
            assert results.index("writer_unlocking") < results.index("reader_locked")

        finally:
            Memory.unlink(shm_name)

    def test_writer_timeout(self):
        """Test writer lock timeout."""
        shm_name = f"/test_rwlock_writer_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")

            # Hold reader lock
            rwlock.reader_lock()

            result = []

            def try_writer():
                acquired = rwlock.writer_lock(timeout=0.1)
                result.append(acquired)

            t = threading.Thread(target=try_writer)
            t.start()
            t.join()

            # Should timeout because reader is active
            assert result[0] == False

            rwlock.reader_unlock()

        finally:
            Memory.unlink(shm_name)


class TestRWLockPatterns:
    """Common patterns with RWLock."""

    def test_shared_read_exclusive_write(self):
        """Test classic shared-read exclusive-write pattern."""
        shm_name = f"/test_rwlock_pattern_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")
            data = Array(memory, "data", dtype=np.int32, capacity=100)
            data[:] = 0

            read_count = [0]
            write_count = [0]

            def reader():
                for _ in range(10):
                    with rwlock.reader():
                        # Multiple readers can read simultaneously
                        _ = np.sum(data[:])
                        read_count[0] += 1
                    time.sleep(0.001)

            def writer():
                for i in range(5):
                    with rwlock.writer():
                        # Exclusive write
                        data[:] = i
                        write_count[0] += 1
                    time.sleep(0.002)

            readers = [threading.Thread(target=reader) for _ in range(3)]
            writers = [threading.Thread(target=writer) for _ in range(2)]

            for t in readers + writers:
                t.start()
            for t in readers + writers:
                t.join()

            assert read_count[0] == 30  # 3 readers × 10 reads
            assert write_count[0] == 10  # 2 writers × 5 writes

        finally:
            Memory.unlink(shm_name)

    def test_read_modify_write(self):
        """Test read-modify-write pattern."""
        shm_name = f"/test_rwlock_rmw_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")
            counter = Array(memory, "counter", dtype=np.int32, capacity=1)
            counter[0] = 0

            def increment():
                # Read phase (shared)
                with rwlock.reader():
                    current = counter[0]

                # Compute new value
                new_val = current + 1

                # Write phase (exclusive)
                with rwlock.writer():
                    counter[0] = new_val

            threads = [threading.Thread(target=increment) for _ in range(20)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            # This pattern has races - not all increments may succeed
            # But final value should be reasonable (not corrupted)
            assert counter[0] > 0 and counter[0] <= 20

        finally:
            Memory.unlink(shm_name)


class TestRWLockEdgeCases:
    """Edge case tests for RWLock."""

    def test_nested_reader_locks(self):
        """Test nested reader locks from same thread."""
        shm_name = f"/test_rwlock_nested_reader_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")

            with rwlock.reader():
                assert rwlock.readers == 1
                with rwlock.reader():
                    # Should increment reader count
                    assert rwlock.readers == 2
                assert rwlock.readers == 1

            assert rwlock.readers == 0

        finally:
            Memory.unlink(shm_name)

    def test_writer_deadlock_on_reentry(self):
        """Test that writer cannot reenter (would deadlock)."""
        shm_name = f"/test_rwlock_writer_reentry_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")

            rwlock.writer_lock()

            # Trying to acquire writer lock again should timeout
            acquired = rwlock.writer_lock(timeout=0.1)
            assert acquired == False

            rwlock.writer_unlock()

        finally:
            Memory.unlink(shm_name)

    def test_many_readers(self):
        """Test with many concurrent readers."""
        shm_name = f"/test_rwlock_many_readers_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")
            results = []

            def reader(tid):
                with rwlock.reader():
                    results.append(tid)
                    time.sleep(0.001)

            threads = [threading.Thread(target=reader, args=(i,)) for i in range(50)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            assert len(results) == 50
            assert set(results) == set(range(50))

        finally:
            Memory.unlink(shm_name)

    def test_alternating_readers_writers(self):
        """Test alternating between readers and writers."""
        shm_name = f"/test_rwlock_alternating_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            rwlock = RWLock(memory, "test")
            data = Array(memory, "data", dtype=np.int32, capacity=1)
            data[0] = 0
            operations = []

            def reader():
                with rwlock.reader():
                    val = data[0]
                    operations.append(("read", val))
                    time.sleep(0.005)

            def writer(value):
                with rwlock.writer():
                    data[0] = value
                    operations.append(("write", value))
                    time.sleep(0.005)

            threads = []
            for i in range(10):
                threads.append(threading.Thread(target=writer, args=(i,)))
                threads.append(threading.Thread(target=reader))

            for t in threads:
                t.start()
            for t in threads:
                t.join()

            # Should have both reads and writes
            reads = [op for op in operations if op[0] == "read"]
            writes = [op for op in operations if op[0] == "write"]
            assert len(reads) > 0
            assert len(writes) == 10

        finally:
            Memory.unlink(shm_name)
