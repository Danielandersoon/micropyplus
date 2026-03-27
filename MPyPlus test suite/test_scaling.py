"""
MicroPython Pointers: Scaling Test
Tests memory behavior as data size increases
"""

import micropython as mp

class SensorData:
    def __init__(self, size=100):
        self.readings = [i * 0.1 for i in range(size)]
        self.timestamps = [i for i in range(size)]
        self.calibration = 1.0

def process_by_value(sensor):
    """Process by creating copy"""
    copy = SensorData(len(sensor.readings))
    copy.readings = sensor.readings
    copy.calibration = sensor.calibration
    
    total = 0.0
    for r in copy.readings:
        total += r * copy.calibration
    return total

def process_by_ref(sensor_ptr):
    """Process via pointer (no copy)"""
    s = *sensor_ptr
    total = 0.0
    for r in s.readings:
        total += r * s.calibration
    return total

print("\n" + "*"*50)
print("SCALING TEST: Memory Efficiency")
print("*"*50)

sizes = [100, 500, 1000]

for size in sizes:
    print(f"\nData size: {size} elements")
    print("-" * 80)
    
    try:
        # By reference
        print("  Pointer-based:")
        mp.mem_info()
        sensor_ref = SensorData(size)
        ptr = &sensor_ref
        s = *ptr
        result = process_by_ref(ptr)
        mp.mem_info()
        
        # By value
        print("  Pass-by-value:")
        mp.mem_info()
        sensor_val = SensorData(size)
        result = process_by_value(sensor_val)
        mp.mem_info()
        
        del sensor_ref, sensor_val, ptr
        
    except MemoryError:
        print(f"     MemoryError at {size} elements - heap exhausted")
        print("  [Stopping allocation attempts]")
        break

print("\n" + "*"*50 + "\n")
