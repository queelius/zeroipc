# Virtual Filesystem

The ZeroIPC CLI tool features a virtual filesystem interface that lets you navigate shared memory segments and structures like a directory tree.

## Concept

Shared memory is presented as a hierarchical filesystem:

```
/                          # Root - all shared memory segments
├── sensor_data/          # Shared memory segment
│   ├── temperatures      # Array structure
│   ├── pressure          # Array structure
│   └── alerts            # Queue structure
└── analytics/            # Another segment
    ├── results           # Array structure
    └── cache             # Map structure
```

## Entering REPL Mode

Start the interactive REPL:

```bash
zeroipc -r
```

You'll see:
```
ZeroIPC Interactive Shell v3.0 - Virtual Filesystem Interface
Type 'help' for available commands, 'quit' to exit

zeroipc>
```

## Navigation Commands

### ls - List Contents

List contents at current location.

**Syntax:**
```bash
ls [path]
```

**At root (/):**
```bash
zeroipc> ls

=== Shared Memory Segments ===
Name                Size
--------------------------------------------------
/sensor_data        10.0 MB
/analytics          50.0 MB
/messages           1.0 MB
```

**In a segment:**
```bash
/sensor_data> ls

=== Table Entries ===
#   Name                Type        Offset      Size
---------------------------------------------------------------------------
0   temperatures        array       1024        4000
1   pressure            array       5024        8000
2   alerts              queue       13024       8192
```

**Specific path:**
```bash
zeroipc> ls /sensor_data
[shows structures in /sensor_data]
```

### cd - Change Directory

Navigate to different locations.

**Syntax:**
```bash
cd <path>
```

**Examples:**

Absolute path:
```bash
zeroipc> cd /sensor_data
/sensor_data>
```

Relative path:
```bash
/sensor_data> cd ../analytics
/analytics>
```

Parent directory:
```bash
/analytics> cd ..
zeroipc>
```

Root:
```bash
/analytics> cd /
zeroipc>
```

### pwd - Print Working Directory

Show current location.

**Syntax:**
```bash
pwd
```

**Example:**
```bash
/sensor_data> pwd
/sensor_data
```

### cat - Display Contents

Show structure contents (like Unix `cat`).

**Syntax:**
```bash
cat <structure_name>
cat <structure_name>[range]
```

**Examples:**

Show entire array:
```bash
/sensor_data> cat temperatures
Array: temperatures
Capacity: 1000
[0] = 23.45
[1] = 24.12
...
```

Show specific range:
```bash
/sensor_data> cat temperatures[0:10]
Array: temperatures
Elements [0:10]:
[0] = 23.45
[1] = 24.12
...
[9] = 24.87
```

Show queue:
```bash
/messages> cat task_queue
Queue: task_queue
Size: 27/100
Head: 15
Tail: 42

Contents:
[0] = Task{...}
[1] = Task{...}
...
```

## Example Session

Here's a complete interactive session:

```bash
$ zeroipc -r
ZeroIPC Interactive Shell v3.0
Type 'help' for available commands, 'quit' to exit

zeroipc> pwd
/

zeroipc> ls
=== Shared Memory Segments ===
/sensor_data        10.0 MB
/analytics          50.0 MB

zeroipc> cd /sensor_data
/sensor_data> ls
=== Table Entries ===
#   Name              Type      Offset    Size
0   temperatures      array     1024      4000
1   pressure          array     5024      8000
2   alerts            queue     13024     8192

/sensor_data> cat temperatures[0:5]
Array: temperatures
Elements [0:5]:
[0] = 23.45
[1] = 24.12
[2] = 22.89
[3] = 25.01
[4] = 23.67

/sensor_data> cd /analytics
/analytics> ls
=== Table Entries ===
#   Name          Type      Offset    Size
0   results       array     1024      80000
1   cache         map       81024     65536

/analytics> cd /
zeroipc> quit
Goodbye!
```

## Advanced Features

### Tab Completion

The REPL supports tab completion (future enhancement):

```bash
zeroipc> cd /sen<TAB>
# Completes to: cd /sensor_data

/sensor_data> cat tem<TAB>
# Completes to: cat temperatures
```

### Command History

Use arrow keys to navigate command history:

- **Up arrow**: Previous command
- **Down arrow**: Next command
- **Ctrl+R**: Reverse search

### Shortcuts

Convenient shortcuts (future enhancement):

```bash
# .. changes to parent directory
/sensor_data> ..
zeroipc>

# ~ changes to root
/sensor_data> ~
zeroipc>

# ll shows detailed listing
/sensor_data> ll
# Same as: ls -l
```

## Prompt Customization

The prompt shows your current location:

```
zeroipc>           # At root
/sensor_data>      # In a segment
```

Colors (when terminal supports it):
- **Green**: Root
- **Blue**: Segment name
- **Yellow**: Structure name (future)

## REPL Commands

### help

Show available commands.

```bash
zeroipc> help

Navigation Commands:
  ls [path]              List contents
  cd <path>              Change directory
  pwd                    Print working directory
  cat <name>[range]      Display contents

Segment Commands:
  show                   Show current segment info
  create <type> <name>   Create new structure
  
General Commands:
  help                   Show this message
  quit, exit             Exit REPL
  clear                  Clear screen
```

### show

Show detailed information about current segment.

```bash
/sensor_data> show
Segment: /sensor_data
Size: 10.0 MB
Table: 3/64 entries used
Memory: 45632 bytes allocated
```

### create

Create a new structure in current segment.

```bash
/sensor_data> create array test_data --capacity 100 --type float
Created array 'test_data' with capacity 100

/sensor_data> ls
# test_data now appears in listing
```

### clear

Clear the screen.

```bash
zeroipc> clear
[screen clears]
```

### quit / exit

Exit the REPL.

```bash
zeroipc> quit
Goodbye!
```

## Tips and Tricks

### 1. Quick Navigation

Jump directly to any path:
```bash
zeroipc> cd /sensor_data
/sensor_data> cat temperatures[0:10]

# Or combine:
zeroipc> cat /sensor_data/temperatures[0:10]
```

### 2. Path Verification

Always use `pwd` to verify location:
```bash
/somewhere> pwd
/sensor_data
```

### 3. Exploring Unknown Segments

```bash
zeroipc> ls           # See what exists
zeroipc> cd /mystery  # Navigate to unknown segment
/mystery> ls          # See what's inside
/mystery> cat struct1 # Examine structures
```

### 4. Quick Checks

```bash
# Quick check of all segments
zeroipc> ls
/sensor_data        10.0 MB
/analytics          50.0 MB

# Quick check of specific structure
zeroipc> cat /sensor_data/temperatures[0]
[0] = 23.45
```

## Next Steps

- **[Basic Commands](basic-commands.md)** - Learn all commands
- **[Structure Inspection](structure-inspection.md)** - Deep dive into structures  
- **[Monitoring](monitoring.md)** - Real-time monitoring
