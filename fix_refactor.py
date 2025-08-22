#!/usr/bin/env python3
"""
Fix issues from the refactoring
"""

import os
import re
from pathlib import Path

def fix_using_declarations(filepath):
    """Fix incorrect using declarations in headers"""
    with open(filepath, 'r') as f:
        content = f.read()
    
    original = content
    
    # Fix the using declarations in zeroipc.h
    content = re.sub(r'using zeroipc::memory = memory_impl<zeroipc::table>;', 
                     r'using memory = memory_impl<table>;', content)
    content = re.sub(r'using zeroipc::memory_small = memory_impl<zeroipc::table_small>;',
                     r'using memory_small = memory_impl<table_small>;', content)
    content = re.sub(r'using zeroipc::memory_large = memory_impl<zeroipc::table_large>;',
                     r'using memory_large = memory_impl<table_large>;', content)
    content = re.sub(r'using zeroipc::memory_huge = memory_impl<zeroipc::table_huge>;',
                     r'using memory_huge = memory_impl<table_huge>;', content)
    
    # Fix template parameters that have zeroipc:: prefix inside namespace
    content = re.sub(r'template<typename TableType = zeroipc::table>',
                     r'template<typename TableType = table>', content)
    content = re.sub(r'static_assert\(std::is_same_v<typename ShmType::table_type, TableType>',
                     r'static_assert(std::is_same_v<typename ShmType::table_type, TableType>', content)
    
    # Fix references to zeroipc:: inside the namespace
    if 'namespace zeroipc' in content:
        # Inside namespace, don't need zeroipc:: prefix
        content = re.sub(r'zeroipc::table(?!\w)', r'table', content)
        content = re.sub(r'zeroipc::array<', r'array<', content)
        content = re.sub(r'zeroipc::queue<', r'queue<', content)
        content = re.sub(r'zeroipc::stack<', r'stack<', content)
        content = re.sub(r'zeroipc::map<', r'map<', content)
        content = re.sub(r'zeroipc::set<', r'set<', content)
        content = re.sub(r'zeroipc::bitset<', r'bitset<', content)
        content = re.sub(r'zeroipc::ring<', r'ring<', content)
        content = re.sub(r'zeroipc::pool<', r'pool<', content)
        content = re.sub(r'zeroipc::memory(?!\w)', r'memory', content)
    
    if content != original:
        with open(filepath, 'w') as f:
            f.write(content)
        return True
    return False

def fix_span_template(filepath):
    """Fix span template issues"""
    with open(filepath, 'r') as f:
        content = f.read()
    
    original = content
    
    # Fix posix_shm_impl references to memory_impl
    content = re.sub(r'posix_shm_impl<TableType>', r'memory_impl<TableType>', content)
    
    if content != original:
        with open(filepath, 'w') as f:
            f.write(content)
        return True
    return False

def main():
    repo_path = Path('/home/spinoza/github/repos/posix_shm')
    
    # Fix headers
    print("Fixing header files...")
    header_files = list((repo_path / 'include').glob('*.h'))
    
    for filepath in header_files:
        if fix_using_declarations(str(filepath)):
            print(f"  Fixed declarations: {filepath.name}")
        if fix_span_template(str(filepath)):
            print(f"  Fixed templates: {filepath.name}")
    
    # Fix test files - they should use zeroipc:: prefix
    print("\nFixing test files...")
    test_files = list((repo_path / 'tests').glob('*.cpp'))
    
    for filepath in test_files:
        with open(filepath, 'r') as f:
            content = f.read()
        
        original = content
        
        # Tests are outside namespace, so they need zeroipc:: prefix
        # But we've already added them, so just clean up any issues
        
        # Fix double namespace issues
        content = content.replace('zeroipc::zeroipc::', 'zeroipc::')
        
        if content != original:
            with open(filepath, 'w') as f:
                f.write(content)
            print(f"  Fixed: {filepath.name}")
    
    print("\nCleanup complete!")

if __name__ == '__main__':
    main()