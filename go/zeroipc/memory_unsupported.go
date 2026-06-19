//go:build !linux && !windows

// Stub backend for operating systems without a zeroipc shared-memory
// implementation (e.g. macOS, *BSD). The package compiles, but NewMemory and
// OpenMemory return a clear error. See memory_linux.go for the reference backend
// and memory_windows.go for notes on adding a platform backend.

package zeroipc

func (m *Memory) create() error { return unsupportedOSError() }
func (m *Memory) open() error   { return unsupportedOSError() }

// Close is a no-op on platforms without a shared-memory backend.
func (m *Memory) Close() error { return nil }

// Unlink is unsupported on this OS.
func (m *Memory) Unlink() error { return unsupportedOSError() }

// UnlinkName removes a shared memory segment by name. Unsupported on this OS.
func UnlinkName(name string) error { return unsupportedOSError() }
