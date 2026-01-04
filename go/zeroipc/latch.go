package zeroipc

import (
	"encoding/binary"
	"errors"
	"fmt"
	"sync/atomic"
	"time"
	"unsafe"
)

// LatchHeaderSize is the size of the latch header in bytes.
// Layout: count(4) + initial_count(4) + padding(8) = 16 bytes
const LatchHeaderSize = 16

// Latch is a lock-free one-time countdown synchronization primitive.
//
// A latch counts down from an initial value to zero. Once it reaches zero,
// it stays at zero (one-time use). Any number of processes can Wait() for
// the count to reach zero, and any process can CountDown() to decrement it.
//
// Unlike Barrier which resets and cycles, Latch is single-use.
//
// Binary layout:
//   - count: int32 (atomic, offset 0) - current count
//   - initial_count: int32 (offset 4) - initial count (immutable)
//   - _padding: [2]int32 (offset 8) - alignment padding
//
// Common use cases:
//   - Start gate: count=1, workers Wait(), coordinator CountDown()
//   - Completion: count=N, each worker CountDown(), coordinator Wait()
type Latch struct {
	memory       *Memory
	name         string
	offset       int
	initialCount int32
}

// NewLatch creates a new latch in shared memory.
func NewLatch(memory *Memory, name string, count int32) (*Latch, error) {
	if len(name) >= NameSize {
		return nil, errors.New("name too long (max 31 characters)")
	}

	if count < 0 {
		return nil, errors.New("count must be non-negative")
	}

	// Check if already exists
	if entry := memory.Find(name); entry != nil {
		return nil, fmt.Errorf("latch '%s' already exists", name)
	}

	// Allocate space
	offset, err := memory.Allocate(name, LatchHeaderSize)
	if err != nil {
		return nil, fmt.Errorf("failed to allocate: %w", err)
	}

	data := memory.Data()

	// Write header
	binary.LittleEndian.PutUint32(data[offset:], uint32(count))   // count
	binary.LittleEndian.PutUint32(data[offset+4:], uint32(count)) // initial_count
	binary.LittleEndian.PutUint32(data[offset+8:], 0)             // padding[0]
	binary.LittleEndian.PutUint32(data[offset+12:], 0)            // padding[1]

	return &Latch{
		memory:       memory,
		name:         name,
		offset:       offset,
		initialCount: count,
	}, nil
}

// OpenLatch opens an existing latch in shared memory.
func OpenLatch(memory *Memory, name string) (*Latch, error) {
	entry := memory.Find(name)
	if entry == nil {
		return nil, fmt.Errorf("latch '%s' not found", name)
	}

	if entry.Size != LatchHeaderSize {
		return nil, fmt.Errorf("invalid latch size: %d, expected %d", entry.Size, LatchHeaderSize)
	}

	offset := int(entry.Offset)
	data := memory.Data()

	initialCount := int32(binary.LittleEndian.Uint32(data[offset+4:]))

	return &Latch{
		memory:       memory,
		name:         name,
		offset:       offset,
		initialCount: initialCount,
	}, nil
}

// countPtr returns a pointer to the atomic count.
func (l *Latch) countPtr() *int32 {
	return (*int32)(unsafe.Pointer(&l.memory.Data()[l.offset]))
}

// CountDown decrements the count by n (default 1).
// Saturates at 0 (never goes negative).
func (l *Latch) CountDown(n int32) error {
	if n <= 0 {
		return errors.New("count_down amount must be positive")
	}

	for {
		current := atomic.LoadInt32(l.countPtr())

		if current <= 0 {
			return nil // Already at 0
		}

		newCount := current - n
		if newCount < 0 {
			newCount = 0
		}

		if atomic.CompareAndSwapInt32(l.countPtr(), current, newCount) {
			return nil
		}
		// CAS failed, retry
	}
}

// CountDownOne decrements the count by 1.
func (l *Latch) CountDownOne() {
	_ = l.CountDown(1)
}

// Wait waits for the count to reach zero.
// If already at 0, returns immediately.
func (l *Latch) Wait() {
	backoff := time.Microsecond
	maxBackoff := time.Millisecond

	for atomic.LoadInt32(l.countPtr()) > 0 {
		time.Sleep(backoff)
		if backoff < maxBackoff {
			backoff *= 2
		}
	}
}

// TryWait checks if the count has reached zero without blocking.
func (l *Latch) TryWait() bool {
	return atomic.LoadInt32(l.countPtr()) == 0
}

// WaitTimeout waits for the count to reach zero with a timeout.
// Returns true if count reached 0, false if timed out.
func (l *Latch) WaitTimeout(timeout time.Duration) bool {
	deadline := time.Now().Add(timeout)
	backoff := time.Microsecond
	maxBackoff := time.Millisecond

	for atomic.LoadInt32(l.countPtr()) > 0 {
		if time.Now().After(deadline) {
			return false
		}

		time.Sleep(backoff)
		if backoff < maxBackoff {
			backoff *= 2
		}
	}

	return true
}

// Count returns the current count value.
func (l *Latch) Count() int32 {
	return atomic.LoadInt32(l.countPtr())
}

// InitialCount returns the initial count value.
func (l *Latch) InitialCount() int32 {
	return l.initialCount
}

// Name returns the latch name.
func (l *Latch) Name() string {
	return l.name
}

// Reset resets the latch to its initial count.
//
// WARNING: This is NOT thread-safe if other goroutines/processes are
// waiting or counting down. Only use with exclusive access.
func (l *Latch) Reset() {
	atomic.StoreInt32(l.countPtr(), l.initialCount)
}
