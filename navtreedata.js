/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "POSIX Shared Memory Data Structures", "index.html", [
    [ "Zero-Overhead Inter-Process Communication for High-Performance Computing", "index.html#autotoc_md18", [
      [ "Overview", "index.html#autotoc_md19", null ],
      [ "Key Features", "index.html#autotoc_md20", null ],
      [ "Why Shared Memory?", "index.html#autotoc_md21", null ],
      [ "Performance Guarantees", "index.html#autotoc_md22", null ],
      [ "Architecture", "index.html#autotoc_md23", null ],
      [ "Core Components", "index.html#autotoc_md24", [
        [ "1. Foundation Layer", "index.html#autotoc_md25", null ],
        [ "2. Data Structures", "index.html#autotoc_md26", null ],
        [ "3. Template Configurations", "index.html#autotoc_md27", null ]
      ] ],
      [ "Quick Start", "index.html#autotoc_md28", null ],
      [ "Use Cases", "index.html#autotoc_md29", [
        [ "High-Frequency Trading", "index.html#autotoc_md30", null ],
        [ "Scientific Simulation", "index.html#autotoc_md31", null ],
        [ "Robotics & Autonomous Systems", "index.html#autotoc_md32", null ],
        [ "Game Servers", "index.html#autotoc_md33", null ]
      ] ],
      [ "Proven Performance", "index.html#autotoc_md34", null ],
      [ "Safety & Correctness", "index.html#autotoc_md35", null ],
      [ "Getting Started", "index.html#autotoc_md36", null ],
      [ "Requirements", "index.html#autotoc_md37", null ],
      [ "License", "index.html#autotoc_md38", null ],
      [ "Contributing", "index.html#autotoc_md39", null ]
    ] ],
    [ "posix_shm", "md_README.html", [
      [ "Features", "md_README.html#autotoc_md1", null ],
      [ "Requirements", "md_README.html#autotoc_md2", null ],
      [ "Installation", "md_README.html#autotoc_md3", [
        [ "Using CMake FetchContent (Recommended)", "md_README.html#autotoc_md4", null ],
        [ "Using CPM.cmake", "md_README.html#autotoc_md5", null ],
        [ "Using Conan", "md_README.html#autotoc_md6", null ],
        [ "Using vcpkg", "md_README.html#autotoc_md7", null ],
        [ "Manual Installation", "md_README.html#autotoc_md8", null ]
      ] ],
      [ "Quick Start", "md_README.html#autotoc_md9", null ],
      [ "Data Structures", "md_README.html#autotoc_md10", null ],
      [ "Documentation", "md_README.html#autotoc_md11", null ],
      [ "Building Tests & Examples", "md_README.html#autotoc_md12", null ],
      [ "Performance", "md_README.html#autotoc_md13", null ],
      [ "License", "md_README.html#autotoc_md14", null ],
      [ "Author", "md_README.html#autotoc_md15", null ],
      [ "Contributing", "md_README.html#autotoc_md16", null ]
    ] ],
    [ "Performance Guide", "md_docs_2performance.html", [
      [ "Memory Access Performance Analysis", "md_docs_2performance.html#autotoc_md41", [
        [ "The Zero-Overhead Claim", "md_docs_2performance.html#autotoc_md42", null ],
        [ "Why It's So Fast", "md_docs_2performance.html#autotoc_md43", [
          [ "1. <strong>Memory Hierarchy - Identical Path</strong>", "md_docs_2performance.html#autotoc_md44", null ],
          [ "2. <strong>Assembly Analysis</strong>", "md_docs_2performance.html#autotoc_md45", null ],
          [ "3. <strong>Cache Line Behavior</strong>", "md_docs_2performance.html#autotoc_md46", null ]
        ] ],
        [ "Lock-Free Performance", "md_docs_2performance.html#autotoc_md47", [
          [ "Atomic Operations Timing", "md_docs_2performance.html#autotoc_md48", null ],
          [ "Queue Performance", "md_docs_2performance.html#autotoc_md49", null ],
          [ "Object Pool Performance", "md_docs_2performance.html#autotoc_md50", null ]
        ] ],
        [ "Optimization Techniques", "md_docs_2performance.html#autotoc_md51", [
          [ "1. <strong>Cache Line Alignment</strong>", "md_docs_2performance.html#autotoc_md52", null ],
          [ "2. <strong>Huge Pages (2MB/1GB)</strong>", "md_docs_2performance.html#autotoc_md53", null ],
          [ "3. <strong>NUMA Awareness</strong>", "md_docs_2performance.html#autotoc_md54", null ],
          [ "4. <strong>Prefetching</strong>", "md_docs_2performance.html#autotoc_md55", null ]
        ] ],
        [ "Real-World Benchmarks", "md_docs_2performance.html#autotoc_md56", [
          [ "Particle Simulation (100K particles)", "md_docs_2performance.html#autotoc_md57", null ],
          [ "Sensor Data Pipeline (1MHz sampling)", "md_docs_2performance.html#autotoc_md58", null ]
        ] ],
        [ "Memory Overhead", "md_docs_2performance.html#autotoc_md59", [
          [ "Table Size Configurations", "md_docs_2performance.html#autotoc_md60", null ],
          [ "Per-Structure Overhead", "md_docs_2performance.html#autotoc_md61", null ]
        ] ],
        [ "Scalability Analysis", "md_docs_2performance.html#autotoc_md62", [
          [ "Process Scaling", "md_docs_2performance.html#autotoc_md63", null ],
          [ "Contention Characteristics", "md_docs_2performance.html#autotoc_md64", null ]
        ] ],
        [ "Platform-Specific Notes", "md_docs_2performance.html#autotoc_md65", [
          [ "Linux", "md_docs_2performance.html#autotoc_md66", null ],
          [ "macOS", "md_docs_2performance.html#autotoc_md67", null ],
          [ "FreeBSD", "md_docs_2performance.html#autotoc_md68", null ]
        ] ],
        [ "Profiling & Tuning", "md_docs_2performance.html#autotoc_md69", [
          [ "Key Metrics to Monitor", "md_docs_2performance.html#autotoc_md70", null ]
        ] ],
        [ "Best Practices Summary", "md_docs_2performance.html#autotoc_md71", null ]
      ] ]
    ] ],
    [ "Tutorial: Building High-Performance IPC Systems", "md_docs_2tutorial.html", [
      [ "Table of Contents", "md_docs_2tutorial.html#autotoc_md73", null ],
      [ "Basic Concepts", "md_docs_2tutorial.html#autotoc_md74", [
        [ "The Power of the Table System", "md_docs_2tutorial.html#autotoc_md75", null ],
        [ "Creating Shared Memory", "md_docs_2tutorial.html#autotoc_md76", null ],
        [ "Choosing Table Configuration", "md_docs_2tutorial.html#autotoc_md77", null ]
      ] ],
      [ "Complete Multi-Structure Example", "md_docs_2tutorial.html#autotoc_md78", [
        [ "Game Server State (Multiple Structures in One Segment)", "md_docs_2tutorial.html#autotoc_md79", null ],
        [ "Memory Layout Visualization", "md_docs_2tutorial.html#autotoc_md80", null ],
        [ "Key Points About Multiple Structures", "md_docs_2tutorial.html#autotoc_md81", null ]
      ] ],
      [ "Simple Producer-Consumer", "md_docs_2tutorial.html#autotoc_md82", [
        [ "Producer Process", "md_docs_2tutorial.html#autotoc_md83", null ],
        [ "Consumer Process", "md_docs_2tutorial.html#autotoc_md84", null ]
      ] ],
      [ "Particle Simulation System", "md_docs_2tutorial.html#autotoc_md85", [
        [ "Simulation Core", "md_docs_2tutorial.html#autotoc_md86", null ],
        [ "Renderer Process", "md_docs_2tutorial.html#autotoc_md87", null ]
      ] ],
      [ "Sensor Data Pipeline", "md_docs_2tutorial.html#autotoc_md88", [
        [ "High-Frequency Data Collection", "md_docs_2tutorial.html#autotoc_md89", null ],
        [ "Data Processing Pipeline", "md_docs_2tutorial.html#autotoc_md90", null ]
      ] ],
      [ "Multi-Process Coordination", "md_docs_2tutorial.html#autotoc_md91", [
        [ "Master-Worker Pattern", "md_docs_2tutorial.html#autotoc_md92", null ]
      ] ],
      [ "Error Handling & Recovery", "md_docs_2tutorial.html#autotoc_md93", [
        [ "Robust Initialization", "md_docs_2tutorial.html#autotoc_md94", null ],
        [ "Crash Recovery", "md_docs_2tutorial.html#autotoc_md95", null ],
        [ "Monitoring & Diagnostics", "md_docs_2tutorial.html#autotoc_md96", null ]
      ] ],
      [ "Best Practices", "md_docs_2tutorial.html#autotoc_md97", [
        [ "1. <strong>Always Name Your Structures</strong>", "md_docs_2tutorial.html#autotoc_md98", null ],
        [ "2. <strong>Handle Full Conditions Gracefully</strong>", "md_docs_2tutorial.html#autotoc_md99", null ],
        [ "3. <strong>Use Bulk Operations for Efficiency</strong>", "md_docs_2tutorial.html#autotoc_md100", null ],
        [ "4. <strong>Monitor System Health</strong>", "md_docs_2tutorial.html#autotoc_md101", null ],
        [ "5. <strong>Plan for Growth</strong>", "md_docs_2tutorial.html#autotoc_md102", null ]
      ] ]
    ] ],
    [ "Architecture Deep Dive", "md_docs_2architecture.html", [
      [ "System Architecture Overview", "md_docs_2architecture.html#autotoc_md104", null ],
      [ "Memory Layout", "md_docs_2architecture.html#autotoc_md105", [
        [ "Shared Memory Segment Structure", "md_docs_2architecture.html#autotoc_md106", null ],
        [ "Metadata Table Entry", "md_docs_2architecture.html#autotoc_md107", null ]
      ] ],
      [ "Component Design", "md_docs_2architecture.html#autotoc_md108", [
        [ "1. posix_shm - Memory Management", "md_docs_2architecture.html#autotoc_md109", null ],
        [ "2. shm_table - Discovery System", "md_docs_2architecture.html#autotoc_md110", null ],
        [ "3. Data Structure Implementations", "md_docs_2architecture.html#autotoc_md111", [
          [ "shm_array<T> - Contiguous Storage", "md_docs_2architecture.html#autotoc_md112", null ],
          [ "shm_queue<T> - Lock-Free FIFO", "md_docs_2architecture.html#autotoc_md113", null ],
          [ "shm_object_pool<T> - Free List", "md_docs_2architecture.html#autotoc_md114", null ],
          [ "shm_ring_buffer<T> - Bulk Operations", "md_docs_2architecture.html#autotoc_md115", null ]
        ] ]
      ] ],
      [ "Synchronization Strategies", "md_docs_2architecture.html#autotoc_md116", [
        [ "1. Lock-Free Algorithms", "md_docs_2architecture.html#autotoc_md117", null ],
        [ "2. Wait-Free Readers", "md_docs_2architecture.html#autotoc_md118", null ],
        [ "3. Memory Ordering", "md_docs_2architecture.html#autotoc_md119", null ]
      ] ],
      [ "Template Metaprogramming", "md_docs_2architecture.html#autotoc_md120", [
        [ "Compile-Time Configuration", "md_docs_2architecture.html#autotoc_md121", null ],
        [ "Concept Constraints", "md_docs_2architecture.html#autotoc_md122", null ]
      ] ],
      [ "Performance Optimizations", "md_docs_2architecture.html#autotoc_md123", [
        [ "1. Cache Line Alignment", "md_docs_2architecture.html#autotoc_md124", null ],
        [ "2. Memory Prefetching", "md_docs_2architecture.html#autotoc_md125", null ],
        [ "3. Bulk Operations", "md_docs_2architecture.html#autotoc_md126", null ],
        [ "4. Branch-Free Code", "md_docs_2architecture.html#autotoc_md127", null ]
      ] ],
      [ "Error Handling Philosophy", "md_docs_2architecture.html#autotoc_md128", [
        [ "1. Constructor Failures", "md_docs_2architecture.html#autotoc_md129", null ],
        [ "2. Operation Failures", "md_docs_2architecture.html#autotoc_md130", null ],
        [ "3. Resource Exhaustion", "md_docs_2architecture.html#autotoc_md131", null ]
      ] ],
      [ "Scalability Considerations", "md_docs_2architecture.html#autotoc_md132", [
        [ "Process Scaling", "md_docs_2architecture.html#autotoc_md133", null ],
        [ "Memory Scaling", "md_docs_2architecture.html#autotoc_md134", null ],
        [ "NUMA Awareness", "md_docs_2architecture.html#autotoc_md135", null ]
      ] ],
      [ "Security Considerations", "md_docs_2architecture.html#autotoc_md136", [
        [ "1. Access Control", "md_docs_2architecture.html#autotoc_md137", null ],
        [ "2. Input Validation", "md_docs_2architecture.html#autotoc_md138", null ],
        [ "3. Resource Limits", "md_docs_2architecture.html#autotoc_md139", null ]
      ] ],
      [ "Future Directions", "md_docs_2architecture.html#autotoc_md140", [
        [ "1. Persistent Memory Support", "md_docs_2architecture.html#autotoc_md141", null ],
        [ "2. GPU Shared Memory", "md_docs_2architecture.html#autotoc_md142", null ],
        [ "3. Distributed Shared Memory", "md_docs_2architecture.html#autotoc_md143", null ]
      ] ],
      [ "Design Principles Summary", "md_docs_2architecture.html#autotoc_md144", null ]
    ] ],
    [ "Namespaces", "namespaces.html", [
      [ "Namespace List", "namespaces.html", "namespaces_dup" ],
      [ "Namespace Members", "namespacemembers.html", [
        [ "All", "namespacemembers.html", null ],
        [ "Functions", "namespacemembers_func.html", null ]
      ] ]
    ] ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", null ],
        [ "Functions", "functions_func.html", null ],
        [ "Variables", "functions_vars.html", null ],
        [ "Typedefs", "functions_type.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", null ],
        [ "Functions", "globals_func.html", null ],
        [ "Variables", "globals_vars.html", null ],
        [ "Typedefs", "globals_type.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"annotated.html",
"md_docs_2architecture.html#autotoc_md107"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';