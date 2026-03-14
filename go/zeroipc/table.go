package zeroipc

import (
	"encoding/binary"
	"errors"
	"fmt"
	"unsafe"
)

const (
	// TableMagic is the magic number 'ZIPM' (0x5A49504D)
	TableMagic uint32 = 0x5A49504D

	// TableVersion is the current format version
	TableVersion uint32 = 1

	// HeaderSize is the size of the table header in bytes
	HeaderSize = 32

	// EntrySize is the size of each table entry in bytes
	EntrySize = 48

	// NameSize is the maximum length of entry names (including null terminator)
	NameSize = 32
)

// Header is the table header stored at the beginning of shared memory.
// Binary layout (32 bytes):
//   - magic: uint32 (offset 0)
//   - version: uint32 (offset 4)
//   - entry_count: uint32 (offset 8)
//   - reserved: uint32 (offset 12)
//   - memory_size: uint64 (offset 16)
//   - next_offset: uint64 (offset 24)
type Header struct {
	Magic      uint32
	Version    uint32
	EntryCount uint32
	MaxEntries uint32
	MemorySize uint64
	NextOffset uint64
}

// Entry is a table entry describing a named structure.
// Binary layout (48 bytes):
//   - name: [32]byte (offset 0)
//   - offset: uint64 (offset 32)
//   - size: uint64 (offset 40)
type Entry struct {
	Name   [32]byte
	Offset uint64
	Size   uint64
}

// NameString returns the name as a Go string (null-terminated).
func (e *Entry) NameString() string {
	for i, b := range e.Name {
		if b == 0 {
			return string(e.Name[:i])
		}
	}
	return string(e.Name[:])
}

// Table manages named structures in shared memory.
type Table struct {
	data       []byte
	maxEntries int
	memorySize int
}

// NewTable creates or opens a table in shared memory.
// If create is true, initializes a new table; otherwise validates existing.
func NewTable(data []byte, maxEntries int, memorySize int, create bool) *Table {
	t := &Table{
		data:       data,
		maxEntries: maxEntries,
		memorySize: memorySize,
	}

	if create {
		t.initialize()
	} else {
		t.validate()
	}

	return t
}

func (t *Table) initialize() {
	h := t.Header()
	h.Magic = TableMagic
	h.Version = TableVersion
	h.EntryCount = 0
	h.MaxEntries = uint32(t.maxEntries)
	h.MemorySize = uint64(t.memorySize)
	h.NextOffset = uint64(CalculateTableSize(t.maxEntries))

	t.writeHeader(h)

	// Zero out entries
	entriesStart := HeaderSize
	entriesEnd := entriesStart + t.maxEntries*EntrySize
	for i := entriesStart; i < entriesEnd; i++ {
		t.data[i] = 0
	}
}

func (t *Table) validate() {
	h := t.Header()

	if h.Magic != TableMagic {
		panic(fmt.Sprintf("invalid table magic: 0x%08X (expected 0x%08X)", h.Magic, TableMagic))
	}

	if h.Version != TableVersion {
		panic(fmt.Sprintf("incompatible table version: %d (expected %d)", h.Version, TableVersion))
	}

	if int(h.EntryCount) > t.maxEntries {
		panic(fmt.Sprintf("table corruption: entry count %d exceeds maximum %d", h.EntryCount, t.maxEntries))
	}

	// Use stored memory size
	t.memorySize = int(h.MemorySize)
}

// Header returns a copy of the table header.
func (t *Table) Header() *Header {
	return &Header{
		Magic:      binary.LittleEndian.Uint32(t.data[0:4]),
		Version:    binary.LittleEndian.Uint32(t.data[4:8]),
		EntryCount: binary.LittleEndian.Uint32(t.data[8:12]),
		MaxEntries: binary.LittleEndian.Uint32(t.data[12:16]),
		MemorySize: binary.LittleEndian.Uint64(t.data[16:24]),
		NextOffset: binary.LittleEndian.Uint64(t.data[24:32]),
	}
}

func (t *Table) writeHeader(h *Header) {
	binary.LittleEndian.PutUint32(t.data[0:4], h.Magic)
	binary.LittleEndian.PutUint32(t.data[4:8], h.Version)
	binary.LittleEndian.PutUint32(t.data[8:12], h.EntryCount)
	binary.LittleEndian.PutUint32(t.data[12:16], h.MaxEntries)
	binary.LittleEndian.PutUint64(t.data[16:24], h.MemorySize)
	binary.LittleEndian.PutUint64(t.data[24:32], h.NextOffset)
}

// Entry returns a pointer to the entry at the given index.
func (t *Table) Entry(index int) *Entry {
	if index < 0 || index >= t.maxEntries {
		panic(fmt.Sprintf("entry index %d out of bounds", index))
	}
	offset := HeaderSize + index*EntrySize
	return (*Entry)(unsafe.Pointer(&t.data[offset]))
}

// Find looks up an entry by name.
func (t *Table) Find(name string) *Entry {
	h := t.Header()
	for i := 0; i < int(h.EntryCount); i++ {
		e := t.Entry(i)
		if e.NameString() == name {
			return e
		}
	}
	return nil
}

// Add adds a new entry to the table.
func (t *Table) Add(name string, offset uint64, size uint64) error {
	if len(name) >= NameSize {
		return errors.New("name too long (max 31 characters)")
	}

	h := t.Header()
	if int(h.EntryCount) >= t.maxEntries {
		return errors.New("table full")
	}

	if t.Find(name) != nil {
		return errors.New("name already exists")
	}

	// Get the entry slot
	e := t.Entry(int(h.EntryCount))

	// Clear and set name
	for i := range e.Name {
		e.Name[i] = 0
	}
	copy(e.Name[:], name)

	// Set offset and size
	entryOffset := HeaderSize + int(h.EntryCount)*EntrySize
	binary.LittleEndian.PutUint64(t.data[entryOffset+32:entryOffset+40], offset)
	binary.LittleEndian.PutUint64(t.data[entryOffset+40:entryOffset+48], size)

	// Increment entry count
	h.EntryCount++
	binary.LittleEndian.PutUint32(t.data[8:12], h.EntryCount)

	return nil
}

// Allocate reserves space for a new structure.
func (t *Table) Allocate(size int, alignment int) int {
	h := t.Header()

	// Align the offset
	aligned := (int(h.NextOffset) + alignment - 1) &^ (alignment - 1)
	result := aligned

	// Check bounds
	if aligned+size > t.memorySize {
		panic(fmt.Sprintf("allocation of %d bytes at offset %d would exceed memory size %d",
			size, aligned, t.memorySize))
	}

	// Update next offset
	binary.LittleEndian.PutUint64(t.data[24:32], uint64(aligned+size))

	return result
}

// EntryCount returns the number of entries in the table.
func (t *Table) EntryCount() int {
	return int(t.Header().EntryCount)
}

// MaxEntries returns the maximum number of entries this table can hold.
func (t *Table) MaxEntries() int {
	return t.maxEntries
}

// NextOffset returns the next allocation offset.
func (t *Table) NextOffset() uint64 {
	return t.Header().NextOffset
}

// Entries returns a slice of all entries.
func (t *Table) Entries() []*Entry {
	count := t.EntryCount()
	entries := make([]*Entry, count)
	for i := 0; i < count; i++ {
		entries[i] = t.Entry(i)
	}
	return entries
}

// CalculateTableSize returns the total size of a table with the given max entries.
func CalculateTableSize(maxEntries int) int {
	return HeaderSize + maxEntries*EntrySize
}
