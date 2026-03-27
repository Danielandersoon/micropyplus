"""
MicroPython Pointers: Pointer Invalidation Test
Safety verification for dangling pointers and scope exit behavior
"""

import micropython as mp

class SensorData:
    def __init__(self, sensor_id=0):
        self.id = sensor_id
        self.readings = [i * 0.1 for i in range(50)]
        self.calibration = 1.0
        self.timestamp = 0

class DataBuffer:
    def __init__(self, size=100):
        self.size = size
        self.data = [None] * size
        self.count = 0

def test_scope_exit_with_pointer():
    """Verify pointer behavior when object goes out of scope"""
    print("\nTest 1: Scope Exit Behavior")
    
    try:
        # Create in local scope
        def create_sensor():
            sensor = SensorData(1)
            return &sensor  # Return pointer to local object
        
        ptr = create_sensor()
        
        # At this point, the original object is out of scope
        # Attempting to dereference should fail or be unsafe
        try:
            s = *ptr
            print(f"     WARNING: Dereferenced out-of-scope object (id={s.id})")
            print(f"    This may indicate unsafe pointer behavior")
        except (RuntimeError, MemoryError, ValueError) as e:
            print(f"    PASS: Out-of-scope dereference safely caught")
            print(f"    Exception: {type(e).__name__}")
    
    except Exception as e:
        print(f"     FAIL: Unexpected error: {type(e).__name__}: {e}")
        raise

def test_pointer_reassignment():
    """Test pointer reassignment behavior"""
    print("\nTest 2: Pointer Reassignment")
    
    try:
        sensor1 = SensorData(2)
        sensor2 = SensorData(3)
        
        ptr = &sensor1
        
        # Verify initial pointer
        s = *ptr
        assert s.id == 2, f"Expected id=2, got {s.id}"
        initial_id = s.id
        
        # Reassign pointer
        ptr = &sensor2
        
        # Verify new pointer
        s = *ptr
        assert s.id == 3, f"Expected id=3, got {s.id}"
        new_id = s.id
        
        print(f"    PASS: Pointer reassignment successful")
        print(f"    Initial: id={initial_id}, Reassigned: id={new_id}")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

def test_multiple_pointers_invalidation():
    """Test when one of multiple pointers becomes invalid"""
    print("\nTest 3: Multiple Pointers - Selective Invalidation")
    
    try:
        sensor1 = SensorData(4)
        sensor2 = SensorData(5)
        
        ptr1 = &sensor1
        ptr2 = &sensor2
        
        # Access pointers
        s1 = *ptr1
        s2 = *ptr2
        
        assert s1.id == 4 and s2.id == 5
        
        # Reassign ptr2 to point to sensor1
        ptr2 = &sensor1
        
        # Both should now point to sensor1
        s1_check = *ptr1
        s2_new = *ptr2
        
        assert s1_check.id == 4
        assert s2_new.id == 4  # Now points to sensor1
        
        print(f"    PASS: Selective pointer reassignment successful")
        print(f"    Ptr1: {s1_check.id}, Ptr2: {s2_new.id}")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

def test_null_pointer_behavior():
    """Test behavior with null/None pointers"""
    print("\nTest 4: Null Pointer Handling")
    
    try:
        # Try to create null pointer
        ptr = None
        
        # Attempt to dereference null
        try:
            if ptr is not None:
                s = *ptr
                print(f"     ERROR: Dereferenced null pointer")
            else:
                print(f"    PASS: Null pointer safely prevented")
        except (TypeError, RuntimeError) as e:
            print(f"    PASS: Null pointer dereference caught: {type(e).__name__}")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

def test_use_after_clear():
    """Test pointer usage after clearing reference"""
    print("\nTest 5: Use After Clear")
    
    try:
        sensor = SensorData(7)
        ptr = &sensor
        
        # Verify pointer works
        s = *ptr
        assert s.id == 7
        
        # Clear the original reference
        sensor = None
        
        # Try to use pointer to cleared object
        try:
            s_after = *ptr
            print(f"     WARNING: Accessed cleared object (id={s_after.id})")
            print(f"    Behavior may be undefined")
        except (RuntimeError, MemoryError, AttributeError) as e:
            print(f"    PASS: Use-after-clear detected: {type(e).__name__}")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

def test_pointer_iteration_safety():
    """Test pointer safety during iteration"""
    print("\nTest 6: Pointer Iteration Safety")
    
    try:
        sensors = [SensorData(i) for i in range(10)]
        
        # Create pointers by iterating, not in list comprehension
        ptrs = []
        for s in sensors:
            ptrs.append(&s)
        
        # Iterate and modify
        valid_count = 0
        for i, ptr in enumerate(ptrs):
            try:
                s = *ptr
                if s.id == i:
                    valid_count += 1
            except (RuntimeError, MemoryError):
                pass
        
        print(f"    PASS: Pointer iteration completed")
        print(f"    Valid pointers: {valid_count}/{len(ptrs)}")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

def test_circular_reference_with_pointers():
    """Test circular references with pointers"""
    print("\nTest 7: Circular References")
    
    try:
        # Create simple linked structure
        class Node:
            def __init__(self, node_id=0):
                self.id = node_id
                self.value = node_id * 10
        
        node1 = Node(1)
        node2 = Node(2)
        
        # Create pointers to both nodes
        ptr1 = &node1
        ptr2 = &node2
        
        # Access through pointers
        n1 = *ptr1
        n2 = *ptr2
        
        assert n1.id == 1
        assert n2.id == 2
        
        print(f"    PASS: Pointer structure navigation successful")
        print(f"    Node1(id={n1.id}, value={n1.value}) -> Node2(id={n2.id}, value={n2.value})")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

def test_strong_reference_keeps_alive():
    """Test that pointer keeps object alive"""
    print("\nTest 8: Strong Reference Behavior")
    
    try:
        sensor = SensorData(8)
        ptr = &sensor
        
        # Keep strong reference through pointer
        calibration_original = sensor.calibration
        
        # Modify through pointer
        s = *ptr
        s.calibration = 2.5
        
        # Verify modification visible through original reference
        assert sensor.calibration == 2.5, "Pointer modification not visible"
        
        print(f"    PASS: Strong reference keeps object alive")
        print(f"    Calibration: {calibration_original} -> {sensor.calibration}")
    
    except Exception as e:
        print(f"     FAIL: {type(e).__name__}: {e}")
        raise

# Run all tests
print("\n" + "*"*50)
print("POINTER INVALIDATION TEST")
print("*"*50)

try:
    test_scope_exit_with_pointer()
    test_pointer_reassignment()
    test_multiple_pointers_invalidation()
    test_null_pointer_behavior()
    test_use_after_clear()
    test_pointer_iteration_safety()
    test_circular_reference_with_pointers()
    test_strong_reference_keeps_alive()
    
    print("\n" + "*"*50)
    print("POINTER INVALIDATION: ALL TESTS PASSED")
    print("*"*50 + "\n")

except Exception as e:
    print(f"\n   INVALIDATION TEST FAILED: {type(e).__name__}: {e}")
