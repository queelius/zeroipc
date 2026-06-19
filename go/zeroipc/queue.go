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

// Queue is a lock-free MPMC (multi-producer multi-consumer) circular buffer
// using the Vyukov bounded queue algorithm with per-slot sequence numbers.
//
// Binary layout (matching C++):
//   - head: uint32 (atomic, offset 0)
//   - tail: uint32 (atomic, offset 4)
//   - capacity: uint32 (offset 8)
//   - elem_size: uint32 (offset 12)
//   - data: capacity * elem_size bytes (offset 16)
//   - sequence: capacity * 4 bytes (after data) - per-slot sequence numbers
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

	// Layout: [Header(16)][data: T*capacity][pad][sequence: uint32*capacity]
	totalSize := QueueHeaderSize + align8(capacity*elemSize) + capacity*4
	offset, err := memory.Allocate(name, totalSize)
	if err != nil {
		return nil, fmt.Errorf("failed to allocate: %w", err)
	}

	data := memory.Data()

	// Write header
	binary.LittleEndian.PutUint32(data[offset:], 0)                    // head
	binary.LittleEndian.PutUint32(data[offset+4:], 0)                  // tail
	binary.LittleEndian.PutUint32(data[offset+8:], uint32(capacity))   // capacity
	binary.LittleEndian.PutUint32(data[offset+12:], uint32(elemSize))  // elem_size

	// Zero-initialize data area
	dataStart := offset + QueueHeaderSize
	dataEnd := dataStart + capacity*elemSize
	for i := dataStart; i < dataEnd; i++ {
		data[i] = 0
	}

	// Initialize per-slot sequence numbers: sequence[i] = i (8-aligned start)
	seqStart := dataStart + align8(capacity*elemSize)
	for i := 0; i < capacity; i++ {
		seqOffset := seqStart + i*4
		binary.LittleEndian.PutUint32(data[seqOffset:], uint32(i))
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

// seqPtr returns a pointer to the sequence number for slot i.
// Sequence array lives after the data array.
func (q *Queue[T]) seqPtr(slot uint32) *uint32 {
	seqOffset := q.offset + QueueHeaderSize + align8(int(q.capacity)*int(q.elemSize)) + int(slot)*4
	return (*uint32)(unsafe.Pointer(&q.memory.Data()[seqOffset]))
}

// Push adds an element to the queue (lock-free Vyukov MPMC).
// Returns false if the queue is full.
func (q *Queue[T]) Push(value T) bool {
	cap := q.capacity
	tailPtr := q.tailPtr()

	for {
		tail := atomic.LoadUint32(tailPtr)
		slot := tail % cap
		seq := atomic.LoadUint32(q.seqPtr(slot))
		diff := int32(seq) - int32(tail)

		if diff == 0 {
			// Slot is ready for writing; try to claim it
			if atomic.CompareAndSwapUint32(tailPtr, tail, tail+1) {
				// We own this slot — write the data
				dataOffset := q.offset + QueueHeaderSize + int(slot)*int(q.elemSize)
				*(*T)(unsafe.Pointer(&q.memory.Data()[dataOffset])) = value
				// Publish: set sequence to tail+1 so consumers can see it
				atomic.StoreUint32(q.seqPtr(slot), tail+1)
				return true
			}
			// CAS failed, another producer got it; retry
		} else if diff < 0 {
			// Queue is full
			return false
		}
		// diff > 0: stale tail or another producer in progress; retry
	}
}

// Pop removes and returns an element from the queue (lock-free Vyukov MPMC).
// Returns the value and true if successful, or zero value and false if empty.
func (q *Queue[T]) Pop() (T, bool) {
	var zero T
	cap := q.capacity
	headPtr := q.headPtr()

	for {
		head := atomic.LoadUint32(headPtr)
		slot := head % cap
		seq := atomic.LoadUint32(q.seqPtr(slot))
		diff := int32(seq) - int32(head+1)

		if diff == 0 {
			// Slot contains data; try to claim it
			if atomic.CompareAndSwapUint32(headPtr, head, head+1) {
				// We own this slot — read the data
				dataOffset := q.offset + QueueHeaderSize + int(slot)*int(q.elemSize)
				value := *(*T)(unsafe.Pointer(&q.memory.Data()[dataOffset]))
				// Release: set sequence to head+capacity so producers can reuse
				atomic.StoreUint32(q.seqPtr(slot), head+cap)
				return value, true
			}
			// CAS failed, another consumer got it; retry
		} else if diff < 0 {
			// Queue is empty
			return zero, false
		}
		// diff > 0: stale head or another consumer in progress; retry
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
	head := atomic.LoadUint32(q.headPtr())
	tail := atomic.LoadUint32(q.tailPtr())
	return (tail - head) >= q.capacity
}

// Size returns the approximate number of elements in the queue.
// Note: In a concurrent context, this is only an approximation.
func (q *Queue[T]) Size() int {
	head := atomic.LoadUint32(q.headPtr())
	tail := atomic.LoadUint32(q.tailPtr())
	// uint32 subtraction handles wraparound correctly
	return int(tail - head)
}

// Capacity returns the maximum number of elements the queue can hold.
func (q *Queue[T]) Capacity() int {
	return int(q.capacity)
}

// Name returns the queue name.
func (q *Queue[T]) Name() string {
	return q.name
}
