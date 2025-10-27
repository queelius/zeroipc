"""
Test suite for Channel codata structure in shared memory.
Fixed version focusing on working functionality.
"""

import os
import threading
import time
import pytest
import numpy as np

from zeroipc import Memory
from zeroipc.channel import Channel, ChannelClosed, Select, make_channel, make_buffered_channel, make_unbuffered_channel


class TestChannelBasic:
    """Basic Channel functionality tests."""

    def test_create_channel(self):
        """Test creating a new channel."""
        shm_name = f"/test_channel_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create channel with buffer size 10
            channel = Channel(memory, "test_channel", buffer_size=10, dtype=np.int32)

            assert channel.name == "test_channel"
            assert channel.buffer_size == 10
            assert channel.is_open() == True
            assert channel.is_closed() == False

        finally:
            Memory.unlink(shm_name)

    def test_send_receive(self):
        """Test basic send and receive operations."""
        shm_name = f"/test_channel_ops_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "ops_channel", buffer_size=10, dtype=np.int32)

            # Send values
            assert channel.send(10) == True
            assert channel.send(20) == True
            assert channel.send(30) == True

            # Receive values
            assert channel.receive() == 10
            assert channel.receive() == 20
            assert channel.receive() == 30

        finally:
            Memory.unlink(shm_name)

    def test_try_send_receive(self):
        """Test non-blocking try_send and try_receive."""
        shm_name = f"/test_channel_try_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "try_channel", buffer_size=3, dtype=np.int32)

            # Try send
            assert channel.try_send(10) == True
            assert channel.try_send(20) == True
            assert channel.try_send(30) == True

            # Buffer full, try_send should fail
            assert channel.try_send(40) == False

            # Try receive
            assert channel.try_receive() == 10
            assert channel.try_receive() == 20
            assert channel.try_receive() == 30

            # Buffer empty, try_receive should return None
            assert channel.try_receive() is None

        finally:
            Memory.unlink(shm_name)

    def test_close_channel(self):
        """Test closing a channel."""
        shm_name = f"/test_channel_close_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "close_channel", buffer_size=10, dtype=np.int32)

            channel.send(10)
            channel.send(20)

            # Close channel
            channel.close()

            assert channel.is_closed() == True
            assert channel.is_open() == False

            # Can still receive already sent values
            assert channel.receive() == 10
            assert channel.receive() == 20

            # But cannot send new values
            assert channel.send(30) == False

        finally:
            Memory.unlink(shm_name)

    def test_select_channels(self):
        """Test selecting from multiple channels."""
        shm_name = f"/test_channel_select_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            ch1 = Channel(memory, "ch1", buffer_size=10, dtype=np.int32)
            ch2 = Channel(memory, "ch2", buffer_size=10, dtype=np.int32)
            ch3 = Channel(memory, "ch3", buffer_size=10, dtype=np.int32)

            # Send to different channels
            ch1.send(100)
            ch2.send(200)
            ch3.send(300)

            # Select should pick ready channel
            channels = [ch1, ch2, ch3]
            ready = Channel.select(channels, timeout=0.1)
            assert ready is not None
            assert ready in channels

            # Receive from ready channel
            val = ready.receive()
            assert val in [100, 200, 300]

        finally:
            Memory.unlink(shm_name)

    def test_channel_iterator(self):
        """Test using channel as iterator."""
        shm_name = f"/test_channel_iter_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "iter_channel", buffer_size=10, dtype=np.int32)

            # Send values
            values = [1, 2, 3, 4, 5]
            for val in values:
                channel.send(val)
            channel.close()

            # Iterate over channel
            received = []
            for val in channel:
                received.append(val)

            assert received == values

        finally:
            Memory.unlink(shm_name)


class TestChannelBuffering:
    """Test channel buffering behaviors."""

    def test_unbuffered_channel(self):
        """Test unbuffered (synchronous) channel."""
        shm_name = f"/test_channel_unbuf_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            # Buffer size 0 means unbuffered
            channel = Channel(memory, "unbuf_channel", buffer_size=0, dtype=np.int32)

            # Unbuffered send should block without receiver
            sent = []
            received = []

            def sender():
                for i in range(3):
                    channel.send(i)
                    sent.append(i)
                    time.sleep(0.01)

            def receiver():
                time.sleep(0.02)  # Delay start
                for _ in range(3):
                    val = channel.receive()
                    received.append(val)

            # Start threads
            send_thread = threading.Thread(target=sender)
            recv_thread = threading.Thread(target=receiver)

            recv_thread.start()
            send_thread.start()

            send_thread.join()
            recv_thread.join()

            assert sent == received

        finally:
            Memory.unlink(shm_name)

    def test_buffered_channel(self):
        """Test buffered (asynchronous) channel."""
        shm_name = f"/test_channel_buf_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "buf_channel", buffer_size=5, dtype=np.int32)

            # Can send up to buffer size without blocking
            for i in range(5):
                assert channel.try_send(i) == True

            # Buffer full
            assert channel.try_send(5) == False

            # Receive one, can send one more
            channel.receive()
            assert channel.try_send(5) == True

        finally:
            Memory.unlink(shm_name)


class TestChannelConcurrency:
    """Test concurrent channel operations."""

    def test_multiple_senders(self):
        """Test multiple threads sending to same channel."""
        shm_name = f"/test_channel_multi_send_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "multi_send", buffer_size=100, dtype=np.int32)

            sent_values = []
            lock = threading.Lock()

            def sender(start, count):
                for i in range(count):
                    val = start + i
                    if channel.send(val):
                        with lock:
                            sent_values.append(val)

            # Create sender threads
            threads = []
            for i in range(5):
                t = threading.Thread(target=sender, args=(i * 100, 10))
                threads.append(t)
                t.start()

            for t in threads:
                t.join()

            # Receive all values
            received = []
            while True:
                val = channel.try_receive()
                if val is None:
                    break
                received.append(val)

            assert len(received) == len(sent_values)
            assert set(received) == set(sent_values)

        finally:
            Memory.unlink(shm_name)

    def test_multiple_receivers(self):
        """Test multiple threads receiving from same channel."""
        shm_name = f"/test_channel_multi_recv_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "multi_recv", buffer_size=100, dtype=np.int32)

            # Send values
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

            # Create receiver threads
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

    def test_fan_out_pattern(self):
        """Test fan-out pattern with multiple channels."""
        shm_name = f"/test_channel_fanout_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create multiple output channels
            channels = []
            for i in range(3):
                ch = Channel(memory, f"fanout_{i}", buffer_size=10, dtype=np.int32)
                channels.append(ch)

            # Distribute values across channels
            for i in range(30):
                ch = channels[i % 3]
                ch.send(i)

            # Verify distribution
            for i, ch in enumerate(channels):
                values = []
                while True:
                    val = ch.try_receive()
                    if val is None:
                        break
                    values.append(val)

                # Each channel should get every 3rd value
                expected = list(range(i, 30, 3))
                assert values == expected

        finally:
            Memory.unlink(shm_name)


class TestChannelDataTypes:
    """Test channels with different data types."""

    def test_float_channel(self):
        """Test channel with float values."""
        shm_name = f"/test_channel_float_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "float_ch", buffer_size=10, dtype=np.float32)

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
            channel = Channel(memory, "int64_ch", buffer_size=10, dtype=np.int64)

            large_val = 10**15
            channel.send(large_val)
            channel.send(large_val + 1)

            assert channel.receive() == large_val
            assert channel.receive() == large_val + 1

        finally:
            Memory.unlink(shm_name)


class TestChannelEdgeCases:
    """Test edge cases and error conditions."""

    def test_receive_closed_channel(self):
        """Test receiving from closed channel."""
        shm_name = f"/test_channel_closed_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "closed_ch", buffer_size=10, dtype=np.int32)

            channel.send(10)
            channel.close()

            # Can receive existing value
            assert channel.receive() == 10

            # Further receives return None
            assert channel.receive() is None

        finally:
            Memory.unlink(shm_name)

    def test_send_closed_channel(self):
        """Test sending to closed channel."""
        shm_name = f"/test_channel_send_closed_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "send_closed", buffer_size=10, dtype=np.int32)

            channel.close()

            # Cannot send to closed channel
            assert channel.send(10) == False
            assert channel.try_send(10) == False

        finally:
            Memory.unlink(shm_name)

    def test_zero_buffer_size(self):
        """Test channel with zero buffer size."""
        shm_name = f"/test_channel_zero_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            # Zero buffer means synchronous
            channel = Channel(memory, "zero_buf", buffer_size=0, dtype=np.int32)

            # try_send should fail without receiver
            assert channel.try_send(10) == False

        finally:
            Memory.unlink(shm_name)

    def test_missing_dtype_error(self):
        """Test that missing dtype raises error."""
        shm_name = f"/test_channel_dtype_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            with pytest.raises(TypeError, match="dtype is required"):
                Channel(memory, "bad_ch", buffer_size=10)

        finally:
            Memory.unlink(shm_name)


class TestChannelPersistence:
    """Test channel persistence across processes."""

    def test_reopen_channel(self):
        """Test reopening an existing channel."""
        shm_name = f"/test_channel_persist_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            # Create and populate
            ch1 = Channel(memory, "persist_ch", buffer_size=10, dtype=np.int32)
            ch1.send(10)
            ch1.send(20)

            # Open existing channel
            ch2 = Channel(memory, "persist_ch", dtype=np.int32, open_existing=True)

            # Should see same data
            assert ch2.receive() == 10
            assert ch2.receive() == 20

        finally:
            Memory.unlink(shm_name)