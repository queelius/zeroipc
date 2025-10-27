"""
CSP-style channel for synchronous message passing between processes.

This module provides a Channel data structure that implements Communicating
Sequential Processes (CSP) semantics, where processes communicate through
synchronous message passing.
"""

import time
from typing import Optional, TypeVar, Union, List
import numpy as np

from .memory import Memory
from .queue import Queue
from .atomic import AtomicInt

T = TypeVar('T')


class ChannelClosed(Exception):
    """Exception raised when operating on a closed channel."""
    pass


class Channel:
    """
    CSP-style channel for synchronous message passing between processes.

    Channel<T> implements Communicating Sequential Processes (CSP) semantics,
    where processes communicate through synchronous message passing. Unlike
    queues which are asynchronous, channels provide rendezvous semantics where
    send blocks until receive, creating a synchronization point.

    Example:
        # Unbuffered channel for synchronization
        mem = Memory("/comms", 1024*1024)
        ch = Channel(mem, "sync_channel", dtype=np.int32)  # capacity=0 means unbuffered

        # Process A: Sender
        ch.send(42)  # Blocks until Process B receives

        # Process B: Receiver
        value = ch.receive()  # Blocks until Process A sends

        # Buffered channel for asynchronous communication up to capacity
        buffered_ch = Channel(mem, "buffered", capacity=10, dtype=np.int32)
        buffered_ch.send(1)  # Doesn't block if buffer has space
        buffered_ch.send(2)
        value1 = buffered_ch.receive()  # Gets 1
        value2 = buffered_ch.receive()  # Gets 2
    """

    def __init__(self, memory: Memory, name: str,
                 capacity: Optional[int] = None,
                 buffer_size: Optional[int] = None,
                 dtype: Optional[Union[np.dtype, str, type]] = None,
                 open_existing: bool = False):
        """
        Create or open a channel.

        Args:
            memory: Shared memory instance
            name: Channel name
            capacity: Buffer capacity (0 for unbuffered/synchronous channel)
            dtype: Message data type
            open_existing: If True, open existing channel; if False, create new

        Raises:
            ValueError: If required parameters are missing
            TypeError: If data type is not specified
            RuntimeError: If channel is not found when opening existing
        """
        if dtype is None:
            raise TypeError("dtype is required")

        # Handle capacity/buffer_size parameter options
        if capacity is not None and buffer_size is not None:
            raise ValueError("Cannot specify both capacity and buffer_size")
        elif buffer_size is not None:
            actual_capacity = buffer_size
        elif capacity is not None:
            actual_capacity = capacity
        else:
            actual_capacity = 0  # Default to unbuffered

        self.memory = memory
        self.name = name
        self.dtype = np.dtype(dtype)
        self.capacity = actual_capacity
        self.buffer_size = actual_capacity  # Alias for compatibility
        self.unbuffered = (actual_capacity == 0)

        # Queue uses circular buffer with one slot always empty to distinguish full/empty
        # So to hold N items, we need capacity N+1
        # For unbuffered channels (capacity 0), we need queue capacity 2 to hold 1 item
        # For buffered channels (capacity N), we need queue capacity N+1
        if actual_capacity == 0:
            queue_capacity = 2  # Unbuffered: needs 2 to hold 1 item
        else:
            queue_capacity = actual_capacity + 1  # Buffered: +1 for circular buffer

        if open_existing:
            # Try to find existing channel
            entry = memory.table.find(name)
            if entry is None:
                raise RuntimeError(f"Channel not found: {name}")

            # Open the existing queue to determine actual capacity
            temp_queue = Queue(memory, name, 0, dtype)  # capacity=0 means open existing
            queue_capacity = temp_queue.capacity

            # Infer channel capacity from queue capacity
            # Unbuffered uses queue capacity 2, buffered uses N+1
            if queue_capacity == 2:
                actual_capacity = 0  # Unbuffered
            else:
                actual_capacity = queue_capacity - 1  # Buffered

            self.capacity = actual_capacity
            self.buffer_size = actual_capacity
            self.unbuffered = (actual_capacity == 0)
            self._queue = temp_queue
        else:
            # Use Queue as underlying storage with additional synchronization state
            self._queue = Queue(memory, name, queue_capacity, dtype)

        # All channels need synchronization state for closing and unbuffered channels need it for coordination
        sync_name = f"{name}_sync"
        sync_entry = memory.table.find(sync_name)

        if sync_entry is None and not open_existing:
            # Create synchronization state: sender_waiting(4) + receiver_waiting(4) + closed(4)
            sync_size = 12
            sync_offset = memory.table.allocate(sync_size)
            if not memory.table.add(sync_name, sync_offset, sync_size):
                raise RuntimeError("Failed to add channel synchronization state")

            self.sync_buffer = memory.at(sync_offset)
            # Initialize: no one waiting, not closed
            import struct
            struct.pack_into('<III', self.sync_buffer, 0, 0, 0, 0)
        elif sync_entry is not None:
            self.sync_buffer = memory.at(sync_entry.offset)
        else:
            raise RuntimeError(f"Channel synchronization state not found: {sync_name}")

    def _is_closed(self) -> bool:
        """Check if channel is closed."""
        if self.sync_buffer is not None:
            import struct
            return struct.unpack_from('<I', self.sync_buffer, 8)[0] != 0
        return False

    def _set_closed(self):
        """Mark channel as closed."""
        if self.sync_buffer is not None:
            import struct
            struct.pack_into('<I', self.sync_buffer, 8, 1)

    def _get_sender_waiting(self) -> int:
        """Get number of senders waiting."""
        if self.sync_buffer is not None:
            import struct
            return struct.unpack_from('<I', self.sync_buffer, 0)[0]
        return 0

    def _get_receiver_waiting(self) -> int:
        """Get number of receivers waiting."""
        if self.sync_buffer is not None:
            import struct
            return struct.unpack_from('<I', self.sync_buffer, 4)[0]
        return 0

    def _inc_sender_waiting(self):
        """Increment sender waiting count."""
        if self.sync_buffer is not None:
            sender_atomic = AtomicInt(self.sync_buffer, 0)
            sender_atomic.fetch_add(1)

    def _dec_sender_waiting(self):
        """Decrement sender waiting count."""
        if self.sync_buffer is not None:
            sender_atomic = AtomicInt(self.sync_buffer, 0)
            current = sender_atomic.load()
            while current > 0:
                if sender_atomic.compare_exchange_weak(current, current - 1):
                    break

    def _inc_receiver_waiting(self):
        """Increment receiver waiting count."""
        if self.sync_buffer is not None:
            receiver_atomic = AtomicInt(self.sync_buffer, 4)
            receiver_atomic.fetch_add(1)

    def _dec_receiver_waiting(self):
        """Decrement receiver waiting count."""
        if self.sync_buffer is not None:
            receiver_atomic = AtomicInt(self.sync_buffer, 4)
            current = receiver_atomic.load()
            while current > 0:
                if receiver_atomic.compare_exchange_weak(current, current - 1):
                    break

    def send(self, value: T, timeout: Optional[float] = None) -> bool:
        """
        Send a value through the channel.

        Args:
            value: Value to send
            timeout: Timeout in seconds (None for blocking)

        Returns:
            True if send succeeded, False if timeout or channel closed
        """
        if self._is_closed():
            return False  # Cannot send on closed channel

        if self.unbuffered:
            return self._send_unbuffered(value, timeout)
        else:
            return self._send_buffered(value, timeout)

    def _send_buffered(self, value: T, timeout: Optional[float] = None) -> bool:
        """Send to buffered channel."""
        start_time = time.time() if timeout is not None else None

        while True:
            if self._is_closed():
                return False  # Cannot send on closed channel

            # Try to send (non-blocking)
            if self._queue.push(value):
                return True

            # Check timeout
            if timeout is not None:
                if time.time() - start_time >= timeout:
                    return False

            # Brief sleep when queue is full
            time.sleep(0.001)

    def _send_unbuffered(self, value: T, timeout: Optional[float] = None) -> bool:
        """
        Send to unbuffered channel (synchronous rendezvous).

        Algorithm:
        1. Increment sender_waiting to signal presence
        2. Wait for receiver_waiting > 0 (receiver is ready)
        3. Push value to queue
        4. Wait for queue to become empty (receiver took the value)
        5. Decrement sender_waiting
        6. Return

        This ensures the receiver has actually taken the value before we return.
        """
        start_time = time.time() if timeout is not None else None

        # Signal that we're waiting to send
        self._inc_sender_waiting()

        try:
            # Wait for a receiver to be ready
            while True:
                if self._is_closed():
                    return False  # Cannot send on closed channel

                if self._get_receiver_waiting() > 0:
                    # Receiver is waiting, push the value
                    if self._queue.push(value):
                        # Value pushed, now wait for receiver to take it
                        while not self._queue.empty():
                            if timeout is not None and time.time() - start_time >= timeout:
                                # Timeout: try to take back our value
                                self._queue.pop()
                                return False
                            time.sleep(0.0001)
                        # Receiver took the value, rendezvous complete
                        return True
                    # If push failed (queue full), retry

                # Check timeout
                if timeout is not None:
                    if time.time() - start_time >= timeout:
                        return False

                # Brief sleep before retry
                time.sleep(0.001)

        finally:
            self._dec_sender_waiting()

    def receive(self, timeout: Optional[float] = None) -> Optional[T]:
        """
        Receive a value from the channel.

        Args:
            timeout: Timeout in seconds (None for blocking)

        Returns:
            Received value, or None if timeout or channel closed and empty

        Raises:
            ChannelClosed: If channel is closed and no data available
        """
        if self.unbuffered:
            return self._receive_unbuffered(timeout)
        else:
            return self._receive_buffered(timeout)

    def _receive_buffered(self, timeout: Optional[float] = None) -> Optional[T]:
        """Receive from buffered channel."""
        start_time = time.time() if timeout is not None else None

        while True:
            # Try to receive (non-blocking)
            result = self._queue.pop()
            if result is not None:
                return result

            # Check if closed and empty
            if self._is_closed() and self._queue.empty():
                return None

            # Check timeout
            if timeout is not None:
                if time.time() - start_time >= timeout:
                    return None

            # Brief sleep when queue is empty
            time.sleep(0.001)

    def _receive_unbuffered(self, timeout: Optional[float] = None) -> Optional[T]:
        """
        Receive from unbuffered channel (synchronous rendezvous).

        Algorithm:
        1. Increment receiver_waiting to signal readiness
        2. Wait for sender_waiting > 0 (sender is ready)
        3. Pop value from queue (sender will have pushed it)
        4. Decrement receiver_waiting
        5. Return value
        """
        start_time = time.time() if timeout is not None else None

        # Signal that we're waiting to receive
        self._inc_receiver_waiting()

        try:
            while True:
                # Check if closed
                if self._is_closed():
                    return None

                # Wait for a sender to be ready
                if self._get_sender_waiting() > 0:
                    # Try to pop the value
                    result = self._queue.pop()
                    if result is not None:
                        # Successfully received, rendezvous complete
                        return result
                    # If pop failed (queue empty), retry

                # Check timeout
                if timeout is not None:
                    if time.time() - start_time >= timeout:
                        return None

                # Brief sleep before retry
                time.sleep(0.001)

        finally:
            self._dec_receiver_waiting()

    def try_send(self, value: T) -> bool:
        """
        Try to send without blocking.

        Args:
            value: Value to send

        Returns:
            True if send succeeded immediately
        """
        if self._is_closed():
            return False  # Cannot send on closed channel

        if self.unbuffered:
            # For unbuffered, only succeed if receiver is waiting
            return self._get_receiver_waiting() > 0 and self._queue.push(value)
        else:
            # For buffered, succeed if queue has space
            return self._queue.push(value)

    def try_receive(self) -> Optional[T]:
        """
        Try to receive without blocking.

        Returns:
            Received value, or None if no data immediately available
        """
        if self.unbuffered:
            # For unbuffered, only succeed if sender is waiting
            if self._get_sender_waiting() > 0:
                return self._queue.pop()
            return None
        else:
            # For buffered, try to get from queue
            return self._queue.pop()

    def close(self):
        """Close the channel."""
        self._set_closed()

    def is_closed(self) -> bool:
        """Check if channel is closed."""
        return self._is_closed()

    def is_open(self) -> bool:
        """Check if channel is open."""
        return not self._is_closed()

    def size(self) -> int:
        """Get current number of messages in channel."""
        return self._queue.size()

    def empty(self) -> bool:
        """Check if channel is empty."""
        return self._queue.empty()

    def full(self) -> bool:
        """Check if channel is full."""
        if self.unbuffered:
            return False  # Unbuffered channels are never "full" in the traditional sense
        return self._queue.full()

    def __len__(self) -> int:
        """Get number of messages in channel."""
        return self.size()

    def __bool__(self) -> bool:
        """Check if channel is open and has data."""
        return not self.is_closed() and not self.empty()

    def __enter__(self):
        """Context manager entry."""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.close()

    def __iter__(self):
        """Iterator interface for receiving values until channel is closed."""
        while True:
            if self.is_closed() and self.empty():
                break
            value = self.receive(timeout=0.1)  # Short timeout to avoid blocking forever
            if value is not None:
                yield value
            elif self.is_closed():
                break

    def __str__(self) -> str:
        """String representation."""
        channel_type = "unbuffered" if self.unbuffered else f"buffered(capacity={self.capacity})"
        status = "closed" if self.is_closed() else "open"
        return (f"Channel(name='{self.name}', type={channel_type}, "
                f"status={status}, size={self.size()}, dtype={self.dtype})")

    def __repr__(self) -> str:
        """String representation."""
        return self.__str__()

    @staticmethod
    def select(channels: List['Channel'], timeout: Optional[float] = None) -> Optional['Channel']:
        """
        Select operation for non-blocking communication on multiple channels.

        Args:
            channels: List of channels to monitor
            timeout: Timeout in seconds (None for blocking)

        Returns:
            First ready channel, or None if timeout
        """
        start_time = time.time() if timeout is not None else None

        while True:
            # Try each channel in round-robin fashion
            for channel in channels:
                if channel.is_closed():
                    continue

                # Check if channel has data available without consuming it
                if not channel._queue.empty():
                    return channel

            # Check timeout
            if timeout is not None:
                if time.time() - start_time >= timeout:
                    return None

            # Brief sleep before trying again
            time.sleep(0.001)


class Select:
    """
    Select operation for non-blocking communication on multiple channels.

    This allows a process to wait on multiple channels simultaneously
    and proceed with the first one that becomes ready.
    """

    def __init__(self, channels: List[Channel], timeout: Optional[float] = None):
        """
        Initialize select operation.

        Args:
            channels: List of channels to monitor
            timeout: Timeout in seconds (None for blocking)
        """
        self.channels = channels
        self.timeout = timeout

    def select_receive(self) -> Optional[tuple]:
        """
        Select on receive operations.

        Returns:
            Tuple of (channel_index, value) for first ready channel,
            or None if timeout
        """
        start_time = time.time() if self.timeout is not None else None

        while True:
            # Try each channel in round-robin fashion
            for i, channel in enumerate(self.channels):
                if channel.is_closed():
                    continue

                value = channel.try_receive()
                if value is not None:
                    return (i, value)

            # Check timeout
            if self.timeout is not None:
                if time.time() - start_time >= self.timeout:
                    return None

            # Brief sleep before trying again
            time.sleep(0.001)

    def select_send(self, values: List[T]) -> Optional[int]:
        """
        Select on send operations.

        Args:
            values: List of values to send (one per channel)

        Returns:
            Index of channel that accepted send, or None if timeout
        """
        if len(values) != len(self.channels):
            raise ValueError("Number of values must match number of channels")

        start_time = time.time() if self.timeout is not None else None

        while True:
            # Try each channel in round-robin fashion
            for i, (channel, value) in enumerate(zip(self.channels, values)):
                if channel.is_closed():
                    continue

                if channel.try_send(value):
                    return i

            # Check timeout
            if self.timeout is not None:
                if time.time() - start_time >= self.timeout:
                    return None

            # Brief sleep before trying again
            time.sleep(0.001)


# Utility functions

def make_channel(memory: Memory, name: str, capacity: int = 0,
                dtype: Union[np.dtype, str, type] = np.int32) -> Channel:
    """
    Create a new channel with default parameters.

    Args:
        memory: Shared memory instance
        name: Channel name
        capacity: Buffer capacity (0 for unbuffered)
        dtype: Message data type

    Returns:
        New channel
    """
    return Channel(memory, name, capacity=capacity, dtype=dtype)


def make_unbuffered_channel(memory: Memory, name: str,
                           dtype: Union[np.dtype, str, type] = np.int32) -> Channel:
    """
    Create an unbuffered (synchronous) channel.

    Args:
        memory: Shared memory instance
        name: Channel name
        dtype: Message data type

    Returns:
        Unbuffered channel
    """
    return Channel(memory, name, capacity=0, dtype=dtype)


def make_buffered_channel(memory: Memory, name: str, capacity: int,
                         dtype: Union[np.dtype, str, type] = np.int32) -> Channel:
    """
    Create a buffered (asynchronous) channel.

    Args:
        memory: Shared memory instance
        name: Channel name
        capacity: Buffer capacity
        dtype: Message data type

    Returns:
        Buffered channel
    """
    return Channel(memory, name, capacity=capacity, dtype=dtype)