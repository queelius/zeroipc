"""Basic tests without numpy dependency"""

import os
import unittest
import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from zeroipc import Memory, Table


class TestBasic(unittest.TestCase):
    
    def setUp(self):
        """Set up test fixtures"""
        self.test_name = f"/test_basic_{os.getpid()}"
    
    def test_memory_creation(self):
        """Test creating shared memory"""
        mem = Memory(self.test_name, 1024 * 1024)  # 1MB
        
        self.assertEqual(mem.size, 1024 * 1024)
        self.assertEqual(mem.name, self.test_name)
        self.assertTrue(mem.owner)
        
        mem.close()
        mem.unlink()
    
    def test_table_operations(self):
        """Test table operations"""
        mem = Memory(self.test_name, 1024 * 1024)
        
        # Add entries
        self.assertTrue(mem.table.add("entry1", 1000, 100))
        self.assertTrue(mem.table.add("entry2", 2000, 200))
        
        # Find entries
        e1 = mem.table.find("entry1")
        self.assertIsNotNone(e1)
        self.assertEqual(e1.offset, 1000)
        self.assertEqual(e1.size, 100)
        
        e2 = mem.table.find("entry2")
        self.assertIsNotNone(e2)
        self.assertEqual(e2.offset, 2000)
        self.assertEqual(e2.size, 200)
        
        # Entry count
        self.assertEqual(mem.table.entry_count(), 2)
        
        mem.close()
        mem.unlink()
    
    def test_persistence(self):
        """Test data persistence across opens"""
        # Create and write (need at least 2576 bytes for 64-entry table)
        mem1 = Memory(self.test_name, 4096)
        mem1.table.add("persistent", 3000, 50)
        mem1.close()
        
        # Open and read
        mem2 = Memory(self.test_name)
        entry = mem2.table.find("persistent")
        self.assertIsNotNone(entry)
        self.assertEqual(entry.offset, 3000)
        self.assertEqual(entry.size, 50)
        
        mem2.close()
        mem2.unlink()


if __name__ == '__main__':
    unittest.main()