package zeroipc

import (
	"encoding/binary"
	"errors"
	"fmt"
	"unsafe"
)

// ArrayHeaderSize is the size of the array header in bytes.
const ArrayHeaderSize = 8

// Array is a fixed-size array in shared memory.
// The element type must be a fixed-size type (numeric types).
//
// Binary layout:
//   - capacity: uint64 (8 bytes)
//   - data: capacity * sizeof(T) bytes
type Array[T Numeric] struct {
	memory   *Memory
	name     string
	offset   int
	capacity int
	elemSize int
}

// Numeric constrains Array elements to numeric types that are binary-compatible.
type Numeric interface {
	~int8 | ~int16 | ~int32 | ~int64 |
		~uint8 | ~uint16 | ~uint32 | ~uint64 |
		~float32 | ~float64
}

// NewArray creates a new array in shared memory.
func NewArray[T Numeric](memory *Memory, name string, capacity int) (*Array[T], error) {
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
		return nil, fmt.Errorf("array '%s' already exists", name)
	}

	// Allocate space
	totalSize := ArrayHeaderSize + capacity*elemSize
	offset, err := memory.Allocate(name, totalSize)
	if err != nil {
		return nil, fmt.Errorf("failed to allocate: %w", err)
	}

	// Write capacity header
	binary.LittleEndian.PutUint64(memory.Data()[offset:offset+8], uint64(capacity))

	// Zero-initialize data
	dataStart := offset + ArrayHeaderSize
	dataEnd := dataStart + capacity*elemSize
	for i := dataStart; i < dataEnd; i++ {
		memory.Data()[i] = 0
	}

	return &Array[T]{
		memory:   memory,
		name:     name,
		offset:   offset,
		capacity: capacity,
		elemSize: elemSize,
	}, nil
}

// OpenArray opens an existing array in shared memory.
func OpenArray[T Numeric](memory *Memory, name string) (*Array[T], error) {
	entry := memory.Find(name)
	if entry == nil {
		return nil, fmt.Errorf("array '%s' not found", name)
	}

	var zero T
	elemSize := int(unsafe.Sizeof(zero))

	// Read capacity from header
	offset := int(entry.Offset)
	capacity := binary.LittleEndian.Uint64(memory.Data()[offset : offset+8])

	// Validate size
	expectedSize := ArrayHeaderSize + int(capacity)*elemSize
	if int(entry.Size) != expectedSize {
		return nil, fmt.Errorf("size mismatch: expected %d, got %d (wrong element type?)",
			expectedSize, entry.Size)
	}

	return &Array[T]{
		memory:   memory,
		name:     name,
		offset:   offset,
		capacity: int(capacity),
		elemSize: elemSize,
	}, nil
}

// Get returns the element at the given index.
func (a *Array[T]) Get(index int) T {
	if index < 0 || index >= a.capacity {
		panic(fmt.Sprintf("index %d out of bounds (capacity=%d)", index, a.capacity))
	}

	dataOffset := a.offset + ArrayHeaderSize + index*a.elemSize
	return *(*T)(unsafe.Pointer(&a.memory.Data()[dataOffset]))
}

// Set sets the element at the given index.
func (a *Array[T]) Set(index int, value T) {
	if index < 0 || index >= a.capacity {
		panic(fmt.Sprintf("index %d out of bounds (capacity=%d)", index, a.capacity))
	}

	dataOffset := a.offset + ArrayHeaderSize + index*a.elemSize
	*(*T)(unsafe.Pointer(&a.memory.Data()[dataOffset])) = value
}

// Capacity returns the array capacity.
func (a *Array[T]) Capacity() int {
	return a.capacity
}

// Name returns the array name.
func (a *Array[T]) Name() string {
	return a.name
}

// Fill sets all elements to the given value.
func (a *Array[T]) Fill(value T) {
	for i := 0; i < a.capacity; i++ {
		a.Set(i, value)
	}
}

// Slice returns a copy of the array data as a Go slice.
func (a *Array[T]) Slice() []T {
	result := make([]T, a.capacity)
	for i := 0; i < a.capacity; i++ {
		result[i] = a.Get(i)
	}
	return result
}

// CopyFrom copies data from a Go slice into the array.
func (a *Array[T]) CopyFrom(data []T) {
	n := len(data)
	if n > a.capacity {
		n = a.capacity
	}
	for i := 0; i < n; i++ {
		a.Set(i, data[i])
	}
}

// Range iterates over elements and calls fn for each.
func (a *Array[T]) Range(fn func(index int, value T) bool) {
	for i := 0; i < a.capacity; i++ {
		if !fn(i, a.Get(i)) {
			break
		}
	}
}
