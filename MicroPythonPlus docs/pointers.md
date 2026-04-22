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

### Pointer Subscripts (`&array[index]`)

You can create pointers to array elements using subscript notation:

```python
arr = [10, 20, 30, 40, 50]
ptr_elem = &arr[2]  # Pointer to element at index 2
print((*ptr_elem))    # Prints: 30
```

This is especially useful for FFI (Foreign Function Interface) when passing array elements to C functions:

```python
# Pass pointer to specific array element to C function
data = [100, 200, 300]
ptr_to_first = &data[0]
result = c_function(ptr_to_first)  # C function receives address
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

### Example 5: Array Element Pointers

```python
arr = [100, 200, 300, 400, 500]

# Get pointer to element at index 3
ptr = &arr[3]
print(f"Element at arr[3]: {*ptr}")  # 400

# Modify through pointer
*ptr = 999
print(f"Updated arr[3]: {arr[3]}")   # 999
```

### Example 6: Pointer Caching for Efficiency

```python
# FFI Use Case: Setup once, use many times
data = [1, 2, 3, 4, 5]

# Calculate pointer offset once (setup phase)
ptr_elem_3 = &data[3]  # Cache this pointer

# Use the cached pointer multiple times (fast)
for _ in range(1000):
    value = *ptr_elem_3  # Very fast - no recalculation
    process(value)
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

## Pointer Arithmetic 

MicroPyPlus supports full pointer arithmetic with proper scaling. When you add an integer to a pointer, it's automatically scaled by the element size:

```python
arr = [10, 20, 30, 40, 50]
ptr = &arr[0]

# Pointer arithmetic (automatically scaled by element size)
first = *ptr                 # arr[0] = 10
second = *(ptr + 1)          # arr[1] = 20
third = *(ptr + 2)           # arr[2] = 30
```

**How scaling works:** When you perform `ptr + 1`, the system computes `ptr + 1 * sizeof(mp_obj_t*)`, not `ptr + 1 byte`. This ensures proper array traversal without manual scaling.

### Recommended Pattern: Cache for Performance

For accessing the same offset repeatedly, cache the pointer calculation:

```python
# Setup: Calculate offset once
base_ptr = &array[0]
ptr_elem_10 = base_ptr + 10  # Offset calculated once

# Hot loop: Reuse cached pointer (very fast)
for _ in range(100000):
    value = *ptr_elem_10     # ~0.29µs per dereference
    process(value)
```

**Why this matters:**
- Calculating offset each time: ~0.57µs (overhead)
- Cached pointer dereference: ~0.29µs (half the cost!)
- For 100,000 iterations: ~2.8ms saved

### Subscript Addressing

Create pointers directly to array elements:

```python
arr = [10, 20, 30, 40, 50]

# Direct subscript addressing (no manual arithmetic)
ptr_0 = &arr[0]
ptr_2 = &arr[2]  # More readable than &arr[0] + 2

print(*ptr_0)    # 10
print(*ptr_2)    # 30
```

This is cleaner and lets the compiler optimize directly.
```

### When to Use Pointer Arithmetic

- **FFI Setup:** Calculate offset once, reuse pointer
- **C Interop:** Pass adjusted pointer address to C functions
- **Memory Regions:** Traverse structured binary data

**Avoid** for:
**Avoid** for:
- Repeated array iteration (use `arr[i]` instead)
- Variable indices in loops (interpreter overhead)

## Performance Characteristics

### Pointer Operations Performance

Benchmark results on 64-bit system (10,000 iterations):

| Operation | Time/Op | vs arr[0] |
|-----------|---------|----------|
| arr[0] (baseline) | 0.334 µs | 1.00x |
| *ptr (simple) | 0.293 µs | 0.88x  Faster |
| arr[3] (direct) | 0.353 µs | 1.06x |
| *(ptr+3) uncached | 0.571 µs | 1.71x (arithmetic cost) |
| *cached_ptr | 0.269 µs | 0.81x  Best |

### Key Insights

1. **Simple dereference is fast** - faster than direct indexing
2. **Arithmetic adds overhead** - Recalculating `ptr + offset` is expensive
3. **Caching is crucial** - Compute offset once, reuse that pointer
### Performance Tips

```python
# Good: Cache pointer before loop
ptr_offset = &data[10]
for i in range(100000):
    val = *ptr_offset

# Avoid: Recalculate in loop
base_ptr = &data[0]
for i in range(100000):
    val = *(base_ptr + 10)  # Slower - recalculates each iteration

# Good: For FFI, one-time setup
array = [1, 2, 3, 4, 5]
ptr = &array[0]  # Setup phase
c_function(ptr)  # Fast - handled in C
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



