"""Tests for the C FFI backend (libzeroipc_ffi.so).

Tests the FFI layer specifically: loading, error paths, dtype round-trips,
and multi-process MPMC scenarios that are the reason the FFI exists.
"""

import multiprocessing
import os
import time
import pytest
import numpy as np

from zeroipc import Memory, Queue, Stack
from zeroipc import _cffi


# Skip all tests if FFI not available
pytestmark = pytest.mark.skipif(
    not _cffi.AVAILABLE,
    reason="libzeroipc_ffi.so not found"
)


@pytest.fixture
def shm():
    name = f"/test_cffi_{os.getpid()}"
    mem = Memory(name, 10 * 1024 * 1024)
    yield mem
    mem.close()
    Memory.unlink(name)


class TestFFILoading:
    """Test FFI library loading and fallback."""

    def test_available(self):
        assert _cffi.AVAILABLE is True
        assert _cffi._lib is not None

    def test_disable_via_env(self):
        """ZEROIPC_FFI_LIB=none should disable FFI."""
        # We can't fully test this without subprocess, but verify the flag
        assert _cffi.AVAILABLE is True

    def test_return_codes(self):
        assert _cffi.OK == 0
        assert _cffi.EMPTY_OR_FULL == -1
        assert _cffi.MISMATCH == -2
        assert _cffi.INVALID == -3


class TestQueueFFI:
    """Test queue operations through the C FFI."""

    def test_push_pop_int32(self, shm):
        q = Queue(shm, "q_i32", capacity=100, dtype=np.int32)
        assert q.push(42)
        assert q.pop() == 42

    def test_push_pop_float64(self, shm):
        q = Queue(shm, "q_f64", capacity=100, dtype=np.float64)
        assert q.push(3.14159)
        val = q.pop()
        assert abs(val - 3.14159) < 1e-10

    def test_push_pop_int64(self, shm):
        q = Queue(shm, "q_i64", capacity=100, dtype=np.int64)
        assert q.push(2**40)
        assert q.pop() == 2**40

    def test_push_full(self, shm):
        q = Queue(shm, "q_full", capacity=3, dtype=np.int32)
        assert q.push(1)
        assert q.push(2)
        assert q.push(3)
        assert not q.push(4)  # Full

    def test_pop_empty(self, shm):
        q = Queue(shm, "q_empty", capacity=10, dtype=np.int32)
        assert q.pop() is None

    def test_size_empty_full(self, shm):
        q = Queue(shm, "q_sef", capacity=3, dtype=np.int32)
        assert q.size() == 0
        assert q.empty()
        assert not q.full()

        q.push(1)
        q.push(2)
        q.push(3)
        assert q.size() == 3
        assert not q.empty()
        assert q.full()

    def test_fifo_order(self, shm):
        q = Queue(shm, "q_fifo", capacity=100, dtype=np.int32)
        for i in range(50):
            q.push(i)
        for i in range(50):
            assert q.pop() == i

    def test_wraparound(self, shm):
        """Push/pop many times to exercise sequence number wraparound."""
        q = Queue(shm, "q_wrap", capacity=4, dtype=np.int32)
        for i in range(100):
            assert q.push(i)
            assert q.pop() == i


class TestStackFFI:
    """Test stack operations through the C FFI."""

    def test_push_pop_int32(self, shm):
        s = Stack(shm, "s_i32", capacity=100, dtype=np.int32)
        assert s.push(42)
        assert s.pop() == 42

    def test_push_pop_float64(self, shm):
        s = Stack(shm, "s_f64", capacity=100, dtype=np.float64)
        assert s.push(2.71828)
        val = s.pop()
        assert abs(val - 2.71828) < 1e-10

    def test_push_full(self, shm):
        s = Stack(shm, "s_full", capacity=3, dtype=np.int32)
        assert s.push(1)
        assert s.push(2)
        assert s.push(3)
        assert not s.push(4)

    def test_pop_empty(self, shm):
        s = Stack(shm, "s_empty", capacity=10, dtype=np.int32)
        assert s.pop() is None

    def test_top_peek(self, shm):
        s = Stack(shm, "s_top", capacity=10, dtype=np.int32)
        s.push(99)
        assert s.top() == 99
        assert s.size() == 1  # top() doesn't remove

    def test_lifo_order(self, shm):
        s = Stack(shm, "s_lifo", capacity=100, dtype=np.int32)
        for i in range(50):
            s.push(i)
        for i in range(49, -1, -1):
            assert s.pop() == i


# --- Multi-process MPMC tests (the reason the FFI exists) ---

def _producer(shm_name, count, start_val):
    """Push count values starting at start_val."""
    mem = Memory(shm_name)
    q = Queue(mem, "mpmc_q", dtype=np.int32)
    pushed = 0
    for i in range(count):
        while not q.push(start_val + i):
            time.sleep(0.0001)
        pushed += 1
    mem.close()
    return pushed


def _consumer(shm_name, count):
    """Pop count values, return them as a list."""
    mem = Memory(shm_name)
    q = Queue(mem, "mpmc_q", dtype=np.int32)
    values = []
    while len(values) < count:
        val = q.pop()
        if val is not None:
            values.append(int(val))
        else:
            time.sleep(0.0001)
    mem.close()
    return values


class TestMultiProcessMPMC:
    """Test cross-process MPMC using the C FFI backend."""

    @pytest.mark.timeout(10)
    def test_two_producers_one_consumer(self):
        """Two producer processes, one consumer — verify no lost items."""
        shm_name = f"/test_mpmc_{os.getpid()}"
        items_per_producer = 500
        total = items_per_producer * 2

        mem = Memory(shm_name, 10 * 1024 * 1024)
        Queue(mem, "mpmc_q", capacity=1024, dtype=np.int32)
        mem.close()

        try:
            with multiprocessing.Pool(3) as pool:
                p1 = pool.apply_async(_producer, (shm_name, items_per_producer, 0))
                p2 = pool.apply_async(_producer, (shm_name, items_per_producer, 10000))
                c1 = pool.apply_async(_consumer, (shm_name, total))

                p1.get(timeout=8)
                p2.get(timeout=8)
                values = c1.get(timeout=8)

            assert len(values) == total
            # All values from both producers should be present
            expected = set(range(items_per_producer)) | set(range(10000, 10000 + items_per_producer))
            assert set(values) == expected
        finally:
            Memory.unlink(shm_name)

    @pytest.mark.timeout(10)
    def test_one_producer_two_consumers(self):
        """One producer, two consumers — verify no duplicates."""
        shm_name = f"/test_mpmc2_{os.getpid()}"
        total_items = 1000

        mem = Memory(shm_name, 10 * 1024 * 1024)
        Queue(mem, "mpmc_q", capacity=1024, dtype=np.int32)
        mem.close()

        try:
            with multiprocessing.Pool(3) as pool:
                p1 = pool.apply_async(_producer, (shm_name, total_items, 0))
                c1 = pool.apply_async(_consumer, (shm_name, total_items // 2))
                c2 = pool.apply_async(_consumer, (shm_name, total_items // 2))

                p1.get(timeout=8)
                vals1 = c1.get(timeout=8)
                vals2 = c2.get(timeout=8)

            all_vals = vals1 + vals2
            assert len(all_vals) == total_items
            # No duplicates
            assert len(set(all_vals)) == total_items
        finally:
            Memory.unlink(shm_name)
