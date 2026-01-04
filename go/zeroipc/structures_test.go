package zeroipc

import (
	"sync"
	"testing"
)

func TestQueueBasic(t *testing.T) {
	name := "/test_go_queue"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	q, err := NewQueue[int32](mem, "test_queue", 10)
	if err != nil {
		t.Fatalf("NewQueue failed: %v", err)
	}

	// Test empty
	if !q.Empty() {
		t.Error("New queue should be empty")
	}

	// Push and pop
	if !q.Push(42) {
		t.Error("Push should succeed")
	}

	if q.Empty() {
		t.Error("Queue should not be empty after push")
	}

	val, ok := q.Pop()
	if !ok {
		t.Error("Pop should succeed")
	}
	if val != 42 {
		t.Errorf("Pop returned %d, expected 42", val)
	}

	if !q.Empty() {
		t.Error("Queue should be empty after pop")
	}

	// Test full
	for i := 0; i < 9; i++ { // capacity-1 to leave room
		if !q.Push(int32(i)) {
			t.Errorf("Push %d should succeed", i)
		}
	}

	if q.Full() {
		t.Log("Queue is full as expected")
	}
}

func TestQueueConcurrent(t *testing.T) {
	name := "/test_go_queue_concurrent"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	q, err := NewQueue[int64](mem, "concurrent_queue", 1000)
	if err != nil {
		t.Fatalf("NewQueue failed: %v", err)
	}

	var wg sync.WaitGroup
	numProducers := 4
	numConsumers := 4
	itemsPerProducer := 100

	// Producers
	for p := 0; p < numProducers; p++ {
		wg.Add(1)
		go func(producerID int) {
			defer wg.Done()
			for i := 0; i < itemsPerProducer; i++ {
				val := int64(producerID*1000 + i)
				for !q.Push(val) {
					// Retry if full
				}
			}
		}(p)
	}

	// Consumers
	consumed := make(chan int64, numProducers*itemsPerProducer)
	for c := 0; c < numConsumers; c++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for {
				if val, ok := q.Pop(); ok {
					consumed <- val
				}
				if len(consumed) >= numProducers*itemsPerProducer {
					return
				}
			}
		}()
	}

	wg.Wait()
	close(consumed)

	count := 0
	for range consumed {
		count++
	}

	if count != numProducers*itemsPerProducer {
		t.Errorf("Consumed %d items, expected %d", count, numProducers*itemsPerProducer)
	}
}

func TestStackBasic(t *testing.T) {
	name := "/test_go_stack"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	s, err := NewStack[float64](mem, "test_stack", 10)
	if err != nil {
		t.Fatalf("NewStack failed: %v", err)
	}

	// Test empty
	if !s.Empty() {
		t.Error("New stack should be empty")
	}

	// Push and pop (LIFO order)
	s.Push(1.0)
	s.Push(2.0)
	s.Push(3.0)

	val, ok := s.Pop()
	if !ok || val != 3.0 {
		t.Errorf("Pop returned %v, %v, expected 3.0, true", val, ok)
	}

	val, ok = s.Pop()
	if !ok || val != 2.0 {
		t.Errorf("Pop returned %v, %v, expected 2.0, true", val, ok)
	}

	val, ok = s.Pop()
	if !ok || val != 1.0 {
		t.Errorf("Pop returned %v, %v, expected 1.0, true", val, ok)
	}

	if !s.Empty() {
		t.Error("Stack should be empty")
	}
}

func TestSemaphoreBasic(t *testing.T) {
	name := "/test_go_semaphore"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	sem, err := NewSemaphore(mem, "test_sem", 3, 5)
	if err != nil {
		t.Fatalf("NewSemaphore failed: %v", err)
	}

	if sem.Count() != 3 {
		t.Errorf("Count = %d, expected 3", sem.Count())
	}

	// Acquire 3 times
	if !sem.TryAcquire() {
		t.Error("TryAcquire 1 should succeed")
	}
	if !sem.TryAcquire() {
		t.Error("TryAcquire 2 should succeed")
	}
	if !sem.TryAcquire() {
		t.Error("TryAcquire 3 should succeed")
	}

	// Should fail now
	if sem.TryAcquire() {
		t.Error("TryAcquire 4 should fail")
	}

	if sem.Count() != 0 {
		t.Errorf("Count = %d, expected 0", sem.Count())
	}

	// Release
	sem.Release()
	sem.Release()

	if sem.Count() != 2 {
		t.Errorf("Count = %d, expected 2", sem.Count())
	}
}

func TestMutexBasic(t *testing.T) {
	name := "/test_go_mutex"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	mtx, err := NewMutex(mem, "test_mutex")
	if err != nil {
		t.Fatalf("NewMutex failed: %v", err)
	}

	if mtx.IsLocked() {
		t.Error("New mutex should not be locked")
	}

	mtx.Lock()

	if !mtx.IsLocked() {
		t.Error("Mutex should be locked after Lock()")
	}

	// TryLock should fail
	if mtx.TryLock() {
		t.Error("TryLock should fail when locked")
	}

	mtx.Unlock()

	if mtx.IsLocked() {
		t.Error("Mutex should not be locked after Unlock()")
	}
}

func TestBarrierBasic(t *testing.T) {
	name := "/test_go_barrier"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	numParticipants := int32(3)
	barrier, err := NewBarrier(mem, "test_barrier", numParticipants)
	if err != nil {
		t.Fatalf("NewBarrier failed: %v", err)
	}

	var wg sync.WaitGroup
	results := make(chan int, numParticipants)

	for i := int32(0); i < numParticipants; i++ {
		wg.Add(1)
		go func(id int32) {
			defer wg.Done()
			// All goroutines wait at the barrier
			barrier.Wait()
			results <- int(id)
		}(i)
	}

	wg.Wait()
	close(results)

	count := 0
	for range results {
		count++
	}

	if count != int(numParticipants) {
		t.Errorf("Only %d goroutines completed, expected %d", count, numParticipants)
	}
}

func TestLatchBasic(t *testing.T) {
	name := "/test_go_latch"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	latch, err := NewLatch(mem, "test_latch", 3)
	if err != nil {
		t.Fatalf("NewLatch failed: %v", err)
	}

	if latch.Count() != 3 {
		t.Errorf("Count = %d, expected 3", latch.Count())
	}

	if latch.TryWait() {
		t.Error("TryWait should return false")
	}

	latch.CountDownOne()
	latch.CountDownOne()

	if latch.Count() != 1 {
		t.Errorf("Count = %d, expected 1", latch.Count())
	}

	latch.CountDownOne()

	if !latch.TryWait() {
		t.Error("TryWait should return true after countdown")
	}

	if latch.Count() != 0 {
		t.Errorf("Count = %d, expected 0", latch.Count())
	}
}

func TestOnceBasic(t *testing.T) {
	name := "/test_go_once"
	size := 1024 * 1024

	UnlinkName(name)

	mem, err := NewMemory(name, size, 64)
	if err != nil {
		t.Fatalf("NewMemory failed: %v", err)
	}
	defer mem.Close()
	defer mem.Unlink()

	once, err := NewOnce(mem, "test_once")
	if err != nil {
		t.Fatalf("NewOnce failed: %v", err)
	}

	if once.IsCalled() {
		t.Error("New Once should not be called")
	}

	callCount := 0
	once.Do(func() {
		callCount++
	})

	if callCount != 1 {
		t.Errorf("Function called %d times, expected 1", callCount)
	}

	if !once.IsCalled() {
		t.Error("Once should be called after Do()")
	}

	// Second call should not execute
	once.Do(func() {
		callCount++
	})

	if callCount != 1 {
		t.Errorf("Function called %d times, expected 1", callCount)
	}
}
