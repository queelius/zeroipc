package zeroipc

import (
	"encoding/binary"
	"errors"
	"fmt"
	"sync/atomic"
	"time"
	"unsafe"
)

// BarrierHeaderSize is the size of the barrier header in bytes.
// Layout: arrived(4) + generation(4) + num_participants(4) + padding(4) = 16 bytes
const BarrierHeaderSize = 16

// Barrier is a lock-free synchronization barrier for multiple processes.
//
// A barrier synchronizes N processes at a checkpoint. All participants must
// call Wait() before any can proceed. Once all arrive, the barrier releases
// all waiters simultaneously and automatically resets for the next cycle.
//
// Binary layout:
//   - arrived: int32 (atomic, offset 0) - number of processes that have arrived
//   - generation: int32 (atomic, offset 4) - generation counter for reusability
//   - num_participants: int32 (offset 8) - total participants required
//   - _padding: int32 (offset 12) - alignment padding
type Barrier struct {
	memory          *Memory
	name            string
	offset          int
	numParticipants int32
}

// NewBarrier creates a new barrier in shared memory.
func NewBarrier(memory *Memory, name string, numParticipants int32) (*Barrier, error) {
	if len(name) >= NameSize {
		return nil, errors.New("name too long (max 31 characters)")
	}

	if numParticipants <= 0 {
		return nil, errors.New("number of participants must be positive")
	}

	// Check if already exists
	if entry := memory.Find(name); entry != nil {
		return nil, fmt.Errorf("barrier '%s' already exists", name)
	}

	// Allocate space
	offset, err := memory.Allocate(name, BarrierHeaderSize)
	if err != nil {
		return nil, fmt.Errorf("failed to allocate: %w", err)
	}

	data := memory.Data()

	// Write header
	binary.LittleEndian.PutUint32(data[offset:], 0)                        // arrived
	binary.LittleEndian.PutUint32(data[offset+4:], 0)                      // generation
	binary.LittleEndian.PutUint32(data[offset+8:], uint32(numParticipants)) // num_participants
	binary.LittleEndian.PutUint32(data[offset+12:], 0)                     // padding

	return &Barrier{
		memory:          memory,
		name:            name,
		offset:          offset,
		numParticipants: numParticipants,
	}, nil
}

// OpenBarrier opens an existing barrier in shared memory.
func OpenBarrier(memory *Memory, name string) (*Barrier, error) {
	entry := memory.Find(name)
	if entry == nil {
		return nil, fmt.Errorf("barrier '%s' not found", name)
	}

	if entry.Size != BarrierHeaderSize {
		return nil, fmt.Errorf("invalid barrier size: %d, expected %d", entry.Size, BarrierHeaderSize)
	}

	offset := int(entry.Offset)
	data := memory.Data()

	numParticipants := int32(binary.LittleEndian.Uint32(data[offset+8:]))

	return &Barrier{
		memory:          memory,
		name:            name,
		offset:          offset,
		numParticipants: numParticipants,
	}, nil
}

// arrivedPtr returns a pointer to the atomic arrived counter.
func (b *Barrier) arrivedPtr() *int32 {
	return (*int32)(unsafe.Pointer(&b.memory.Data()[b.offset]))
}

// generationPtr returns a pointer to the atomic generation counter.
func (b *Barrier) generationPtr() *int32 {
	return (*int32)(unsafe.Pointer(&b.memory.Data()[b.offset+4]))
}

// Wait waits for all participants to arrive at the barrier.
// Blocks until all numParticipants processes have called Wait().
// Once all arrive, all waiters are released and the barrier resets.
func (b *Barrier) Wait() {
	// Capture current generation before arriving
	myGeneration := atomic.LoadInt32(b.generationPtr())

	// Atomically increment arrived counter
	arrived := atomic.AddInt32(b.arrivedPtr(), 1)

	if arrived == b.numParticipants {
		// Last to arrive - reset and release everyone
		atomic.StoreInt32(b.arrivedPtr(), 0)
		atomic.AddInt32(b.generationPtr(), 1)
	} else {
		// Not last - wait for generation to change
		backoff := time.Microsecond
		maxBackoff := time.Millisecond

		for atomic.LoadInt32(b.generationPtr()) == myGeneration {
			time.Sleep(backoff)
			if backoff < maxBackoff {
				backoff *= 2
			}
		}
	}
}

// WaitTimeout waits for all participants with a timeout.
// Returns true if barrier released, false if timed out.
//
// WARNING: If a timeout occurs, the barrier state may be inconsistent.
// Use with caution and ensure proper recovery coordination.
func (b *Barrier) WaitTimeout(timeout time.Duration) bool {
	deadline := time.Now().Add(timeout)

	// Capture current generation before arriving
	myGeneration := atomic.LoadInt32(b.generationPtr())

	// Atomically increment arrived counter
	arrived := atomic.AddInt32(b.arrivedPtr(), 1)

	if arrived == b.numParticipants {
		// Last to arrive - reset and release everyone
		atomic.StoreInt32(b.arrivedPtr(), 0)
		atomic.AddInt32(b.generationPtr(), 1)
		return true
	}

	// Not last - wait for generation to change or timeout
	backoff := time.Microsecond
	maxBackoff := time.Millisecond

	for atomic.LoadInt32(b.generationPtr()) == myGeneration {
		if time.Now().After(deadline) {
			// Timeout - decrement arrived count
			atomic.AddInt32(b.arrivedPtr(), -1)
			return false
		}

		time.Sleep(backoff)
		if backoff < maxBackoff {
			backoff *= 2
		}
	}

	return true
}

// Arrived returns the number of processes currently waiting at the barrier.
func (b *Barrier) Arrived() int32 {
	return atomic.LoadInt32(b.arrivedPtr())
}

// Generation returns the current generation number.
// The generation increments each time all participants pass through.
func (b *Barrier) Generation() int32 {
	return atomic.LoadInt32(b.generationPtr())
}

// NumParticipants returns the number of participants required.
func (b *Barrier) NumParticipants() int32 {
	return b.numParticipants
}

// Name returns the barrier name.
func (b *Barrier) Name() string {
	return b.name
}
