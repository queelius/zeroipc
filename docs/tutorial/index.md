# Tutorial

Welcome to the ZeroIPC tutorial! This hands-on guide will take you from basics to advanced usage through practical examples.

## What You'll Learn

This tutorial covers:

1. **[Your First Shared Memory](first-shared-memory.md)** - Create and access shared memory
2. **[Working with Arrays](arrays.md)** - Store and access array data
3. **[Using Queues and Stacks](queues-stacks.md)** - FIFO and LIFO data structures
4. **[Reactive Streams](streams.md)** - Event-driven programming with streams
5. **[Synchronization Primitives](synchronization.md)** - Semaphores, barriers, and latches
6. **[Advanced Patterns](advanced-patterns.md)** - Real-world usage patterns

## Prerequisites

Before starting:

- **Completed**: [Installation](../getting-started/installation.md)
- **Completed**: [Quick Start](../getting-started/quick-start.md)
- **Understood**: [Basic Concepts](../getting-started/concepts.md)

## Tutorial Structure

Each lesson follows this structure:

1. **Concept** - What you'll learn
2. **Code** - Working examples in C++ and Python
3. **Explanation** - How it works
4. **Exercise** - Try it yourself
5. **Common Pitfalls** - What to avoid

## Learning Path

### Beginners

Start from the beginning and work through in order:
1. First Shared Memory
2. Working with Arrays
3. Using Queues and Stacks
4. Synchronization Primitives

### Intermediate

If you're familiar with shared memory:
1. Skim First Shared Memory
2. Focus on Reactive Streams
3. Study Advanced Patterns

### Language-Specific

**C++ Developers:**
- Focus on template usage
- Pay attention to memory ordering
- Study lock-free implementations

**Python Developers:**
- Understand NumPy dtype mapping
- Focus on duck typing examples
- Note type consistency requirements

## Example Project

Throughout the tutorial, we'll build a complete example: a **real-time sensor monitoring system** with:

- C++ sensor simulator (producer)
- Python data processor (consumer)
- Real-time visualization
- Alert generation
- Historical data storage

By the end, you'll have a working multi-process application!

## Code Examples

All examples are available in the repository:

```bash
cd examples/tutorial/
ls -la
# lesson01_first_memory/
# lesson02_arrays/
# lesson03_queues_stacks/
# lesson04_streams/
# lesson05_sync/
# lesson06_advanced/
```

Each lesson includes:
- Working C++ code
- Working Python code
- Build scripts
- README with instructions

## Tips for Success

1. **Type along** - Don't just read, write the code yourself
2. **Experiment** - Modify examples and see what happens
3. **Use the CLI** - Inspect structures with `zeroipc` as you go
4. **Read errors** - Error messages are helpful, not punishing
5. **Ask questions** - Check GitHub Discussions if stuck

## Common Questions

**Q: Do I need to know both C++ and Python?**

A: No! Pick one language and focus on those examples. The concepts apply to both.

**Q: Can I skip lessons?**

A: Yes, but each lesson builds on previous ones. Skipping may cause confusion.

**Q: How long does the tutorial take?**

A: About 2-3 hours for all lessons, depending on your pace.

**Q: What if I get stuck?**

A: Check the [Common Pitfalls](../best-practices/pitfalls.md) page and GitHub Issues.

## Let's Begin!

Ready to start? Head to **[Your First Shared Memory](first-shared-memory.md)** to begin the tutorial!
