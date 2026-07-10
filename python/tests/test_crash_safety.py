"""Crash-safety tests: bounded spin on a stuck slot.

A peer that dies mid-operation leaves its slot state permanently claimed
(WRITING/READING). These tests simulate such a ghost by poking the
slot-state array directly, then assert push/pop/top fail within the bounded
spin instead of hanging, undo their top reservation (no silent item loss),
and that the stack works again once the slot is repaired.

Runs against whichever backend is loaded: with the C FFI present this
exercises c/src/ffi.c's bounded spin; with ZEROIPC_FFI_LIB=none it
exercises the pure-Python fallback's.
"""

import os
import struct

import numpy as np
import pytest

from zeroipc import Memory, Stack

HEADER_SIZE = 16
SLOT_EMPTY = 0
SLOT_WRITING = 1
SLOT_READY = 2
SLOT_READING = 3


def _align8(n: int) -> int:
    return (n + 7) & ~7


@pytest.fixture
def mem():
    name = f"/test_crash_{os.getpid()}"
    m = Memory(name, 1024 * 1024)
    yield m
    m.close()
    m.unlink()


def _state_offset(mem, name, elem_size, capacity, i):
    entry = mem.table.find(name)
    assert entry is not None
    side_off = HEADER_SIZE + _align8(elem_size * capacity)
    return entry.offset + side_off + i * 4


def _poke_state(mem, off, value):
    struct.pack_into("<I", mem.data, off, value)


def test_stack_crashed_peer_bounded_spin(mem):
    stack = Stack(mem, "s", capacity=8, dtype=np.int32)

    assert stack.push(1)
    assert stack.push(2)

    state1 = _state_offset(mem, "s", 4, 8, 1)

    # Ghost pusher died mid-write: slot 1 stuck in WRITING.
    _poke_state(mem, state1, SLOT_WRITING)
    assert stack.pop() is None  # bounded bail-out, not a hang
    assert stack.size() == 2  # undo restored top; nothing lost

    # Repair; both values drain.
    _poke_state(mem, state1, SLOT_READY)
    assert stack.pop() == 2
    assert stack.pop() == 1

    # Ghost popper holding the slot above top: push fails bounded, top restored.
    assert stack.push(1)
    _poke_state(mem, state1, SLOT_READING)
    assert not stack.push(2)
    assert stack.size() == 1

    _poke_state(mem, state1, SLOT_EMPTY)
    assert stack.push(2)
    assert stack.size() == 2


def test_stack_top_bails_out_on_stuck_slot(mem):
    stack = Stack(mem, "t", capacity=8, dtype=np.int32)
    assert stack.push(42)

    state0 = _state_offset(mem, "t", 4, 8, 0)
    _poke_state(mem, state0, SLOT_WRITING)
    assert stack.top() is None

    _poke_state(mem, state0, SLOT_READY)
    assert stack.top() == 42
