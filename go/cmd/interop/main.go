// Test Go <-> C++ interoperability for ZeroIPC
//
// Run with: go run test_go_cpp_interop.go
package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"github.com/spinoza/zeroipc/zeroipc"
)

var cppIncludePath string

func init() {
	// Find the cpp/include directory relative to current working directory
	// Try common locations
	candidates := []string{
		"../cpp/include",     // from go/
		"cpp/include",        // from project root
		"../../cpp/include",  // from go/cmd/interop
	}

	for _, path := range candidates {
		if _, err := os.Stat(filepath.Join(path, "zeroipc/memory.h")); err == nil {
			cppIncludePath, _ = filepath.Abs(path)
			return
		}
	}

	// Fallback to absolute path
	cppIncludePath = "/home/spinoza/github/beta/zeroipc/cpp/include"
}

func compileCpp(code, outputPath string) error {
	srcPath := outputPath + ".cpp"
	if err := os.WriteFile(srcPath, []byte(code), 0644); err != nil {
		return fmt.Errorf("write source: %w", err)
	}

	cmd := exec.Command("g++", "-std=c++23", "-I"+cppIncludePath, srcPath,
		"-o", outputPath, "-lrt", "-lpthread")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("compile: %s\n%s", err, output)
	}
	return nil
}

func runCpp(exePath string) (string, error) {
	cmd := exec.Command(exePath)
	output, err := cmd.CombinedOutput()
	return string(output), err
}

// Test: Go creates Array, C++ reads it
func testGoCreatesCppReads() bool {
	fmt.Println("\n=== Array: Go creates, C++ reads ===")

	shmName := "/go_cpp_array_test"
	zeroipc.UnlinkName(shmName)

	// Go creates shared memory and array
	mem, err := zeroipc.NewMemory(shmName, 1024*1024, 64)
	if err != nil {
		fmt.Printf("Go: Failed to create memory: %v\n", err)
		return false
	}
	defer mem.Close()
	defer mem.Unlink()

	arr, err := zeroipc.NewArray[float64](mem, "temperatures", 5)
	if err != nil {
		fmt.Printf("Go: Failed to create array: %v\n", err)
		return false
	}

	// Write test values
	arr.Set(0, 10.5)
	arr.Set(1, 20.5)
	arr.Set(2, 30.5)
	arr.Set(3, 40.5)
	arr.Set(4, 50.5)

	fmt.Println("Go: Created array with values [10.5, 20.5, 30.5, 40.5, 50.5]")

	// C++ code to read the array
	cppCode := fmt.Sprintf(`
#include <zeroipc/memory.h>
#include <zeroipc/array.h>
#include <iostream>
#include <cmath>

int main() {
    zeroipc::Memory mem("%s");
    zeroipc::Array<double> arr(mem, "temperatures");

    std::cout << "C++: Opened array, capacity=" << arr.capacity() << std::endl;

    double expected[] = {10.5, 20.5, 30.5, 40.5, 50.5};
    for (size_t i = 0; i < 5; i++) {
        std::cout << "C++: arr[" << i << "] = " << arr[i] << std::endl;
        if (std::abs(arr[i] - expected[i]) > 0.001) {
            std::cerr << "ERROR: Expected " << expected[i] << std::endl;
            return 1;
        }
    }

    std::cout << "C++: All values match!" << std::endl;
    return 0;
}
`, shmName)

	if err := compileCpp(cppCode, "/tmp/go_cpp_read"); err != nil {
		fmt.Printf("C++ compile error: %v\n", err)
		return false
	}

	output, err := runCpp("/tmp/go_cpp_read")
	fmt.Print(output)
	if err != nil {
		fmt.Printf("C++ run error: %v\n", err)
		return false
	}

	fmt.Println("PASSED")
	return true
}

// Test: C++ creates Array, Go reads it
func testCppCreatesGoReads() bool {
	fmt.Println("\n=== Array: C++ creates, Go reads ===")

	shmName := "/cpp_go_array_test"
	zeroipc.UnlinkName(shmName)

	// C++ code to create the array
	cppCode := fmt.Sprintf(`
#include <zeroipc/memory.h>
#include <zeroipc/array.h>
#include <iostream>

int main() {
    zeroipc::Memory mem("%s", 1024*1024);
    zeroipc::Array<int32_t> arr(mem, "counters", 10);

    for (int i = 0; i < 10; i++) {
        arr[i] = i * 100;
    }

    std::cout << "C++: Created array with values [0, 100, 200, ..., 900]" << std::endl;
    return 0;
}
`, shmName)

	if err := compileCpp(cppCode, "/tmp/cpp_go_create"); err != nil {
		fmt.Printf("C++ compile error: %v\n", err)
		return false
	}

	output, err := runCpp("/tmp/cpp_go_create")
	fmt.Print(output)
	if err != nil {
		fmt.Printf("C++ run error: %v\n", err)
		return false
	}

	// Go opens and reads
	mem, err := zeroipc.OpenMemory(shmName, 64)
	if err != nil {
		fmt.Printf("Go: Failed to open memory: %v\n", err)
		return false
	}
	defer mem.Close()
	defer mem.Unlink()

	arr, err := zeroipc.OpenArray[int32](mem, "counters")
	if err != nil {
		fmt.Printf("Go: Failed to open array: %v\n", err)
		return false
	}

	fmt.Printf("Go: Opened array, capacity=%d\n", arr.Capacity())

	for i := 0; i < 10; i++ {
		expected := int32(i * 100)
		got := arr.Get(i)
		fmt.Printf("Go: arr[%d] = %d\n", i, got)
		if got != expected {
			fmt.Printf("ERROR: Expected %d\n", expected)
			return false
		}
	}

	fmt.Println("Go: All values match!")
	fmt.Println("PASSED")
	return true
}

// Test: Bidirectional - Go writes, C++ modifies, Go reads back
func testBidirectional() bool {
	fmt.Println("\n=== Array: Bidirectional (Go -> C++ -> Go) ===")

	shmName := "/bidirectional_test"
	zeroipc.UnlinkName(shmName)

	// Go creates and writes
	mem, err := zeroipc.NewMemory(shmName, 1024*1024, 64)
	if err != nil {
		fmt.Printf("Go: Failed to create memory: %v\n", err)
		return false
	}
	defer mem.Close()
	defer mem.Unlink()

	arr, err := zeroipc.NewArray[int64](mem, "shared_data", 3)
	if err != nil {
		fmt.Printf("Go: Failed to create array: %v\n", err)
		return false
	}

	arr.Set(0, 100)
	arr.Set(1, 200)
	arr.Set(2, 300)
	fmt.Println("Go: Created array [100, 200, 300]")

	// C++ doubles each value
	cppCode := fmt.Sprintf(`
#include <zeroipc/memory.h>
#include <zeroipc/array.h>
#include <iostream>

int main() {
    zeroipc::Memory mem("%s");
    zeroipc::Array<int64_t> arr(mem, "shared_data");

    std::cout << "C++: Read values [" << arr[0] << ", " << arr[1] << ", " << arr[2] << "]" << std::endl;

    // Double each value
    arr[0] *= 2;
    arr[1] *= 2;
    arr[2] *= 2;

    std::cout << "C++: Doubled to [" << arr[0] << ", " << arr[1] << ", " << arr[2] << "]" << std::endl;
    return 0;
}
`, shmName)

	if err := compileCpp(cppCode, "/tmp/bidirectional"); err != nil {
		fmt.Printf("C++ compile error: %v\n", err)
		return false
	}

	output, err := runCpp("/tmp/bidirectional")
	fmt.Print(output)
	if err != nil {
		fmt.Printf("C++ run error: %v\n", err)
		return false
	}

	// Go reads back the modified values
	expected := []int64{200, 400, 600}
	for i := 0; i < 3; i++ {
		got := arr.Get(i)
		fmt.Printf("Go: arr[%d] = %d (expected %d)\n", i, got, expected[i])
		if got != expected[i] {
			fmt.Println("ERROR: Value mismatch!")
			return false
		}
	}

	fmt.Println("Go: All modified values correct!")
	fmt.Println("PASSED")
	return true
}

// Test: Go creates Queue, C++ reads
func testQueueGoCreatesCppReads() bool {
	fmt.Println("\n=== Queue: Go creates, C++ reads ===")

	shmName := "/go_cpp_queue_test"
	zeroipc.UnlinkName(shmName)

	mem, err := zeroipc.NewMemory(shmName, 1024*1024, 64)
	if err != nil {
		fmt.Printf("Go: Failed to create memory: %v\n", err)
		return false
	}
	defer mem.Close()
	defer mem.Unlink()

	q, err := zeroipc.NewQueue[int32](mem, "test_queue", 10)
	if err != nil {
		fmt.Printf("Go: Failed to create queue: %v\n", err)
		return false
	}

	// Push values
	q.Push(100)
	q.Push(200)
	q.Push(300)

	fmt.Println("Go: Created queue with values [100, 200, 300]")

	cppCode := fmt.Sprintf(`
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>
#include <iostream>

int main() {
    zeroipc::Memory mem("%s");
    zeroipc::Queue<int32_t> q(mem, "test_queue");

    std::cout << "C++: Opened queue, size=" << q.size() << std::endl;

    int32_t expected[] = {100, 200, 300};
    for (int i = 0; i < 3; i++) {
        auto val = q.pop();
        if (!val || *val != expected[i]) {
            std::cerr << "ERROR: Expected " << expected[i] << std::endl;
            return 1;
        }
        std::cout << "C++: Popped " << *val << std::endl;
    }

    std::cout << "C++: All values match!" << std::endl;
    return 0;
}
`, shmName)

	if err := compileCpp(cppCode, "/tmp/queue_go_cpp"); err != nil {
		fmt.Printf("C++ compile error: %v\n", err)
		return false
	}

	output, err := runCpp("/tmp/queue_go_cpp")
	fmt.Print(output)
	if err != nil {
		fmt.Printf("C++ run error: %v\n", err)
		return false
	}

	fmt.Println("PASSED")
	return true
}

// Test: C++ creates Stack, Go reads
func testStackCppCreatesGoReads() bool {
	fmt.Println("\n=== Stack: C++ creates, Go reads ===")

	shmName := "/cpp_go_stack_test"
	zeroipc.UnlinkName(shmName)

	cppCode := fmt.Sprintf(`
#include <zeroipc/memory.h>
#include <zeroipc/stack.h>
#include <iostream>

int main() {
    zeroipc::Memory mem("%s", 1024*1024);
    zeroipc::Stack<double> s(mem, "test_stack", 10);

    s.push(1.5);
    s.push(2.5);
    s.push(3.5);

    std::cout << "C++: Created stack with values [1.5, 2.5, 3.5]" << std::endl;
    std::cout << "C++: Size=" << s.size() << std::endl;
    return 0;
}
`, shmName)

	if err := compileCpp(cppCode, "/tmp/stack_cpp_go"); err != nil {
		fmt.Printf("C++ compile error: %v\n", err)
		return false
	}

	output, err := runCpp("/tmp/stack_cpp_go")
	fmt.Print(output)
	if err != nil {
		fmt.Printf("C++ run error: %v\n", err)
		return false
	}

	// Go opens and reads (LIFO order)
	mem, err := zeroipc.OpenMemory(shmName, 64)
	if err != nil {
		fmt.Printf("Go: Failed to open memory: %v\n", err)
		return false
	}
	defer mem.Close()
	defer mem.Unlink()

	s, err := zeroipc.OpenStack[float64](mem, "test_stack")
	if err != nil {
		fmt.Printf("Go: Failed to open stack: %v\n", err)
		return false
	}

	fmt.Printf("Go: Opened stack, size=%d\n", s.Size())

	// Pop in LIFO order: 3.5, 2.5, 1.5
	expected := []float64{3.5, 2.5, 1.5}
	for i, exp := range expected {
		val, ok := s.Pop()
		if !ok {
			fmt.Printf("ERROR: Pop %d failed\n", i)
			return false
		}
		fmt.Printf("Go: Popped %v\n", val)
		if val != exp {
			fmt.Printf("ERROR: Expected %v, got %v\n", exp, val)
			return false
		}
	}

	fmt.Println("Go: All values match (LIFO order)!")
	fmt.Println("PASSED")
	return true
}

// Test: Go creates Semaphore, C++ uses it
func testSemaphoreInterop() bool {
	fmt.Println("\n=== Semaphore: Go creates, C++ uses ===")

	shmName := "/go_cpp_sem_test"
	zeroipc.UnlinkName(shmName)

	mem, err := zeroipc.NewMemory(shmName, 1024*1024, 64)
	if err != nil {
		fmt.Printf("Go: Failed to create memory: %v\n", err)
		return false
	}
	defer mem.Close()
	defer mem.Unlink()

	sem, err := zeroipc.NewSemaphore(mem, "test_sem", 5, 10)
	if err != nil {
		fmt.Printf("Go: Failed to create semaphore: %v\n", err)
		return false
	}

	fmt.Printf("Go: Created semaphore with count=%d, max=%d\n", sem.Count(), sem.MaxCount())

	cppCode := fmt.Sprintf(`
#include <zeroipc/memory.h>
#include <zeroipc/semaphore.h>
#include <iostream>

int main() {
    zeroipc::Memory mem("%s");
    zeroipc::Semaphore sem(mem, "test_sem");

    std::cout << "C++: Opened semaphore, count=" << sem.count() << std::endl;

    // Acquire 3 times
    for (int i = 0; i < 3; i++) {
        if (!sem.try_acquire()) {
            std::cerr << "ERROR: Acquire " << i << " failed" << std::endl;
            return 1;
        }
        std::cout << "C++: Acquired, count=" << sem.count() << std::endl;
    }

    // Release once
    sem.release();
    std::cout << "C++: Released, count=" << sem.count() << std::endl;

    return 0;
}
`, shmName)

	if err := compileCpp(cppCode, "/tmp/sem_interop"); err != nil {
		fmt.Printf("C++ compile error: %v\n", err)
		return false
	}

	output, err := runCpp("/tmp/sem_interop")
	fmt.Print(output)
	if err != nil {
		fmt.Printf("C++ run error: %v\n", err)
		return false
	}

	// Verify Go sees the updated count (5 - 3 + 1 = 3)
	fmt.Printf("Go: Final count=%d (expected 3)\n", sem.Count())
	if sem.Count() != 3 {
		fmt.Println("ERROR: Count mismatch!")
		return false
	}

	fmt.Println("PASSED")
	return true
}

func main() {
	// Change to interop directory for relative paths
	if err := os.Chdir(filepath.Dir(os.Args[0])); err != nil {
		// If that fails, try current directory
	}

	fmt.Println("======================================")
	fmt.Println("Go <-> C++ Interoperability Tests")
	fmt.Println("======================================")

	tests := []struct {
		name string
		fn   func() bool
	}{
		{"Array: Go creates, C++ reads", testGoCreatesCppReads},
		{"Array: C++ creates, Go reads", testCppCreatesGoReads},
		{"Array: Bidirectional", testBidirectional},
		{"Queue: Go creates, C++ reads", testQueueGoCreatesCppReads},
		{"Stack: C++ creates, Go reads", testStackCppCreatesGoReads},
		{"Semaphore: Go creates, C++ uses", testSemaphoreInterop},
	}

	passed := 0
	for _, test := range tests {
		if test.fn() {
			passed++
		}
	}

	fmt.Println("\n======================================")
	fmt.Printf("Results: %d/%d tests passed\n", passed, len(tests))
	if passed == len(tests) {
		fmt.Println("ALL TESTS PASSED!")
	}
	fmt.Println("======================================")

	if passed != len(tests) {
		os.Exit(1)
	}
}
