"""Format v2 layout regression tests.

The queue's sequence array and the stack's slot-state array must start at
header(16) + align8(elem_size * capacity). These tests recompute that offset
from the spec formula and verify, via raw reads of the shared-memory buffer
at the computed address, that the implementation actually placed the side
arrays there.

When the C FFI backend is loaded, Stack.push/pop delegate to c/src/ffi.c, so
the stack test also pins ffi.c's copy of the layout to the same formula.
"""

import os
import struct

import numpy as np
import pytest

from zeroipc import Memory, Queue, Stack

HEADER_SIZE = 16
SLOT_EMPTY = 0
SLOT_READY = 2


def spec_align8(n: int) -> int:
    """The spec formula, written independently of the implementation's
    _align8 so a drift in either shows up."""
    return (n + 7) & ~7


@pytest.fixture
def mem():
    name = f"/test_layout_{os.getpid()}"
    m = Memory(name, 1024 * 1024)
    yield m
    m.close()
    m.unlink()


@pytest.mark.parametrize(
    "dtype",
    [np.int8, np.uint32, np.float64],
    ids=["elem1", "elem4", "elem8"],
)
def test_queue_side_array_at_8_byte_boundary(mem, dtype):
    capacity = 5
    elem_size = np.dtype(dtype).itemsize
    name = f"q_{elem_size}"
    queue = Queue(mem, name, capacity=capacity, dtype=dtype)

    # The queue rounds the requested capacity up to a power of two
    # (wrap-safety); the layout is computed from the actual capacity.
    actual_cap = queue.capacity
    assert actual_cap >= capacity
    assert actual_cap & (actual_cap - 1) == 0

    entry = mem.table.find(name)
    assert entry is not None

    side_off = HEADER_SIZE + spec_align8(elem_size * actual_cap)
    assert entry.size == side_off + actual_cap * 4

    # Vyukov invariant: seq[i] == i after creation. Finding those values at
    # the spec-computed offset proves placement.
    for i in range(actual_cap):
        seq = struct.unpack_from("<I", mem.data, entry.offset + side_off + i * 4)[0]
        assert seq == i, f"seq[{i}] not at spec offset for elem_size {elem_size}"


@pytest.mark.parametrize(
    "dtype",
    [np.int8, np.uint32, np.float64],
    ids=["elem1", "elem4", "elem8"],
)
def test_stack_side_array_at_8_byte_boundary(mem, dtype):
    capacity = 5
    elem_size = np.dtype(dtype).itemsize
    name = f"s_{elem_size}"
    stack = Stack(mem, name, capacity=capacity, dtype=dtype)

    entry = mem.table.find(name)
    assert entry is not None

    side_off = HEADER_SIZE + spec_align8(elem_size * capacity)
    assert entry.size == side_off + capacity * 4

    # Push two, then read the slot states at the spec-computed offset.
    assert stack.push(1)
    assert stack.push(2)

    def state_at(i):
        return struct.unpack_from("<I", mem.data, entry.offset + side_off + i * 4)[0]

    assert state_at(0) == SLOT_READY
    assert state_at(1) == SLOT_READY
    assert state_at(2) == SLOT_EMPTY

    assert stack.pop() is not None
    assert state_at(1) == SLOT_EMPTY, "popped slot not recycled at spec offset"
