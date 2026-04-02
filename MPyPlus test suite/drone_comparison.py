"""
Drone flight controller comparison: PASS-BY-VALUE vs POINTER-BASED
Testing memory efficiency and execution performance
"""

import micropython as mp
import time


class IMUSensor:
    def __init__(self):
        self.accel_x = 0.0
        self.accel_y = 0.0
        self.accel_z = 9.81
        self.gyro_x = 0.0
        self.gyro_y = 0.0
        self.gyro_z = 0.0
        self.roll = 0.0
        self.pitch = 0.0
        self.yaw = 0.0
        self.timestamp = time.ticks_ms()
        self.ready = True


class BarometerSensor:
    def __init__(self):
        self.altitude = 0.0
        self.pressure = 101325.0
        self.temperature = 25.0
        self.vertical_velocity = 0.0
        self.timestamp = time.ticks_ms()


class BatteryMonitor:
    def __init__(self):
        self.voltage = 11.8
        self.current = 0.0
        self.power_consumption = 0.0
        self.capacity_used = 0.0
        self.estimated_flight_time = 1800.0
        self.low_battery_warning = False
        self.critical_battery = False


class FlightController:
    def __init__(self):
        self.roll = 0.0
        self.pitch = 0.0
        self.yaw = 0.0
        self.throttle = 0.0
        self.armed = False
        self.mode = "DISARMED"
        self.timestamp = time.ticks_ms()


class MotorController:
    def __init__(self, motor_id):
        self.motor_id = motor_id
        self.target_speed = 0
        self.actual_speed = 0
        self.current_draw = 0.0
        self.temperature = 25.0
        self.armed = False
        self.error = False

#
# PASS-BY-VALUE FUNCS
#

def stabilize_flight_by_val(imu, baro, controller):
    """Stabilization - creates copies of objects"""
    # Simulate copying objects for pass-by-value
    imu_copy = IMUSensor()
    imu_copy.roll = imu.roll
    imu_copy.pitch = imu.pitch
    imu_copy.yaw = imu.yaw
    
    controller_copy = FlightController()
    controller_copy.roll = controller.roll
    controller_copy.pitch = controller.pitch
    controller_copy.yaw = controller.yaw
    controller_copy.throttle = controller.throttle
    
    roll_error = controller_copy.roll - imu_copy.roll
    pitch_error = controller_copy.pitch - imu_copy.pitch
    yaw_error = controller_copy.yaw - imu_copy.yaw
    
    motor_1 = controller_copy.throttle + roll_error + pitch_error - yaw_error
    motor_2 = controller_copy.throttle - roll_error + pitch_error + yaw_error
    motor_3 = controller_copy.throttle - roll_error - pitch_error - yaw_error
    motor_4 = controller_copy.throttle + roll_error - pitch_error + yaw_error
    
    return {
        "motor_1": motor_1,
        "motor_2": motor_2,
        "motor_3": motor_3,
        "motor_4": motor_4
    }


def battery_monitor_by_val(battery):
    """Monitor battery - creates copy"""
    battery_copy = BatteryMonitor()
    battery_copy.voltage = battery.voltage
    battery_copy.critical_battery = battery.critical_battery
    battery_copy.low_battery_warning = battery.low_battery_warning
    
    if battery_copy.voltage < 9.5:
        battery_copy.critical_battery = True
        return "CRITICAL"
    elif battery_copy.voltage < 10.5:
        battery_copy.low_battery_warning = True
        return "WARNING"
    else:
        battery_copy.low_battery_warning = False
        return "OK"

#
# PASS-BY-REFERENCE (POINTER) FUNCTIONS 
#

def stabilize_flight_by_ref(imu, baro, controller):
    """Stabilization - no copies, direct access"""
    roll_error = controller.roll - imu.roll
    pitch_error = controller.pitch - imu.pitch
    yaw_error = controller.yaw - imu.yaw
    
    motor_1 = controller.throttle + roll_error + pitch_error - yaw_error
    motor_2 = controller.throttle - roll_error + pitch_error + yaw_error
    motor_3 = controller.throttle - roll_error - pitch_error - yaw_error
    motor_4 = controller.throttle + roll_error - pitch_error + yaw_error
    
    return {
        "motor_1": motor_1,
        "motor_2": motor_2,
        "motor_3": motor_3,
        "motor_4": motor_4
    }


def battery_monitor_by_ref(battery):
    """Monitor battery - direct access"""
    if battery.voltage < 9.5:
        battery.critical_battery = True
        return "CRITICAL"
    elif battery.voltage < 10.5:
        battery.low_battery_warning = True
        return "WARNING"
    else:
        battery.low_battery_warning = False
        return "OK"

#
# PASS-BY-ASSIGNMENT FUNCTIONS
#

def stabilize_flight_by_assignment(imu, baro, controller):
    """Stabilization - Python's default pass-by-assignment"""
    # Direct access to object references (Python default)
    roll_error = controller.roll - imu.roll
    pitch_error = controller.pitch - imu.pitch
    yaw_error = controller.yaw - imu.yaw
    
    motor_1 = controller.throttle + roll_error + pitch_error - yaw_error
    motor_2 = controller.throttle - roll_error + pitch_error + yaw_error
    motor_3 = controller.throttle - roll_error - pitch_error - yaw_error
    motor_4 = controller.throttle + roll_error - pitch_error + yaw_error
    
    return {
        "motor_1": motor_1,
        "motor_2": motor_2,
        "motor_3": motor_3,
        "motor_4": motor_4
    }


def battery_monitor_by_assignment(battery):
    """Monitor battery - Python's default pass-by-assignment"""
    if battery.voltage < 9.5:
        battery.critical_battery = True
        return "CRITICAL"
    elif battery.voltage < 10.5:
        battery.low_battery_warning = True
        return "WARNING"
    else:
        battery.low_battery_warning = False
        return "OK"

#
# MAIN COMPARISON
#

print("\n" + "#" * 90)
print("#  DRONE FLIGHT CONTROLLER: PASS-BY-VALUE vs POINTER-BASED COMPARISON")
print("#" * 90 + "\n")

#
# TEST 1: PASS-BY-VALUE
#
print("TEST 1: PASS-BY-VALUE VERSION")
print("-" * 90)

print("\nInitializing sensors...")
imu_val = IMUSensor()
baro_val = BarometerSensor()
battery_val = BatteryMonitor()
controller_val = FlightController()

print("Memory before test:")
mp.mem_info()

print("\nRunning 50 control loop iterations (with object copying)...")
total_time_val = 0
for cycle in range(86):
    cycle_start = time.ticks_ms()
    
    # Update sensor values
    imu_val.roll = 0.1 * cycle
    imu_val.pitch = 0.05 * cycle
    imu_val.yaw = 0.2 * cycle
    baro_val.altitude = 10.0 + (0.1 * cycle)
    controller_val.throttle = 0.5 + (0.0001 * cycle)
    
    # Run control algorithms (creates copies inside functions)
    stab_result = stabilize_flight_by_val(imu_val, baro_val, controller_val)
    batt_status = battery_monitor_by_val(battery_val)
    
    cycle_end = time.ticks_ms()
    cycle_time = cycle_end - cycle_start
    total_time_val += cycle_time

print(f"Completed 86 cycles")
print(f"Total time: {total_time_val} ms")
print("Memory after test:")
mp.mem_info()

# ========== TEST 2: PASS-BY-REFERENCE (DIRECT ACCESS) ==========
print("\n\nTEST 2: PASS-BY-REFERENCE (DIRECT ACCESS) VERSION")
print("-" * 90)

print("\nInitializing sensors...")
imu_ref = IMUSensor()
baro_ref = BarometerSensor()
battery_ref = BatteryMonitor()
controller_ref = FlightController()

print("Memory before test (direct access):")
mp.mem_info()

print("\nRunning 86 control loop iterations (no object copying)...")
total_time_ref = 0
for cycle in range(86):
    cycle_start = time.ticks_ms()
    
    # Update sensor values
    imu_ref.roll = 0.1 * cycle
    imu_ref.pitch = 0.05 * cycle
    imu_ref.yaw = 0.2 * cycle
    baro_ref.altitude = 10.0 + (0.1 * cycle)
    controller_ref.throttle = 0.5 + (0.0001 * cycle)
    
    # Run control algorithms (no copies, direct access)
    stab_result = stabilize_flight_by_ref(imu_ref, baro_ref, controller_ref)
    batt_status = battery_monitor_by_ref(battery_ref)
    
    cycle_end = time.ticks_ms()
    cycle_time = cycle_end - cycle_start
    total_time_ref += cycle_time

print(f"Completed 86 cycles")
print(f"Total time: {total_time_ref} ms")
print("Memory after test:")
mp.mem_info()

# ========== TEST 3: PASS-BY-ASSIGNMENT (PYTHON DEFAULT) ==========
print("\n\nTEST 3: PASS-BY-ASSIGNMENT (PYTHON DEFAULT) VERSION")
print("-" * 90)

print("\nInitializing sensors...")
imu_assign = IMUSensor()
baro_assign = BarometerSensor()
battery_assign = BatteryMonitor()
controller_assign = FlightController()

print("Memory before test (Python default pass-by-assignment):")
mp.mem_info()

print("\nRunning 86 control loop iterations (Python's default semantics)...")
total_time_assign = 0
for cycle in range(86):
    cycle_start = time.ticks_ms()
    
    # Update sensor values
    imu_assign.roll = 0.1 * cycle
    imu_assign.pitch = 0.05 * cycle
    imu_assign.yaw = 0.2 * cycle
    baro_assign.altitude = 10.0 + (0.1 * cycle)
    controller_assign.throttle = 0.5 + (0.0001 * cycle)
    
    # Run control algorithms (Python's default: pass-by-assignment)
    stab_result = stabilize_flight_by_assignment(imu_assign, baro_assign, controller_assign)
    batt_status = battery_monitor_by_assignment(battery_assign)
    
    cycle_end = time.ticks_ms()
    cycle_time = cycle_end - cycle_start
    total_time_assign += cycle_time

print(f"Completed 86 cycles")
print(f"Total time: {total_time_assign} ms")
print("Memory after test:")
mp.mem_info()

# ========== COMPARISON ANALYSIS ==========
print("\n\n" + "#" * 90)
print("#\n# PERFORMANCE COMPARISON\n#")
print("#" * 90)

print(f"""
EXECUTION TIME:
  Pass-by-Value:           {total_time_val} ms
  Pass-by-Reference:       {total_time_ref} ms
  Pass-by-Assignment:      {total_time_assign} ms (Python Default)
  
COMPARISON:
  Value vs Reference:      {total_time_val - total_time_ref} ms
  Value vs Assignment:     {total_time_val - total_time_assign} ms
  Reference vs Assignment: {total_time_ref - total_time_assign} ms
""")
