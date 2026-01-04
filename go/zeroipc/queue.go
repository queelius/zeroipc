package zeroipc

import (
	"encoding/binary"
	"errors"
	"fmt"
	"sync/atomic"
	"unsafe"
)

// QueueHeaderSize is the size of the queue header in bytes.
// Layout: head(4) + tail(4) + capacity(4) + elem_size(4) = 16 bytes
const QueueHeaderSize = 16

// Queue is a lock-free MPMC (multi-producer multi-consumer) circular buffer.
//
// Binary layout:
//   - head: uint32 (atomic, offset 0)
//   - tail: uint32 (atomic, offset 4)
//   - capacity: uint32 (offset 8)
//   - elem_size: uint32 (offset 12)
//   - data: capacity * elem_size bytes (offset 16)
type Queue[T Numeric] struct {
	memory   *Memory
	name     string
	offset   int
	capacity uint32
	elemSize uint32
}

// NewQueue creates a new queue in shared memory.
func NewQueue[T Numeric](memory *Memory, name string, capacity int) (*Queue[T], error) {
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
		return nil, fmt.Errorf("queue '%s' already exists", name)
	}

	// Allocate space
	totalSize := QueueHeaderSize + capacity*elemSize
	offset, err := memory.Allocate(name, totalSize)
	if err != nil {
		return nil, fmt.Errorf("failed to allocate: %w", err)
	}

	data := memory.Data()

	// Write header
	binary.LittleEndian.PutUint32(data[offset:], 0)                  // head
	binary.LittleEndian.PutUint32(data[offset+4:], 0)                // tail
	binary.LittleEndian.PutUint32(data[offset+8:], uint32(capacity)) // capacity
	binary.LittleEndian.PutUint32(data[offset+12:], uint32(elemSize)) // elem_size

	// Zero-initialize data area
	dataStart := offset + QueueHeaderSize
	dataEnd := dataStart + capacity*elemSize
	for i := dataStart; i < dataEnd; i++ {
		data[i] = 0
	}

	return &Queue[T]{
		memory:   memory,
		name:     name,
		offset:   offset,
		capacity: uint32(capacity),
		elemSize: uint32(elemSize),
	}, nil
}

// OpenQueue opens an existing queue in shared memory.
func OpenQueue[T Numeric](memory *Memory, name string) (*Queue[T], error) {
	entry := memory.Find(name)
	if entry == nil {
		return nil, fmt.Errorf("queue '%s' not found", name)
	}

	var zero T
	elemSize := int(unsafe.Sizeof(zero))

	offset := int(entry.Offset)
	data := memory.Data()

	// Read header
	capacity := binary.LittleEndian.Uint32(data[offset+8:])
	storedElemSize := binary.LittleEndian.Uint32(data[offset+12:])

	if int(storedElemSize) != elemSize {
		return nil, fmt.Errorf("element size mismatch: stored %d, expected %d", storedElemSize, elemSize)
	}

	return &Queue[T]{
		memory:   memory,
		name:     name,
		offset:   offset,
		capacity: capacity,
		elemSize: uint32(elemSize),
	}, nil
}

// headPtr returns a pointer to the atomic head counter.
func (q *Queue[T]) headPtr() *uint32 {
	return (*uint32)(unsafe.Pointer(&q.memory.Data()[q.offset]))
}

// tailPtr returns a pointer to the atomic tail counter.
func (q *Queue[T]) tailPtr() *uint32 {
	return (*uint32)(unsafe.Pointer(&q.memory.Data()[q.offset+4]))
}

// Push adds an element to the queue (lock-free).
// Returns false if the queue is full.
func (q *Queue[T]) Push(value T) bool {
	headPtr := q.headPtr()
	tailPtr := q.tailPtr()

	for {
		currentTail := atomic.LoadUint32(tailPtr)
		nextTail := (currentTail + 1) % q.capacity

		// Check if full
		currentHead := atomic.LoadUint32(headPtr)
		if nextTail == currentHead {
			return false
		}

		// Try to claim this slot
		if atomic.CompareAndSwapUint32(tailPtr, currentTail, nextTail) {
			// Write the value to the claimed slot
			dataOffset := q.offset + QueueHeaderSize + int(currentTail)*int(q.elemSize)
			*(*T)(unsafe.Pointer(&q.memory.Data()[dataOffset])) = value
			return true
		}
		// CAS failed, retry
	}
}

// Pop removes and returns an element from the queue (lock-free).
// Returns the value and true if successful, or zero value and false if empty.
func (q *Queue[T]) Pop() (T, bool) {
	var zero T
	headPtr := q.headPtr()
	tailPtr := q.tailPtr()

	for {
		currentHead := atomic.LoadUint32(headPtr)

		// Check if empty
		currentTail := atomic.LoadUint32(tailPtr)
		if currentHead == currentTail {
			return zero, false
		}

		nextHead := (currentHead + 1) % q.capacity

		// Read the value before claiming the slot
		dataOffset := q.offset + QueueHeaderSize + int(currentHead)*int(q.elemSize)
		value := *(*T)(unsafe.Pointer(&q.memory.Data()[dataOffset]))

		// Try to claim this slot
		if atomic.CompareAndSwapUint32(headPtr, currentHead, nextHead) {
			return value, true
		}
		// CAS failed, retry
	}
}

// TryPush is an alias for Push (both are non-blocking).
func (q *Queue[T]) TryPush(value T) bool {
	return q.Push(value)
}

// TryPop is an alias for Pop (both are non-blocking).
func (q *Queue[T]) TryPop() (T, bool) {
	return q.Pop()
}

// Empty returns true if the queue appears empty.
// Note: In a concurrent context, this is only an approximation.
func (q *Queue[T]) Empty() bool {
	return atomic.LoadUint32(q.headPtr()) == atomic.LoadUint32(q.tailPtr())
}

// Full returns true if the queue appears full.
// Note: In a concurrent context, this is only an approximation.
func (q *Queue[T]) Full() bool {
	tail := atomic.LoadUint32(q.tailPtr())
	nextTail := (tail + 1) % q.capacity
	return nextTail == atomic.LoadUint32(q.headPtr())
}

// Size returns the approximate number of elements in the queue.
// Note: In a concurrent context, this is only an approximation.
func (q *Queue[T]) Size() int {
	head := atomic.LoadUint32(q.headPtr())
	tail := atomic.LoadUint32(q.tailPtr())
	if tail >= head {
		return int(tail - head)
	}
	return int(q.capacity - head + tail)
}

// Capacity returns the maximum number of elements the queue can hold.
func (q *Queue[T]) Capacity() int {
	return int(q.capacity)
}

// Name returns the queue name.
func (q *Queue[T]) Name() string {
	return q.name
}
