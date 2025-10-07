"""
Test suite for core Channel functionality.
Focused TDD approach to fix channel issues.
"""

import os
import time
import pytest
import numpy as np

from zeroipc import Memory
from zeroipc.channel import Channel, ChannelClosed


class TestChannelConstructorAndBasics:
    """Test basic channel construction and simple operations."""

    def test_create_buffered_channel(self):
        """Test creating a buffered channel - this should work."""
        shm_name = f"/test_basic_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)

            channel = Channel(memory, "basic", capacity=5, dtype=np.int32)
            assert channel.name == "basic"
            assert channel.capacity == 5
            assert not channel.unbuffered
            assert channel.is_open()
            assert not channel.is_closed()

        finally:
            Memory.unlink(shm_name)

    def test_simple_send_receive(self):
        """Test simple send/receive without timeouts."""
        shm_name = f"/test_simple_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "simple", capacity=5, dtype=np.int32)

            # Simple operations that should work
            assert channel.send(42) == True
            assert channel.receive() == 42

        finally:
            Memory.unlink(shm_name)

    def test_try_operations(self):
        """Test non-blocking try operations."""
        shm_name = f"/test_try_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "try", capacity=2, dtype=np.int32)

            # Fill buffer
            assert channel.try_send(1) == True
            assert channel.try_send(2) == True

            # Buffer should be full now
            assert channel.try_send(3) == False

            # Empty buffer
            assert channel.try_receive() == 1
            assert channel.try_receive() == 2

            # Buffer should be empty now
            assert channel.try_receive() is None

        finally:
            Memory.unlink(shm_name)

    def test_channel_state_queries(self):
        """Test channel state query methods."""
        shm_name = f"/test_state_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "state", capacity=3, dtype=np.int32)

            # Initial state
            assert channel.empty() == True
            assert channel.full() == False
            assert channel.size() == 0
            assert len(channel) == 0

            # Add one item
            channel.send(1)
            assert not channel.empty()
            assert not channel.full()
            assert channel.size() == 1

            # Fill buffer
            channel.send(2)
            channel.send(3)
            assert channel.full() == True
            assert channel.size() == 3

        finally:
            Memory.unlink(shm_name)


class TestChannelClosingFixed:
    """Test that channel closing works properly."""

    def test_close_buffered_channel_basic(self):
        """Test closing a buffered channel - define expected behavior."""
        shm_name = f"/test_close_basic_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "close_basic", capacity=5, dtype=np.int32)

            # Initially open
            assert channel.is_open() == True
            assert channel.is_closed() == False

            # Close it
            channel.close()

            # Should be closed now
            assert channel.is_closed() == True
            assert channel.is_open() == False

        finally:
            Memory.unlink(shm_name)

    def test_send_to_closed_channel(self):
        """Test that sending to closed channel raises exception."""
        shm_name = f"/test_send_closed_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "send_closed", capacity=5, dtype=np.int32)

            channel.close()

            # Should return False (cannot send to closed channel)
            assert channel.send(42) == False
            assert channel.try_send(42) == False

        finally:
            Memory.unlink(shm_name)

    def test_receive_from_closed_empty_channel(self):
        """Test receiving from closed empty channel returns None."""
        shm_name = f"/test_recv_closed_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "recv_closed", capacity=5, dtype=np.int32)

            # Close empty channel
            channel.close()

            # Should return None
            assert channel.receive() is None
            assert channel.try_receive() is None

        finally:
            Memory.unlink(shm_name)

    def test_receive_remaining_data_from_closed_channel(self):
        """Test that we can receive remaining data from closed channel."""
        shm_name = f"/test_recv_remaining_{os.getpid()}"

        try:
            memory = Memory(shm_name, size=10*1024*1024)
            channel = Channel(memory, "recv_remaining", capacity=5, dtype=np.int32)

            # Send data then close
            channel.send(10)
            channel.send(20)
            channel.close()

            # Should still receive the data
            assert channel.receive() == 10
            assert channel.receive() == 20

            # Then return None
            assert channel.receive() is None

        finally:
            Memory.unlink(shm_name)