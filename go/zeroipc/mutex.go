package zeroipc

import (
	"fmt"
	"time"
)

// Mutex provides mutual exclusion across processes using shared memory.
//
// Internally implemented as a binary semaphore (initial_count=1, max_count=1).
// Compatible with Go's sync.Locker interface.
//
// Example:
//
//	mem, _ := NewMemory("/data", 1024*1024, 64)
//	mtx, _ := NewMutex(mem, "data_lock")
//
//	mtx.Lock()
//	// ... critical section ...
//	mtx.Unlock()
type Mutex struct {
	sem *Semaphore
}

// NewMutex creates a new mutex in shared memory.
func NewMutex(memory *Memory, name string) (*Mutex, error) {
	// Create binary semaphore with initial_count=1, max_count=1
	sem, err := NewSemaphore(memory, name, 1, 1)
	if err != nil {
		return nil, fmt.Errorf("failed to create mutex: %w", err)
	}

	return &Mutex{sem: sem}, nil
}

// OpenMutex opens an existing mutex in shared memory.
func OpenMutex(memory *Memory, name string) (*Mutex, error) {
	sem, err := OpenSemaphore(memory, name)
	if err != nil {
		return nil, fmt.Errorf("failed to open mutex: %w", err)
	}

	// Verify it's a binary semaphore
	if sem.MaxCount() != 1 {
		return nil, fmt.Errorf("not a mutex: max_count=%d, expected 1", sem.MaxCount())
	}

	return &Mutex{sem: sem}, nil
}

// Lock acquires the mutex.
// Blocks until the mutex is available.
func (m *Mutex) Lock() {
	m.sem.Acquire()
}

// TryLock tries to acquire the mutex without blocking.
// Returns true if the mutex was acquired, false if already locked.
func (m *Mutex) TryLock() bool {
	return m.sem.TryAcquire()
}

// LockTimeout tries to acquire the mutex with a timeout.
// Returns true if the mutex was acquired, false if timed out.
func (m *Mutex) LockTimeout(timeout time.Duration) bool {
	return m.sem.AcquireTimeout(timeout)
}

// Unlock releases the mutex.
func (m *Mutex) Unlock() {
	// Ignore error since mutex should never overflow
	_ = m.sem.Release()
}

// Name returns the mutex name.
func (m *Mutex) Name() string {
	return m.sem.Name()
}

// IsLocked returns true if the mutex appears to be locked.
// Note: In a concurrent context, this is only an approximation.
func (m *Mutex) IsLocked() bool {
	return m.sem.Count() == 0
}
