package zeroipc

// Format v2 layout regression: the queue's sequence array and the stack's
// slot-state array must start at header(16) + align8(elem_size * capacity).
// These tests recompute that offset from the spec formula and verify, via
// raw reads of the mapped memory at the computed address, that the
// implementation actually placed the side arrays there.

import (
	"encoding/binary"
	"fmt"
	"os"
	"testing"
	"unsafe"
)

// specAlign8 is the spec formula, written independently of the
// implementation's align8 so a drift in either shows up.
func specAlign8(n int) int { return (n + 7) &^ 7 }

func checkQueueLayout[T Numeric](t *testing.T, mem *Memory, name string, capacity int) {
	t.Helper()

	q, err := NewQueue[T](mem, name, capacity)
	if err != nil {
		t.Fatalf("NewQueue(%s) failed: %v", name, err)
	}

	// The queue rounds the requested capacity up to a power of two
	// (wrap-safety); the layout is computed from the actual capacity.
	actualCap := q.Capacity()
	if actualCap < capacity || actualCap&(actualCap-1) != 0 {
		t.Errorf("capacity %d not rounded to a power of two >= %d", actualCap, capacity)
	}

	entry := mem.Find(name)
	if entry == nil {
		t.Fatalf("table entry %q not found", name)
	}

	var zero T
	elemSize := int(unsafe.Sizeof(zero))
	sideOff := QueueHeaderSize + specAlign8(elemSize*actualCap)

	if got, want := int(entry.Size), sideOff+actualCap*4; got != want {
		t.Errorf("elem_size %d: table entry size = %d, want %d", elemSize, got, want)
	}

	// Vyukov invariant: seq[i] == i after creation. Finding those values at
	// the spec-computed offset proves placement.
	data := mem.Data()
	for i := 0; i < actualCap; i++ {
		seq := binary.LittleEndian.Uint32(data[int(entry.Offset)+sideOff+i*4:])
		if seq != uint32(i) {
			t.Errorf("elem_size %d: seq[%d] = %d at spec offset, want %d",
				elemSize, i, seq, i)
		}
	}
}

func checkStackLayout[T Numeric](t *testing.T, mem *Memory, name string, capacity int) {
	t.Helper()

	s, err := NewStack[T](mem, name, capacity)
	if err != nil {
		t.Fatalf("NewStack(%s) failed: %v", name, err)
	}

	entry := mem.Find(name)
	if entry == nil {
		t.Fatalf("table entry %q not found", name)
	}

	var zero T
	elemSize := int(unsafe.Sizeof(zero))
	sideOff := StackHeaderSize + specAlign8(elemSize*capacity)

	if got, want := int(entry.Size), sideOff+capacity*4; got != want {
		t.Errorf("elem_size %d: table entry size = %d, want %d", elemSize, got, want)
	}

	// Push two, then read the slot states (READY=2, EMPTY=0) at the
	// spec-computed offset.
	if !s.Push(1) || !s.Push(2) {
		t.Fatalf("push failed")
	}

	data := mem.Data()
	stateAt := func(i int) uint32 {
		return binary.LittleEndian.Uint32(data[int(entry.Offset)+sideOff+i*4:])
	}
	if stateAt(0) != slotReady || stateAt(1) != slotReady {
		t.Errorf("elem_size %d: pushed slot states = %d,%d at spec offset, want READY(%d)",
			elemSize, stateAt(0), stateAt(1), slotReady)
	}
	if stateAt(2) != slotEmpty {
		t.Errorf("elem_size %d: unused slot state = %d, want EMPTY(%d)",
			elemSize, stateAt(2), slotEmpty)
	}

	if _, ok := s.Pop(); !ok {
		t.Fatalf("pop failed")
	}
	if stateAt(1) != slotEmpty {
		t.Errorf("elem_size %d: popped slot state = %d, want EMPTY(%d)",
			elemSize, stateAt(1), slotEmpty)
	}
}

func TestSectionAlignmentLayout(t *testing.T) {
	name := fmt.Sprintf("/test_layout_%d", os.Getpid())
	mem, err := NewMemory(name, 1024*1024, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer func() {
		mem.Close()
		mem.Unlink()
	}()

	checkQueueLayout[int8](t, mem, "q_i8", 5)     // 5 -> pad to 8
	checkQueueLayout[uint32](t, mem, "q_u32", 5)  // 20 -> pad to 24
	checkQueueLayout[float64](t, mem, "q_f64", 5) // 40, no pad
	checkStackLayout[int8](t, mem, "s_i8", 5)
	checkStackLayout[uint32](t, mem, "s_u32", 5)
	checkStackLayout[float64](t, mem, "s_f64", 5)
}
