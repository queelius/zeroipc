//go:build linux

// Linux backend: POSIX shared memory mapped under /dev/shm via mmap.
//
// To support another OS, add a file (e.g. memory_windows.go with
// //go:build windows) implementing the same unexported methods —
// create, open, Close, Unlink — plus the UnlinkName function, and remove that
// OS from the constraints in memory_unsupported.go.

package zeroipc

import (
	"errors"
	"fmt"
	"path/filepath"

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
