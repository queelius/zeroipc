# Shared Memory Data Structures Design Document

## 1. Introduction

This document describes the design and implementation of shared memory data structures (`shm_array<T>` and `shm_queue<T>`) using POSIX shared memory. These structures are designed to provide efficient, inter-process shared storage with dynamic discovery capabilities.

## 2. System Architecture

The shared memory data structures are fundamentally built on top of three components:

- `posix_shm`: Manages the underlying POSIX shared memory segment. A single may be used to manage multiple shared data structures.
- `shm_table`: Provides metadata management for shared structures. Allows for dynamic discovery, creation, and deletion of shared data structures.
- `shm_span<T>`: Base class providing common functionality for shared memory data structures like `shm_array<T>`. It provides basic memory layout and access operations.

We implement many different data structures over shared memory that are optimized to be used with the shared memory system:

- `shm_bitarray`: Bit array data structure over shared memory. Conceptually, it is like `shm_array<bool>`, but each element is a single bit, which is useful for storing flags or boolean values compactly.

- `shm_array<T>`: Array data structure over shared memory. It provides standard array operations (read element at index, write element at index, iterate over elements). We implement it using a contiguous block of bytes from the shared memory segment. Reading does not need any synchronization, but writing does. We use a compare-and-swap operation to update the value of an element in the array. This is a lockfree operation that allows multiple processes to write to the array concurrently. Growing or shrinking the array is a complex operation that requires allocating a new block of shared memory, copying the elements from the old array to the new array, and updating the metadata in the shared memory segment. This is a slow operation, and it prevents any writes to the array while it is happening.
  
- `shm_queue<T>`: Queue data structure over shared memory. It uses a circular buffer implementation for efficient space usage. It provides standard queue operations (enqueue, dequeue).
  
- `shm_stack<T>`: Stack data structure over shared memory. It provides standard stack operations (push, pop).
  
- `shm_set<T>`: Set data structure over shared memory. It provides standard set operations (insert, erase, contains). We implement it using a hash table with open addressing and linear probing, where we represent the elements as nodes with a flag indicating whether the node is occupied. This representation is multi-process friendly, as all operations are lockfree and can be performed concurrently by multiple processes.
  
  - To erase an element, we do a compare-and-swap operation to set the flag to false.
  - To insert an element, we find the first free slot in the hash table and do a compare-and-swap operation to set the flag to true. If it fails, we try the next slot. If it suceeds, we do a compare-and-swap operation to set the value of the node. If it fails, we try the next slot.
  
- `shm_tree<T>`: Tree data structure over shared memory. It provides standard tree operations (insert, erase, find, get). We represent the tree as an array of nodes, where each node has a payload of type `T`, and a parent offset in the shared memory. This is not the most efficient representation of a tree, but it allows us to use a simple array-based representation that can be easily stored in shared memory. When we want to add a node, we simply find the first free slot in the array and insert the node there. This is not the most efficient way to store a tree, but it is simple and allows us to store the tree in shared memory. We could have used a more efficient representation, such as a binary search tree or a B-tree, but these would have been more complex to implement and would have required more memory overhead.

- `shm_map<K,V>`: Map data structure over shared memory. We represent this as an array of key-index pairs, sorted by key, so `K` values must be less-than comparable. The index represents the offset of the value in the shared memory. We provide standard map operations (insert, erase, find, set, get). We actually store the key-index pairs and the values in separate arrays. This decreases the granularity of the data structure, which means we can "fit" the map into existing free blocks of the contiguous shared memory. Since many processes may be simultaneously using data structures in a shared memory segment, we avoid any techniques to automatically defragment the shared memory. Instead, we rely on the user to manage the shared memory segment and the data structures within it. The `shm_map` is a good example of this, as it uses separate data structures in the shared memory segment to store the keys and values. This may allow the map to fit into existing free blocks of the shared memory segment, even if the keys and values are not contiguous in memory.
  
- `shm_graph<V,E>`: Graph data structure over shared memory. It provides standard graph operations (`add_vertex`, `add_edge`, `remove_vertex`, `remove_edge`, `get_vertex`, `get_edge`). We represent the graph as an array of vertices and an array of edges. The vertices are stored in a contiguous block of shared memory, and the edges are stored in a contiguous block of shared memory. This allows the graph to more easily fit into existing free blocks of the shared memory segment.

## 3. Component Details

### 3.1 `posix_shm`

Responsible for creating, opening, and managing POSIX shared memory segments. It provides a simple interface for allocating and accessing shared memory.

Key features:

- Reference counting for automatic cleanup
- Thread-safe operations
- Error handling for system calls

### 3.2 `shm_table`

Manages metadata for shared structures within the shared memory segment. It allows for dynamic discovery and creation of shared data structures.

Key features:

- Fixed-size entries for constant-time lookup
- Name-based discovery of shared structures
- Storage of size and offset information

### 3.3 `shm_span<T>`

Base class that provides common functionality for spanning a region of shared memory.

Key features:

- Basic memory layout and access operations
- Common interface for derived classes

### 3.4 `shm_array<T>`

Implements an array interface over a region of shared memory.

Key features:

- Dynamic creation and discovery of arrays
- Standard array operations (element access, iterators)
- Bounds checking for safe access
- Automatic registration with shm_table

### 3.5 `shm_queue<T>`

Implements a queue interface over a region of shared memory.

Key features:

- Dynamic creation and discovery of queues
- Standard queue operations (enqueue, dequeue)
- Circular buffer implementation for efficient space usage
- Automatic registration with shm_table

## 4. Algorithms and Data Structures

### 4.1 Memory Layout

The shared memory is laid out as follows:

```
[shm_table][data structure 1][data structure 2]...[data structure N]
```

### 4.2 Metadata Management

Metadata is stored in the `shm_table` at the beginning of the shared memory segment. Each entry contains:

- Unique name (fixed-size char array)
- Offset from the start of the shared memory (in bytes)
- Type of the data structure (fixed-size char array)
- Size of the data structure (in bytes)

### 4.3 Data Structure Operations

- Array Operations:
  - Element Access: O(1) time complexity, direct indexing into shared memory
  - Iteration: Linear time, using pointer arithmetic

- Queue Operations:
  - Enqueue/Dequeue: O(1) time complexity, using circular buffer implementation

- Creation/Discovery/Destruction: O(n) in the number of existing structures, due to linear search in the metadata table

## 5. Synchronization and Thread Safety

- The `posix_shm` class ensures thread-safe reference counting.
- The data structures themselves may or may not be thread-safe; external synchronization is required for concurrent access from multiple threads or processes.

## 6. Error Handling

Exceptions are used to handle error conditions, including:

- Shared memory allocation failures
- Size mismatches when opening existing structures
- Out-of-bounds access attempts

## 7. Performance Considerations

- Minimal overhead for element access (direct memory access)
- Constant-time complexity for most operations
- Potential for false sharing if multiple small structures are created adjacently

## 8. Conclusion

The `shm_array<T>` and `shm_queue<T>` provide flexible and efficient solutions for sharing data structures between processes using POSIX shared memory. Their design allows for dynamic discovery and creation of shared structures, making them suitable for a wide range of inter-process communication scenarios.