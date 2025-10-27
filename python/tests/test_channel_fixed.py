"""
Test suite for Channel codata structure in shared memory.
Fixed version using TDD principles.
"""

import os
import threading
import time
import pytest
import numpy as np

from zeroipc import Memory
from zeroipc.channel import Channel, ChannelClosed, Select, make_channel, make_buffered_channel, make_unbuffered_channel


class TestChannelConstructor:
    """Test Channel constructor and parameter validation."""

    def test_create_buffered_channel(self):
        """Test creating a buffered channel."""
        shm_name = f"/test_channel_create_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Test different parameter combinations
            ch1 = Channel(memory, "test1", capacity=10, dtype=np.int32)
            assert ch1.name == "test1"
            assert ch1.capacity == 10
            assert ch1.buffer_size == 10
            assert not ch1.unbuffered
            assert ch1.is_open()
            assert not ch1.is_closed()

            ch2 = Channel(memory, "test2", buffer_size=5, dtype=np.float32)
            assert ch2.name == "test2"
            assert ch2.capacity == 5
            assert ch2.buffer_size == 5

        finally:
            Memory.unlink(shm_name)

    def test_create_unbuffered_channel(self):
        """Test creating an unbuffered (synchronous) channel."""
        shm_name = f"/test_channel_unbuf_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Test unbuffered channel creation
            ch = Channel(memory, "sync_ch", capacity=0, dtype=np.int32)
            assert ch.name == "sync_ch"
            assert ch.capacity == 0
            assert ch.buffer_size == 0
            assert ch.unbuffered
            assert ch.is_open()

        finally:
            Memory.unlink(shm_name)

    def test_constructor_parameter_validation(self):
        """Test constructor parameter validation."""
        shm_name = f"/test_channel_params_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Missing dtype should raise TypeError
            with pytest.raises(TypeError, match="dtype is required"):
                Channel(memory, "no_dtype", capacity=5)

            # Both capacity and buffer_size should raise ValueError
            with pytest.raises(ValueError, match="Cannot specify both capacity and buffer_size"):
                Channel(memory, "both_params", capacity=5, buffer_size=10, dtype=np.int32)

            # Default capacity should be 0 (unbuffered)
            ch = Channel(memory, "default", dtype=np.int32)
            assert ch.capacity == 0
            assert ch.unbuffered

        finally:
            Memory.unlink(shm_name)

    def test_open_existing_channel(self):
        """Test opening an existing channel."""
        shm_name = f"/test_channel_existing_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create channel
            ch1 = Channel(memory, "existing", capacity=5, dtype=np.int32)
            ch1.send(42)

            # Open existing channel
            ch2 = Channel(memory, "existing", dtype=np.int32, open_existing=True)
            assert ch2.capacity == 5
            assert ch2.receive() == 42

            # Try to open non-existent channel
            with pytest.raises(RuntimeError, match="Channel not found"):
                Channel(memory, "nonexistent", dtype=np.int32, open_existing=True)

        finally:
            Memory.unlink(shm_name)


class TestBufferedChannelOperations:
    """Test buffered channel send/receive operations."""

    def test_basic_send_receive(self):
        """Test basic send and receive operations."""
        shm_name = f"/test_channel_ops_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "ops_channel", capacity=10, dtype=np.int32)

            # Send values
            assert channel.send(10) == True
            assert channel.send(20) == True
            assert channel.send(30) == True

            # Check channel state
            assert channel.size() == 3
            assert len(channel) == 3
            assert not channel.empty()
            assert not channel.full()

            # Receive values
            assert channel.receive() == 10
            assert channel.receive() == 20
            assert channel.receive() == 30

            # Channel should be empty now
            assert channel.empty()
            assert channel.size() == 0

        finally:
            Memory.unlink(shm_name)

    def test_try_operations(self):
        """Test non-blocking try_send and try_receive."""
        shm_name = f"/test_channel_try_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "try_channel", capacity=3, dtype=np.int32)

            # Try send to fill buffer
            assert channel.try_send(10) == True
            assert channel.try_send(20) == True
            assert channel.try_send(30) == True

            # Buffer full, try_send should fail
            assert channel.full()
            assert channel.try_send(40) == False

            # Try receive to empty buffer
            assert channel.try_receive() == 10
            assert channel.try_receive() == 20
            assert channel.try_receive() == 30

            # Buffer empty, try_receive should return None
            assert channel.empty()
            assert channel.try_receive() is None

        finally:
            Memory.unlink(shm_name)

    def test_blocking_send_timeout(self):
        """Test send with timeout when buffer is full."""
        shm_name = f"/test_channel_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "timeout_channel", capacity=2, dtype=np.int32)

            # Fill buffer
            channel.send(1)
            channel.send(2)
            assert channel.full()

            # Send with timeout should fail
            start_time = time.time()
            result = channel.send(3, timeout=0.1)
            elapsed = time.time() - start_time

            assert result == False
            assert elapsed >= 0.1
            assert elapsed < 0.2  # Should timeout reasonably quickly

        finally:
            Memory.unlink(shm_name)

    def test_blocking_receive_timeout(self):
        """Test receive with timeout when buffer is empty."""
        shm_name = f"/test_channel_recv_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "recv_timeout", capacity=5, dtype=np.int32)

            assert channel.empty()

            # Receive with timeout should fail
            start_time = time.time()
            result = channel.receive(timeout=0.1)
            elapsed = time.time() - start_time

            assert result is None
            assert elapsed >= 0.1
            assert elapsed < 0.2

        finally:
            Memory.unlink(shm_name)


class TestChannelClosing:
    """Test channel closing behavior."""

    def test_close_channel(self):
        """Test closing a channel."""
        shm_name = f"/test_channel_close_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "close_channel", capacity=10, dtype=np.int32)

            # Send some data
            channel.send(10)
            channel.send(20)

            # Close channel
            channel.close()
            assert channel.is_closed()
            assert not channel.is_open()

            # Can still receive existing data
            assert channel.receive() == 10
            assert channel.receive() == 20

            # But further receives return None
            assert channel.receive() is None

        finally:
            Memory.unlink(shm_name)

    def test_send_to_closed_channel(self):
        """Test sending to a closed channel."""
        shm_name = f"/test_channel_send_closed_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "send_closed", capacity=10, dtype=np.int32)

            channel.close()

            # Sending to closed channel should return False
            assert channel.send(10) == False
            assert channel.try_send(10) == False

        finally:
            Memory.unlink(shm_name)

    def test_context_manager(self):
        """Test channel as context manager."""
        shm_name = f"/test_channel_context_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            with Channel(memory, "context_ch", capacity=5, dtype=np.int32) as channel:
                channel.send(42)
                assert channel.is_open()

            # Channel should be closed after context
            assert channel.is_closed()

        finally:
            Memory.unlink(shm_name)


class TestChannelDataTypes:
    """Test channels with different data types."""

    def test_float_channel(self):
        """Test channel with float values."""
        shm_name = f"/test_channel_float_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "float_ch", capacity=5, dtype=np.float32)

            values = [3.14, 2.718, 1.618]
            for val in values:
                channel.send(val)

            for expected in values:
                actual = channel.receive()
                assert abs(actual - expected) < 0.001

        finally:
            Memory.unlink(shm_name)

    def test_int64_channel(self):
        """Test channel with 64-bit integers."""
        shm_name = f"/test_channel_int64_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "int64_ch", capacity=5, dtype=np.int64)

            large_val = 10**15
            channel.send(large_val)
            channel.send(large_val + 1)

            assert channel.receive() == large_val
            assert channel.receive() == large_val + 1

        finally:
            Memory.unlink(shm_name)


class TestChannelIterator:
    """Test channel iterator interface."""

    def test_channel_iterator(self):
        """Test using channel as iterator."""
        shm_name = f"/test_channel_iter_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "iter_channel", capacity=10, dtype=np.int32)

            # Send values and close
            values = [1, 2, 3, 4, 5]
            for val in values:
                channel.send(val)
            channel.close()

            # Iterate over channel
            received = list(channel)
            assert received == values

        finally:
            Memory.unlink(shm_name)

    def test_boolean_conversion(self):
        """Test channel boolean conversion."""
        shm_name = f"/test_channel_bool_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "bool_ch", capacity=5, dtype=np.int32)

            # Empty open channel is falsy
            assert not bool(channel)

            # Channel with data is truthy
            channel.send(42)
            assert bool(channel)

            # Closed channel is falsy
            channel.close()
            assert not bool(channel)

        finally:
            Memory.unlink(shm_name)


class TestChannelStringRepresentation:
    """Test channel string representations."""

    def test_string_representation(self):
        """Test channel __str__ and __repr__ methods."""
        shm_name = f"/test_channel_str_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Buffered channel
            ch1 = Channel(memory, "str_ch", capacity=5, dtype=np.int32)
            str_repr = str(ch1)
            assert "str_ch" in str_repr
            assert "buffered" in str_repr
            assert "capacity=5" in str_repr
            assert "open" in str_repr
            assert repr(ch1) == str(ch1)

            # Unbuffered channel
            ch2 = Channel(memory, "sync_ch", capacity=0, dtype=np.float32)
            str_repr = str(ch2)
            assert "sync_ch" in str_repr
            assert "unbuffered" in str_repr

        finally:
            Memory.unlink(shm_name)


class TestUtilityFunctions:
    """Test utility functions for creating channels."""

    def test_make_channel(self):
        """Test make_channel utility function."""
        shm_name = f"/test_make_channel_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Default parameters
            ch1 = make_channel(memory, "util1")
            assert ch1.capacity == 0  # Default unbuffered
            assert ch1.dtype == np.int32

            # Custom parameters
            ch2 = make_channel(memory, "util2", capacity=10, dtype=np.float32)
            assert ch2.capacity == 10
            assert ch2.dtype == np.float32

        finally:
            Memory.unlink(shm_name)

    def test_make_buffered_channel(self):
        """Test make_buffered_channel utility function."""
        shm_name = f"/test_make_buffered_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch = make_buffered_channel(memory, "buffered", capacity=20, dtype=np.int64)
            assert ch.capacity == 20
            assert not ch.unbuffered
            assert ch.dtype == np.int64

        finally:
            Memory.unlink(shm_name)

    def test_make_unbuffered_channel(self):
        """Test make_unbuffered_channel utility function."""
        shm_name = f"/test_make_unbuffered_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch = make_unbuffered_channel(memory, "unbuffered", dtype=np.float64)
            assert ch.capacity == 0
            assert ch.unbuffered
            assert ch.dtype == np.float64

        finally:
            Memory.unlink(shm_name)


class TestSelectOperations:
    """Test Select class for multi-channel operations."""

    def test_select_basic(self):
        """Test basic Select functionality."""
        shm_name = f"/test_select_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch1 = Channel(memory, "sel1", capacity=5, dtype=np.int32)
            ch2 = Channel(memory, "sel2", capacity=5, dtype=np.int32)
            ch3 = Channel(memory, "sel3", capacity=5, dtype=np.int32)

            # Send to one channel
            ch2.send(42)

            # Select should find the ready channel
            select = Select([ch1, ch2, ch3], timeout=0.1)
            result = select.select_receive()

            assert result is not None
            assert result[0] == 1  # Index of ch2
            assert result[1] == 42  # Value received

        finally:
            Memory.unlink(shm_name)

    def test_select_timeout(self):
        """Test Select with timeout."""
        shm_name = f"/test_select_timeout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch1 = Channel(memory, "sel1", capacity=5, dtype=np.int32)
            ch2 = Channel(memory, "sel2", capacity=5, dtype=np.int32)

            # No data in channels
            select = Select([ch1, ch2], timeout=0.1)
            start_time = time.time()
            result = select.select_receive()
            elapsed = time.time() - start_time

            assert result is None
            assert elapsed >= 0.1

        finally:
            Memory.unlink(shm_name)

    def test_select_send(self):
        """Test Select send operations."""
        shm_name = f"/test_select_send_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch1 = Channel(memory, "sel1", capacity=1, dtype=np.int32)
            ch2 = Channel(memory, "sel2", capacity=1, dtype=np.int32)

            # Fill first channel
            ch1.send(1)

            # Select send should choose second channel
            select = Select([ch1, ch2], timeout=0.1)
            result = select.select_send([10, 20])

            assert result == 1  # Index of ch2
            assert ch2.receive() == 20

        finally:
            Memory.unlink(shm_name)

    def test_select_send_validation(self):
        """Test Select send validation."""
        shm_name = f"/test_select_validation_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch1 = Channel(memory, "sel1", capacity=5, dtype=np.int32)
            ch2 = Channel(memory, "sel2", capacity=5, dtype=np.int32)

            select = Select([ch1, ch2])

            # Wrong number of values should raise ValueError
            with pytest.raises(ValueError, match="Number of values must match"):
                select.select_send([1, 2, 3])  # 3 values for 2 channels

        finally:
            Memory.unlink(shm_name)


class TestChannelStaticSelect:
    """Test Channel.select static method."""

    def test_channel_static_select(self):
        """Test Channel.select static method."""
        shm_name = f"/test_static_select_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch1 = Channel(memory, "static1", capacity=5, dtype=np.int32)
            ch2 = Channel(memory, "static2", capacity=5, dtype=np.int32)
            ch3 = Channel(memory, "static3", capacity=5, dtype=np.int32)

            # Send to different channels
            ch1.send(100)
            ch2.send(200)

            # Select should find a ready channel
            ready = Channel.select([ch1, ch2, ch3], timeout=0.1)
            assert ready is not None
            assert ready in [ch1, ch2]

            # Receive from ready channel
            val = ready.receive()
            assert val in [100, 200]

        finally:
            Memory.unlink(shm_name)


class TestConcurrentOperations:
    """Test thread-safe operations on buffered channels."""

    def test_concurrent_senders(self):
        """Test multiple threads sending to same channel."""
        shm_name = f"/test_concurrent_send_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "concurrent", capacity=100, dtype=np.int32)

            sent_values = []
            lock = threading.Lock()

            def sender(thread_id, count):
                for i in range(count):
                    val = thread_id * 1000 + i
                    if channel.send(val, timeout=1.0):
                        with lock:
                            sent_values.append(val)

            # Start multiple sender threads
            threads = []
            for i in range(3):
                t = threading.Thread(target=sender, args=(i, 10))
                threads.append(t)
                t.start()

            for t in threads:
                t.join()

            # Receive all values
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
        """Test multiple threads receiving from same channel."""
        shm_name = f"/test_concurrent_recv_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "concurrent_recv", capacity=100, dtype=np.int32)

            # Pre-fill channel
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

            # Start multiple receiver threads
            threads = []
            for _ in range(5):
                t = threading.Thread(target=receiver, args=(10,))
                threads.append(t)
                t.start()

            for t in threads:
                t.join()

            # Each value should be received exactly once
            assert len(received_values) == len(set(received_values))
            assert set(received_values).issubset(set(sent))

        finally:
            Memory.unlink(shm_name)