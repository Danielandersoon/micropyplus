# drone_control_simulation.py
import time

class GyroCalibration:
    __slots__ = ('bias',)
    def __init__(self):
        self.bias = 0.001   # gyro bias value (rad/s)

class IMU:
    __slots__ = ('calibration',)
    def __init__(self):
        self.calibration = GyroCalibration()

class Sensors:
    __slots__ = ('imu',)
    def __init__(self):
        self.imu = IMU()

class FlightController:
    __slots__ = ('sensors',)
    def __init__(self):
        self.sensors = Sensors()

def standard_control_loop(fc, iterations):
    total = 0.0
    for _ in range(iterations):
        # Each iteration traverses the chain: fc.sensors.imu.calibration.bias
        bias = fc.sensors.imu.calibration.bias
        total += bias   # simulate using the value
    return total

def pointer_control_loop(ptr_to_fc, iterations):
    fc = *ptr_to_fc
    print("Dereferenced:", (ptr_to_fc))

    # Cache the deepest object (the GyroCalibration instance)
    calib = fc.sensors.imu.calibration
    total = 0.0
    for _ in range(iterations):
        bias = calib.bias   # direct attribute access
        total += bias
    return total

fc = FlightController()
ptr = &fc
obj = *ptr
print("Dereferenced:", obj)
cal = obj.sensors.imu.calibration
print("Calibration:", cal)
print("Bias:", cal.bias)
iterations = 1000000

# Warm‑up
standard_control_loop(fc, 1000)
pointer_control_loop(ptr, 1000)

# Benchmark standard
start = time.ticks_us()
res_std = standard_control_loop(fc, iterations)
std_time = time.ticks_diff(time.ticks_us(), start)

# Benchmark pointer cached
start = time.ticks_us()
res_ptr = pointer_control_loop(ptr, iterations)
ptr_time = time.ticks_diff(time.ticks_us(), start)

print(f"Standard control loop: {std_time} µs")
print(f"Pointer‑cached loop:   {ptr_time} µs")
print(f"Speedup:               {std_time / ptr_time:.2f}x")

# Verify both loops computed the same total
expected = iterations * 0.001
print(f"Standard result: {res_std:.3f}, Pointer result: {res_ptr:.3f}")
if abs(res_std - expected) < 0.001 and abs(res_ptr - expected) < 0.001:
    print("Correctness verified.")
else:
    print("Results mismatch.")