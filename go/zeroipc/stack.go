package zeroipc

import (
	"encoding/binary"
	"errors"
	"fmt"
	"runtime"
	"sync/atomic"
	"unsafe"
)

// StackHeaderSize is the size of the stack header in bytes.
// Layout: top(4) + capacity(4) + elem_size(4) + reserved(4) = 16 bytes
const StackHeaderSize = 16

// align8 rounds n up to the next multiple of 8 (8-byte section alignment,
// format v2). Atomic side-arrays start on an 8-byte boundary so their atomics
// are always naturally aligned regardless of element size.
func align8(n int) int { return (n + 7) &^ 7 }

// Per-slot states for the 4-state CAS protocol (matching C++):
//
//	EMPTY(0) -> WRITING(1) -> READY(2) -> READING(3) -> EMPTY(0)
//
// Push: CAS(top), CAS(EMPTY -> WRITING), write data, store(READY)
// Pop:  CAS(top), CAS(READY -> READING), read data, store(EMPTY)
const (
	slotEmpty   uint32 = 0
	slotWriting uint32 = 1
	slotReady   uint32 = 2
	slotReading uint32 = 3
)

// Stack is a lock-free LIFO stack in shared memory.
//
// Binary layout (matching C++):
//   - top: int32 (atomic, offset 0) - index of top element, -1 when empty
//   - capacity: uint32 (offset 4)
//   - elem_size: uint32 (offset 8)
//   - data: capacity * elem_size bytes (offset 12)
//   - state: capacity * 4 bytes (after data) - per-slot atomic state
type Stack[T Numeric] struct {
	memory   *Memory
	name     string
	offset   int
	capacity uint32
	elemSize uint32
}

// NewStack creates a new stack in shared memory.
func NewStack[T Numeric](memory *Memory, name string, capacity int) (*Stack[T], error) {
	if len(name) >= NameSize {
		return nil, errors.New("name too long (max 31 characters)")
	}

	if capacity <= 0 {
		return nil, errors.New("capacity must be positive")
	}

	var zero T
	elemSize := int(unsafe.Sizeof(zero))

	// Check if already exists
	if entry := memory.Find(name); entry != nil {
		return nil, fmt.Errorf("stack '%s' already exists", name)
	}

	// Layout: [Header(16)][data: T*capacity][pad][state: uint32*capacity]
	totalSize := StackHeaderSize + align8(capacity*elemSize) + capacity*4
	offset, err := memory.Allocate(name, totalSize)
	if err != nil {
		return nil, fmt.Errorf("failed to allocate: %w", err)
	}

	data := memory.Data()

	// Write header (top = -1 means empty)
	binary.LittleEndian.PutUint32(data[offset:], 0xFFFFFFFF)          // top = -1
	binary.LittleEndian.PutUint32(data[offset+4:], uint32(capacity))  // capacity
	binary.LittleEndian.PutUint32(data[offset+8:], uint32(elemSize))  // elem_size
	binary.LittleEndian.PutUint32(data[offset+12:], 0)               // reserved

	// Zero-initialize data, padding, and state areas (state[i] = EMPTY = 0)
	start := offset + StackHeaderSize
	end := start + align8(capacity*elemSize) + capacity*4
	for i := start; i < end; i++ {
		data[i] = 0
	}

	return &Stack[T]{
		memory:   memory,
		name:     name,
		offset:   offset,
		capacity: uint32(capacity),
		elemSize: uint32(elemSize),
	}, nil
}

// OpenStack opens an existing stack in shared memory.
func OpenStack[T Numeric](memory *Memory, name string) (*Stack[T], error) {
	entry := memory.Find(name)
	if entry == nil {
		return nil, fmt.Errorf("stack '%s' not found", name)
	}

	var zero T
	elemSize := int(unsafe.Sizeof(zero))

	offset := int(entry.Offset)
	data := memory.Data()

	// Read header
	capacity := binary.LittleEndian.Uint32(data[offset+4:])
	storedElemSize := binary.LittleEndian.Uint32(data[offset+8:])

	if int(storedElemSize) != elemSize {
		return nil, fmt.Errorf("element size mismatch: stored %d, expected %d", storedElemSize, elemSize)
	}

	return &Stack[T]{
		memory:   memory,
		name:     name,
		offset:   offset,
		capacity: capacity,
		elemSize: uint32(elemSize),
	}, nil
}

// topPtr returns a pointer to the atomic top counter.
func (s *Stack[T]) topPtr() *int32 {
	return (*int32)(unsafe.Pointer(&s.memory.Data()[s.offset]))
}

// slotStatePtr returns a pointer to the atomic state for slot i.
// State array lives after the data array.
func (s *Stack[T]) slotStatePtr(i int32) *uint32 {
	stateOffset := s.offset + StackHeaderSize + align8(int(s.capacity)*int(s.elemSize)) + int(i)*4
	return (*uint32)(unsafe.Pointer(&s.memory.Data()[stateOffset]))
}

// Push adds an element to the top of the stack (lock-free with per-slot state).
// Returns false if the stack is full.
func (s *Stack[T]) Push(value T) bool {
	topPtr := s.topPtr()

	// Step 1: Reserve a slot by CAS-advancing top
	var currentTop, newTop int32
	for {
		currentTop = atomic.LoadInt32(topPtr)
		if currentTop >= int32(s.capacity)-1 {
			return false // full
		}
		newTop = currentTop + 1
		if atomic.CompareAndSwapInt32(topPtr, currentTop, newTop) {
			break
		}
	}

	// Step 2: CAS the slot state EMPTY -> WRITING
	statePtr := s.slotStatePtr(newTop)
	for {
		expected := slotEmpty
		if atomic.CompareAndSwapUint32(statePtr, expected, slotWriting) {
			break
		}
		runtime.Gosched()
	}

	// Step 3: Write data (we have exclusive ownership)
	dataOffset := s.offset + StackHeaderSize + int(newTop)*int(s.elemSize)
	*(*T)(unsafe.Pointer(&s.memory.Data()[dataOffset])) = value

	// Step 4: Publish: WRITING -> READY
	atomic.StoreUint32(statePtr, slotReady)
	return true
}

// Pop removes and returns the top element from the stack (lock-free with per-slot state).
// Returns the value and true if successful, or zero value and false if empty.
func (s *Stack[T]) Pop() (T, bool) {
	var zero T
	topPtr := s.topPtr()

	// Step 1: Reserve a slot by CAS-decrementing top
	var currentTop, newTop int32
	for {
		currentTop = atomic.LoadInt32(topPtr)
		if currentTop < 0 {
			return zero, false // empty
		}
		newTop = currentTop - 1
		if atomic.CompareAndSwapInt32(topPtr, currentTop, newTop) {
			break
		}
	}

	// Step 2: CAS the slot state READY -> READING
	statePtr := s.slotStatePtr(currentTop)
	for {
		expected := slotReady
		if atomic.CompareAndSwapUint32(statePtr, expected, slotReading) {
			break
		}
		runtime.Gosched()
	}

	// Step 3: Read data (we have exclusive ownership)
	dataOffset := s.offset + StackHeaderSize + int(currentTop)*int(s.elemSize)
	value := *(*T)(unsafe.Pointer(&s.memory.Data()[dataOffset]))

	// Step 4: Release: READING -> EMPTY
	atomic.StoreUint32(statePtr, slotEmpty)
	return value, true
}

// Top returns the top element without removing it.
// Returns the value and true if successful, or zero value and false if empty.
func (s *Stack[T]) Top() (T, bool) {
	var zero T

	// A peek cannot passively read the slot: between observing READY and the
	// read, a concurrent pop could recycle the slot and a new push begin
	// overwriting it, racing the read (TOCTOU). Claim the slot exclusively via
	// the same state machine pop uses (READY -> READING), copy, then restore it
	// to READY. The bounded spin preserves crash-safety.
	for spins := 0; spins < 10000; spins++ {
		currentTop := atomic.LoadInt32(s.topPtr())
		if currentTop < 0 {
			return zero, false
		}

		statePtr := s.slotStatePtr(currentTop)
		if atomic.CompareAndSwapUint32(statePtr, slotReady, slotReading) {
			dataOffset := s.offset + StackHeaderSize + int(currentTop)*int(s.elemSize)
			value := *(*T)(unsafe.Pointer(&s.memory.Data()[dataOffset]))
			atomic.StoreUint32(statePtr, slotReady)
			return value, true
		}
		runtime.Gosched()
	}
	return zero, false
}

// Empty returns true if the stack appears empty.
func (s *Stack[T]) Empty() bool {
	return atomic.LoadInt32(s.topPtr()) < 0
}

// Full returns true if the stack appears full.
func (s *Stack[T]) Full() bool {
	return atomic.LoadInt32(s.topPtr()) >= int32(s.capacity)-1
}

// Size returns the approximate number of elements in the stack.
func (s *Stack[T]) Size() int {
	top := atomic.LoadInt32(s.topPtr())
	if top < 0 {
		return 0
	}
	return int(top) + 1
}

// Capacity returns the maximum number of elements the stack can hold.
func (s *Stack[T]) Capacity() int {
	return int(s.capacity)
}

// Name returns the stack name.
func (s *Stack[T]) Name() string {
	return s.name
}
