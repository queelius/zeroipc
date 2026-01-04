// zeroipc - CLI tool for inspecting and managing ZeroIPC shared memory
//
// Usage:
//
//	zeroipc <command> [arguments]
//
// Commands:
//
//	list                     List shared memory segments in /dev/shm
//	info <shm>               Show table info for a segment
//	entries <shm>            List all entries in a segment's table
//	create <shm> <size>      Create a new shared memory segment
//	delete <shm>             Delete a shared memory segment
//	dump <shm>               Hexdump the first N bytes of a segment
//
// Structure inspection:
//
//	array <shm> <name>       Inspect an array
//	queue <shm> <name>       Inspect a queue
//	stack <shm> <name>       Inspect a stack
//	semaphore <shm> <name>   Inspect a semaphore
//	mutex <shm> <name>       Inspect a mutex
//	barrier <shm> <name>     Inspect a barrier
//	latch <shm> <name>       Inspect a latch
//	once <shm> <name>        Inspect a once flag
package main

import (
	"encoding/binary"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"text/tabwriter"

	"github.com/spinoza/zeroipc/zeroipc"
)

const version = "1.0.0"

func main() {
	if len(os.Args) < 2 {
		printUsage()
		os.Exit(1)
	}

	cmd := os.Args[1]
	args := os.Args[2:]

	var err error
	switch cmd {
	case "help", "-h", "--help":
		printUsage()
	case "version", "-v", "--version":
		fmt.Printf("zeroipc version %s\n", version)
	case "list", "ls":
		err = cmdList()
	case "info":
		err = cmdInfo(args)
	case "entries":
		err = cmdEntries(args)
	case "create":
		err = cmdCreate(args)
	case "delete", "rm":
		err = cmdDelete(args)
	case "dump":
		err = cmdDump(args)
	case "array":
		err = cmdArray(args)
	case "queue":
		err = cmdQueue(args)
	case "stack":
		err = cmdStack(args)
	case "semaphore", "sem":
		err = cmdSemaphore(args)
	case "mutex":
		err = cmdMutex(args)
	case "barrier":
		err = cmdBarrier(args)
	case "latch":
		err = cmdLatch(args)
	case "once":
		err = cmdOnce(args)
	default:
		fmt.Fprintf(os.Stderr, "Unknown command: %s\n", cmd)
		printUsage()
		os.Exit(1)
	}

	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}

func printUsage() {
	fmt.Print(`zeroipc - CLI tool for ZeroIPC shared memory

Usage: zeroipc <command> [arguments]

Commands:
  list                     List shared memory segments in /dev/shm
  info <shm>               Show table header for a segment
  entries <shm>            List all entries in a segment's table
  create <shm> <size>      Create a new shared memory segment
  delete <shm>             Delete a shared memory segment
  dump <shm> [bytes]       Hexdump the first N bytes (default 256)

Structure inspection:
  array <shm> <name>       Inspect an array
  queue <shm> <name>       Inspect a queue
  stack <shm> <name>       Inspect a stack
  semaphore <shm> <name>   Inspect a semaphore
  mutex <shm> <name>       Inspect a mutex
  barrier <shm> <name>     Inspect a barrier
  latch <shm> <name>       Inspect a latch
  once <shm> <name>        Inspect a once flag

Examples:
  zeroipc list
  zeroipc create /mydata 1048576
  zeroipc info /mydata
  zeroipc entries /mydata
  zeroipc array /mydata temperatures
  zeroipc delete /mydata

Notes:
  - <shm> is the shared memory name (e.g., /mydata or mydata)
  - Names are automatically prefixed with / if needed
`)
}

// normalizeName ensures the shm name starts with /
func normalizeName(name string) string {
	if !strings.HasPrefix(name, "/") {
		return "/" + name
	}
	return name
}

// cmdList lists all shared memory segments in /dev/shm
func cmdList() error {
	entries, err := os.ReadDir("/dev/shm")
	if err != nil {
		return fmt.Errorf("failed to read /dev/shm: %w", err)
	}

	if len(entries) == 0 {
		fmt.Println("No shared memory segments found.")
		return nil
	}

	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "NAME\tSIZE\tZEROIPC")

	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}

		info, err := entry.Info()
		if err != nil {
			continue
		}

		name := "/" + entry.Name()
		size := info.Size()
		isZeroIPC := checkZeroIPC(name)

		zeroipcStr := ""
		if isZeroIPC {
			zeroipcStr = "yes"
		}

		fmt.Fprintf(w, "%s\t%s\t%s\n", name, formatSize(size), zeroipcStr)
	}

	w.Flush()
	return nil
}

// checkZeroIPC checks if a shared memory segment has the ZeroIPC magic number
func checkZeroIPC(name string) bool {
	path := filepath.Join("/dev/shm", strings.TrimPrefix(name, "/"))
	f, err := os.Open(path)
	if err != nil {
		return false
	}
	defer f.Close()

	var magic uint32
	if err := binary.Read(f, binary.LittleEndian, &magic); err != nil {
		return false
	}

	return magic == zeroipc.TableMagic
}

// formatSize formats a byte size as human-readable
func formatSize(size int64) string {
	const (
		KB = 1024
		MB = KB * 1024
		GB = MB * 1024
	)

	switch {
	case size >= GB:
		return fmt.Sprintf("%.1fG", float64(size)/GB)
	case size >= MB:
		return fmt.Sprintf("%.1fM", float64(size)/MB)
	case size >= KB:
		return fmt.Sprintf("%.1fK", float64(size)/KB)
	default:
		return fmt.Sprintf("%dB", size)
	}
}

// cmdInfo shows table header info for a segment
func cmdInfo(args []string) error {
	if len(args) < 1 {
		return fmt.Errorf("usage: zeroipc info <shm>")
	}

	name := normalizeName(args[0])

	mem, err := zeroipc.OpenMemory(name, 64)
	if err != nil {
		return fmt.Errorf("failed to open %s: %w", name, err)
	}
	defer mem.Close()

	header := mem.Table().Header()

	fmt.Printf("Shared Memory: %s\n", name)
	fmt.Printf("Size:          %s (%d bytes)\n", formatSize(int64(mem.Size())), mem.Size())
	fmt.Println()
	fmt.Printf("Table Header:\n")
	fmt.Printf("  Magic:       0x%08X", header.Magic)
	if header.Magic == zeroipc.TableMagic {
		fmt.Print(" (ZIPM ✓)")
	} else {
		fmt.Print(" (invalid!)")
	}
	fmt.Println()
	fmt.Printf("  Version:     %d\n", header.Version)
	fmt.Printf("  Entries:     %d\n", header.EntryCount)
	fmt.Printf("  Next Offset: %d (0x%X)\n", header.NextOffset, header.NextOffset)

	return nil
}

// cmdEntries lists all entries in a segment's table
func cmdEntries(args []string) error {
	if len(args) < 1 {
		return fmt.Errorf("usage: zeroipc entries <shm>")
	}

	name := normalizeName(args[0])

	mem, err := zeroipc.OpenMemory(name, 256)
	if err != nil {
		return fmt.Errorf("failed to open %s: %w", name, err)
	}
	defer mem.Close()

	entries := mem.Table().Entries()

	if len(entries) == 0 {
		fmt.Printf("No entries in %s\n", name)
		return nil
	}

	w := tabwriter.NewWriter(os.Stdout, 0, 0, 2, ' ', 0)
	fmt.Fprintln(w, "NAME\tOFFSET\tSIZE\tTYPE (guess)")

	for _, entry := range entries {
		entryName := entry.NameString()
		typeGuess := guessType(int(entry.Size))
		fmt.Fprintf(w, "%s\t%d\t%d\t%s\n", entryName, entry.Offset, entry.Size, typeGuess)
	}

	w.Flush()
	return nil
}

// guessType tries to guess the structure type based on size
func guessType(size int) string {
	switch size {
	case 4:
		return "once"
	case 8:
		return "array/signal header"
	case 12:
		return "stack header"
	case 16:
		return "semaphore/barrier/latch/queue header"
	default:
		if size > 16 {
			return "data structure"
		}
		return "unknown"
	}
}

// cmdCreate creates a new shared memory segment
func cmdCreate(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("usage: zeroipc create <shm> <size>")
	}

	name := normalizeName(args[0])
	size, err := parseSize(args[1])
	if err != nil {
		return fmt.Errorf("invalid size: %w", err)
	}

	mem, err := zeroipc.NewMemory(name, size, 64)
	if err != nil {
		return fmt.Errorf("failed to create %s: %w", name, err)
	}
	defer mem.Close()

	fmt.Printf("Created shared memory: %s (%s)\n", name, formatSize(int64(size)))
	return nil
}

// parseSize parses a size string like "1M", "1024K", "1048576"
func parseSize(s string) (int, error) {
	s = strings.ToUpper(strings.TrimSpace(s))

	multiplier := 1
	if strings.HasSuffix(s, "K") {
		multiplier = 1024
		s = s[:len(s)-1]
	} else if strings.HasSuffix(s, "M") {
		multiplier = 1024 * 1024
		s = s[:len(s)-1]
	} else if strings.HasSuffix(s, "G") {
		multiplier = 1024 * 1024 * 1024
		s = s[:len(s)-1]
	}

	n, err := strconv.Atoi(s)
	if err != nil {
		return 0, err
	}

	return n * multiplier, nil
}

// cmdDelete deletes a shared memory segment
func cmdDelete(args []string) error {
	if len(args) < 1 {
		return fmt.Errorf("usage: zeroipc delete <shm>")
	}

	name := normalizeName(args[0])

	if err := zeroipc.UnlinkName(name); err != nil {
		return fmt.Errorf("failed to delete %s: %w", name, err)
	}

	fmt.Printf("Deleted shared memory: %s\n", name)
	return nil
}

// cmdDump hexdumps the first N bytes of a segment
func cmdDump(args []string) error {
	if len(args) < 1 {
		return fmt.Errorf("usage: zeroipc dump <shm> [bytes]")
	}

	name := normalizeName(args[0])
	numBytes := 256
	if len(args) > 1 {
		n, err := strconv.Atoi(args[1])
		if err != nil {
			return fmt.Errorf("invalid byte count: %w", err)
		}
		numBytes = n
	}

	mem, err := zeroipc.OpenMemory(name, 64)
	if err != nil {
		return fmt.Errorf("failed to open %s: %w", name, err)
	}
	defer mem.Close()

	data := mem.Data()
	if numBytes > len(data) {
		numBytes = len(data)
	}

	hexDump(data[:numBytes])
	return nil
}

// hexDump prints a hex dump of data
func hexDump(data []byte) {
	for i := 0; i < len(data); i += 16 {
		// Offset
		fmt.Printf("%08x  ", i)

		// Hex bytes
		for j := 0; j < 16; j++ {
			if i+j < len(data) {
				fmt.Printf("%02x ", data[i+j])
			} else {
				fmt.Print("   ")
			}
			if j == 7 {
				fmt.Print(" ")
			}
		}

		// ASCII
		fmt.Print(" |")
		for j := 0; j < 16 && i+j < len(data); j++ {
			b := data[i+j]
			if b >= 32 && b < 127 {
				fmt.Printf("%c", b)
			} else {
				fmt.Print(".")
			}
		}
		fmt.Println("|")
	}
}

// cmdArray inspects an array
func cmdArray(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("usage: zeroipc array <shm> <name>")
	}

	name := normalizeName(args[0])
	arrayName := args[1]

	mem, err := zeroipc.OpenMemory(name, 64)
	if err != nil {
		return fmt.Errorf("failed to open %s: %w", name, err)
	}
	defer mem.Close()

	entry := mem.Find(arrayName)
	if entry == nil {
		return fmt.Errorf("array '%s' not found", arrayName)
	}

	// Read array header
	offset := int(entry.Offset)
	data := mem.Data()
	capacity := binary.LittleEndian.Uint64(data[offset:])

	fmt.Printf("Array: %s\n", arrayName)
	fmt.Printf("  Offset:   %d\n", entry.Offset)
	fmt.Printf("  Size:     %d bytes\n", entry.Size)
	fmt.Printf("  Capacity: %d elements\n", capacity)

	// Calculate element size
	headerSize := 8
	dataSize := int(entry.Size) - headerSize
	if capacity > 0 {
		elemSize := dataSize / int(capacity)
		fmt.Printf("  Elem Size: %d bytes\n", elemSize)

		// Show first few elements as hex
		maxShow := 10
		if int(capacity) < maxShow {
			maxShow = int(capacity)
		}
		fmt.Printf("  First %d elements (hex):\n", maxShow)
		for i := 0; i < maxShow; i++ {
			elemOffset := offset + headerSize + i*elemSize
			elemData := data[elemOffset : elemOffset+elemSize]
			fmt.Printf("    [%d]: %x\n", i, elemData)
		}
	}

	return nil
}

// cmdQueue inspects a queue
func cmdQueue(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("usage: zeroipc queue <shm> <name>")
	}

	name := normalizeName(args[0])
	queueName := args[1]

	mem, err := zeroipc.OpenMemory(name, 64)
	if err != nil {
		return fmt.Errorf("failed to open %s: %w", name, err)
	}
	defer mem.Close()

	entry := mem.Find(queueName)
	if entry == nil {
		return fmt.Errorf("queue '%s' not found", queueName)
	}

	// Read queue header
	offset := int(entry.Offset)
	data := mem.Data()
	head := binary.LittleEndian.Uint32(data[offset:])
	tail := binary.LittleEndian.Uint32(data[offset+4:])
	capacity := binary.LittleEndian.Uint32(data[offset+8:])
	elemSize := binary.LittleEndian.Uint32(data[offset+12:])

	// Calculate size
	var size uint32
	if tail >= head {
		size = tail - head
	} else {
		size = capacity - head + tail
	}

	fmt.Printf("Queue: %s\n", queueName)
	fmt.Printf("  Offset:    %d\n", entry.Offset)
	fmt.Printf("  Head:      %d\n", head)
	fmt.Printf("  Tail:      %d\n", tail)
	fmt.Printf("  Capacity:  %d\n", capacity)
	fmt.Printf("  Elem Size: %d bytes\n", elemSize)
	fmt.Printf("  Size:      %d elements\n", size)
	fmt.Printf("  Empty:     %v\n", head == tail)
	fmt.Printf("  Full:      %v\n", (tail+1)%capacity == head)

	return nil
}

// cmdStack inspects a stack
func cmdStack(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("usage: zeroipc stack <shm> <name>")
	}

	name := normalizeName(args[0])
	stackName := args[1]

	mem, err := zeroipc.OpenMemory(name, 64)
	if err != nil {
		return fmt.Errorf("failed to open %s: %w", name, err)
	}
	defer mem.Close()

	entry := mem.Find(stackName)
	if entry == nil {
		return fmt.Errorf("stack '%s' not found", stackName)
	}

	// Read stack header
	offset := int(entry.Offset)
	data := mem.Data()
	top := int32(binary.LittleEndian.Uint32(data[offset:]))
	capacity := binary.LittleEndian.Uint32(data[offset+4:])
	elemSize := binary.LittleEndian.Uint32(data[offset+8:])

	var size int32
	if top >= 0 {
		size = top + 1
	}

	fmt.Printf("Stack: %s\n", stackName)
	fmt.Printf("  Offset:    %d\n", entry.Offset)
	fmt.Printf("  Top:       %d\n", top)
	fmt.Printf("  Capacity:  %d\n", capacity)
	fmt.Printf("  Elem Size: %d bytes\n", elemSize)
	fmt.Printf("  Size:      %d elements\n", size)
	fmt.Printf("  Empty:     %v\n", top < 0)
	fmt.Printf("  Full:      %v\n", top >= int32(capacity)-1)

	return nil
}

// cmdSemaphore inspects a semaphore
func cmdSemaphore(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("usage: zeroipc semaphore <shm> <name>")
	}

	name := normalizeName(args[0])
	semName := args[1]

	mem, err := zeroipc.OpenMemory(name, 64)
	if err != nil {
		return fmt.Errorf("failed to open %s: %w", name, err)
	}
	defer mem.Close()

	entry := mem.Find(semName)
	if entry == nil {
		return fmt.Errorf("semaphore '%s' not found", semName)
	}

	// Read semaphore header
	offset := int(entry.Offset)
	data := mem.Data()
	count := int32(binary.LittleEndian.Uint32(data[offset:]))
	waiting := int32(binary.LittleEndian.Uint32(data[offset+4:]))
	maxCount := int32(binary.LittleEndian.Uint32(data[offset+8:]))

	fmt.Printf("Semaphore: %s\n", semName)
	fmt.Printf("  Offset:    %d\n", entry.Offset)
	fmt.Printf("  Count:     %d\n", count)
	fmt.Printf("  Waiting:   %d\n", waiting)
	fmt.Printf("  Max Count: %d", maxCount)
	if maxCount == 0 {
		fmt.Print(" (unbounded)")
	} else if maxCount == 1 {
		fmt.Print(" (binary/mutex)")
	}
	fmt.Println()

	return nil
}

// cmdMutex inspects a mutex (which is a binary semaphore)
func cmdMutex(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("usage: zeroipc mutex <shm> <name>")
	}

	name := normalizeName(args[0])
	mtxName := args[1]

	mem, err := zeroipc.OpenMemory(name, 64)
	if err != nil {
		return fmt.Errorf("failed to open %s: %w", name, err)
	}
	defer mem.Close()

	entry := mem.Find(mtxName)
	if entry == nil {
		return fmt.Errorf("mutex '%s' not found", mtxName)
	}

	// Read semaphore header (mutex is a binary semaphore)
	offset := int(entry.Offset)
	data := mem.Data()
	count := int32(binary.LittleEndian.Uint32(data[offset:]))
	waiting := int32(binary.LittleEndian.Uint32(data[offset+4:]))
	maxCount := int32(binary.LittleEndian.Uint32(data[offset+8:]))

	fmt.Printf("Mutex: %s\n", mtxName)
	fmt.Printf("  Offset:  %d\n", entry.Offset)
	fmt.Printf("  Locked:  %v\n", count == 0)
	fmt.Printf("  Waiting: %d\n", waiting)

	if maxCount != 1 {
		fmt.Printf("  Warning: max_count=%d (expected 1 for mutex)\n", maxCount)
	}

	return nil
}

// cmdBarrier inspects a barrier
func cmdBarrier(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("usage: zeroipc barrier <shm> <name>")
	}

	name := normalizeName(args[0])
	barrierName := args[1]

	mem, err := zeroipc.OpenMemory(name, 64)
	if err != nil {
		return fmt.Errorf("failed to open %s: %w", name, err)
	}
	defer mem.Close()

	entry := mem.Find(barrierName)
	if entry == nil {
		return fmt.Errorf("barrier '%s' not found", barrierName)
	}

	// Read barrier header
	offset := int(entry.Offset)
	data := mem.Data()
	arrived := int32(binary.LittleEndian.Uint32(data[offset:]))
	generation := int32(binary.LittleEndian.Uint32(data[offset+4:]))
	numParticipants := int32(binary.LittleEndian.Uint32(data[offset+8:]))

	fmt.Printf("Barrier: %s\n", barrierName)
	fmt.Printf("  Offset:       %d\n", entry.Offset)
	fmt.Printf("  Arrived:      %d / %d\n", arrived, numParticipants)
	fmt.Printf("  Generation:   %d\n", generation)
	fmt.Printf("  Participants: %d\n", numParticipants)

	return nil
}

// cmdLatch inspects a latch
func cmdLatch(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("usage: zeroipc latch <shm> <name>")
	}

	name := normalizeName(args[0])
	latchName := args[1]

	mem, err := zeroipc.OpenMemory(name, 64)
	if err != nil {
		return fmt.Errorf("failed to open %s: %w", name, err)
	}
	defer mem.Close()

	entry := mem.Find(latchName)
	if entry == nil {
		return fmt.Errorf("latch '%s' not found", latchName)
	}

	// Read latch header
	offset := int(entry.Offset)
	data := mem.Data()
	count := int32(binary.LittleEndian.Uint32(data[offset:]))
	initialCount := int32(binary.LittleEndian.Uint32(data[offset+4:]))

	fmt.Printf("Latch: %s\n", latchName)
	fmt.Printf("  Offset:        %d\n", entry.Offset)
	fmt.Printf("  Count:         %d / %d\n", count, initialCount)
	fmt.Printf("  Initial Count: %d\n", initialCount)
	fmt.Printf("  Released:      %v\n", count == 0)

	return nil
}

// cmdOnce inspects a once flag
func cmdOnce(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("usage: zeroipc once <shm> <name>")
	}

	name := normalizeName(args[0])
	onceName := args[1]

	mem, err := zeroipc.OpenMemory(name, 64)
	if err != nil {
		return fmt.Errorf("failed to open %s: %w", name, err)
	}
	defer mem.Close()

	entry := mem.Find(onceName)
	if entry == nil {
		return fmt.Errorf("once flag '%s' not found", onceName)
	}

	// Read once state
	offset := int(entry.Offset)
	data := mem.Data()
	state := binary.LittleEndian.Uint32(data[offset:])

	fmt.Printf("Once: %s\n", onceName)
	fmt.Printf("  Offset: %d\n", entry.Offset)
	fmt.Printf("  Called: %v\n", state == 1)

	return nil
}
