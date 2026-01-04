package zeroipc

import (
	"encoding/binary"
	"errors"
	"fmt"
	"sync/atomic"
	"unsafe"
)

// StackHeaderSize is the size of the stack header in bytes.
// Layout: top(4) + capacity(4) + elem_size(4) = 12 bytes
const StackHeaderSize = 12

// Stack is a lock-free LIFO stack in shared memory.
//
// Binary layout:
//   - top: int32 (atomic, offset 0) - index of top element, -1 when empty
//   - capacity: uint32 (offset 4)
//   - elem_size: uint32 (offset 8)
//   - data: capacity * elem_size bytes (offset 12)
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

	// Allocate space
	totalSize := StackHeaderSize + capacity*elemSize
	offset, err := memory.Allocate(name, totalSize)
	if err != nil {
		return nil, fmt.Errorf("failed to allocate: %w", err)
	}

	data := memory.Data()

	// Write header (top = -1 means empty)
	// Store -1 as int32 in little-endian (0xFFFFFFFF)
	binary.LittleEndian.PutUint32(data[offset:], 0xFFFFFFFF)    // top = -1
	binary.LittleEndian.PutUint32(data[offset+4:], uint32(capacity))  // capacity
	binary.LittleEndian.PutUint32(data[offset+8:], uint32(elemSize))  // elem_size

	// Zero-initialize data area
	dataStart := offset + StackHeaderSize
	dataEnd := dataStart + capacity*elemSize
	for i := dataStart; i < dataEnd; i++ {
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

// Push adds an element to the top of the stack (lock-free).
// Returns false if the stack is full.
func (s *Stack[T]) Push(value T) bool {
	topPtr := s.topPtr()

	for {
		currentTop := atomic.LoadInt32(topPtr)

		// Check if full
		if currentTop >= int32(s.capacity)-1 {
			return false
		}

		newTop := currentTop + 1

		// Try to claim this slot
		if atomic.CompareAndSwapInt32(topPtr, currentTop, newTop) {
			// Write the value to the claimed slot
			dataOffset := s.offset + StackHeaderSize + int(newTop)*int(s.elemSize)
			*(*T)(unsafe.Pointer(&s.memory.Data()[dataOffset])) = value
			return true
		}
		// CAS failed, retry
	}
}

// Pop removes and returns the top element from the stack (lock-free).
// Returns the value and true if successful, or zero value and false if empty.
func (s *Stack[T]) Pop() (T, bool) {
	var zero T
	topPtr := s.topPtr()

	for {
		currentTop := atomic.LoadInt32(topPtr)

		// Check if empty
		if currentTop < 0 {
			return zero, false
		}

		newTop := currentTop - 1

		// Read the value before claiming the slot
		dataOffset := s.offset + StackHeaderSize + int(currentTop)*int(s.elemSize)
		value := *(*T)(unsafe.Pointer(&s.memory.Data()[dataOffset]))

		// Try to claim this slot
		if atomic.CompareAndSwapInt32(topPtr, currentTop, newTop) {
			return value, true
		}
		// CAS failed, retry
	}
}

// Top returns the top element without removing it.
// Returns the value and true if successful, or zero value and false if empty.
func (s *Stack[T]) Top() (T, bool) {
	var zero T
	currentTop := atomic.LoadInt32(s.topPtr())

	if currentTop < 0 {
		return zero, false
	}

	dataOffset := s.offset + StackHeaderSize + int(currentTop)*int(s.elemSize)
	value := *(*T)(unsafe.Pointer(&s.memory.Data()[dataOffset]))
	return value, true
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
