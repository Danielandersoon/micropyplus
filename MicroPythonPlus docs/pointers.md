# Pointer Reference System

## Overview

The pointer reference system in MicroPython Plus allows you to work with references to objects, enabling **pass-by-reference** semantics. This is especially useful on memory-constrained embedded systems where avoiding object copies can be critical for performance and memory usage.

Pointers are memory addresses stored as values, allowing you to:
- Modify objects in-place without creating copies
- Use output parameters to return multiple values
- Write more memory-efficient code for large data structures

## Basic Syntax

### Address-of Operator (`&`)

The `&` operator creates a pointer to a variable or object:

```python
x = 42
ptr_x = &x  # ptr_x now points to x
```

### Dereference Operator (`*`)

The `*` operator accesses the value that a pointer refers to:

```python
x = 42
ptr_x = &x
value = *ptr_x  # value is now 42
```

### Pointer Assignment (`*`)

You can assign new values through a pointer using the dereference operator:

```python
x = 42
ptr_x = &x
*ptr_x = 100  # x is now 100
```

## Practical Examples

### Example 1: Basic Pointer Usage

```python
# Create a variable and a pointer to it
temperature = 25.0
ptr_temp = &temperature

# Read through the pointer
current = *ptr_temp
print(f"Current temperature: {current}°C")

# Modify through the pointer
*ptr_temp = 30.0
print(f"Updated temperature: {temperature}°C")  # 30.0
```

### Example 2: Pass-by-Reference in Functions

**Without pointers (pass-by-value):**
```python
class Sensor:
    def __init__(self):
        self.reading = 0.0

def update_sensor_copy(sensor):
    """This creates a copy, original is unchanged"""
    sensor.reading = 100.0
    # Original sensor is NOT modified

sensor = Sensor()
update_sensor_copy(sensor)
print(sensor.reading)  # Still 0.0 - not modified!
```

**With pointers (pass-by-reference):**
```python
class Sensor:
    def __init__(self):
        self.reading = 0.0

def update_sensor_ref(sensor_ptr):
    """Direct reference to original object"""
    s = *sensor_ptr
    s.reading = 100.0
    # Original sensor IS modified

sensor = Sensor()
update_sensor_ref(&sensor)
print(sensor.reading)  # 100.0 - modified!
```

### Example 3: Output Parameters

Functions can modify multiple values using pointers as output parameters:

```python
class DataBuffer:
    def __init__(self):
        self.sum = 0
        self.count = 0

def accumulate(buffer_ptr, values):
    """Accumulate results into buffer through pointer"""
    b = *buffer_ptr
    for value in values:
        b.sum += value
        b.count += 1

buffer = DataBuffer()
accumulate(&buffer, [10, 20, 30])
print(f"Sum: {buffer.sum}, Count: {buffer.count}")  # Sum: 60, Count: 3
```

### Example 4: Modifying Object Fields

```python
class Point:
    def __init__(self, x=0, y=0):
        self.x = x
        self.y = y

point = Point(10, 20)
ptr = &point

# Modify through pointer
p = *ptr
p.x = 50
p.y = 60

print(f"Point: ({point.x}, {point.y})")  # (50, 60)
```

### Example 5: Multiple Pointer Operations

```python
# Swap values using pointers
val1 = 10
val2 = 20

ptr1 = &val1
ptr2 = &val2

# Swap through pointers
temp = *ptr1
*ptr1 = *ptr2
*ptr2 = temp

print(f"val1: {val1}, val2: {val2}")  # val1: 20, val2: 10
```

## Use Cases

### 1. Memory-Efficient Large Data Structures

On embedded systems with limited RAM, copying large objects is expensive:

```python
class SensorArray:
    def __init__(self, size=1000):
        self.readings = [0.0] * size
        self.timestamps = [0] * size

def process_array_with_pointer(array_ptr):
    """No copy - direct access to original"""
    arr = *array_ptr
    for i in range(len(arr.readings)):
        arr.readings[i] *= 1.1  # Scale readings by 10%

sensors = SensorArray(1000)
process_array_with_pointer(&sensors)
# sensors.readings is now updated in-place
```

### 2. Real-Time Control Systems

Perfect for drone/robot controllers where efficiency matters:

```python
class MotorController:
    def __init__(self):
        self.speed = 0
        self.direction = 0
        self.enabled = False

def update_motor(motor_ptr, new_speed, new_direction):
    """Update motor parameters directly"""
    m = *motor_ptr
    m.speed = new_speed
    m.direction = new_direction
    m.enabled = True

motor = MotorController()
update_motor(&motor, 255, 90)
print(f"Motor speed: {motor.speed}")  # 255
```

### 3. Sensor Fusion Algorithms

Efficiently combine data from multiple sensors:

```python
class IMU:
    def __init__(self):
        self.accel = [0, 0, 0]
        self.gyro = [0, 0, 0]
        self.timestamp = 0

def fuse_sensors(imu_ptr, accel_data, gyro_data):
    """Update IMU state through pointer"""
    imu = *imu_ptr
    imu.accel = accel_data
    imu.gyro = gyro_data
    imu.timestamp = time.ticks_ms()

imu = IMU()
fuse_sensors(&imu, [1.0, 2.0, 3.0], [0.1, 0.2, 0.3])
```

### 4. Callback and Event Handlers

Update shared state from callbacks:

```python
class SystemState:
    def __init__(self):
        self.temperature = 0
        self.alarm = False

state = SystemState()

def on_temperature_alert(state_ptr, temp):
    """Handle temperature alert"""
    s = *state_ptr
    s.temperature = temp
    if temp > 80:
        s.alarm = True

# In interrupt handler...
on_temperature_alert(&state, 85)
```

## Memory Efficiency

### Comparison: Pointer vs Copy

**Without pointers (creates copies):**
```python
def process_large_array(array):
    # Memory: Original + Copy = 2x memory usage
    for i in range(len(array)):
        array[i] += 1
    return array  # Copy is returned

big_array = [i for i in range(5000)]
process_large_array(big_array)  # Creates temporary copy
```

**With pointers (direct access):**
```python
def process_large_array(array_ptr):
    # Memory: Original only = 1x memory usage
    arr = *array_ptr
    for i in range(len(arr)):
        arr[i] += 1

big_array = [i for i in range(5000)]
process_large_array(&big_array)  # No copy created
```

### Memory Impact on Embedded Systems

On an ESP32 with 320 KB RAM:
- **Pass-by-value**: Creating a 5000-element array copy uses ~40 KB extra
- **Pass-by-reference**: No extra memory for the structure
- **Difference**: Critical when memory is limited

## Important Notes

### Mutable vs Immutable Data

**Mutable objects** (lists, custom classes) can be modified through pointers:
```python
class Container:
    def __init__(self):
        self.items = [1, 2, 3]

c = Container()
ptr = &c
obj = *ptr
obj.items[0] = 999  # This works - list is mutable
print(c.items[0])   # 999
```

**Immutable objects** (tuples, strings, numbers) cannot be mutated in-place, but the data they're inside can be reassigned:
```python
class Data:
    def __init__(self):
        self.version = (1, 0, 0)  # Immutable tuple

d = Data()
ptr = &d
obj = *ptr
# obj.version[0] = 2  # ERROR - cannot modify tuple
obj.version = (2, 0, 0)  # This works - reassign the field
print(d.version)  # (2, 0, 0)
```

### Lifetime Considerations

A pointer remains valid as long as the object it refers to exists:

```python
def get_pointer():
    x = 42
    return &x  # WARNING: Dangerous! x is only valid in function scope

# The returned pointer is invalid after function returns

def safe_pointer():
    # Safe: object persists after function returns
    global_obj = {"value": 42}
    return &global_obj

ptr = safe_pointer()
print(*ptr)  # Valid - object persists
```

## Best Practices

### 1. Use Pointers for Large Objects

```python
# Good: Large structure passed by reference
def process_image(image_ptr, filter_type):
    img = *image_ptr
    apply_filter(img, filter_type)

# Avoid: Large structure passed by value
def process_image_bad(image, filter_type):
    # Creates unnecessary copy
    apply_filter(image, filter_type)
```

### 2. Document Pointer Parameters

```python
def modify_state(state_ptr):
    """
    Modify system state in-place.
    
    Args:
        state_ptr: Pointer to SystemState object (modified in-place)
    """
    s = *state_ptr
    s.counter += 1
```

### 3. Use Clear Variable Names

```python
sensor_data = read_sensors()
ptr_data = &sensor_data  # Clear: 'ptr_' prefix indicates pointer

# Dereference for readability
data = *ptr_data
data.calibrate()
```

### 4. Combine with Type Hints

```python
def update_config(config_ptr: 'ptr(Config)') -> None:
    """Update configuration object through pointer."""
    cfg = *config_ptr
    cfg.value = 100
```

## Common Patterns

### Pattern 1: Pointer In, Void Out (State Modification)

```python
def setup_pins(config_ptr):
    """Configure pins - modifies config in-place"""
    cfg = *config_ptr
    cfg.pin_count = 10
    cfg.ready = True

config = {"pin_count": 0, "ready": False}
setup_pins(&config)
```

### Pattern 2: Pointer Arithmetic (Object Chains)

```python
class Node:
    def __init__(self, value):
        self.value = value
        self.next = None

def add_to_chain(chain_ptr, value):
    """Add node to linked list through pointer"""
    node = *chain_ptr
    if node.next is None:
        node.next = Node(value)
    else:
        add_to_chain(&node.next, value)
```

### Pattern 3: Pointer Out (Return Multiple Values)

```python
def compute_stats(data, result_ptr):
    """Compute and return stats through pointer"""
    result = {
        "min": min(data),
        "max": max(data),
        "avg": sum(data) / len(data)
    }
    *result_ptr = result

stats = {}
compute_stats([1, 2, 3, 4, 5], &stats)
print(stats["avg"])  # 3.0
```

## Troubleshooting

### Issue: "Object modified but changes don't appear"

**Problem:** Forgetting to dereference before assignment

```python
# WRONG
config = {"value": 0}
ptr = &config
*ptr = {"value": 100}  # This overwrites the entire object

# RIGHT
cfg = *ptr
cfg["value"] = 100  # Modify field, not entire object
```

### Issue: "Pointer points to stale data"

**Problem:** Pointer outlives the referenced object

```python
# WRONG
def create_pointer():
    temp = [1, 2, 3]
    return &temp  # temp is destroyed after function returns

ptr = create_pointer()
print(*ptr)  # Undefined behavior!

# RIGHT
temp = [1, 2, 3]  # Lives in outer scope
ptr = &temp
print(*ptr)  # Safe
```

### Issue: "Type errors with pointer operations"

**Problem:** Mixing pointer and non-pointer code

```python
# WRONG
data = [1, 2, 3]
ptr = &data
for x in ptr:  # ptr is not iterable!
    print(x)

# RIGHT
data = [1, 2, 3]
ptr = &data
arr = *ptr
for x in arr:
    print(x)
```

## Advanced: Pointers and Memory Management

### Pointers Work with Garbage Collection

MicroPython's garbage collector automatically tracks pointers stored in objects:

```python
class Cache:
    def __init__(self):
        self.data = None
        self.ptr = None  # This pointer is tracked by GC

cache = Cache()
obj = {"items": [1, 2, 3]}
cache.ptr = &obj

# If obj is no longer referenced, GC will free it
# cache.ptr will point to freed memory - be careful!
```

### Keep Objects Alive While Using Pointers

```python
class DataManager:
    def __init__(self):
        self.buffer = []
        self.ptr = None
    
    def init_buffer(self):
        self.buffer = [0] * 1000
        self.ptr = &self.buffer  # Safe: buffer is stored in self
```

## Summary

| Feature | Use Case | Benefit |
|---------|----------|---------|
| **Pass-by-Reference** | Modifying objects in functions | Avoids unnecessary copies |
| **Output Parameters** | Returning multiple values | More efficient than tuples |
| **State Modification** | Updating shared state | Direct in-place changes |
| **Memory Efficiency** | Large data structures | Critical for embedded systems |
| **Callback Data** | Event handlers | Efficient parameter passing |

