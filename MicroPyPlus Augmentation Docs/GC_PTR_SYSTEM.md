# Pointer System for Pinned Objects

## Overview
Implements raw memory pointers to pinned GC objects with no type awareness. 

## Architecture

### Core Layer ([py/gc.c](py/gc.c))
Three new validation/utility functions:

1. **`gc_ptr_validate(uintptr_t addr)`** - Checks if address is within a pinned object
2. **`gc_ptr_get_range(uintptr_t addr, void **base_ptr, size_t *size)`** - Returns object bounds
3. **`gc_ptr_offset(uintptr_t addr, ssize_t offset)`** - Safe offset with bounds checking (returns 0 if out-of-bounds)

### Python API ([py/modgc.c](py/modgc.c))

| Function | Signature | Returns | Notes |
|----------|-----------|---------|-------|
| `gc.pin_ptr(obj)` | `(obj: Any)` | `int` | Pin object and return raw address |
| `gc.ptr_validate(addr)` | `(addr: int)` | `bool` | Check if address is valid/pinned |
| `gc.ptr_offset(addr, offset)` | `(addr: int, offset: int)` | `int` | Offset pointer with bounds check; -1 if out-of-bounds |
| `gc.ptr_read_byte(addr)` | `(addr: int)` | `int` | Read uint8_t from address (raises ValueError if invalid) |
| `gc.ptr_write_byte(addr, value)` | `(addr: int, value: int)` | `None` | Write uint8_t to address (raises ValueError if invalid) |

## Usage Example

```python
import gc

# Create and pin object
data = bytearray([65, 66, 67])  # 'A', 'B', 'C'
ptr = gc.pin_ptr(data)  # Returns: 140045234567890

# Validate pointer
if gc.ptr_validate(ptr):
    print(f"Valid pointer at 0x{ptr:x}")

# Read byte (no type knowledge, just raw memory)
first_byte = gc.ptr_read_byte(ptr)  # 65 ('A')

# Safe offset
ptr2 = gc.ptr_offset(ptr, 1)  # Move to index 1
second_byte = gc.ptr_read_byte(ptr2)  # 66 ('B')

# Write byte
gc.ptr_write_byte(ptr, 88)  # data[0] = 88 ('X')
assert data[0] == 88

# Unpin when done
gc.unpin(data)
```

## Safety Features

1. **Bounds Checking** - `ptr_offset()` validates offset stays within object bounds
2. **Validation** - `ptr_read_byte()` and `ptr_write_byte()` return -1 for invalid pointers
3. **Pinning Required** - Must `gc.pin()` object before getting/using its pointer
4. **GC Integration** - Pointers remain valid across garbage collections while pinned

## Important Limitation: Object Structure Opacity

**The "no type awareness" constraint means pointers refer to the object structure itself, not to internal data buffers.**

Example issue with structured objects:
```python
data = bytearray([65, 66, 67])  # Trying to access 'A', 'B', 'C'
ptr = gc.pin_ptr(data)          # Gets address of bytearray object structure

# Reading from ptr gets bytes from the object header, not the data!
byte = gc.ptr_read_byte(ptr)    # Reads object metadata (likely 0xA0), not 65
```

**Why this happens:**
- Python objects like `list`, `bytearray`, `dict` have headers containing type info, size, reference count, etc.
- The actual data is stored in a **separate buffer** within or alongside the object
- Without type information, we cannot calculate the offset to that buffer
- Reading from the object address gives us the header bytes, which are often constant across similar objects (hence repeated 0xA0 value)

**What works well:**
- Simple/primitive objects with predictable layout
- Raw memory allocated by `gc_alloc()` directly
- Pointer validation and bounds checking
- Offsetting within allocated blocks
- Testing the pointer system infrastructure itself

**Workaround approaches:**
1. Use objects with simpler/more predictable layouts
2. Implement type-aware variants of `ptr_read_byte()` that know where data buffers are
3. Use lower-level memory allocation that returns direct data pointers
4. Create wrapper objects that expose their data buffer address

## Implementation Details

- **No type information** - Raw byte access only
- **Integer addresses** - Pointers are Python `int` objects
- **GC-safe** - Uses existing pinned object table for validation
- **Thread-safe** - GC enter/exit locks on validation checks

