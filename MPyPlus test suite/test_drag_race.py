"""
MicroPython Pointers: Drag Race Test
Compares pass-by-value vs pointers on equal ground
"""

import micropython as mp
import time

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
print("DRAG RACE: 50 iterations * 200-element sensor")
print("*"*50)

# Test 1: Pass-by-value
print("\nPASS-BY-VALUE (copies):")
sensor_val = SensorData(200)

mp.mem_info()
t0 = time.ticks_ms()
for _ in range(50):
    process_by_value(sensor_val)
t1 = time.ticks_ms()
time_val = t1 - t0

print(f"Time: {time_val} ms")
mp.mem_info()

# Test 2: Pass-by-ref
print("\nPASS-BY-REFERENCE (pointers):")
sensor_ref = SensorData(200)
ptr_sensor = &sensor_ref

mp.mem_info()
t0 = time.ticks_ms()
for _ in range(50):
    process_by_ref(ptr_sensor)
t1 = time.ticks_ms()
time_ref = t1 - t0

print(f"Time: {time_ref} ms")
mp.mem_info()

# Analysis
print("\nRESULTS:")
print(f"  Pass-by-value: {time_val} ms")
print(f"  Pass-by-ref:   {time_ref} ms")
if time_ref > 0:
    print(f"  Speedup:       {time_val / time_ref:.1f}x faster")
print(f"  Time saved:    {time_val - time_ref} ms")
print("\n" + "*"*50 + "\n")
