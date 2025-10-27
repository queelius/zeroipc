# Quick Start

Get up and running with ZeroIPC in 5 minutes! This guide shows you the fastest path from installation to your first working program.

## Your First Shared Memory Program

Let's create a simple producer-consumer example where a C++ program writes data and a Python program reads it.

### Step 1: Create the Producer (C++)

Create a file called `producer.cpp`:

```cpp
#include <zeroipc/memory.h>
#include <zeroipc/array.h>
#include <iostream>

int main() {
    // Create 1MB of shared memory named "/demo"
    zeroipc::Memory mem("/demo", 1024 * 1024);
    
    // Create an array of 100 integers named "numbers"
    zeroipc::Array<int> numbers(mem, "numbers", 100);
    
    // Fill the array with values
    for (int i = 0; i < 100; ++i) {
        numbers[i] = i * 2;  // Even numbers
    }
    
    std::cout << "Producer: Created array with 100 numbers\n";
    std::cout << "Producer: First value = " << numbers[0] << "\n";
    std::cout << "Producer: Last value = " << numbers[99] << "\n";
    std::cout << "Producer: Keeping shared memory alive. Press Ctrl+C to exit.\n";
    
    // Keep running so consumer can read
    std::this_thread::sleep_for(std::chrono::seconds(60));
    
    return 0;
}
```

### Step 2: Compile the Producer

```bash
g++ -std=c++23 -o producer producer.cpp -lrt -lpthread
```

### Step 3: Create the Consumer (Python)

Create a file called `consumer.py`:

```python
from zeroipc import Memory, Array
import numpy as np

# Open the existing shared memory
mem = Memory("/demo")

# Access the array (specify type as int32)
numbers = Array(mem, "numbers", dtype=np.int32)

print(f"Consumer: Found array with {len(numbers)} numbers")
print(f"Consumer: First value = {numbers[0]}")
print(f"Consumer: Last value = {numbers[99]}")
print(f"Consumer: Sum of all values = {np.sum(numbers[:])}")

# Verify the values
assert numbers[0] == 0
assert numbers[50] == 100
assert numbers[99] == 198

print("Consumer: All values correct!")
```

### Step 4: Run It!

In one terminal:
```bash
./producer
```

In another terminal (while producer is running):
```bash
python consumer.py
```

You should see:

```
# Producer terminal
Producer: Created array with 100 numbers
Producer: First value = 0
Producer: Last value = 198
Producer: Keeping shared memory alive. Press Ctrl+C to exit.

# Consumer terminal
Consumer: Found array with 100 numbers
Consumer: First value = 0
Consumer: Last value = 198
Consumer: Sum of all values = 9900
Consumer: All values correct!
```

Congratulations! You just created your first cross-language shared memory communication!

## Understanding What Happened

Let's break down what just occurred:

1. **Memory Creation**: The C++ producer created a shared memory segment named "/demo"
2. **Structure Registration**: An array called "numbers" was registered in the metadata table
3. **Data Writing**: The producer filled the array with even numbers
4. **Cross-Language Access**: Python opened the same memory and read the data
5. **Type Specification**: Python used NumPy's `int32` to match C++'s `int`

### Key Concepts

#### Shared Memory Name
```cpp
zeroipc::Memory mem("/demo", 1024 * 1024);
```
The name `/demo` is global on the system. Any process can access it.

#### Structure Name
```cpp
zeroipc::Array<int> numbers(mem, "numbers", 100);
```
Structures are registered with names in the metadata table for discovery.

#### Type Consistency
```cpp
// C++: int (typically 32-bit)
zeroipc::Array<int> numbers(mem, "numbers", 100);
```
```python
# Python: must match with np.int32
numbers = Array(mem, "numbers", dtype=np.int32)
```

!!! warning "Type Matching"
    You must ensure type sizes match between languages! C++ `int` = Python `np.int32`, C++ `double` = Python `np.float64`.

## Try It: Bidirectional Communication

Now let's make it bidirectional—both processes can read and write!

### Modified Producer

```cpp
#include <zeroipc/memory.h>
#include <zeroipc/queue.h>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    zeroipc::Memory mem("/chat", 1024 * 1024);
    
    // Create two queues for bidirectional messaging
    zeroipc::Queue<int> to_python(mem, "to_python", 10);
    zeroipc::Queue<int> from_python(mem, "from_python", 10);
    
    // Send messages to Python
    for (int i = 1; i <= 5; ++i) {
        to_python.enqueue(i * 10);
        std::cout << "C++: Sent " << i * 10 << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // Receive messages from Python
    for (int i = 0; i < 5; ++i) {
        auto msg = from_python.dequeue();
        if (msg) {
            std::cout << "C++: Received " << *msg << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    return 0;
}
```

### Modified Consumer

```python
from zeroipc import Memory, Queue
import numpy as np
import time

mem = Memory("/chat")

to_python = Queue(mem, "to_python", dtype=np.int32)
from_python = Queue(mem, "from_python", dtype=np.int32)

# Receive from C++
for _ in range(5):
    msg = to_python.dequeue()
    if msg is not None:
        print(f"Python: Received {msg}")
    time.sleep(0.5)

# Send to C++
for i in range(1, 6):
    from_python.enqueue(i * 100)
    print(f"Python: Sent {i * 100}")
    time.sleep(0.5)
```

Compile and run:
```bash
# Terminal 1
g++ -std=c++23 -o chat producer_chat.cpp -lrt -lpthread
./chat

# Terminal 2
python consumer_chat.py
```

## Inspecting with the CLI Tool

ZeroIPC comes with a powerful CLI tool for inspection:

```bash
# List all shared memory segments
zeroipc list

# Show structures in /demo
zeroipc show /demo

# Inspect the array
zeroipc array /demo numbers

# Interactive REPL mode
zeroipc -r
```

In REPL mode:
```
zeroipc> ls /
demo/        1.0 MB      1 structures
chat/        1.0 MB      2 structures

zeroipc> cd /demo
/demo> ls
numbers      array<int>[100]      400 bytes

/demo> cd /
zeroipc> quit
```

## Common Patterns

### Pattern 1: Single Producer, Multiple Consumers

One process writes, many processes read:

```cpp
// Producer
zeroipc::Memory mem("/data", 10*1024*1024);
zeroipc::Array<float> sensors(mem, "temp", 1000);

// Update continuously
while (running) {
    sensors[0] = read_sensor();
}
```

```python
# Consumer 1: Logger
mem = Memory("/data")
temps = Array(mem, "temp", dtype=np.float32)
while True:
    log_temperature(temps[0])

# Consumer 2: Monitor
mem = Memory("/data")
temps = Array(mem, "temp", dtype=np.float32)
while True:
    if temps[0] > 100:
        send_alert()
```

### Pattern 2: Work Queue

Distribute tasks across worker processes:

```cpp
// Manager
zeroipc::Memory mem("/tasks", 10*1024*1024);
zeroipc::Queue<Task> tasks(mem, "work", 100);

for (auto& task : all_tasks) {
    tasks.enqueue(task);
}
```

```python
# Worker (many of these)
mem = Memory("/tasks")
tasks = Queue(mem, "work", dtype=task_dtype)

while True:
    task = tasks.dequeue()
    if task:
        process(task)
```

### Pattern 3: Reactive Stream

Event-driven processing with transformations:

```cpp
zeroipc::Memory mem("/events", 10*1024*1024);
zeroipc::Stream<Event> events(mem, "stream", 1000);

// Create derived streams
auto filtered = events.filter(mem, "important", 
    [](Event& e) { return e.priority > 5; });

auto transformed = filtered.map(mem, "alerts",
    [](Event& e) { return Alert{e}; });
```

## What's Next?

You now know the basics! Here's where to go from here:

1. **[Tutorial](../tutorial/index.md)** - Deep dive into all data structures
2. **[CLI Tool Guide](../cli/index.md)** - Master the virtual filesystem interface
3. **[API Reference](../api/index.md)** - Complete API documentation
4. **[Examples](../examples/index.md)** - Real-world usage patterns
5. **[Best Practices](../best-practices/index.md)** - Tips and pitfalls

### Recommended Learning Path

- **Beginners**: Tutorial → Examples → Best Practices
- **Intermediate**: API Reference → CLI Tool → Advanced Topics
- **Experts**: Architecture → Lock-Free Patterns → Contributing

Happy coding with ZeroIPC!
