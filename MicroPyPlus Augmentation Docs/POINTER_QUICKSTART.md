# Quick Start: Pointer Reference Module

## What is it?

The `ref` module makes pointers work like they do in C++:
- `ptr.get()` = `*ptr` (read value)
- `ptr.set(x)` = `*ptr = x` (write value)
- `ptr.offset(n)` = `ptr + n` (pointer arithmetic)

## Installation

### Integration Points

The `ref` module builds on top of the existing `gc` module:
- Uses `gc.pin_ptr()` to create pinned object pointers
- Uses `gc_ptr_validate()` for pointer validation
- Uses `gc_ptr_get_range()` to determine object bounds
- Uses `gc_ptr_offset()` for safe pointer arithmetic
- Uses `gc.unpin()` to release pins

1. Add `py/ref.c` and `py/ref.h` to your MicroPython build
2. Rebuild MicroPython
3. Import with `import ref`

## Basic Usage

```python
import gc
import ref

# Step 1: Create and pin a Python object
data = bytearray([65, 66, 67])  # 'A', 'B', 'C'
address = gc.pin_ptr(data)      # Pin it to prevent garbage collection

# Step 2: Wrap address in a Pointer object (like C++ pointers)
ptr = ref.Pointer(address)

# Step 3: Use C++-like pointer operations
print(ptr.get())       # Read: 65 (like *ptr in C++)
ptr.set(88)            # Write: change to 88 (like *ptr = 88)
print(data)            # [88, 66, 67]

# Step 4: Pointer arithmetic
ptr2 = ptr.offset(1)   # Point to next byte (like ptr + 1)
print(ptr2.get())      # 66

# Step 5: Clean up
gc.unpin(data)
```

## Common Patterns

### Reading from a Pointer
```python
value = ptr.get()              # Read single byte
byte_at_offset = ptr.read_byte(5)  # Read byte at offset
```

### Writing to a Pointer
```python
ptr.set(42)                    # Write single byte
ptr.write_byte(100, 5)         # Write byte at offset
```

### Pointer Arithmetic
```python
ptr2 = ptr.offset(1)           # Point to next byte
ptr3 = ptr.offset(-1)          # Point to previous byte
ptr_far = ptr.offset(100)      # Jump 100 bytes forward
```

### Checking if Pointer is Valid
```python
if ptr.is_valid():
    value = ptr.get()
else:
    print("Pointer was unpinned!")
```

### Quick Operations (without Pointer object)
```python
# If you need raw operations without the Pointer wrapper
addr = gc.pin_ptr(obj)
value = ref.deref(addr)           # Quick read
ref.set_deref(addr, 99)           # Quick write
gc.unpin(obj)
```

## Examples

### Example 1: Iterate Array with Pointers
```python
import gc
import ref

arr = bytearray([10, 20, 30, 40])
ptr = ref.Pointer(gc.pin_ptr(arr))

for i in range(len(arr)):
    print(ptr.read_byte(i))

gc.unpin(arr)
```

### Example 2: Swap Values
```python
def swap(ptr1, ptr2):
    temp = ptr1.get()
    ptr1.set(ptr2.get())
    ptr2.set(temp)

arr = bytearray([10, 20])
ptr = ref.Pointer(gc.pin_ptr(arr))
swap(ptr, ptr.offset(1))
print(arr)  # [20, 10]
gc.unpin(arr)
```

### Example 3: Pass Pointer to Function
```python
def modify(ptr):
    ptr.set(ptr.get() * 2)

value = bytearray([5])
ptr = ref.Pointer(gc.pin_ptr(value))
modify(ptr)
print(value[0])  # 10
gc.unpin(value)
```

### Example 4: Bounds-Safe Offsets
```python
ptr = ref.Pointer(gc.pin_ptr(small_array))

try:
    invalid = ptr.offset(1000000)  # OOB
except ValueError:
    print("Safe! OOB prevented")

gc.unpin(small_array)
```

## Pointer Object Properties

```python
ptr = ref.Pointer(address)

ptr.address          # Raw memory address (int)
ptr.size             # Size of object in bytes

ptr.get()            # Read byte at pointer (returns decimal value, may need conversion)
ptr.set(value)       # Write byte at pointer
ptr.deref()          # Alias for get()

ptr.offset(n)        # Pointer arithmetic, returns new Pointer
ptr.is_valid()       # Check if still pinned

ptr.read_byte(off)   # Read byte with optional offset
ptr.write_byte(v, off)  # Write byte with optional offset
```

## Error Handling

```python
try:
    # Creates Pointer (fails if address not pinned)
    ptr = ref.Pointer(bad_address)
except ValueError as e:
    print(f"Bad address: {e}")

try:
    # Offset fails if OOB
    ptr2 = ptr.offset(999999)
except ValueError as e:
    print(f"OOB: {e}")

try:
    # Dereference fails if pointer invalid
    gc.unpin(obj)
    ptr.get()
except ValueError as e:
    print(f"Pointer no longer valid: {e}")
```

## Key Differences: C++ vs Python

| Concept | C++ | Python (`ref` module) |
|---------|-----|----------------------|
| Declare | `int* p = &x;` | `p = ref.Pointer(gc.pin_ptr(x))` |
| Read | `*p` | `p.get()` |
| Write | `*p = 42;` | `p.set(42)` |
| Arithmetic | `p + 1` | `p.offset(1)` |
| Valid? | No runtime check | `p.is_valid()` |
| Safety | Unsafe (crashes possible) | Safe (bounds checked) |

## Rules to Remember

1. **Must pin objects**: Always use `gc.pin_ptr()` before creating a `Pointer`
2. **Keep pinned while using**: Don't unpin while the Pointer is active
3. **Byte-level operations**: `get()` and `set()` work with single bytes (0-255)
4. **Bounds checking**: `offset()` checks bounds and raises errors
5. **Validation**: Always check `is_valid()` after unpinning

## When to Use Pointers

**Use when:**
- You need low-level memory manipulation
- Interfacing with C extensions
- Implementing pointer-based algorithms
- You want safer pointer semantics than raw addresses

**Don't use for:**
- Simple data access (use normal Python indexing)
- Object-oriented operations (use Python objects directly)
- Frequent allocations (pointers have overhead)

## Performance Note

Pointer operations are fast (O(1)), but don't overuse them:
- Creating a Pointer object: ~24 bytes overhead
- Reading/writing: Single memory access (very fast)
- Validation: O(1) table lookup

## Complete Example

```python
import gc
import ref

# Create test data
print("Creating bytearray: [65, 66, 67]")
data = bytearray([65, 66, 67])

# Pin and wrap in Pointer
print("Creating pointer...")
ptr = ref.Pointer(gc.pin_ptr(data))

# Basic operations
print(f"ptr.get() = {ptr.get()} (character: '{chr(ptr.get())}')")

ptr.set(88)
print(f"After ptr.set(88): {data}")

# Pointer arithmetic
ptr2 = ptr.offset(1)
print(f"ptr.offset(1).get() = {ptr2.get()}")

# Bounds checking
print(f"Object size: {ptr.size} bytes")
try:
    ptr.offset(ptr.size)  # This works (at boundary)
    ptr.offset(ptr.size + 1)  # This fails (OOB)
except ValueError as e:
    print(f"Caught: {e}")

# Cleanup
gc.unpin(data)
print("Done!")
```