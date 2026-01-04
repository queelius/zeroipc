"""Tests for Signal implementation."""

import os
import time
import threading
import pytest
import numpy as np

from zeroipc import Memory, Signal


class TestSignalBasic:
    """Basic Signal functionality tests."""

    def test_create_signal(self):
        """Test creating a new signal."""
        shm_name = f"/test_signal_create_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test_signal", dtype=np.int32, initial_value=0)

            assert signal.name == "test_signal"
            assert signal.version == 0  # Version starts at 0

        finally:
            Memory.unlink(shm_name)

    def test_open_existing_signal(self):
        """Test opening an existing signal."""
        shm_name = f"/test_signal_open_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)

            # Create signal
            signal1 = Signal(memory, "existing", dtype=np.float32, initial_value=0)
            signal1.set(3.14)
            assert signal1.version == 1  # First set() increments to 1

            # Open existing signal
            signal2 = Signal(memory, "existing", dtype=np.float32, create_if_missing=False)
            assert signal2.name == "existing"
            assert abs(signal2.get() - 3.14) < 0.001
            assert signal2.version == 1

        finally:
            Memory.unlink(shm_name)

    def test_get_set(self):
        """Test basic get and set operations."""
        shm_name = f"/test_signal_getset_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)

            # Initial value is undefined, set a value
            signal.set(42)
            assert signal.get() == 42
            assert signal.version == 1

            # Set another value
            signal.set(99)
            assert signal.get() == 99
            assert signal.version == 2

        finally:
            Memory.unlink(shm_name)

    def test_version_tracking(self):
        """Test version increments on changes."""
        shm_name = f"/test_signal_version_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)

            assert signal.version == 0

            signal.set(1)
            assert signal.version == 1

            signal.set(2)
            assert signal.version == 2

            signal.set(3)
            assert signal.version == 3

        finally:
            Memory.unlink(shm_name)

    def test_update_function(self):
        """Test update with function."""
        shm_name = f"/test_signal_update_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)

            signal.set(10)
            assert signal.get() == 10
            assert signal.version == 1

            # Update with function
            signal.update(lambda x: x * 2)
            assert signal.get() == 20
            assert signal.version == 2

            signal.update(lambda x: x + 5)
            assert signal.get() == 25
            assert signal.version == 3

        finally:
            Memory.unlink(shm_name)

    def test_has_changed(self):
        """Test has_changed detection."""
        shm_name = f"/test_signal_changed_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)

            v0 = signal.version
            assert not signal.has_changed(v0)

            signal.set(42)
            v1 = signal.version
            assert signal.has_changed(v0)
            assert not signal.has_changed(v1)

            signal.set(99)
            v2 = signal.version
            assert signal.has_changed(v1)
            assert not signal.has_changed(v2)

        finally:
            Memory.unlink(shm_name)

    def test_repr(self):
        """Test string representation."""
        shm_name = f"/test_signal_repr_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test_signal", dtype=np.int32, initial_value=0)

            repr_str = repr(signal)
            assert "Signal" in repr_str
            assert "test_signal" in repr_str
            assert "int32" in repr_str
            assert "version=0" in repr_str

        finally:
            Memory.unlink(shm_name)


class TestSignalThreading:
    """Multi-threaded Signal tests."""

    def test_concurrent_readers(self):
        """Test multiple threads reading signal."""
        shm_name = f"/test_signal_readers_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)
            signal.set(42)
            results = []

            def reader(tid):
                value = signal.get()
                results.append((tid, value))

            threads = [threading.Thread(target=reader, args=(i,)) for i in range(10)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            assert len(results) == 10
            # All should read the same value
            assert all(v == 42 for _, v in results)

        finally:
            Memory.unlink(shm_name)

    def test_concurrent_writers(self):
        """Test multiple threads writing signal."""
        shm_name = f"/test_signal_writers_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)
            signal.set(0)

            def writer(value):
                signal.set(value)
                time.sleep(0.001)

            threads = [threading.Thread(target=writer, args=(i,)) for i in range(10)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            # Version should be 11 (initial set + 10 writes)
            assert signal.version == 11

        finally:
            Memory.unlink(shm_name)

    def test_wait_for_change(self):
        """Test waiting for signal change."""
        shm_name = f"/test_signal_wait_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)
            signal.set(0)

            v0 = signal.version

            def changer():
                time.sleep(0.05)
                signal.set(42)

            t = threading.Thread(target=changer)
            t.start()

            # Wait for change
            start = time.time()
            result = signal.wait_for_change(v0, timeout=1.0)
            elapsed = time.time() - start

            t.join()

            assert result == True
            assert signal.get() == 42
            assert elapsed < 0.2  # Should not wait full timeout

        finally:
            Memory.unlink(shm_name)

    def test_wait_for_change_timeout(self):
        """Test wait_for_change with timeout."""
        shm_name = f"/test_signal_wait_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)
            signal.set(0)

            v0 = signal.version

            # No one changes the signal - should timeout
            start = time.time()
            result = signal.wait_for_change(v0, timeout=0.1)
            elapsed = time.time() - start

            assert result == False
            assert elapsed >= 0.09  # Should wait close to timeout

        finally:
            Memory.unlink(shm_name)

    def test_observer_pattern(self):
        """Test observer pattern with signals."""
        shm_name = f"/test_signal_observer_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)
            signal.set(0)

            observations = []

            def observer():
                version = signal.version
                for _ in range(5):
                    if signal.wait_for_change(version, timeout=0.5):
                        value = signal.get()
                        version = signal.version
                        observations.append(value)

            def producer():
                for i in range(1, 6):
                    time.sleep(0.02)
                    signal.set(i)

            obs_thread = threading.Thread(target=observer)
            prod_thread = threading.Thread(target=producer)

            obs_thread.start()
            prod_thread.start()

            obs_thread.join()
            prod_thread.join()

            # Observer should have seen all 5 changes
            assert len(observations) == 5
            assert observations == [1, 2, 3, 4, 5]

        finally:
            Memory.unlink(shm_name)


class TestSignalTypes:
    """Test Signal with different data types."""

    def test_float_signal(self):
        """Test signal with float32."""
        shm_name = f"/test_signal_float_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.float32, initial_value=0)

            signal.set(3.14159)
            assert abs(signal.get() - 3.14159) < 0.0001

            signal.update(lambda x: x * 2)
            assert abs(signal.get() - 6.28318) < 0.0001

        finally:
            Memory.unlink(shm_name)

    def test_float64_signal(self):
        """Test signal with float64."""
        shm_name = f"/test_signal_float64_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.float64, initial_value=0)

            signal.set(2.718281828459045)
            assert abs(signal.get() - 2.718281828459045) < 1e-10

        finally:
            Memory.unlink(shm_name)

    def test_int64_signal(self):
        """Test signal with int64."""
        shm_name = f"/test_signal_int64_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int64, initial_value=0)

            large_num = 9223372036854775807  # Max int64
            signal.set(large_num)
            assert signal.get() == large_num

        finally:
            Memory.unlink(shm_name)

    def test_uint32_signal(self):
        """Test signal with uint32."""
        shm_name = f"/test_signal_uint32_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.uint32, initial_value=0)

            signal.set(4294967295)  # Max uint32
            assert signal.get() == 4294967295

        finally:
            Memory.unlink(shm_name)


class TestSignalEdgeCases:
    """Edge case tests for Signal."""

    def test_rapid_updates(self):
        """Test rapid consecutive updates."""
        shm_name = f"/test_signal_rapid_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)

            for i in range(100):
                signal.set(i)

            assert signal.get() == 99
            assert signal.version == 100

        finally:
            Memory.unlink(shm_name)

    def test_same_value_increments_version(self):
        """Test that setting same value still increments version."""
        shm_name = f"/test_signal_same_value_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)

            signal.set(42)
            assert signal.version == 1

            # Set same value again
            signal.set(42)
            assert signal.version == 2  # Version should still increment

            signal.set(42)
            assert signal.version == 3

        finally:
            Memory.unlink(shm_name)

    def test_update_with_identity(self):
        """Test update with identity function."""
        shm_name = f"/test_signal_identity_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)

            signal.set(42)
            v1 = signal.version

            signal.update(lambda x: x)  # Identity function
            v2 = signal.version

            assert signal.get() == 42
            assert v2 == v1 + 1  # Version increments even for identity

        finally:
            Memory.unlink(shm_name)

    def test_multiple_observers(self):
        """Test multiple threads observing same signal."""
        shm_name = f"/test_signal_multi_obs_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)
            signal.set(0)

            observations = [[] for _ in range(3)]

            def observer(obs_id):
                version = signal.version
                for _ in range(5):
                    if signal.wait_for_change(version, timeout=0.5):
                        value = signal.get()
                        version = signal.version
                        observations[obs_id].append(value)

            def producer():
                for i in range(1, 6):
                    time.sleep(0.02)
                    signal.set(i)

            obs_threads = [threading.Thread(target=observer, args=(i,)) for i in range(3)]
            prod_thread = threading.Thread(target=producer)

            for t in obs_threads:
                t.start()
            prod_thread.start()

            for t in obs_threads:
                t.join()
            prod_thread.join()

            # All observers should see all changes
            for obs in observations:
                assert len(obs) == 5
                assert obs == [1, 2, 3, 4, 5]

        finally:
            Memory.unlink(shm_name)

    def test_read_modify_write_race(self):
        """Test that update provides atomic read-modify-write."""
        shm_name = f"/test_signal_rmw_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)
            signal.set(0)

            def increment():
                for _ in range(100):
                    signal.update(lambda x: x + 1)

            threads = [threading.Thread(target=increment) for _ in range(4)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()

            # With atomic update, should be exactly 400
            assert signal.get() == 400
            assert signal.version == 401  # Initial set + 400 updates

        finally:
            Memory.unlink(shm_name)

    def test_zero_version_detection(self):
        """Test detecting changes from initial zero version."""
        shm_name = f"/test_signal_zero_version_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=1024*1024)
            signal = Signal(memory, "test", dtype=np.int32, initial_value=0)

            # Initial version is 0
            assert signal.version == 0
            assert not signal.has_changed(0)

            # First set
            signal.set(42)
            assert signal.version == 1
            assert signal.has_changed(0)

        finally:
            Memory.unlink(shm_name)
