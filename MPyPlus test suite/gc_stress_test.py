"""
GC Defragmentation Stress Test Suite
Tests compaction limits with fragmentation, pinned objects, and memory pressure
"""

import gc
import micropython
import time

SEP = "=" * 60

# Enable allocation tracking if available
try:
    gc.set_alloc_tracking(True)
except AttributeError:
    pass


def print_test_exception(label, exc):
    print(label)
    try:
        print("  Exception:", exc)
    except Exception:
        print("  Exception: <unprintable>")


def format_bytes(b):
    for unit in ['B', 'KB', 'MB', 'GB']:
        if b < 1024.0:
            return f"{b:.1f} {unit}"
        b /= 1024.0
    return f"{b:.1f} TB"


def heap_summary(phase):
    print(f"\n{SEP}")
    print(f"HEAP SUMMARY: {phase}")
    print(SEP)
    
    free = gc.mem_free()
    used = gc.mem_alloc()
    total = free + used
    
    print(f"Memory: {format_bytes(used)} used / {format_bytes(total)} total ({used/total*100:.1f}% used)")
    print(f"Free: {format_bytes(free)} ({free/total*100:.1f}% free)")
    
    #  medium block to gauge fragmentation
    test_sizes = [1024, 4096, 16384, 65536]
    print("\nFragmentation test (largest allocatable contiguous blocks):")
    for size in test_sizes:
        try:
            test_obj = bytearray(size)
            del test_obj
            print(f"  Can allocate {format_bytes(size)}")
        except MemoryError:
            print(f"  Cannot allocate {format_bytes(size)} - fragmentation detected")
            break
    
    # Block statistics (if available via GC dump)
    try:
        gc.dump_alloc_table()
    except AttributeError:
        pass


def create_fragmentation(num_objects=200, obj_size=512, pin_interval=10):
    """
    Create fragmented heap with interleaved live/dead/pinned objects
    Returns list of pinned objects that must be kept alive
    """
    print(f"\n--- Creating fragmentation: {num_objects} objects of {obj_size} bytes ---")
    
    objects = []  # Normal objects
    pinned = []   # Pinned objects
    
    for i in range(num_objects):
        # Create alternating pattern of allocation sizes to increase fragmentation
        if i % 3 == 0:
            obj = bytearray(obj_size)  # Medium
        elif i % 3 == 1:
            obj = bytearray(obj_size * 2)  # Large
        else:
            obj = bytearray(obj_size // 2)  # Small
        
        # Fill with pattern to verify integrity later
        if len(obj) >= 4:
            obj[0:4] = b'\xAA\xBB\xCC\xDD'
        
        # Pin every Nth object
        if pin_interval and i % pin_interval == 0:
            try:
                micropython.mem_info()  # Ensure GC is initialized
                gc.pin(obj)  # Pin if your API supports it
                pinned.append(obj)
                print(f"  [PINNED] Object {i}")
            except AttributeError:
                print(f"  Warning: gc.pin() not available - check your build")
                objects.append(obj)
        else:
            objects.append(obj)
        
        # Progress indicator
        if i % 50 == 0 and i > 0:
            print(f"  Created {i}/{num_objects} objects...")
    
    print(f"Created {len(objects)} normal + {len(pinned)} pinned objects")
    
    # Delete normal objects to create holes
    print("\n--- Deleting normal objects to create holes ---")
    del objects
    objects = None
    gc.collect()
    
    return pinned


def torture_pattern_alternating():
    """Pattern 1: Alternating allocation/deallocation spikes"""
    print("\n" + SEP)
    print("TEST 1: Alternating Allocation Spikes")
    print(SEP)
    
    heap_summary("Before test")
    
    # Phase 1: Allocate many small objects
    print("\n[Phase 1] Allocating 500 small objects...")
    small_objs = [bytearray(64) for _ in range(500)]
    heap_summary("After small allocations")
    
    # Phase 2: Delete half (odd indices)
    print("\n[Phase 2] Deleting half (odd indices)...")
    small_objs = [obj for i, obj in enumerate(small_objs) if i % 2 == 0]
    gc.collect()
    heap_summary("After deleting half")
    
    # Phase 3: Allocate large objects (should fragment into holes)
    print("\n[Phase 3] Allocating large objects into holes...")
    large_objs = []
    for i in range(100):
        try:
            large_objs.append(bytearray(1024))
        except MemoryError:
            print(f"  Failed at {i} objects - fragmentation limiting")
            break
    heap_summary("After large allocations")
    
    # Phase 4: Delete everything and force collection
    print("\n[Phase 4] Cleaning up...")
    del small_objs
    del large_objs
    gc.collect()
    heap_summary("After full cleanup")
    
    return True


def torture_pattern_stress(num_cycles=10):
    """Pattern 2: Repeated allocate/free cycles to stress compaction"""
    print("\n" + SEP)
    print(f"TEST 2: Stress Cycle Test ({num_cycles} cycles)")
    print(SEP)
    
    results = {'cycles': [], 'free_before': [], 'free_after': [], 'max_alloc': []}
    
    for cycle in range(num_cycles):
        print(f"\n--- Cycle {cycle+1}/{num_cycles} ---")
        
        # Record free memory before
        free_before = gc.mem_free()
        results['free_before'].append(free_before)
        
        # Create fragmentation
        print(f"  Creating {50 + cycle*10} objects...")
        temp_objs = []
        for size in range(100, 2000, 50):
            temp_objs.append(bytearray(size))
        
        # Delete some, keep others
        print(f"  Deleting 60% of objects...")
        temp_objs = [obj for i, obj in enumerate(temp_objs) if i % 5 < 2]  # Keep 40%
        
        # Try to allocate maximum possible contiguous block
        print(f"  Testing max allocation...")
        max_size = 0
        for size in [65536, 32768, 16384, 8192, 4096, 2048, 1024, 512]:
            try:
                test = bytearray(size)
                max_size = size
                del test
                break
            except MemoryError:
                continue
        
        results['max_alloc'].append(max_size)
        print(f"  Largest allocatable: {format_bytes(max_size)}")
        
        # Force collection
        gc.collect()
        free_after = gc.mem_free()
        results['free_after'].append(free_after)
        
        # Cleanup
        del temp_objs
        gc.collect()
        
        # Show trend
        fragmentation_loss = (free_before - free_after) / free_before * 100 if free_before > 0 else 0
        print(f"  Free memory: {format_bytes(free_before)} → {format_bytes(free_after)}")
        print(f"  Fragmentation loss: {fragmentation_loss:.1f}%")
    
    # Summary
    print(f"\n--- Stress Test Summary ---")
    avg_loss = sum((results['free_before'][i] - results['free_after'][i]) 
                   for i in range(num_cycles)) / num_cycles
    print(f"Average fragmentation per cycle: {format_bytes(avg_loss)}")
    print(f"Max contiguous allocation achieved: {format_bytes(max(results['max_alloc']))}")
    print(f"Min contiguous allocation achieved: {format_bytes(min(results['max_alloc']))}")
    
    return results


def torture_pattern_pinned_islands(num_islands=20):
    """Pattern 3: Pinned objects acting as barriers to compaction"""
    print("\n" + SEP)
    print(f"TEST 3: Pinned Island Test ({num_islands} pinned islands)")
    print(SEP)
    
    pinned_objects = []
    
    heap_summary("Before pinned allocations")
    
    # Create pinned 'islands' with free space between them
    print(f"\nCreating {num_islands} pinned islands...")
    for i in range(num_islands):
        # Pinned medium object
        obj = bytearray(4096)
        obj[0:8] = f"PINNED{i:02d}".encode()
        
        try:
            gc.pin(obj)
            pinned_objects.append(obj)
            print(f"  Island {i}: Pinned {format_bytes(4096)}")
        except AttributeError:
            print("  Warning: gc.pin() not available")
            break
        
        # Create some unpinned filler between islands
        if i < num_islands - 1:
            filler = [bytearray(1024) for _ in range(5)]
            # Delete filler immediately to create holes
            del filler
            gc.collect()
    
    heap_summary("After pinned islands created")
    
    # Try to allocate a huge object that should span between pinned islands
    print("\nAttempting massive allocation that must avoid pinned regions...")
    try:
        massive = bytearray(1024 * 50)  # 50KB
        print(f"  SUCCESS: Allocated {format_bytes(len(massive))} around pinned objects")
        del massive
    except MemoryError:
        print(f"  FAILED: Cannot allocate large contiguous block - pinned objects preventing compaction")
        
        # Try smaller allocations
        for size in [32768, 16384, 8192, 4096, 2048]:
            try:
                test = bytearray(size)
                print(f"  Can only allocate {format_bytes(size)} around pins")
                del test
                break
            except MemoryError:
                continue
    
    heap_summary("Final state")
    
    # Verify pinned objects intact
    print("\nVerifying pinned object integrity...")
    all_ok = True
    for i, obj in enumerate(pinned_objects):
        expected = f"PINNED{i:02d}".encode()
        if obj[0:8] != expected:
            print(f"  Island {i} corrupted!")
            all_ok = False
        else:
            print(f"  Island {i} intact")
    
    return all_ok, pinned_objects


def torture_pattern_max_fragmentation():
    """Pattern 4: Worst-case fragmentation scenario"""
    print("\n" + SEP)
    print("TEST 4: Maximum Fragmentation Test")
    print(SEP)
    
    heap_summary("Initial state")
    
    # Create worst-case fragmentation: alternating sizes that prevent coalescing
    print("\nCreating worst-case fragmentation pattern...")
    frag_objects = []
    alloc_failed = False
    
    # Pattern: Large, Small, Large, Small... so free blocks are all small
    # Scale intensity to current free memory so this test reports useful data
    # instead of aborting immediately when prior tests left little headroom.
    target_pairs = min(200, max(20, gc.mem_free() // 8192))
    for i in range(target_pairs):
        try:
            frag_objects.append(bytearray(2048))  # Large
            frag_objects.append(bytearray(256))   # Small
        except MemoryError:
            alloc_failed = True
            break

    if alloc_failed:
        print("  Warning: hit memory limit while building fragmentation pattern")
    
    heap_summary("After allocation")
    
    # Delete the large objects, leaving many small free blocks
    print("\nDeleting large objects to create small free blocks...")
    frag_objects = [obj for i, obj in enumerate(frag_objects) if i % 2 == 1]  # Keep small only
    gc.collect()
    heap_summary("After deleting large objects (should be fragmented)")
    
    # Try to allocate a block larger than a single free block
    print("\nAttempting to allocate medium block (needs coalescing)...")
    success = False
    for size in [4096, 2048, 1024, 512, 256]:
        try:
            test = bytearray(size)
            if size > 256:
                print(f"  SUCCESS: Allocated {format_bytes(size)} - compaction working!")
                success = True
            else:
                print(f"  PARTIAL: Can allocate {format_bytes(size)} (compaction limited)")
            del test
            break
        except MemoryError:
            if size > 256:
                print(f"  FAILED: Cannot allocate {format_bytes(size)} - severe fragmentation")
            continue
    
    # Cleanup
    del frag_objects
    gc.collect()
    heap_summary("After full cleanup (should be defragmented)")
    
    return success


def torture_pattern_thrashing(num_iterations=5):
    """Pattern 5: Allocation thrashing to stress GC limits"""
    print("\n" + SEP)
    print(f"TEST 5: GC Thrashing Test ({num_iterations} iterations)")
    print(SEP)
    
    times = []
    failures = 0
    ticks_ms = getattr(time, "ticks_ms", None)
    ticks_diff = getattr(time, "ticks_diff", None)

    def now_ms():
        if ticks_ms is not None:
            return ticks_ms()
        return int(time.time() * 1000)

    def diff_ms(new, old):
        if ticks_diff is not None:
            return ticks_diff(new, old)
        return new - old
    
    for iteration in range(num_iterations):
        if gc.mem_free() < 32768:
            print("\nStopping thrash: low free memory")
            failures += 1
            break

        try:
            print(f"\nIteration {iteration+1}/{num_iterations}")
            start_time = now_ms()

            # Rapid allocate/free cycle
            objects = []
            max_objects = min(100, max(30, gc.mem_free() // 8192))
            for i in range(max_objects):
                # Create objects of varying sizes
                size = 64 * (1 + (i % 10))
                obj = bytearray(size)
                obj[0] = iteration & 0xFF
                objects.append(obj)
                
                # Free some early ones
                if i > 50 and i % 10 == 0:
                    objects.pop(0)
            
            # Force multiple collections
            for _ in range(3):
                gc.collect()
            
            # Clean up
            del objects
            gc.collect()
            
            elapsed = diff_ms(now_ms(), start_time)
            times.append(elapsed)
            print(f"  Completed in {elapsed} ms")
            
        except MemoryError:
            failures += 1
            print("  MemoryError during iteration")
            gc.collect()
            # Show memory health
            free = gc.mem_free()
            used = gc.mem_alloc()
            print(f"  Memory: {format_bytes(used)} used / {format_bytes(free)} free")

        except Exception as e:
            failures += 1
            gc.collect()
            print(f"  Iteration aborted ({type(e).__name__}: {e})")
    
    print(f"\n--- Thrashing Summary ---")
    print(f"Successful iterations: {num_iterations - failures}/{num_iterations}")
    if times:
        avg_time = sum(times) / len(times)
        print(f"Average iteration time: {avg_time:.1f} ms")
    print(f"Failures: {failures}")
    
    return times, failures


def main():
    """Run all torture tests"""
    print("\n" + SEP)
    print("GC DEFRAGMENTATION TORTURE TEST SUITE")
    print(SEP)
    
    # Print build info
    try:
        import sys
        print(f"MicroPython version: {sys.version}")
        print(f"Platform: {sys.platform}")
    except:
        pass
    
    print(f"Initial memory: {format_bytes(gc.mem_alloc())} used / {format_bytes(gc.mem_free())} free")
    
    # Track overall results
    results = {
        'test1': False,
        'test2': None,
        'test3': (False, 0),
        'test4': False,
        'test5': None
    }
    
    # Run tests
    try:
        results['test1'] = torture_pattern_alternating()
    except Exception as e:
        print_test_exception("Test 1 failed", e)
    
    try:
        results['test2'] = torture_pattern_stress(num_cycles=8)
    except Exception as e:
        print_test_exception("Test 2 failed", e)
    
    try:
        results['test4'] = torture_pattern_max_fragmentation()
    except Exception as e:
        print_test_exception("Test 4 failed", e)
    
    try:
        pinned = []
        try:
            ok, pinned = torture_pattern_pinned_islands(num_islands=15)
            results['test3'] = (ok, len(pinned))
        finally:
            # Always release pinning, even if the test raises midway.
            try:
                for obj in pinned:
                    gc.unpin(obj)
            except AttributeError:
                pass
            gc.collect()
    except Exception as e:
        print_test_exception("Test 3 failed", e)

    try:
        results['test5'] = torture_pattern_thrashing(num_iterations=10)
    except Exception as e:
        print_test_exception("Test 5 failed", e)

    # Final summary (be tolerant when heap is nearly exhausted)
    gc.collect()
    try:
        print()
        print(SEP)
        print("FINAL SUMMARY")
        print(SEP)
        print(f"Test 1 (Alternating spikes): {'PASS' if results['test1'] else 'FAIL'}")
        print(f"Test 2 (Stress cycles): {'COMPLETED' if results['test2'] else 'FAIL'}")
        print(f"Test 3 (Pinned islands): {'PASS' if results['test3'][0] else 'FAIL'} ({results['test3'][1]} pins)")
        print(f"Test 4 (Max fragmentation): {'PASS' if results['test4'] else 'FAIL'}")
        print(f"Test 5 (GC thrashing): {'COMPLETED' if results['test5'] else 'FAIL'}")
    except MemoryError:
        gc.collect()
        print("FINAL SUMMARY (low memory)")
        print("Test 1:", bool(results['test1']))
        print("Test 2:", bool(results['test2']))
        print("Test 3:", bool(results['test3'][0]), "pins:", results['test3'][1])
        print("Test 4:", bool(results['test4']))
        print("Test 5:", bool(results['test5']))

    # Final heap state
    print("\nFinal heap state:")
    gc.collect()
    try:
        heap_summary("Complete")
    except MemoryError:
        gc.collect()
        print("Heap summary skipped due to low memory")

    print()
    print(SEP)
    print("TEST SUITE COMPLETE")
    print(SEP)


if __name__ == "__main__":
    try:
        main()
    except MemoryError:
        gc.collect()
        print("Aborted: out of memory")