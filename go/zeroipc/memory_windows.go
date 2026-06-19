//go:build windows

// Windows stub: shared memory is not yet implemented on Windows.
//
// zeroipc's Linux backend (memory_linux.go) maps names under /dev/shm. Windows
// has no /dev/shm; a native backend would use the Win32 file-mapping API
// (CreateFileMapping / OpenFileMapping + MapViewOfFile, from
// golang.org/x/sys/windows) with a named mapping such as "Local\\<name>" in
// place of the /dev/shm path, and would derive the segment size from a header
// field rather than fstat (Windows mappings do not expose size the way fstat
// does on Linux).
//
// This file is the place to implement it: replace the stubs below with a real
// backend providing create/open/Close/Unlink and UnlinkName, mirroring
// memory_linux.go. Contributions welcome — see issue #1.
//
// Caveat: a Go-only Windows backend gives Go<->Go IPC on Windows. Cross-language
// interop additionally requires the C++/C/Python implementations to use the same
// Windows named-mapping scheme, so a Go-only backend is Go-to-Go on Windows.

package zeroipc

func (m *Memory) create() error { return unsupportedOSError() }
func (m *Memory) open() error   { return unsupportedOSError() }

// Close is a no-op on Windows; no segment can be created here.
func (m *Memory) Close() error { return nil }

// Unlink is unsupported on Windows.
func (m *Memory) Unlink() error { return unsupportedOSError() }

// UnlinkName removes a shared memory segment by name. Unsupported on Windows.
func UnlinkName(name string) error { return unsupportedOSError() }
