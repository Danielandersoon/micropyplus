# Pointer Reference Module (`ref`)

## Overview

The `ref` module provides C++-like pointer reference functionality for MicroPython's pinned objects. It wraps raw memory addresses with a convenient object-oriented interface, making pointers more usable and readable.

## Core Concept

Like C++, you can now:
- **Create a pointer** from a pinned object (`gc.pin_ptr()`)
- **Dereference** to get the value (`ptr.get()` == `*ptr` in C++)
- **Set through a pointer** (`ptr.set(value)` == `*ptr = value` in C++)
- **Perform pointer arithmetic** (`ptr.offset(n)` == `ptr + n` in C++)

## The Pointer Class

### Creating a Pointer

```python
import gc
import ref

# Create and pin an object
data = bytearray([10, 20, 30])
raw_addr = gc.pin_ptr(data)

# Wrap the address in a Pointer object for C++-like usage
ptr = ref.Pointer(raw_addr)
```

### Properties

**`address`** - Get the raw memory address
```python
addr = ptr.address  # Returns int address
```

**`size`** - Get the size of the referenced object in bytes
```python
size = ptr.size  # Returns size of pinned object
```

### Methods - Dereferencing

**`get()`** - Read value at pointer (equivalent to `*ptr` in C++)
```python
value = ptr.get()  # Returns uint8_t at address
```

**`set(value)`** - Write value at pointer (equivalent to `*ptr = value` in C++)
```python
ptr.set(42)  # Sets uint8_t at address to 42
```

**`deref()`** - Alias for `get()` with clearer semantics
```python
value = ptr.deref()  # Same as ptr.get()
```

### Methods - Pointer Arithmetic

**`offset(n)`** - Offset pointer by n bytes (equivalent to `ptr + n` in C++)
```python
ptr2 = ptr.offset(1)   # Points to next byte
ptr3 = ptr.offset(-1)  # Points to previous byte
```

Returns a new `Pointer` object. Raises `ValueError` if offset is out of bounds.

### Methods - Byte-level Operations

**`read_byte(offset=0)`** - Read byte at pointer + offset
```python
byte0 = ptr.read_byte()     # Read byte at pointer
byte1 = ptr.read_byte(1)    # Read byte at pointer + 1
byte_neg = ptr.read_byte(-1) # Read byte at pointer - 1
```

**`write_byte(value, offset=0)`** - Write byte at pointer + offset
```python
ptr.write_byte(65)        # Write 'A' at pointer
ptr.write_byte(66, 1)     # Write 'B' at pointer + 1
```

### Methods - Validation

**`is_valid()`** - Check if pointer is still valid/pinned
```python
if ptr.is_valid():
    value = ptr.get()
else:
    print("Pointer has been unpinned!")
```

## Module-level Functions

### `deref(address)` - Quick dereference
```python
import ref

raw_addr = gc.pin_ptr(data)
value = ref.deref(raw_addr)  # Quick dereference without Pointer object
```

### `set_deref(address, value)` - Quick dereference and set
```python
ref.set_deref(raw_addr, 99)  # Quickly set value at address
```

## Complete Example: C++ vs MicroPython

### C++ Version
```cpp
#include <iostream>
using namespace std;

int main() {
    int x = 10;
    int* ptr = &x;      // Create pointer
    
    cout << *ptr << endl;  // Print 10 (dereference)
    *ptr = 20;             // Set through pointer
    cout << x << endl;     // Print 20
    
    int* ptr2 = ptr + 1;   // Pointer arithmetic
    return 0;
}
```

### MicroPython Equivalent
```python
import gc
import ref

# Create and pin object
x = bytearray([10])  # Start with 10
x_ptr = ref.Pointer(gc.pin_ptr(x))

print(x_ptr.get())       # Print 10 (dereference)
x_ptr.set(20)            # Set through pointer
print(x[0])              # Print 20

# Pointer arithmetic
ptr2 = x_ptr.offset(1)   # ptr + 1

# Clean up
gc.unpin(x)
```

## Practical Examples

### Example 1: Array iteration with pointers
```python
import gc
import ref

# Create array
arr = bytearray([65, 66, 67, 68, 69])  # 'A', 'B', 'C', 'D', 'E'
ptr = ref.Pointer(gc.pin_ptr(arr))

# Iterate through array using pointer arithmetic
for i in range(len(arr)):
    byte = ptr.read_byte(i)
    print(f"[{i}] = {byte} ('{chr(byte)}')")

gc.unpin(arr)
```

### Example 2: Swap values through pointers
```python
import gc
import ref

def swap(ptr1, ptr2):
    """Swap values at two pointer addresses"""
    temp = ptr1.get()
    ptr1.set(ptr2.get())
    ptr2.set(temp)

# Create data
arr = bytearray([10, 20, 30])
p1 = ref.Pointer(gc.pin_ptr(arr))

# Swap array[0] and array[1]
swap(p1, p1.offset(1))
print(arr)  # [20, 10, 30]

gc.unpin(arr)
```

### Example 3: Pointer to pointer (double pointer)
```python
import gc
import ref

# Create object and pointer to it
obj = bytearray([42])
ptr = gc.pin_ptr(obj)

# Create a "pointer to pointer" (store the address of the pointer)
ptr_to_ptr = ref.Pointer(ptr)  # This stores the pointer's address

# The value of ptr_to_ptr is the address of obj
print(f"ptr = {ptr}")
print(f"*ptr_to_ptr = {ptr_to_ptr.get()}")  # Same as ptr

gc.unpin(obj)
```

### Example 4: Function takes pointer parameter
```python
import gc
import ref

def modify_by_pointer(ptr):
    """Function that takes a pointer parameter (pass by pointer)"""
    if not ptr.is_valid():
        raise ValueError("Invalid pointer")
    
    current = ptr.get()
    ptr.set(current * 2)  # Double the value

# Create pinned object
value = bytearray([5])
ptr = ref.Pointer(gc.pin_ptr(value))

modify_by_pointer(ptr)
print(value[0])  # 10

gc.unpin(value)
```

## Safety Features

### Automatic Bounds Checking
```python
ptr = ref.Pointer(addr)

# These raise ValueError if OOB
try:
    ptr.offset(1000000)  # OOB
except ValueError as e:
    print(f"Safe bounds check: {e}")
```

### Validation Required
```python
ptr = ref.Pointer(addr)

# Must be pinned to use
if ptr.is_valid():
    value = ptr.get()
else:
    print("Object was unpinned, pointer no longer valid")

# Unpinning invalidates all pointers to that object
gc.unpin(obj)
print(ptr.is_valid())  # False
```

## Important Limitations

### 1. Byte-level Access Only
The current implementation operates at the byte level:
- `ptr.get()` returns a uint8_t (0-255)
- `ptr.set(value)` expects a uint8_t
- For multi-byte values, use `read_byte()` and `write_byte()` with offsets

### 2. Object Structure Opacity
Pointers refer to the Python object structure, not always the data buffer:
```python
# This works (simple byte array):
arr = bytearray([65, 66, 67])
ptr = ref.Pointer(gc.pin_ptr(arr))
print(ptr.get())  # Gets first byte (might be object header)
```

### 3. Must Keep Object Pinned
While using a pointer, the object must remain pinned:
```python
arr = bytearray([10, 20])
ptr = ref.Pointer(gc.pin_ptr(arr))

gc.unpin(arr)  # Now ptr is invalid!
ptr.get()      # ValueError: Address not pinned
```

## Integration with gc Module

The `ref` module works alongside `gc` module:

```python
import gc
import ref

# Step 1: Create and pin object
obj = bytearray([10, 20, 30])
raw_addr = gc.pin_ptr(obj)

# Step 2: Wrap in Pointer for C++-like usage
ptr = ref.Pointer(raw_addr)

# Step 3: Use pointer operations
print(ptr.get())     # Dereference
ptr.set(100)         # Assign

# Step 4: Cleanup
gc.unpin(obj)
```

## Type Support

Currently works with any pinned object, but is most useful with:
- `bytearray` - Direct byte access
- `bytes` - Read-only byte access (will error on write)
- `memoryview` - Byte-level memory view
-  Other objects - Limited by object structure opacity (requires knowladge of structure)

## Performance Notes

- **Pointer creation**: Fast (just wraps an address)
- **Dereferencing** (`get()`, `set()`): Single memory access
- **Bounds checking**: O(1) lookup in pinned object table
- **Offset operations**: Fast address arithmetic with validation

## Error Handling

All operations validate pointers and raise appropriate exceptions:

```python
import ref

try:
    ptr = ref.Pointer(invalid_addr)  # ValueError: Address not pinned
except ValueError as e:
    print(f"Pointer error: {e}")

try:
    ptr = ref.Pointer(valid_addr)
    ptr.offset(999999)               # ValueError: Offset out of bounds
except ValueError as e:
    print(f"Bounds error: {e}")

try:
    gc.unpin(obj)
    ptr.get()                        # ValueError: Pointer is no longer valid
except ValueError as e:
    print(f"Validity error: {e}")
```

## Thread Safety

Like the `gc` module, pointer operations are protected by the GC lock when necessary. Multiple threads can safely create and use pointers concurrently.
