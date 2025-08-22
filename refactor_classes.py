#!/usr/bin/env python3
"""
Complete refactoring from posix_shm to zeroipc with namespace
"""

import os
import re
from pathlib import Path

def process_file(filepath, is_header=False):
    """Process a single file for refactoring"""
    with open(filepath, 'r') as f:
        content = f.read()
    
    original = content
    
    # Update file header comments
    content = content.replace('@file posix_shm.h', '@file zeroipc.h')
    content = content.replace('POSIX SHM Library', 'ZeroIPC Library')
    content = content.replace('shm_table.h for', 'table.h for')
    content = content.replace('shm_array.h,', 'array.h,')
    content = content.replace('shm_queue.h for', 'queue.h for')
    
    # Class name replacements
    # Main class: posix_shm -> memory (will be in namespace)
    content = re.sub(r'\bposix_shm_impl\b', 'memory_impl', content)
    content = re.sub(r'\bposix_shm\b', 'zeroipc::memory', content)
    content = re.sub(r'\bposix_shm_small\b', 'zeroipc::memory_small', content)
    content = re.sub(r'\bposix_shm_large\b', 'zeroipc::memory_large', content)
    content = re.sub(r'\bposix_shm_huge\b', 'zeroipc::memory_huge', content)
    
    # Table classes
    content = re.sub(r'\bshm_table_impl\b', 'table_impl', content)
    content = re.sub(r'\bshm_table\b', 'zeroipc::table', content)
    content = re.sub(r'\bshm_table_small\b', 'zeroipc::table_small', content)
    content = re.sub(r'\bshm_table_large\b', 'zeroipc::table_large', content)
    content = re.sub(r'\bshm_table_huge\b', 'zeroipc::table_huge', content)
    
    # Data structure classes
    content = re.sub(r'\bshm_array\b', 'zeroipc::array', content)
    content = re.sub(r'\bshm_queue\b', 'zeroipc::queue', content)
    content = re.sub(r'\bshm_stack\b', 'zeroipc::stack', content)
    content = re.sub(r'\bshm_hash_map\b', 'zeroipc::map', content)
    content = re.sub(r'\bshm_set\b', 'zeroipc::set', content)
    content = re.sub(r'\bshm_bitset\b', 'zeroipc::bitset', content)
    content = re.sub(r'\bshm_ring_buffer\b', 'zeroipc::ring', content)
    content = re.sub(r'\bshm_object_pool\b', 'zeroipc::pool', content)
    content = re.sub(r'\bshm_atomic\b', 'zeroipc::atomic_value', content)
    content = re.sub(r'\bshm_span\b', 'zeroipc::span', content)
    
    # Type aliases at the end of files
    content = re.sub(r'\bshm_array_int\b', 'zeroipc::array_int', content)
    content = re.sub(r'\bshm_array_float\b', 'zeroipc::array_float', content)
    content = re.sub(r'\bshm_array_double\b', 'zeroipc::array_double', content)
    content = re.sub(r'\bshm_queue_int\b', 'zeroipc::queue_int', content)
    content = re.sub(r'\bshm_stack_int\b', 'zeroipc::stack_int', content)
    content = re.sub(r'\bshm_bitset_64\b', 'zeroipc::bitset_64', content)
    
    # Fix double namespace issues (zeroipc::zeroipc::)
    content = content.replace('zeroipc::zeroipc::', 'zeroipc::')
    
    # Handle existing namespaced items that shouldn't be double-namespaced
    content = content.replace('class zeroipc::memory', 'class memory')
    content = content.replace('class zeroipc::array', 'class array')
    content = content.replace('class zeroipc::queue', 'class queue')
    content = content.replace('class zeroipc::stack', 'class stack')
    content = content.replace('class zeroipc::map', 'class map')
    content = content.replace('class zeroipc::set', 'class set')
    content = content.replace('class zeroipc::bitset', 'class bitset')
    content = content.replace('class zeroipc::ring', 'class ring')
    content = content.replace('class zeroipc::pool', 'class pool')
    content = content.replace('class zeroipc::table', 'class table')
    content = content.replace('class zeroipc::atomic_value', 'class atomic_value')
    content = content.replace('class zeroipc::span', 'class span')
    
    if content != original:
        with open(filepath, 'w') as f:
            f.write(content)
        return True
    return False

def wrap_header_in_namespace(filepath):
    """Wrap header file content in zeroipc namespace"""
    with open(filepath, 'r') as f:
        lines = f.readlines()
    
    # Find where to insert namespace
    pragma_idx = -1
    last_include_idx = -1
    
    for i, line in enumerate(lines):
        if '#pragma once' in line:
            pragma_idx = i
        elif '#include' in line:
            last_include_idx = i
    
    if pragma_idx == -1:
        return False
    
    # Insert namespace after includes
    insert_idx = max(last_include_idx + 1, pragma_idx + 1)
    
    # Check if already has namespace
    content = ''.join(lines)
    if 'namespace zeroipc' in content:
        return False
    
    # Find the last line before EOF
    last_real_line = len(lines) - 1
    while last_real_line > 0 and lines[last_real_line].strip() == '':
        last_real_line -= 1
    
    # Insert namespace opening
    lines.insert(insert_idx, '\nnamespace zeroipc {\n\n')
    
    # Insert namespace closing
    lines.insert(last_real_line + 2, '\n} // namespace zeroipc\n')
    
    with open(filepath, 'w') as f:
        f.writelines(lines)
    
    return True

def main():
    repo_path = Path('/home/spinoza/github/repos/posix_shm')
    
    # Process all C++ files
    print("Processing C++ files...")
    cpp_files = list(repo_path.glob('**/*.cpp')) + list(repo_path.glob('**/*.h'))
    
    for filepath in cpp_files:
        if 'build' in str(filepath) or '.git' in str(filepath):
            continue
        
        if process_file(str(filepath), is_header=filepath.suffix == '.h'):
            print(f"  Updated: {filepath}")
    
    # Wrap headers in namespace
    print("\nWrapping headers in namespace...")
    header_files = list((repo_path / 'include').glob('*.h'))
    
    for filepath in header_files:
        if wrap_header_in_namespace(str(filepath)):
            print(f"  Wrapped: {filepath.name}")
    
    print("\nRefactoring complete!")

if __name__ == '__main__':
    main()