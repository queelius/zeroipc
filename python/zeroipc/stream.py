"""
Reactive stream for event-driven data processing in shared memory.

This module provides a Stream data structure that implements reactive
programming patterns, allowing processes to emit, transform, and consume
data in a push-based model.
"""

from typing import Optional, TypeVar, Union, List, Callable, Iterator
import numpy as np
import time

from .memory import Memory
from .ring import Ring

T = TypeVar('T')
U = TypeVar('U')


class Stream:
    """
    Reactive stream for event-driven data processing in shared memory.

    Stream<T> implements reactive programming patterns in shared memory, allowing
    processes to emit, transform, and consume data in a push-based model. This is
    inspired by ReactiveX (Rx) and enables functional reactive programming (FRP)
    across process boundaries.

    Example:
        # Producer process: Emit sensor data
        mem = Memory("/sensors", 10*1024*1024)
        sensor_stream = Stream(mem, "temperature", capacity=1024, dtype=np.float64)

        # Emit data
        for temp in sensor_readings():
            sensor_stream.emit(temp)

        # Consumer process: Process stream
        mem = Memory("/sensors")
        sensor_stream = Stream(mem, "temperature", open_existing=True, dtype=np.float64)

        # Read and process data
        for temp_batch in sensor_stream.read_batches(batch_size=10):
            process_temperatures(temp_batch)

        # Functional operations
        hot_temps = sensor_stream.filter(lambda x: x > 30.0)
        avg_temp = sensor_stream.window(100).map(lambda window: np.mean(window))
    """

    def __init__(self, memory: Memory, name: str,
                 capacity: Optional[int] = None,
                 dtype: Optional[Union[np.dtype, str, type]] = None,
                 open_existing: bool = False):
        """
        Create or open a reactive stream.

        Args:
            memory: Shared memory instance
            name: Stream name
            capacity: Ring buffer capacity in bytes (required for creation)
            dtype: Element data type
            open_existing: If True, open existing stream; if False, create new

        Raises:
            ValueError: If required parameters are missing
            TypeError: If data type is not specified
            RuntimeError: If stream is not found when opening existing
        """
        if dtype is None:
            raise TypeError("dtype is required")

        self.memory = memory
        self.name = name
        self.dtype = np.dtype(dtype)

        # Use Ring buffer as underlying storage
        if open_existing:
            # Try to find existing stream
            entry = memory.table.find(name)
            if entry is None:
                raise RuntimeError(f"Stream not found: {name}")

        self._ring = Ring(memory, name, capacity, dtype)

    def emit(self, value: T) -> bool:
        """
        Emit a single value to the stream.

        Args:
            value: Value to emit

        Returns:
            True if emission succeeded
        """
        return self._ring.write(value) > 0

    def emit_batch(self, values: Union[List[T], np.ndarray]) -> int:
        """
        Emit a batch of values to the stream.

        Args:
            values: Values to emit

        Returns:
            Number of elements written
        """
        bytes_written = self._ring.write(values)
        return bytes_written // self.dtype.itemsize

    def read(self, max_elements: Optional[int] = None) -> Optional[np.ndarray]:
        """
        Read available data from the stream.

        Args:
            max_elements: Maximum number of elements to read

        Returns:
            Array of read data, or None if no data available
        """
        max_bytes = None
        if max_elements is not None:
            max_bytes = max_elements * self.dtype.itemsize

        return self._ring.read(max_bytes)

    def peek(self, max_elements: Optional[int] = None) -> Optional[np.ndarray]:
        """
        Peek at available data without consuming it.

        Args:
            max_elements: Maximum number of elements to peek

        Returns:
            Array of data, or None if no data available
        """
        max_bytes = None
        if max_elements is not None:
            max_bytes = max_elements * self.dtype.itemsize

        return self._ring.peek(max_bytes)

    def read_batches(self, batch_size: int, timeout: Optional[float] = None) -> Iterator[np.ndarray]:
        """
        Read data in batches as an iterator.

        Args:
            batch_size: Number of elements per batch
            timeout: Timeout in seconds (None for blocking)

        Yields:
            Batches of data as numpy arrays
        """
        start_time = time.time() if timeout is not None else None

        while True:
            data = self.read(batch_size)

            if data is not None and len(data) > 0:
                yield data
                if timeout is not None:
                    start_time = time.time()  # Reset timeout after successful read
            else:
                if timeout is not None:
                    if time.time() - start_time >= timeout:
                        break
                time.sleep(0.001)  # Brief sleep when no data available

    def available(self) -> int:
        """Get number of elements available for reading."""
        return self._ring.available_read() // self.dtype.itemsize

    def empty(self) -> bool:
        """Check if stream has no data to read."""
        return self._ring.empty()

    def capacity_bytes(self) -> int:
        """Get stream capacity in bytes."""
        return self._ring.capacity

    def capacity_elements(self) -> int:
        """Get stream capacity in elements."""
        return self._ring.capacity // self.dtype.itemsize

    # Reactive operators (functional programming style)

    def map(self, transform: Callable[[T], U], output_stream: 'Stream') -> 'Stream':
        """
        Transform stream elements using a function.

        Args:
            transform: Function to apply to each element
            output_stream: Stream to write transformed values

        Returns:
            Output stream for chaining

        Note: This is a blocking operation that processes all available data
        """
        data = self.read()
        if data is not None:
            transformed = np.array([transform(x) for x in data], dtype=output_stream.dtype)
            output_stream.emit_batch(transformed)

        return output_stream

    def filter(self, predicate: Callable[[T], bool], output_stream: 'Stream') -> 'Stream':
        """
        Filter stream elements using a predicate.

        Args:
            predicate: Function that returns True for elements to keep
            output_stream: Stream to write filtered values

        Returns:
            Output stream for chaining
        """
        data = self.read()
        if data is not None:
            filtered = np.array([x for x in data if predicate(x)], dtype=self.dtype)
            if len(filtered) > 0:
                output_stream.emit_batch(filtered)

        return output_stream

    def take(self, count: int, output_stream: 'Stream') -> 'Stream':
        """
        Take first N elements from stream.

        Args:
            count: Number of elements to take
            output_stream: Stream to write taken values

        Returns:
            Output stream for chaining
        """
        data = self.read(count)
        if data is not None:
            output_stream.emit_batch(data[:count])

        return output_stream

    def skip(self, count: int) -> 'Stream':
        """
        Skip first N elements from stream.

        Args:
            count: Number of elements to skip

        Returns:
            Self for chaining
        """
        self.read(count)  # Read and discard
        return self

    def window(self, size: int) -> Iterator[np.ndarray]:
        """
        Create sliding windows of elements.

        Args:
            size: Size of each window

        Yields:
            Windows as numpy arrays

        Note: This reads all currently available data and produces windows
        """
        # Read all available data at once
        data = self.read()
        if data is None or len(data) < size:
            return

        # Generate sliding windows
        for i in range(len(data) - size + 1):
            yield data[i:i + size]

    def batch(self, size: int, output_stream: 'Stream') -> 'Stream':
        """
        Group elements into fixed-size batches.

        Args:
            size: Batch size
            output_stream: Stream to write batches

        Returns:
            Output stream for chaining

        Note: This reads all currently available data and batches it
        """
        data = self.read()
        if data is not None:
            # Group into fixed-size batches
            for i in range(0, len(data), size):
                batch = data[i:i + size]
                if len(batch) == size:  # Only emit complete batches
                    output_stream.emit_batch(batch)

        return output_stream

    def reduce(self, reducer: Callable[[U, T], U], initial: U) -> U:
        """
        Reduce stream to a single value.

        Args:
            reducer: Function to combine accumulator and current element
            initial: Initial accumulator value

        Returns:
            Final reduced value

        Note: This processes all currently available data
        """
        accumulator = initial
        data = self.read()

        if data is not None:
            for element in data:
                accumulator = reducer(accumulator, element)

        return accumulator

    def scan(self, scanner: Callable[[U, T], U], initial: U, output_stream: 'Stream') -> 'Stream':
        """
        Scan stream producing intermediate results.

        Args:
            scanner: Function to combine accumulator and current element
            initial: Initial accumulator value
            output_stream: Stream to write intermediate results

        Returns:
            Output stream for chaining
        """
        accumulator = initial
        data = self.read()

        if data is not None:
            results = []
            for element in data:
                accumulator = scanner(accumulator, element)
                results.append(accumulator)

            if results:
                output_stream.emit_batch(np.array(results, dtype=output_stream.dtype))

        return output_stream

    # Statistics and aggregation

    def mean(self) -> Optional[float]:
        """Calculate mean of available elements."""
        data = self.peek()  # Don't consume data
        return float(np.mean(data)) if data is not None and len(data) > 0 else None

    def std(self) -> Optional[float]:
        """Calculate standard deviation of available elements."""
        data = self.peek()  # Don't consume data
        return float(np.std(data)) if data is not None and len(data) > 0 else None

    def min(self) -> Optional[T]:
        """Get minimum of available elements."""
        data = self.peek()  # Don't consume data
        return data.min() if data is not None and len(data) > 0 else None

    def max(self) -> Optional[T]:
        """Get maximum of available elements."""
        data = self.peek()  # Don't consume data
        return data.max() if data is not None and len(data) > 0 else None

    def sum(self) -> Optional[T]:
        """Get sum of available elements."""
        data = self.peek()  # Don't consume data
        return data.sum() if data is not None and len(data) > 0 else None

    def reset(self):
        """Reset the stream (clear all data)."""
        self._ring.reset()

    def __iter__(self) -> Iterator[T]:
        """Iterate over stream elements."""
        while True:
            data = self.read(1)
            if data is None or len(data) == 0:
                time.sleep(0.001)  # Brief sleep when no data
                continue
            yield data[0]

    def __len__(self) -> int:
        """Get number of available elements."""
        return self.available()

    def __bool__(self) -> bool:
        """Check if stream has data."""
        return not self.empty()

    def __str__(self) -> str:
        """String representation."""
        return (f"Stream(name='{self.name}', available={self.available()}, "
                f"capacity={self.capacity_elements()}, dtype={self.dtype})")

    def __repr__(self) -> str:
        """String representation."""
        return self.__str__()


# Utility functions for creating streams

def create_number_stream(memory: Memory, name: str, start: float = 0.0, step: float = 1.0,
                        capacity: int = 1024) -> Stream:
    """
    Create a stream that generates sequential numbers.

    Args:
        memory: Shared memory instance
        name: Stream name
        start: Starting number
        step: Step between numbers
        capacity: Stream capacity

    Returns:
        Stream that can generate numbers
    """
    stream = Stream(memory, name, capacity=capacity * 8, dtype=np.float64)  # 8 bytes per float64

    # Generate initial batch
    numbers = np.arange(start, start + capacity * step, step, dtype=np.float64)
    stream.emit_batch(numbers)

    return stream


def create_random_stream(memory: Memory, name: str, size: int = 1000,
                        capacity: int = 1024, seed: Optional[int] = None) -> Stream:
    """
    Create a stream with random data.

    Args:
        memory: Shared memory instance
        name: Stream name
        size: Number of random values to generate
        capacity: Stream capacity
        seed: Random seed for reproducibility

    Returns:
        Stream with random data
    """
    if seed is not None:
        np.random.seed(seed)

    stream = Stream(memory, name, capacity=capacity * 8, dtype=np.float64)

    # Generate random data
    random_data = np.random.random(size)
    stream.emit_batch(random_data)

    return stream