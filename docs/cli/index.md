# CLI Tool Guide

The ZeroIPC CLI tool provides a powerful interface for inspecting, debugging, and monitoring shared memory segments. With support for all 16 data structures and a virtual filesystem interface, it's an essential tool for development and operations.

## Overview

The `zeroipc` tool offers two modes of operation:

1. **Command-line mode**: Execute single commands and exit
2. **REPL mode**: Interactive exploration with virtual filesystem navigation

## Key Features

- **Structure Inspection**: View contents of all 16 data structure types
- **Virtual Filesystem**: Navigate shared memory like a directory structure  
- **Real-time Monitoring**: Watch structures update in real-time
- **Cross-Language Debugging**: Inspect structures created by any language
- **JSON Output**: Machine-readable output for scripting

## Quick Examples

### List All Shared Memory

```bash
zeroipc list
```

Output:
```
/sensor_data    10.0 MB    5 structures
/analytics      50.0 MB   12 structures  
/messages        1.0 MB    2 structures
```

### Show Segment Details

```bash
zeroipc show /sensor_data
```

### Inspect an Array

```bash
zeroipc array /sensor_data temperatures
```

### Interactive REPL Mode

```bash
zeroipc -r
```

## Installation

The CLI tool is built with the C++ library:

```bash
cd cpp
cmake -B build .
cmake --build build

# The tool is at build/tools/zeroipc
./build/tools/zeroipc --help

# Optional: install system-wide
sudo cp build/tools/zeroipc /usr/local/bin/
```

## Documentation Sections

- **[Basic Commands](basic-commands.md)** - Core CLI commands and usage
- **[Virtual Filesystem](virtual-filesystem.md)** - Navigate shared memory interactively
- **[Structure Inspection](structure-inspection.md)** - Detailed structure viewing
- **[Monitoring and Debugging](monitoring.md)** - Real-time monitoring and troubleshooting

## Supported Structures

The CLI tool supports all ZeroIPC data structures:

### Traditional Structures
- **Array** - View elements with indices
- **Queue** - Show queue state and contents
- **Stack** - Display stack contents
- **Ring** - Ring buffer inspection
- **Map** - Key-value pair listing
- **Set** - Set member listing
- **Pool** - Pool allocation state
- **Table** - Metadata table inspection

### Synchronization Primitives
- **Semaphore** - Current count and waiters
- **Barrier** - Participants and state
- **Latch** - Countdown state

### Codata Structures
- **Future** - Value and state
- **Lazy** - Computed state
- **Stream** - Stream contents
- **Channel** - Channel state and buffer

## Common Workflows

### Development Workflow

```bash
# 1. Run your application
./myapp &

# 2. Inspect what it created
zeroipc list
zeroipc show /myapp_data

# 3. Check specific structures
zeroipc array /myapp_data sensor_readings
zeroipc queue /myapp_data task_queue

# 4. Monitor in real-time
zeroipc monitor /myapp_data sensor_readings
```

### Debugging Workflow

```bash
# 1. Interactive exploration
zeroipc -r

# 2. Navigate to segment
zeroipc> cd /myapp_data

# 3. List structures
/myapp_data> ls

# 4. Inspect suspicious structure
/myapp_data> cat problematic_array

# 5. Check raw memory if needed
/myapp_data> dump --offset 1024 --size 256
```

## Next Steps

- **[Basic Commands](basic-commands.md)** - Learn all available commands
- **[Virtual Filesystem](virtual-filesystem.md)** - Master interactive navigation
- **[Structure Inspection](structure-inspection.md)** - Deep dive into structure viewing
- **[Monitoring](monitoring.md)** - Real-time monitoring techniques
