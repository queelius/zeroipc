package zeroipc

// Crash-safety: a peer that dies mid-operation leaves its slot state
// permanently claimed. These tests simulate such a ghost by poking the
// slot-state array directly, then assert Push/Pop fail within the bounded
// spin instead of hanging, undo their top reservation (no silent item
// loss), and that the stack works again once the slot is repaired.

import (
	"fmt"
	"os"
	"sync/atomic"
	"testing"
	"unsafe"
)

func TestStackCrashedPeerBoundedSpin(t *testing.T) {
	name := fmt.Sprintf("/test_crash_%d", os.Getpid())
	mem, err := NewMemory(name, 1024*1024, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer func() {
		mem.Close()
		mem.Unlink()
	}()

	s, err := NewStack[int32](mem, "s", 8)
	if err != nil {
		t.Fatalf("NewStack failed: %v", err)
	}

	if !s.Push(1) || !s.Push(2) {
		t.Fatalf("setup pushes failed")
	}

	// Slot-state pointer per the spec formula (elem_size 4, cap 8).
	entry := mem.Find("s")
	if entry == nil {
		t.Fatalf("table entry not found")
	}
	sideOff := StackHeaderSize + align8(4*8)
	stateAt := func(i int) *uint32 {
		return (*uint32)(unsafe.Pointer(&mem.Data()[int(entry.Offset)+sideOff+i*4]))
	}

	// Ghost pusher died mid-write: slot 1 stuck in WRITING.
	atomic.StoreUint32(stateAt(1), slotWriting)
	if _, ok := s.Pop(); ok {
		t.Errorf("Pop succeeded on a stuck slot")
	}
	if got := s.Size(); got != 2 {
		t.Errorf("Pop bail-out did not restore top: size = %d, want 2", got)
	}

	// Repair; both values drain.
	atomic.StoreUint32(stateAt(1), slotReady)
	if v, ok := s.Pop(); !ok || v != 2 {
		t.Errorf("Pop after repair = %v, %v; want 2, true", v, ok)
	}
	if v, ok := s.Pop(); !ok || v != 1 {
		t.Errorf("Pop after repair = %v, %v; want 1, true", v, ok)
	}

	// Ghost popper holding the slot above top: push fails bounded, top restored.
	if !s.Push(1) {
		t.Fatalf("push failed")
	}
	atomic.StoreUint32(stateAt(1), slotReading)
	if s.Push(2) {
		t.Errorf("Push succeeded on a stuck slot")
	}
	if got := s.Size(); got != 1 {
		t.Errorf("Push bail-out did not restore top: size = %d, want 1", got)
	}

	atomic.StoreUint32(stateAt(1), slotEmpty)
	if !s.Push(2) {
		t.Errorf("Push failed after slot repair")
	}
	if got := s.Size(); got != 2 {
		t.Errorf("size after recovery = %d, want 2", got)
	}
}
