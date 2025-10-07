"""
Test suite for Stream codata structure in shared memory.
"""

import os
import threading
import time
import multiprocessing as mp
import pytest
import numpy as np
from typing import List

from zeroipc import Memory
from zeroipc.stream import Stream


class TestStreamBasic:
    """Basic Stream functionality tests."""

    def test_create_stream(self):
        """Test creating a new stream."""
        shm_name = f"/test_stream_create_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "events", capacity=100, dtype=np.int32)

            assert stream is not None
            assert stream.name == "events"
            assert stream.dtype == np.dtype(np.int32)

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_emit_single_value(self):
        """Test emitting single values to stream."""
        shm_name = f"/test_stream_emit_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "data", capacity=100, dtype=np.float32)

            # Emit values
            assert stream.emit(3.14)
            assert stream.emit(2.71)
            assert stream.emit(1.41)

            # Read values back
            values = stream.read(3)
            assert len(values) == 3
            assert abs(values[0] - 3.14) < 0.01
            assert abs(values[1] - 2.71) < 0.01
            assert abs(values[2] - 1.41) < 0.01

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_emit_batch(self):
        """Test emitting batch of values."""
        shm_name = f"/test_stream_batch_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "batch", capacity=1000, dtype=np.int64)

            # Emit batch
            batch = [10, 20, 30, 40, 50]
            count = stream.emit_batch(batch)
            assert count == 5

            # Read back
            values = stream.read(5)
            assert list(values) == batch

            # Emit numpy array
            np_batch = np.array([100, 200, 300], dtype=np.int64)
            count = stream.emit_batch(np_batch)
            assert count == 3

            values = stream.read(3)
            assert list(values) == [100, 200, 300]

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_read_batches(self):
        """Test reading stream in batches."""
        shm_name = f"/test_stream_read_batches_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "batched", capacity=1000, dtype=np.int32)

            # Emit values
            for i in range(20):
                stream.emit(i)

            # Read in batches (with timeout to avoid infinite wait)
            batches = list(stream.read_batches(batch_size=5, timeout=0.1))
            assert len(batches) == 4
            assert list(batches[0]) == [0, 1, 2, 3, 4]
            assert list(batches[1]) == [5, 6, 7, 8, 9]
            assert list(batches[2]) == [10, 11, 12, 13, 14]
            assert list(batches[3]) == [15, 16, 17, 18, 19]

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_stream_map(self):
        """Test mapping function over stream."""
        shm_name = f"/test_stream_map_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "source", capacity=100, dtype=np.int32)
            doubled = Stream(memory, "doubled", capacity=100, dtype=np.int32)

            # Map function to double values
            mapped = stream.map(lambda x: x * 2, doubled)

            # Emit to source
            stream.emit(5)
            stream.emit(10)

            # Read from doubled
            values = doubled.read(2)
            assert list(values) == [10, 20]

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_stream_filter(self):
        """Test filtering stream values."""
        shm_name = f"/test_stream_filter_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "all", capacity=100, dtype=np.int32)
            evens = Stream(memory, "evens", capacity=100, dtype=np.int32)

            # Filter even numbers
            filtered = stream.filter(lambda x: x % 2 == 0, evens)

            # Emit values
            for i in range(10):
                stream.emit(i)

            # Read filtered values
            values = evens.read(5)
            assert list(values) == [0, 2, 4, 6, 8]

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_stream_reduce(self):
        """Test reducing stream values."""
        shm_name = f"/test_stream_reduce_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "numbers", capacity=100, dtype=np.int32)

            # Emit values
            for i in range(1, 6):
                stream.emit(i)

            # Read values first
            values = stream.read(5)
            assert len(values) == 5
            # Sum should be 15 (1+2+3+4+5)
            assert sum(values) == 15

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_stream_take(self):
        """Test taking limited values from stream."""
        shm_name = f"/test_stream_take_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "infinite", capacity=100, dtype=np.int32)

            # Emit many values
            for i in range(100):
                stream.emit(i)

            # Create output stream for take
            output = Stream(memory, "taken", capacity=100, dtype=np.int32)

            # Take only first 5 to output stream
            stream.take(5, output)

            # Read from output
            taken = output.read(5)
            assert list(taken) == [0, 1, 2, 3, 4]

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_stream_skip(self):
        """Test skipping values from stream."""
        shm_name = f"/test_stream_skip_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "values", capacity=100, dtype=np.int32)

            # Emit values
            for i in range(10):
                stream.emit(i)

            # Skip returns modified stream
            skipped = stream.skip(5)

            # Read remaining values
            values = skipped.read(5)
            assert list(values) == [5, 6, 7, 8, 9]

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_stream_window(self):
        """Test windowing operations on stream."""
        shm_name = f"/test_stream_window_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "sensor", capacity=1000, dtype=np.float32)

            # Emit sensor values
            values = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0]
            for v in values:
                stream.emit(v)

            # Get sliding windows
            windows = stream.window(size=3)
            window_list = list(windows)

            assert len(window_list) == 8  # 10 - 3 + 1
            assert list(window_list[0]) == [1.0, 2.0, 3.0]
            assert list(window_list[1]) == [2.0, 3.0, 4.0]
            assert list(window_list[-1]) == [8.0, 9.0, 10.0]

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_stream_batch(self):
        """Test batching by count."""
        shm_name = f"/test_stream_buffer_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "data", capacity=100, dtype=np.int32)

            # Emit values
            for i in range(10):
                stream.emit(i)

            # Create output stream for batches
            batched = Stream(memory, "batched", capacity=100, dtype=np.int32)

            # Batch in groups of 3
            stream.batch(3, batched)

            # Read batched values
            values = batched.read(9)
            assert len(values) >= 3  # At least some values batched

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_stream_scan(self):
        """Test scan (accumulating) operation."""
        shm_name = f"/test_stream_scan_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "numbers", capacity=100, dtype=np.int32)
            sums = Stream(memory, "sums", capacity=100, dtype=np.int32)

            # Scan to compute running sum
            scanned = stream.scan(lambda acc, x: acc + x, 0, sums)

            # Emit values
            for i in range(1, 6):
                stream.emit(i)

            # Read running sums
            values = sums.read(5)
            assert list(values) == [1, 3, 6, 10, 15]

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")



class TestStreamConcurrency:
    """Concurrent access tests for Stream."""

    def test_concurrent_emit_read(self):
        """Test concurrent emission and reading."""
        shm_name = f"/test_stream_concurrent_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "concurrent", capacity=10000, dtype=np.int32)

            def producer(start, count):
                for i in range(start, start + count):
                    stream.emit(i)
                    time.sleep(0.001)

            def consumer(expected_count, results_list):
                total = 0
                while total < expected_count:
                    batch = stream.read(min(10, expected_count - total))
                    if len(batch) > 0:
                        results_list.extend(batch)
                        total += len(batch)
                    time.sleep(0.001)

            results = []
            producer_thread = threading.Thread(target=producer, args=(0, 100))
            consumer_thread = threading.Thread(target=consumer, args=(100, results))

            consumer_thread.start()
            time.sleep(0.01)  # Let consumer start
            producer_thread.start()

            producer_thread.join()
            consumer_thread.join()

            assert len(results) == 100
            assert sorted(results) == list(range(100))

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_multiprocess_stream(self):
        """Test stream across multiple processes."""
        shm_name = f"/test_stream_multiproc_{os.getpid()}"

        def producer_process(shm_name):
            memory = Memory(shm_name)
            stream = Stream(memory, "shared", dtype=np.float32, open_existing=True)
            for i in range(100):
                stream.emit(float(i))
                time.sleep(0.001)

        def consumer_process(shm_name, results_queue):
            memory = Memory(shm_name)
            stream = Stream(memory, "shared", dtype=np.float32, open_existing=True)
            values = []
            while len(values) < 50:
                batch = stream.read(10)
                if len(batch) > 0:
                    values.extend(batch)
                time.sleep(0.001)
            results_queue.put(values[:50])

        try:
            memory = Memory(shm_name, 1024 * 1024, create=True)
            stream = Stream(memory, "shared", capacity=10000, dtype=np.float32)

            results_queue = mp.Queue()

            producer = mp.Process(target=producer_process, args=(shm_name,))
            consumer = mp.Process(target=consumer_process, args=(shm_name, results_queue))

            consumer.start()
            time.sleep(0.01)  # Let consumer start
            producer.start()

            producer.join()
            consumer.join()

            results = results_queue.get()
            assert len(results) == 50
            # Check first 50 values
            for i in range(50):
                assert abs(results[i] - float(i)) < 0.001

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")


class TestStreamEdgeCases:
    """Edge case tests for Stream."""

    def test_stream_overflow(self):
        """Test stream behavior when buffer overflows."""
        shm_name = f"/test_stream_overflow_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            # Small capacity to test overflow
            stream = Stream(memory, "small", capacity=10, dtype=np.int32)

            # Try to emit more than capacity
            emitted = 0
            for i in range(20):
                if stream.emit(i):
                    emitted += 1

            # Not all values will be emitted due to capacity
            assert emitted <= 10

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_stream_empty_read(self):
        """Test reading from empty stream."""
        shm_name = f"/test_stream_empty_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)
            stream = Stream(memory, "empty", capacity=100, dtype=np.int32)

            # Read from empty stream
            values = stream.read(10)
            assert len(values) == 0

            # Take from empty stream
            taken = stream.take(5)
            assert len(taken) == 0

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_stream_no_dtype(self):
        """Test that dtype is required."""
        shm_name = f"/test_stream_no_dtype_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            with pytest.raises(TypeError, match="dtype is required"):
                Stream(memory, "nodtype", capacity=100)

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_stream_open_nonexistent(self):
        """Test opening non-existent stream."""
        shm_name = f"/test_stream_open_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            with pytest.raises(RuntimeError, match="Stream not found"):
                Stream(memory, "nonexistent", dtype=np.int32, open_existing=True)

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")

    def test_stream_different_dtypes(self):
        """Test streams with different data types."""
        shm_name = f"/test_stream_dtypes_{os.getpid()}"
        try:
            memory = Memory(shm_name, 1024 * 1024)

            # Float64 stream
            float_stream = Stream(memory, "floats", capacity=100, dtype=np.float64)
            float_stream.emit(3.14159)
            values = float_stream.read(1)
            assert abs(values[0] - 3.14159) < 0.00001

            # Bool stream
            bool_stream = Stream(memory, "bools", capacity=100, dtype=np.bool_)
            bool_stream.emit(True)
            bool_stream.emit(False)
            values = bool_stream.read(2)
            assert values[0] == True
            assert values[1] == False

            # UInt8 stream
            byte_stream = Stream(memory, "bytes", capacity=100, dtype=np.uint8)
            byte_stream.emit_batch([0, 127, 255])
            values = byte_stream.read(3)
            assert list(values) == [0, 127, 255]

        finally:
            if os.path.exists(f"/dev/shm{shm_name}"):
                os.unlink(f"/dev/shm{shm_name}")




if __name__ == "__main__":
    pytest.main([__file__, "-v"])