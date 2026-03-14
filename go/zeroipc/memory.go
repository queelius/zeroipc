// Package zeroipc provides high-performance cross-language shared memory IPC.
//
// ZeroIPC implements a binary format for data structures in shared memory
// that can be accessed from multiple processes and programming languages.
// This Go implementation is binary-compatible with the C++ and Python versions.
package zeroipc

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"unsafe"

	"golang.org/x/sys/unix"
)

// shmPath converts a POSIX shm name (e.g., "/myshm") to a filesystem path.
// On Linux, POSIX shared memory is implemented via /dev/shm/.
func shmPath(name string) string {
	// Remove leading slash if present
	if len(name) > 0 && name[0] == '/' {
		name = name[1:]
	}
	return filepath.Join("/dev/shm", name)
}

// Memory wraps a POSIX shared memory segment and its metadata table.
type Memory struct {
	name       string
	size       int
	maxEntries int
	fd         int
	data       []byte
	table      *Table
	owner      bool
}

// NewMemory creates a new shared memory segment.
// name should start with "/" (e.g., "/myshm")
// size is the total size in bytes
// maxEntries is the maximum number of table entries (default 64 if 0)
func NewMemory(name string, size int, maxEntries int) (*Memory, error) {
	if maxEntries == 0 {
		maxEntries = 64
	}

	m := &Memory{
		name:       name,
		size:       size,
		maxEntries: maxEntries,
		fd:         -1,
		owner:      true,
	}

	if err := m.create(); err != nil {
		return nil, err
	}

	// Initialize table
	m.table = NewTable(m.data, maxEntries, size, true)

	return m, nil
}

// OpenMemory opens an existing shared memory segment.
func OpenMemory(name string, maxEntries int) (*Memory, error) {
	if maxEntries == 0 {
		maxEntries = 64
	}

	m := &Memory{
		name:       name,
		maxEntries: maxEntries,
		fd:         -1,
		owner:      false,
	}

	if err := m.open(); err != nil {
		return nil, err
	}

	// Open existing table
	m.table = NewTable(m.data, maxEntries, m.size, false)

	return m, nil
}

func (m *Memory) create() error {
	path := shmPath(m.name)

	// Try to create shared memory (O_EXCL means fail if exists)
	fd, err := unix.Open(path, unix.O_CREAT|unix.O_RDWR|unix.O_EXCL, 0666)
	if err != nil {
		if errors.Is(err, unix.EEXIST) {
			// Unlink and retry
			_ = unix.Unlink(path)
			fd, err = unix.Open(path, unix.O_CREAT|unix.O_RDWR|unix.O_EXCL, 0666)
		}
		if err != nil {
			return fmt.Errorf("shm_open: %w", err)
		}
	}
	m.fd = fd

	// Set size
	if err := unix.Ftruncate(fd, int64(m.size)); err != nil {
		unix.Close(fd)
		unix.Unlink(path)
		return fmt.Errorf("ftruncate: %w", err)
	}

	// Map memory
	data, err := unix.Mmap(fd, 0, m.size, unix.PROT_READ|unix.PROT_WRITE, unix.MAP_SHARED)
	if err != nil {
		unix.Close(fd)
		unix.Unlink(path)
		return fmt.Errorf("mmap: %w", err)
	}
	m.data = data
	// Note: ftruncate + mmap on Linux zero-fills new pages; explicit zeroing
	// omitted for performance. If portability to non-Linux is needed, add
	// copy(data, make([]byte, len(data))) here.

	return nil
}

func (m *Memory) open() error {
	path := shmPath(m.name)

	// Open existing shared memory
	fd, err := unix.Open(path, unix.O_RDWR, 0666)
	if err != nil {
		return fmt.Errorf("shm_open: %w", err)
	}
	m.fd = fd

	// Get size using fstat
	var stat unix.Stat_t
	if err := unix.Fstat(fd, &stat); err != nil {
		unix.Close(fd)
		return fmt.Errorf("fstat: %w", err)
	}
	m.size = int(stat.Size)

	// Map memory
	data, err := unix.Mmap(fd, 0, m.size, unix.PROT_READ|unix.PROT_WRITE, unix.MAP_SHARED)
	if err != nil {
		unix.Close(fd)
		return fmt.Errorf("mmap: %w", err)
	}
	m.data = data

	return nil
}

// Close unmaps the memory and closes the file descriptor.
// It does NOT unlink the shared memory.
func (m *Memory) Close() error {
	var errs []error

	if m.data != nil {
		if err := unix.Munmap(m.data); err != nil {
			errs = append(errs, fmt.Errorf("munmap: %w", err))
		}
		m.data = nil
	}

	if m.fd >= 0 {
		if err := unix.Close(m.fd); err != nil {
			errs = append(errs, fmt.Errorf("close: %w", err))
		}
		m.fd = -1
	}

	if len(errs) > 0 {
		return errors.Join(errs...)
	}
	return nil
}

// Unlink removes the shared memory segment from the system.
func (m *Memory) Unlink() error {
	return unix.Unlink(shmPath(m.name))
}

// UnlinkName removes a shared memory segment by name (static method equivalent).
func UnlinkName(name string) error {
	return unix.Unlink(shmPath(name))
}

// Data returns the raw byte slice of the shared memory.
func (m *Memory) Data() []byte {
	return m.data
}

// At returns a pointer to memory at a specific offset.
func (m *Memory) At(offset int) unsafe.Pointer {
	if offset < 0 || offset >= m.size {
		panic(fmt.Sprintf("offset %d out of bounds (size=%d)", offset, m.size))
	}
	return unsafe.Pointer(&m.data[offset])
}

// Table returns the metadata table.
func (m *Memory) Table() *Table {
	return m.table
}

// Size returns the total size of the shared memory segment.
func (m *Memory) Size() int {
	return m.size
}

// Name returns the name of the shared memory segment.
func (m *Memory) Name() string {
	return m.name
}

// IsOwner returns true if this instance created the shared memory.
func (m *Memory) IsOwner() bool {
	return m.owner
}

// Allocate reserves space in shared memory and adds an entry to the table.
func (m *Memory) Allocate(name string, size int) (int, error) {
	offset := m.table.Allocate(size, 8)
	if err := m.table.Add(name, uint64(offset), uint64(size)); err != nil {
		return 0, err
	}
	return offset, nil
}

// Find looks up an entry in the table.
func (m *Memory) Find(name string) *Entry {
	return m.table.Find(name)
}

// Dump writes shared memory info to a file for debugging.
func (m *Memory) Dump(path string) error {
	return os.WriteFile(path, m.data, 0644)
}
