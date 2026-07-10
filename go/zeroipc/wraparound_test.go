package zeroipc

// Regression for the 2^32 counter wraparound. head/tail increase
// monotonically and wrap; with a power-of-two capacity the slot mapping
// counter % capacity is continuous across the wrap. Seed the counters just
// below MaxUint32 (with matching per-slot sequence numbers, seq[pos % cap]
// = pos) and stream elements across the boundary in FIFO order.

import (
	"fmt"
	"os"
	"sync/atomic"
	"testing"
	"unsafe"
)

func TestQueueWraparoundAt2To32(t *testing.T) {
	name := fmt.Sprintf("/test_wrap_%d", os.Getpid())
	mem, err := NewMemory(name, 1024*1024, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer func() {
		mem.Close()
		mem.Unlink()
	}()

	const cap = 8
	q, err := NewQueue[uint32](mem, "wq", cap)
	if err != nil {
		t.Fatalf("NewQueue failed: %v", err)
	}
	if q.Capacity() != cap {
		t.Fatalf("Capacity = %d, want %d", q.Capacity(), cap)
	}

	entry := mem.Find("wq")
	if entry == nil {
		t.Fatalf("table entry not found")
	}
	base := int(entry.Offset)
	data := mem.Data()
	head := (*uint32)(unsafe.Pointer(&data[base]))
	tail := (*uint32)(unsafe.Pointer(&data[base+4]))
	seqAt := func(i uint32) *uint32 {
		return (*uint32)(unsafe.Pointer(&data[base+QueueHeaderSize+align8(4*cap)+int(i)*4]))
	}

	// Position both counters 4 increments before the wrap.
	const t0 = uint32(0xFFFFFFFC)
	atomic.StoreUint32(head, t0)
	atomic.StoreUint32(tail, t0)
	for k := uint32(0); k < cap; k++ {
		pos := t0 + k // wraps through 0
		atomic.StoreUint32(seqAt(pos%cap), pos)
	}

	// Stream 3 full generations through the queue, crossing the wrap.
	nextIn, nextOut := uint32(0), uint32(0)
	for round := 0; round < 3; round++ {
		for i := 0; i < cap; i++ {
			if !q.Push(nextIn) {
				t.Fatalf("push %d failed", nextIn)
			}
			nextIn++
		}
		if !q.Full() {
			t.Errorf("queue should be full")
		}
		for i := 0; i < cap; i++ {
			v, ok := q.Pop()
			if !ok {
				t.Fatalf("pop %d failed", nextOut)
			}
			if v != nextOut {
				t.Errorf("FIFO order broken at wrap: got %d, want %d", v, nextOut)
			}
			nextOut++
		}
		if !q.Empty() {
			t.Errorf("queue should be empty")
		}
	}
	if atomic.LoadUint32(tail) >= t0 {
		t.Errorf("counters did not wrap: tail = %d", atomic.LoadUint32(tail))
	}
}
