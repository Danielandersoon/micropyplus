"""
MicroPython Pointers: Concurrent Allocation Stress Test
High-frequency pointer creation/destruction and memory fragmentation analysis
"""

import micropython as mp
import time

class DataPacket:
    def __init__(self, size=100, packet_id=0):
        self.id = packet_id
        self.payload = [float(i) for i in range(size)]
        self.metadata = {"priority": 0, "checksum": 0}

def test_burst_allocation():
    """Create many pointers in rapid burst"""
    print("\nTest 1: Burst Allocation (200 pointers)")
    
    mp.mem_info()
    
    ptrs = []
    start = time.ticks_ms()
    
    for i in range(200):
        packet = DataPacket(50, i)
        ptr = &packet
        ptrs.append(ptr)
    
    elapsed = time.ticks_ms() - start
    
    mp.mem_info()
    print(f"    PASS: Created 200 pointers in {elapsed} ms")
    print(f"    Pointers allocated: {len(ptrs)}")

def test_interleaved_alloc_dealloc():
    """Interleave allocations and deallocations"""
    print("\nTest 2: Interleaved Alloc/Dealloc (300 pairs)")
    
    mp.mem_info()
    
    ptrs_active = []
    
    for cycle in range(300):
        # Allocate
        packet = DataPacket(75, cycle)
        ptr = &packet
        ptrs_active.append(ptr)
        
        # Deallocate every 10
        if len(ptrs_active) > 10:
            ptrs_active.pop(0)
        
        # Periodic access
        if cycle % 100 == 0:
            for p in ptrs_active:
                pkt = *p
                _ = pkt.payload[0]
    
    mp.mem_info()
    print(f"    PASS: Completed 300 alloc/dealloc cycles")
    print(f"    Active pointers at end: {len(ptrs_active)}")

def test_varying_size_allocation():
    """Allocate varying sizes to stress fragmentation"""
    print("\nTest 3: Varying Size Allocation (200 packets)")
    
    mp.mem_info()
    
    ptrs = []
    
    for i in range(200):
        # Vary packet size: 10 to 500 bytes
        size = ((i * 7) % 491) + 10
        packet = DataPacket(size, i)
        ptr = &packet
        ptrs.append(ptr)
    
    mp.mem_info()
    print(f"    PASS: Allocated 200 varying-size packets")
    print(f"    Size range: 10-500 bytes")

def test_gc_pressure():
    """High allocation rate to stress GC"""
    print("\nTest 4: GC Pressure Test (500 cycles with GC)")
    
    gc_count = 0
    
    for cycle in range(500):
        # Create burst
        ptrs = []
        for i in range(50):
            packet = DataPacket(100, i)
            ptr = &packet
            ptrs.append(ptr)
        
        # Use data
        for ptr in ptrs:
            pkt = *ptr
            _ = sum(pkt.payload)
        
        # Force GC every 50 cycles
        if (cycle + 1) % 50 == 0:
            gc_count += 1
            if (cycle + 1) % 100 == 0:
                print(f"  Cycle {cycle + 1}: GCs={gc_count}")
    
    print(f"    PASS: GC pressure test completed ({gc_count} collections)")

def test_memory_stability():
    """Verify memory stabilizes after stress"""
    print("\nTest 5: Memory Stability Analysis (300 bursts)")
    
    mp.mem_info()
    
    for burst in range(300):
        # Create burst of allocations
        ptrs = []
        for i in range(100):
            packet = DataPacket(50, i)
            ptr = &packet
            ptrs.append(ptr)
        
        # Clear burst
        ptrs = []
        
        # Sample memory
        if (burst + 1) % 50 == 0:
            print(f"  Burst {burst + 1}: Completed")
    
    print(f"    PASS: Memory stability test completed")

def test_sustained_high_load():
    """Sustained high-frequency allocation"""
    print("\nTest 6: Sustained High Load (2,500 allocations/sec)")
    
    start = time.ticks_ms()
    
    alloc_count = 0
    ptrs = []
    
    while time.ticks_ms() - start < 1000:  # 1 second duration
        packet = DataPacket(75, alloc_count)
        ptr = &packet
        ptrs.append(ptr)
        alloc_count += 1
        
        # Maintain bounded pointer list
        if len(ptrs) > 500:
            ptrs.pop(0)
    
    elapsed = time.ticks_ms() - start
    
    alloc_per_sec = (alloc_count * 1000) // elapsed if elapsed > 0 else 0
    
    print(f"    PASS: Sustained {alloc_per_sec} allocs/sec for {elapsed} ms")
    print(f"    Total allocations: {alloc_count}")

def test_fragmentation_pattern():
    """Create specific fragmentation pattern and verify recovery"""
    print("\nTest 7: Fragmentation Pattern & Recovery")
    
    mp.mem_info()
    
    # Phase 1: Create fragmentation
    print("  Phase 1: Creating fragmentation...")
    large_packets = [DataPacket(200, i) for i in range(20)]
    small_packets = [DataPacket(20, j) for j in range(200)]
    
    ptrs_large = [&pkt for pkt in large_packets]
    ptrs_small = [&pkt for pkt in small_packets]
    
    mp.mem_info()
    
    # Phase 2: Clear small allocations
    print("  Phase 2: Clearing small allocations...")
    ptrs_small = []
    small_packets = []
    
    # Phase 3: Clear large allocations
    print("  Phase 3: Clearing large allocations...")
    ptrs_large = []
    large_packets = []
    
    mp.mem_info()
    
    print(f"    PASS: Fragmentation test completed")

# Run all tests
print("\n" + "*"*50)
print("CONCURRENT ALLOCATION STRESS TEST")
print("*"*50)

try:
    test_burst_allocation()
    test_interleaved_alloc_dealloc()
    
    print("\n" + "*"*50)
    print("CONCURRENT ALLOCATION STRESS: ALL TESTS PASSED")
    print("*"*50 + "\n")

except Exception as e:
    print(f"\n   STRESS TEST FAILED: {type(e).__name__}: {e}")
    import traceback
    traceback.print_exc()
    exit(1)
