"""
Test suite for buffered Channel operations only.
This avoids the problematic unbuffered channel synchronization logic.
"""

import os
import threading
import time
import pytest
import numpy as np

from zeroipc import Memory
from zeroipc.channel import Channel, ChannelClosed, Select, make_channel, make_buffered_channel


class TestBufferedChannelBasics:
    """Test basic buffered channel functionality."""

    def test_create_buffered_channel(self):
        """Test creating a buffered channel."""
        shm_name = f"/test_buffered_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Test capacity parameter
            ch1 = Channel(memory, "test1", capacity=10, dtype=np.int32)
            assert ch1.name == "test1"
            assert ch1.capacity == 10
            assert ch1.buffer_size == 10
            assert not ch1.unbuffered
            assert ch1.is_open()
            assert not ch1.is_closed()

            # Test buffer_size parameter
            ch2 = Channel(memory, "test2", buffer_size=5, dtype=np.float32)
            assert ch2.name == "test2"
            assert ch2.capacity == 5
            assert ch2.buffer_size == 5
            assert not ch2.unbuffered

        finally:
            Memory.unlink(shm_name)

    def test_send_receive_operations(self):
        """Test basic send and receive operations."""
        shm_name = f"/test_ops_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "ops", capacity=10, dtype=np.int32)

            # Send values
            assert channel.send(10) == True
            assert channel.send(20) == True
            assert channel.send(30) == True

            # Check state
            assert channel.size() == 3
            assert len(channel) == 3
            assert not channel.empty()
            assert not channel.full()

            # Receive values
            assert channel.receive() == 10
            assert channel.receive() == 20
            assert channel.receive() == 30

            # Empty now
            assert channel.empty()
            assert channel.size() == 0

        finally:
            Memory.unlink(shm_name)

    def test_try_send_receive(self):
        """Test non-blocking operations."""
        shm_name = f"/test_try_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "try", capacity=3, dtype=np.int32)

            # Fill buffer
            assert channel.try_send(10) == True
            assert channel.try_send(20) == True
            assert channel.try_send(30) == True

            # Buffer full
            assert channel.full()
            assert channel.try_send(40) == False

            # Empty buffer
            assert channel.try_receive() == 10
            assert channel.try_receive() == 20
            assert channel.try_receive() == 30

            # Buffer empty
            assert channel.empty()
            assert channel.try_receive() is None

        finally:
            Memory.unlink(shm_name)

    def test_channel_closing(self):
        """Test channel closing behavior."""
        shm_name = f"/test_close_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "close", capacity=5, dtype=np.int32)

            # Send data
            channel.send(10)
            channel.send(20)

            # Close channel
            channel.close()
            assert channel.is_closed()
            assert not channel.is_open()

            # Can receive existing data
            assert channel.receive() == 10
            assert channel.receive() == 20

            # Further receives return None
            assert channel.receive() is None

            # Cannot send to closed channel (returns False)
            assert channel.send(30) == False
            assert channel.try_send(30) == False

        finally:
            Memory.unlink(shm_name)

    def test_timeouts(self):
        """Test timeout operations."""
        shm_name = f"/test_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "timeout", capacity=2, dtype=np.int32)

            # Fill buffer
            channel.send(1)
            channel.send(2)

            # Send with timeout should fail
            start_time = time.time()
            result = channel.send(3, timeout=0.1)
            elapsed = time.time() - start_time

            assert result == False
            assert 0.1 <= elapsed < 0.2

            # Empty buffer
            channel.receive()
            channel.receive()

            # Receive with timeout should fail
            start_time = time.time()
            result = channel.receive(timeout=0.1)
            elapsed = time.time() - start_time

            assert result is None
            assert 0.1 <= elapsed < 0.2

        finally:
            Memory.unlink(shm_name)


class TestDataTypes:
    """Test different data types."""

    def test_float_channel(self):
        """Test float data type."""
        shm_name = f"/test_float_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "float", capacity=5, dtype=np.float32)

            values = [3.14, 2.718, 1.618]
            for val in values:
                channel.send(val)

            for expected in values:
                actual = channel.receive()
                assert abs(actual - expected) < 0.001

        finally:
            Memory.unlink(shm_name)

    def test_int64_channel(self):
        """Test 64-bit integers."""
        shm_name = f"/test_int64_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "int64", capacity=5, dtype=np.int64)

            large_val = 10**15
            channel.send(large_val)
            channel.send(large_val + 1)

            assert channel.receive() == large_val
            assert channel.receive() == large_val + 1

        finally:
            Memory.unlink(shm_name)


class TestContextManager:
    """Test context manager interface."""

    def test_context_manager(self):
        """Test channel as context manager."""
        shm_name = f"/test_context_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            with Channel(memory, "context", capacity=5, dtype=np.int32) as channel:
                channel.send(42)
                assert channel.is_open()

            # Should be closed after context
            assert channel.is_closed()

        finally:
            Memory.unlink(shm_name)


class TestIterator:
    """Test iterator interface."""

    def test_channel_iterator(self):
        """Test channel as iterator."""
        shm_name = f"/test_iter_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "iter", capacity=10, dtype=np.int32)

            # Send values and close
            values = [1, 2, 3, 4, 5]
            for val in values:
                channel.send(val)
            channel.close()

            # Iterate
            received = list(channel)
            assert received == values

        finally:
            Memory.unlink(shm_name)

    def test_boolean_conversion(self):
        """Test boolean conversion."""
        shm_name = f"/test_bool_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "bool", capacity=5, dtype=np.int32)

            # Empty open channel is falsy
            assert not bool(channel)

            # With data is truthy
            channel.send(42)
            assert bool(channel)

            # Closed is falsy
            channel.close()
            assert not bool(channel)

        finally:
            Memory.unlink(shm_name)


class TestStringRepresentation:
    """Test string representation."""

    def test_string_repr(self):
        """Test __str__ and __repr__."""
        shm_name = f"/test_str_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "str_ch", capacity=5, dtype=np.int32)

            str_repr = str(channel)
            assert "str_ch" in str_repr
            assert "buffered" in str_repr
            assert "capacity=5" in str_repr
            assert "open" in str_repr
            assert repr(channel) == str(channel)

        finally:
            Memory.unlink(shm_name)


class TestConstructorValidation:
    """Test constructor parameter validation."""

    def test_parameter_validation(self):
        """Test constructor validation."""
        shm_name = f"/test_validation_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Missing dtype
            with pytest.raises(TypeError, match="dtype is required"):
                Channel(memory, "no_dtype", capacity=5)

            # Both capacity and buffer_size
            with pytest.raises(ValueError, match="Cannot specify both"):
                Channel(memory, "both", capacity=5, buffer_size=10, dtype=np.int32)

        finally:
            Memory.unlink(shm_name)

    def test_open_existing(self):
        """Test opening existing channel."""
        shm_name = f"/test_existing_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create channel
            ch1 = Channel(memory, "existing", capacity=5, dtype=np.int32)
            ch1.send(42)

            # Open existing
            ch2 = Channel(memory, "existing", dtype=np.int32, open_existing=True)
            assert ch2.capacity == 5
            assert ch2.receive() == 42

            # Try to open non-existent
            with pytest.raises(RuntimeError, match="Channel not found"):
                Channel(memory, "nonexistent", dtype=np.int32, open_existing=True)

        finally:
            Memory.unlink(shm_name)


class TestUtilityFunctions:
    """Test utility functions."""

    def test_make_channel(self):
        """Test make_channel function."""
        shm_name = f"/test_make_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Default
            ch1 = make_channel(memory, "util1")
            assert ch1.capacity == 0  # Default unbuffered

            # Custom
            ch2 = make_channel(memory, "util2", capacity=10, dtype=np.float32)
            assert ch2.capacity == 10
            assert ch2.dtype == np.float32

        finally:
            Memory.unlink(shm_name)

    def test_make_buffered_channel(self):
        """Test make_buffered_channel function."""
        shm_name = f"/test_make_buf_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch = make_buffered_channel(memory, "buf", capacity=20, dtype=np.int64)
            assert ch.capacity == 20
            assert not ch.unbuffered
            assert ch.dtype == np.int64

        finally:
            Memory.unlink(shm_name)


class TestSelectOperations:
    """Test Select operations on buffered channels."""

    def test_select_receive(self):
        """Test Select for receiving."""
        shm_name = f"/test_select_recv_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch1 = Channel(memory, "sel1", capacity=5, dtype=np.int32)
            ch2 = Channel(memory, "sel2", capacity=5, dtype=np.int32)
            ch3 = Channel(memory, "sel3", capacity=5, dtype=np.int32)

            # Send to one channel
            ch2.send(42)

            # Select should find it
            select = Select([ch1, ch2, ch3], timeout=0.1)
            result = select.select_receive()

            assert result is not None
            assert result[0] == 1  # Index of ch2
            assert result[1] == 42

        finally:
            Memory.unlink(shm_name)

    def test_select_timeout(self):
        """Test Select timeout."""
        shm_name = f"/test_select_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch1 = Channel(memory, "sel1", capacity=5, dtype=np.int32)
            ch2 = Channel(memory, "sel2", capacity=5, dtype=np.int32)

            select = Select([ch1, ch2], timeout=0.1)
            start_time = time.time()
            result = select.select_receive()
            elapsed = time.time() - start_time

            assert result is None
            assert elapsed >= 0.1

        finally:
            Memory.unlink(shm_name)

    def test_select_send(self):
        """Test Select for sending."""
        shm_name = f"/test_select_send_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch1 = Channel(memory, "sel1", capacity=1, dtype=np.int32)
            ch2 = Channel(memory, "sel2", capacity=1, dtype=np.int32)

            # Fill first channel
            ch1.send(1)

            # Select send should choose second
            select = Select([ch1, ch2], timeout=0.1)
            result = select.select_send([10, 20])

            assert result == 1  # Index of ch2
            assert ch2.receive() == 20

        finally:
            Memory.unlink(shm_name)

    def test_select_validation(self):
        """Test Select validation."""
        shm_name = f"/test_select_val_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch1 = Channel(memory, "sel1", capacity=5, dtype=np.int32)
            ch2 = Channel(memory, "sel2", capacity=5, dtype=np.int32)

            select = Select([ch1, ch2])

            # Wrong number of values
            with pytest.raises(ValueError, match="Number of values must match"):
                select.select_send([1, 2, 3])

        finally:
            Memory.unlink(shm_name)

    def test_channel_static_select(self):
        """Test Channel.select static method."""
        shm_name = f"/test_static_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch1 = Channel(memory, "st1", capacity=5, dtype=np.int32)
            ch2 = Channel(memory, "st2", capacity=5, dtype=np.int32)
            ch3 = Channel(memory, "st3", capacity=5, dtype=np.int32)

            # Send to channels
            ch1.send(100)
            ch2.send(200)

            # Select should find ready channel
            ready = Channel.select([ch1, ch2, ch3], timeout=0.1)
            assert ready is not None
            assert ready in [ch1, ch2]

        finally:
            Memory.unlink(shm_name)


class TestConcurrentBuffered:
    """Test concurrent operations on buffered channels."""

    def test_concurrent_senders(self):
        """Test multiple threads sending."""
        shm_name = f"/test_conc_send_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "conc", capacity=100, dtype=np.int32)

            sent_values = []
            lock = threading.Lock()

            def sender(thread_id, count):
                for i in range(count):
                    val = thread_id * 1000 + i
                    if channel.send(val, timeout=1.0):
                        with lock:
                            sent_values.append(val)

            # Start threads
            threads = []
            for i in range(3):
                t = threading.Thread(target=sender, args=(i, 10))
                threads.append(t)
                t.start()

            for t in threads:
                t.join()

            # Receive all
            received = []
            while not channel.empty():
                val = channel.try_receive()
                if val is not None:
                    received.append(val)

            assert len(received) == len(sent_values)
            assert set(received) == set(sent_values)

        finally:
            Memory.unlink(shm_name)

    def test_concurrent_receivers(self):
        """Test multiple threads receiving."""
        shm_name = f"/test_conc_recv_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "conc_recv", capacity=100, dtype=np.int32)

            # Pre-fill
            sent = list(range(50))
            for val in sent:
                channel.send(val)

            received_values = []
            lock = threading.Lock()

            def receiver(count):
                for _ in range(count):
                    val = channel.try_receive()
                    if val is not None:
                        with lock:
                            received_values.append(val)

            # Start threads
            threads = []
            for _ in range(5):
                t = threading.Thread(target=receiver, args=(10,))
                threads.append(t)
                t.start()

            for t in threads:
                t.join()

            # Each value received once
            assert len(received_values) == len(set(received_values))
            assert set(received_values).issubset(set(sent))

        finally:
            Memory.unlink(shm_name)