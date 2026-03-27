"""
MicroPython Pointers: Edge Cases Test
Type safety, boundary conditions, error handling
"""

import micropython as mp

class SensorData:
    def __init__(self, size=100):
        self.readings = [i * 0.1 for i in range(size)]
        self.timestamps = [i for i in range(size)]
        self.calibration = 1.0

print("\n" + "*"*50)
print("EDGE CASE TEST: Invalid Operations")
print("*"*50)

# Test 1: Dereference non-pointer
print("\nTest 1: Dereference non-pointer")
try:
    not_a_ptr = 42
    result = *not_a_ptr
    print("  ERROR: Should have raised TypeError!")
except TypeError as e:
    print(f"  Correctly caught: {type(e).__name__}")
except Exception as e:
    print(f"  Wrong exception: {type(e).__name__}: {e}")

# Test 2: Address-of immutable
print("\nTest 2: Address of immutable")
try:
    t = (1, 2, 3)
    ptr = &t
    print(f"  Can create pointer to tuple: {ptr}")
except Exception as e:
    print(f"  Failed: {type(e).__name__}: {e}")

# Test 3: Pointer member access
print("\nTest 3: Pointer member access")
try:
    sensor = SensorData(10)
    ptr_sensor = &sensor
    s = *ptr_sensor
    reading = s.readings[0]
    print(f"  Member access works: {reading}")
except Exception as e:
    print(f"  Failed: {type(e).__name__}: {e}")

print("\n" + "*"*50)
print("EDGE CASE TEST: Boundary Conditions")
print("*"*50)

# Test 1: Empty structures
print("\nTest 1: Empty structure")
try:
    class EmptyStruct:
        pass
    
    e = EmptyStruct()
    ptr_e = &e
    s = *ptr_e
    print(f"  Empty structure pointer works")
except Exception as e:
    print(f"  Failed: {type(e).__name__}: {e}")

# Test 2: Pointer to primitive
print("\nTest 2: Pointer to primitive")
try:
    x = 42
    ptr_x = &x
    v = *ptr_x
    print(f"  Primitive pointer: {ptr_x} -> {v}")
except Exception as e:
    print(f"  Failed: {type(e).__name__}: {e}")

# Test 3: Reassignment through pointer
print("\nTest 3: Reassignment through pointer")
try:
    obj = SensorData(5)
    ptr = &obj
    obj.calibration = 2.0
    s = *ptr
    print(f"  Calibration before: {s.calibration}")
    s.calibration = 5.0
    s = *ptr
    print(f"  Calibration after:  {s.calibration}")
    print(f"  Reassignment works")
except Exception as e:
    print(f"  Failed: {type(e).__name__}: {e}")

print("\n" + "*"*50)
print("EDGE CASE TEST: Type Safety")
print("*"*50)

# Test 1: Type validation
print("\nTest 1: Type validation")
try:
    x = 42
    ptr = &x
    v = *ptr
    print(f"  Type validation passed: {v}")
except TypeError as e:
    print(f"  Correctly caught type error: {e}")
except Exception as e:
    print(f"  Wrong error: {type(e).__name__}: {e}")

# Test 2: Invalid member access
print("\nTest 2: Invalid member access")
try:
    sensor = SensorData(10)
    ptr = &sensor
    s = *ptr
    val = s.nonexistent_field
    print(f"  Should have raised AttributeError")
except AttributeError as e:
    print(f"  Correctly caught: AttributeError")
except Exception as e:
    print(f"  Exception raised: {type(e).__name__}")

print("\n" + "*"*50 + "\n")
