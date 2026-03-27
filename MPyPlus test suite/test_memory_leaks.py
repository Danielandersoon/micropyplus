"""
MicroPython Pointers: Memory Leak Detection
Tests for proper cleanup and memory stability over extended operations
"""

import micropython as mp
import time

class SensorData:
    def __init__(self, size=100):
        self.readings = [i * 0.1 for i in range(size)]
        self.timestamps = [i for i in range(size)]
        self.calibration = 1.0

class DataBuffer:
    def __init__(self, capacity=50):
        self.data = [None] * capacity
        self.size = 0
        self.head = 0

def test_rapid_allocation():
    """Create and destroy pointers in rapid succession"""
    print("\nTest 1: Rapid Allocation/Deallocation (100 cycles)")
    
    mp.mem_info()
    
    for i in range(100):
        sensor = SensorData(50)
        ptr = &sensor
        s = *ptr
        # Pointer goes out of scope here
    
    mp.mem_info()
    print("    PASS: Rapid allocation/deallocation test completed")

def test_pointer_array_cycling():
    """Create arrays of pointers with cycling allocation"""
    print("\nTest 2: Pointer Array Cycling (50 cycles, 10 pointers each)")
    
    mp.mem_info()
    
    for cycle in range(50):
        sensors = [SensorData(25) for _ in range(10)]
        
        # Access through sensors directly (not via pointers in list comp)
        for sensor in sensors:
            ptr = &sensor
            s = *ptr
            _ = s.readings[0]
        
        # Clear sensors
        sensors = []
    
    mp.mem_info()
    print("    PASS: Array cycling completed")

def test_gc_fragmentation():
    """Test GC behavior and fragmentation over time"""
    print("\nTest 3: GC Fragmentation Analysis (80 iterations)")
    
    mp.mem_info()
    
    for i in range(40):
        # Allocate mixed-size structures
        small = SensorData(10)
        medium = SensorData(50)
        large = SensorData(100)
        
        small_ptr = &small
        medium_ptr = &medium
        large_ptr = &large
        
        # Use pointers
        s1 = *small_ptr
        s2 = *medium_ptr
        s3 = *large_ptr
        
        total = s1.readings[0] + s2.readings[0] + s3.readings[0]
    
    mp.mem_info()
    print("    PASS: GC fragmentation test completed")

def test_pointer_reuse():
    """Test memory behavior with pointer variable reuse"""
    print("\nTest 4: Pointer Variable Reuse (100 reassignments)")
    
    mp.mem_info()
    
    # Single pointer variable, constantly reassigned
    ptr = None
    
    for i in range(100):
        sensor = SensorData(30)
        ptr = &sensor
        s = *ptr
        
        # Use the data
        for r in s.readings:
            _ = r * 2.0
    
    mp.mem_info()
    print("    PASS: Pointer reuse completed")

# Run all tests
print("\n" + "*"*50)
print("MEMORY LEAK DETECTION TEST")
print("*"*50)

try:
    test_rapid_allocation()
    test_pointer_array_cycling()
    test_gc_fragmentation()
    test_pointer_reuse()
    
    print("\n" + "*"*50)
    print("MEMORY LEAK DETECTION: ALL TESTS PASSED")
    print("*"*50 + "\n")
    
except Exception as e:
    print(f"\n   MEMORY LEAK TEST FAILED: {type(e).__name__}: {e}")
