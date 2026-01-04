package zeroipc

import (
	"errors"
	"fmt"
	"sync/atomic"
	"unsafe"
)

// OnceHeaderSize is the size of the Once flag in bytes.
// Layout: state(4) = 4 bytes
const OnceHeaderSize = 4

// Once is a one-time initialization primitive for shared memory.
//
// Ensures a function is executed exactly once across all processes,
// similar to sync.Once. Thread-safe and process-safe.
//
// Binary layout:
//   - state: uint32 (atomic, offset 0) - 0 = not called, 1 = done
type Once struct {
	memory *Memory
	name   string
	offset int
}

// NewOnce creates a new Once flag in shared memory.
func NewOnce(memory *Memory, name string) (*Once, error) {
	if len(name) >= NameSize {
		return nil, errors.New("name too long (max 31 characters)")
	}

	// Check if already exists
	if entry := memory.Find(name); entry != nil {
		return nil, fmt.Errorf("once flag '%s' already exists", name)
	}

	// Allocate space
	offset, err := memory.Allocate(name, OnceHeaderSize)
	if err != nil {
		return nil, fmt.Errorf("failed to allocate: %w", err)
	}

	// Initialize state to 0 (not called)
	atomic.StoreUint32((*uint32)(unsafe.Pointer(&memory.Data()[offset])), 0)

	return &Once{
		memory: memory,
		name:   name,
		offset: offset,
	}, nil
}

// OpenOnce opens an existing Once flag in shared memory.
func OpenOnce(memory *Memory, name string) (*Once, error) {
	entry := memory.Find(name)
	if entry == nil {
		return nil, fmt.Errorf("once flag '%s' not found", name)
	}

	if entry.Size != OnceHeaderSize {
		return nil, fmt.Errorf("invalid once flag size: %d, expected %d", entry.Size, OnceHeaderSize)
	}

	return &Once{
		memory: memory,
		name:   name,
		offset: int(entry.Offset),
	}, nil
}

// statePtr returns a pointer to the atomic state.
func (o *Once) statePtr() *uint32 {
	return (*uint32)(unsafe.Pointer(&o.memory.Data()[o.offset]))
}

// Do executes the function exactly once across all processes.
//
// If this is the first call, executes fn. Otherwise, returns immediately
// without executing fn.
//
// Note: If fn panics, the Once flag is still marked as called.
func (o *Once) Do(fn func()) {
	// Fast path: already called
	if atomic.LoadUint32(o.statePtr()) == 1 {
		return
	}

	// Try to be the caller
	if atomic.CompareAndSwapUint32(o.statePtr(), 0, 1) {
		// We won the race - execute the function
		fn()
	}
	// If CAS failed, someone else already called it
}

// Call is an alias for Do (matches C++ API).
func (o *Once) Call(fn func()) {
	o.Do(fn)
}

// IsCalled returns true if the function has been executed.
func (o *Once) IsCalled() bool {
	return atomic.LoadUint32(o.statePtr()) == 1
}

// AlreadyCalled is an alias for IsCalled (matches C++ API).
func (o *Once) AlreadyCalled() bool {
	return o.IsCalled()
}

// Name returns the Once flag name.
func (o *Once) Name() string {
	return o.name
}

// ResetUnsafe resets the Once flag to uncalled state.
//
// WARNING: This is NOT thread-safe. Only use when you have exclusive
// access or for testing purposes.
func (o *Once) ResetUnsafe() {
	atomic.StoreUint32(o.statePtr(), 0)
}
