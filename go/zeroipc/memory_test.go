package zeroipc

import (
	"os"
	"testing"
)

func TestMemoryCreateAndOpen(t *testing.T) {
	name := "/test_go_memory"
	size := 1024 * 1024 // 1MB

	// Clean up any leftover
	UnlinkName(name)

	// Create
	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	if mem.Size() != size {
		t.Errorf("Size mismatch: got %d, want %d", mem.Size(), size)
	}

	if !mem.IsOwner() {
		t.Error("Expected IsOwner to be true")
	}

	// Verify table was initialized
	if mem.Table().EntryCount() != 0 {
		t.Errorf("Expected 0 entries, got %d", mem.Table().EntryCount())
	}

	// Open from another "process" (same process, but tests the open path)
	mem2, err := OpenMemory(name, 64)
	if err != nil {
		t.Fatalf("OpenMemory failed: %v", err)
	}
	defer mem2.Close()

	if mem2.IsOwner() {
		t.Error("Expected IsOwner to be false for opened memory")
	}

	// Verify magic number is correct
	header := mem2.Table().Header()
	if header.Magic != TableMagic {
		t.Errorf("Magic mismatch: got 0x%08X, want 0x%08X", header.Magic, TableMagic)
	}
}

func TestArrayCreateAndAccess(t *testing.T) {
	name := "/test_go_array"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	// Create array of float64
	arr, err := NewArray[float64](mem, "temperatures", 100)
	if err != nil {
		t.Fatalf("NewArray failed: %v", err)
	}

	if arr.Capacity() != 100 {
		t.Errorf("Capacity mismatch: got %d, want 100", arr.Capacity())
	}

	// Write some values
	arr.Set(0, 25.5)
	arr.Set(50, 30.0)
	arr.Set(99, 42.0)

	// Read them back
	if arr.Get(0) != 25.5 {
		t.Errorf("Get(0) = %v, want 25.5", arr.Get(0))
	}
	if arr.Get(50) != 30.0 {
		t.Errorf("Get(50) = %v, want 30.0", arr.Get(50))
	}
	if arr.Get(99) != 42.0 {
		t.Errorf("Get(99) = %v, want 42.0", arr.Get(99))
	}

	// Open the array
	arr2, err := OpenArray[float64](mem, "temperatures")
	if err != nil {
		t.Fatalf("OpenArray failed: %v", err)
	}

	// Verify data persisted
	if arr2.Get(0) != 25.5 {
		t.Errorf("arr2.Get(0) = %v, want 25.5", arr2.Get(0))
	}
}

func TestArrayInt32(t *testing.T) {
	name := "/test_go_array_int32"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	arr, err := NewArray[int32](mem, "counters", 10)
	if err != nil {
		t.Fatalf("NewArray failed: %v", err)
	}

	arr.Fill(42)

	for i := 0; i < 10; i++ {
		if arr.Get(i) != 42 {
			t.Errorf("Get(%d) = %d, want 42", i, arr.Get(i))
		}
	}

	// Test Slice
	slice := arr.Slice()
	if len(slice) != 10 {
		t.Errorf("Slice length = %d, want 10", len(slice))
	}
	for i, v := range slice {
		if v != 42 {
			t.Errorf("slice[%d] = %d, want 42", i, v)
		}
	}
}

func TestMain(m *testing.M) {
	// Run tests
	code := m.Run()

	// Clean up any test shared memory
	UnlinkName("/test_go_memory")
	UnlinkName("/test_go_array")
	UnlinkName("/test_go_array_int32")

	os.Exit(code)
}
