# Virtual Filesystem Design for ZeroIPC CLI

## Overview

Transform the `zeroipc` CLI tool into a virtual filesystem interface where shared memory segments and data structures can be navigated like directories and files.

## Path Structure

```
/                           # Root - lists all shared memory segments
├── myapp_data/            # Shared memory segment
│   ├── temperatures/      # Array structure
│   ├── task_queue/        # Queue structure
│   ├── sync_barrier/      # Barrier structure
│   └── cache_map/         # Map structure
└── sensors/               # Another shared memory segment
    ├── readings/
    └── status/
```

## Navigation Commands

### `ls [path]`
List contents at current location or specified path

**At root (`/`):**
```bash
zeroipc> ls
myapp_data/     10485760 bytes    3 structures
sensors/         5242880 bytes    2 structures
cache/         104857600 bytes   15 structures
```

**In shared memory segment (`/myapp_data`):**
```bash
/myapp_data> ls
temperatures     array<float>[1000]        4000 bytes
task_queue       queue<task>[100]           424 bytes
sync_barrier     barrier(4 participants)     32 bytes
cache_map        map<string,value>         8192 bytes
```

**In a structure (`/myapp_data/temperatures`):**
```bash
/myapp_data/temperatures> ls
Type: array<float>
Capacity: 1000
Size: 4000 bytes
Contents: [0..999]
  [0] = 23.5
  [1] = 24.1
  [2] = 22.8
  ...
```

### `cd <path>`
Change current directory

```bash
zeroipc> cd /myapp_data          # Absolute path
/myapp_data> cd temperatures     # Relative path
/myapp_data/temperatures> cd ..  # Parent directory
/myapp_data> cd /sensors        # Jump to another segment
/sensors> cd /                   # Back to root
```

### `pwd`
Print working directory

```bash
/myapp_data/temperatures> pwd
/myapp_data/temperatures
```

### `cat <path|range>`
Display structure contents

```bash
/myapp_data> cat temperatures          # Show entire array
/myapp_data> cat temperatures[0-10]    # Show range
/myapp_data> cat task_queue            # Show queue contents
```

## Implementation Plan

### Phase 1: Core Infrastructure (Priority 1)

1. **Path Management**
   - `struct Path { std::vector<std::string> components; }`
   - `std::string current_path = "/"`
   - Path parsing: `/myapp/data` → `["myapp", "data"]`
   - Path resolution: handle `.`, `..`, `/`, relative vs absolute

2. **Location Context**
   ```cpp
   enum class LocationType { ROOT, SEGMENT, STRUCTURE };

   struct NavigationContext {
       LocationType type;
       std::string segment_name;      // If in segment or structure
       std::string structure_name;    // If in structure
       Memory* current_memory;        // Pointer to open memory
   };
   ```

3. **Table Inspection**
   - Read metadata table from shared memory
   - List all registered structures
   - Determine structure types (currently not stored - enhancement needed)

### Phase 2: Navigation Commands (Priority 1)

1. **`ls` command**
   - At root: scan `/dev/shm`, list shared memory segments
   - In segment: read table, list structures
   - In structure: show type-specific contents

2. **`cd` command**
   - Parse path (absolute/relative)
   - Validate destination exists
   - Update current context
   - Handle special cases: `.`, `..`, `/`

3. **`pwd` command**
   - Return current path string

### Phase 3: Content Display (Priority 2)

1. **`cat` command**
   - Array: show elements with indices
   - Queue: show queue contents head→tail
   - Stack: show stack contents top→bottom
   - Map: show key-value pairs
   - Semaphore/Barrier/Latch: show state

2. **Type-specific formatters**
   - Different display logic per structure type

### Phase 4: Enhanced Features (Priority 3)

1. **Tab completion**
   - Complete segment names at root
   - Complete structure names in segment
   - Complete commands

2. **Aliases and shortcuts**
   - `ll` = `ls -l` (long format)
   - `..` = `cd ..`
   - `~` = `cd /`

3. **Filtering and searching**
   - `ls | grep pattern`
   - `find <name>`

## Technical Challenges

### Challenge 1: Structure Type Detection

**Problem**: The metadata table only stores name, offset, and size. No type information.

**Solutions:**
1. **Type inference heuristics**: Guess type based on size patterns
2. **Type registry**: Store type info in a separate metadata structure
3. **Magic numbers**: Add type identifier at start of each structure
4. **Naming conventions**: Require type prefix (e.g., `array_temperatures`)

**Recommended**: Option 3 (Magic numbers) - minimal overhead, reliable

### Challenge 2: Open Memory Management

**Problem**: Currently can only have one shared memory segment open at a time.

**Solutions:**
1. Keep only current segment open (close when navigating away)
2. Implement connection pool (keep multiple segments open)
3. Open on-demand, cache handles

**Recommended**: Option 1 initially (simple), upgrade to 2 later

### Challenge 3: Large Structure Display

**Problem**: Arrays with 1M elements would flood terminal.

**Solutions:**
1. Pagination (show first N, prompt for more)
2. Smart truncation (show first/last N)
3. Range-based display only
4. Lazy loading on scroll

**Recommended**: Option 2 (smart truncation) with option for full display

## Prompt Enhancement

Update prompt to show current location:

```bash
zeroipc>                    # At root
/myapp_data>               # In segment
/myapp_data/temps>         # In structure
```

## Backward Compatibility

Keep existing commands working:
- `open /name` → same as `cd /name`
- `create` commands → work from any location
- `list` → same as `ls` at root

## Example Session

```bash
$ zeroipc -r
ZeroIPC Interactive Shell v4.0 - Virtual Filesystem Interface

zeroipc> ls
myapp_data/     10 MB      3 structures
sensors/         5 MB      2 structures

zeroipc> cd myapp_data
/myapp_data> ls
temperatures     array<float>[1000]        4000 bytes
task_queue       queue<task>[100]           424 bytes
sync_barrier     barrier(4 participants)     32 bytes

/myapp_data> cd temperatures
/myapp_data/temperatures> ls
Type: array<float>
Capacity: 1000
Elements: [0..999]

/myapp_data/temperatures> cat [0-5]
[0] = 23.5
[1] = 24.1
[2] = 22.8
[3] = 24.0
[4] = 23.2
[5] = 22.9

/myapp_data/temperatures> cd ..
/myapp_data> cat task_queue
Type: queue<task>
Head: 0, Tail: 5, Size: 5/100
[0] = {id: 1, priority: 5, data: "process_image"}
[1] = {id: 2, priority: 3, data: "backup_data"}
...

/myapp_data> cd /
zeroipc> quit
```

## CLI Parity

All REPL commands should also work as CLI one-liners:

```bash
# List all shared memory
zeroipc ls /

# List structures in a segment
zeroipc ls /myapp_data

# Show array contents
zeroipc cat /myapp_data/temperatures[0-10]

# Navigate and list
zeroipc cd /myapp_data && ls
```

## Implementation Timeline

1. **Week 1**: Core infrastructure (Path, NavigationContext, table inspection)
2. **Week 2**: Basic navigation (ls, cd, pwd)
3. **Week 3**: Content display (cat for all structure types)
4. **Week 4**: Polish (tab completion, aliases, CLI parity)

## Testing Strategy

1. Unit tests for path parsing and resolution
2. Integration tests for navigation scenarios
3. Manual testing with real shared memory segments
4. Performance testing with large structures
5. Cross-language interop testing (Python creates, CLI navigates)
