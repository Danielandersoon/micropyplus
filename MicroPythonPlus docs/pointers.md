# Pointer Reference System

## Overview

The pointer reference system in MicroPyPlus allows you to work with references to objects, enabling explicit **pass-by-reference** semantics. This is especially useful on memory-constrained embedded systems where avoiding object copies can be critical for performance and memory usage.

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

### Example 2: Output Parameters

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

### Example 3: Modifying Object Fields

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

### Example 4: Multiple Pointer Operations

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
print((*ptr))  # Valid - object persists
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
print((*ptr))  # Undefined behavior!

# RIGHT
temp = [1, 2, 3]  # Lives in outer scope
ptr = &temp
print((*ptr))  # Safe
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



