package zeroipc

import (
	"encoding/binary"
	"errors"
	"fmt"
	"sync/atomic"
	"time"
	"unsafe"
)

// SemaphoreHeaderSize is the size of the semaphore header in bytes.
// Layout: count(4) + waiting(4) + max_count(4) + padding(4) = 16 bytes
const SemaphoreHeaderSize = 16

// Semaphore is a lock-free counting semaphore for cross-process synchronization.
//
// Supports three modes:
//   - Binary semaphore (max_count=1): Acts as a mutex
//   - Counting semaphore (max_count=N): Resource pool with N permits
//   - Unbounded semaphore (max_count=0): No upper limit on count
//
// Binary layout:
//   - count: int32 (atomic, offset 0) - current count
//   - waiting: int32 (atomic, offset 4) - number of waiting processes
//   - max_count: int32 (offset 8) - maximum count (0 = unbounded)
//   - _padding: int32 (offset 12) - alignment padding
type Semaphore struct {
	memory   *Memory
	name     string
	offset   int
	maxCount int32
}

// NewSemaphore creates a new semaphore in shared memory.
//
// Parameters:
//   - initialCount: Initial value for the semaphore count (must be non-negative)
//   - maxCount: Maximum count (0 for unbounded, 1 for binary/mutex-like behavior)
func NewSemaphore(memory *Memory, name string, initialCount, maxCount int32) (*Semaphore, error) {
	if len(name) >= NameSize {
		return nil, errors.New("name too long (max 31 characters)")
	}

	if initialCount < 0 {
		return nil, errors.New("initial count must be non-negative")
	}

	if maxCount < 0 {
		return nil, errors.New("max count must be non-negative (0 for unbounded)")
	}

	if maxCount > 0 && initialCount > maxCount {
		return nil, errors.New("initial count cannot exceed max count")
	}

	// Check if already exists
	if entry := memory.Find(name); entry != nil {
		return nil, fmt.Errorf("semaphore '%s' already exists", name)
	}

	// Allocate space
	offset, err := memory.Allocate(name, SemaphoreHeaderSize)
	if err != nil {
		return nil, fmt.Errorf("failed to allocate: %w", err)
	}

	data := memory.Data()

	// Write header
	binary.LittleEndian.PutUint32(data[offset:], uint32(initialCount))  // count
	binary.LittleEndian.PutUint32(data[offset+4:], 0)                   // waiting
	binary.LittleEndian.PutUint32(data[offset+8:], uint32(maxCount))    // max_count
	binary.LittleEndian.PutUint32(data[offset+12:], 0)                  // padding

	return &Semaphore{
		memory:   memory,
		name:     name,
		offset:   offset,
		maxCount: maxCount,
	}, nil
}

// OpenSemaphore opens an existing semaphore in shared memory.
func OpenSemaphore(memory *Memory, name string) (*Semaphore, error) {
	entry := memory.Find(name)
	if entry == nil {
		return nil, fmt.Errorf("semaphore '%s' not found", name)
	}

	if entry.Size != SemaphoreHeaderSize {
		return nil, fmt.Errorf("invalid semaphore size: %d, expected %d", entry.Size, SemaphoreHeaderSize)
	}

	offset := int(entry.Offset)
	data := memory.Data()

	maxCount := int32(binary.LittleEndian.Uint32(data[offset+8:]))

	return &Semaphore{
		memory:   memory,
		name:     name,
		offset:   offset,
		maxCount: maxCount,
	}, nil
}

// countPtr returns a pointer to the atomic count.
func (s *Semaphore) countPtr() *int32 {
	return (*int32)(unsafe.Pointer(&s.memory.Data()[s.offset]))
}

// waitingPtr returns a pointer to the atomic waiting counter.
func (s *Semaphore) waitingPtr() *int32 {
	return (*int32)(unsafe.Pointer(&s.memory.Data()[s.offset+4]))
}

// Acquire acquires one permit from the semaphore.
// Blocks until a permit is available.
func (s *Semaphore) Acquire() {
	atomic.AddInt32(s.waitingPtr(), 1)
	defer atomic.AddInt32(s.waitingPtr(), -1)

	backoff := time.Microsecond
	maxBackoff := time.Millisecond

	for {
		current := atomic.LoadInt32(s.countPtr())

		if current > 0 {
			// Try to decrement
			if atomic.CompareAndSwapInt32(s.countPtr(), current, current-1) {
				return
			}
		}

		// Exponential backoff
		time.Sleep(backoff)
		if backoff < maxBackoff {
			backoff *= 2
		}
	}
}

// TryAcquire tries to acquire one permit without blocking.
// Returns true if permit was acquired, false if count was 0.
func (s *Semaphore) TryAcquire() bool {
	for {
		current := atomic.LoadInt32(s.countPtr())

		if current <= 0 {
			return false
		}

		if atomic.CompareAndSwapInt32(s.countPtr(), current, current-1) {
			return true
		}
		// CAS failed, retry
	}
}

// AcquireTimeout tries to acquire one permit with a timeout.
// Returns true if permit was acquired, false if timed out.
func (s *Semaphore) AcquireTimeout(timeout time.Duration) bool {
	atomic.AddInt32(s.waitingPtr(), 1)
	defer atomic.AddInt32(s.waitingPtr(), -1)

	deadline := time.Now().Add(timeout)
	backoff := time.Microsecond
	maxBackoff := time.Millisecond

	for {
		current := atomic.LoadInt32(s.countPtr())

		if current > 0 {
			if atomic.CompareAndSwapInt32(s.countPtr(), current, current-1) {
				return true
			}
		}

		// Check timeout
		if time.Now().After(deadline) {
			return false
		}

		// Exponential backoff
		time.Sleep(backoff)
		if backoff < maxBackoff {
			backoff *= 2
		}
	}
}

// Release releases one permit back to the semaphore.
// Returns an error if max_count would be exceeded.
// Uses a CAS loop to atomically check and increment, preventing TOCTOU races.
func (s *Semaphore) Release() error {
	for {
		current := atomic.LoadInt32(s.countPtr())
		if s.maxCount > 0 && current >= s.maxCount {
			return errors.New("semaphore count would exceed maximum")
		}
		if atomic.CompareAndSwapInt32(s.countPtr(), current, current+1) {
			return nil
		}
		// CAS failed, retry
	}
}

// Count returns the current semaphore count.
func (s *Semaphore) Count() int32 {
	return atomic.LoadInt32(s.countPtr())
}

// Waiting returns the number of processes currently waiting.
func (s *Semaphore) Waiting() int32 {
	return atomic.LoadInt32(s.waitingPtr())
}

// MaxCount returns the maximum count (0 = unbounded).
func (s *Semaphore) MaxCount() int32 {
	return s.maxCount
}

// Name returns the semaphore name.
func (s *Semaphore) Name() string {
	return s.name
}
