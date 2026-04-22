# Garbage Collector Changes: Compaction & Allocation

## Overview

The MicroPyPlus garbage collector has been enhanced with two major improvements:

1. **Heap Compaction** - A new compaction phase that defragments the heap by moving objects, reducing fragmentation and enabling better memory utilization.
2. **Bidirectional Allocation** - Objects are allocated from both ends of the heap: regular objects from the left, pinned objects from the right, preventing compaction-related corruption of pointers.

These changes work together to support the pointer system while maintaining GC efficiency on memory-constrained embedded systems.

## Architecture Overview

The enhanced GC pipeline:

```
            Allocation Phase
                    ↓
                Mark Phase 
                    ↓
                Compaction Phase 
(Defragments the heap by moving identified objects)
                    ↓
              Update References 
(Fixes internal and root pointers to new locations)
                    ↓
                Sweep Phase 
        (Finalizes unreachable objects)
                    ↓
                Finalization
```

## Bidirectional Allocation Strategy

### The Problem

In a traditional GC with moving objects:
- Pointers store absolute addresses
- When GC moves an object, all pointers to it become invalid
- Compaction solves this by updating all references
- But pinned objects can't move, creating fragmentation

### The Solution: Left/Right Allocation

```
Heap Layout:
    ┌─────────────────────────────────────┐
    │                                     │
    │  LEFT SIDE        RIGHT SIDE        │
    │  (Moving)         (Pinned)          │
    │                                     │
    │  [Regular]  [Free]  [Pinned]        │
    │   Objects   Space   Objects         │
    │                                     │
    │  ←─────────────  ─────────→         │
    │   Grows right    Grows left         │
    │                                     │
    └─────────────────────────────────────┘
```

**Key insight:** By allocating pinned objects from the right and regular objects from the left, compaction can move left-side objects without affecting right-side pointers.

### Implementation Details

#### Heap Pointers

Three tracking pointers maintain the allocation boundaries:

```c
// From gc.c allocation logic
area->gc_last_used_block_from_left      // Last block allocated from left
area->gc_last_used_block_from_right     // Last block allocated from right
area->gc_num_blocks                      // Total blocks in heap
```

**Invariant:** 
```
gc_last_used_block_from_left <= gc_last_used_block_from_right
```

#### Left-Side Allocation (Regular Objects)

```c
// Traditional allocation from heap start
// Search for free blocks in sequence from left boundary
// Allocates blocks [0 ... gc_last_used_block_from_left]
```

**Priority:**
1. Search from last successful allocation (cache locality)
2. Extend rightward into free space
3. Compact and retry if fragmented

#### Right-Side Allocation (Pinned Objects)

```c
static size_t gc_alloc_right(mp_state_mem_area_t *area, size_t n_blocks) {
    // Start from right boundary
    size_t block = area->gc_last_used_block_from_right;
    
    // Search backward for free blocks
    while (block > last_left_used) {
        if (block is free) {
            // Mark allocated
            area->gc_last_used_block_from_right = new_boundary;
            return block;
        }
        block--;
    }
    
    // No free space - require compaction
    return ALLOC_FAILED;
}
```

**Stack-like allocation pattern:**
- Rightmost blocks allocated first
- Deallocating rightmost block allows reuse immediately
- Prevents left/right gap expansion

### Allocation Flag Integration

The allocator chooses direction based on flags:

```c
void *gc_alloc(size_t n_bytes, unsigned int alloc_flags) {
    // Check if pinning requested
    if (alloc_flags & GC_ALLOC_FLAG_IS_PINNED) {
        // Allocate from right side
        block = gc_alloc_right(area, n_blocks);
    } else {
        // Allocate from left side (traditional)
        block = gc_alloc_left(area, n_blocks);
    }
}
```

**Flags:**
```c
enum {
    GC_ALLOC_FLAG_HAS_FINALISER = 1,    // Object has __del__
    GC_ALLOC_FLAG_IS_PINNED = 2,        // Object is pinned
};
```

## Compaction Phase

### Problem: Fragmentation

As objects are allocated and freed:

```
Initial:  [A][B][C][ free ][ free ]
(B Marked and sweeped)
After:    [A][C][ free ][ free ][ free ]
              
```

**Issues:**
- Large allocations fail even when total free space exists
- Cache locality deteriorates
- Pointer validity becomes uncertain

### Compaction Algorithm

Three-stage process:

#### Stage 1: Compute Forwarding Addresses

Maps old block positions to new positions:

```c
void gc_compute_forwarding_addresses(
    mp_state_mem_area_t *area,
    gc_forward_table_t *forward_table
)
```

**Algorithm:**

```
1. Initialization:
   - compact_ptr = 0 (start of heap)
   - mark_block = 0 (current source block)

2. Iterate through allocation table:
   for each block in heap:
       if block is HEAD (start of allocation):
           // This block will move
           forward_table[block] = compact_ptr
           
           // Find size of this allocation
           block_size = count_consecutive_TAIL_blocks()
           
           // Update destination
           compact_ptr += block_size
           
       mark_block++

3. Result:
   forward_table contains mapping:
   old_block_position → new_block_position
```

**Data structure:**

```c
typedef struct {
    size_t old_block;     // Original location
    size_t new_block;     // Destination location
} gc_forward_entry_t;

typedef struct {
    gc_forward_entry_t *entries;  // Array of mappings
    size_t count;                  // Number of entries
    size_t capacity;               // Allocated capacity
} gc_forward_table_t;
```

**Example:**

```
Before compaction:
   Block:  0    1    2    3    4    5    6    7
   ATB:    HEAD TAIL FREE HEAD TAIL TAIL FREE FREE
   
   Objects:
   - Object A: blocks 0-1 (2 blocks)
   - Object B: blocks 3-5 (3 blocks)
   - Object C: would go in blocks 6-7

After compaction:
   Block:  0    1    2    3    4    5    6    7
   ATB:    HEAD TAIL HEAD TAIL TAIL FREE FREE FREE
   
   Objects:
   - Object A: blocks 0-1 (unchanged)
   - Object B: blocks 2-4 (moved from 3-5)
   - Object C: blocks 5-6 (new allocation point)

Forward table:
   3 → 2
   4 → 3
   5 → 4
```

#### Stage 2: Copy Objects

Move objects to new locations and update allocation table:

```c
void gc_compact_copy(
    mp_state_mem_area_t *area,
    gc_forward_table_t *forward_table
)
```

**Algorithm:**

```
1. For each forward table entry (old_block → new_block):
   
2. Copy object data:
   src = PTR_FROM_BLOCK(area, old_block)
   dst = PTR_FROM_BLOCK(area, new_block)
   size = block_count * BYTES_PER_BLOCK
   memmove(dst, src, size)
   
3. Update allocation table:
   ATB[old_block] = FREE
   ATB[new_block] = HEAD / TAIL (as appropriate)
   
4. Clear finalizer table if needed:
   FTB[old_block] = 0
   Copy FTB bits to new location
```

**Memory layout after copy:**

```
Before:
[Object A][Object A][free][Object B][Object B][Object B][free][free]
  0           1        2      3         4         5        6     7

After memmove:
[Object A][Object A][Object B][Object B][Object B][uninitialized...][free][free]
    0         1          2        3         4         5                6    7
```

**Key points:**
- Uses `memmove()` for safe overlapping copies
- Preserves exact byte layout (no serialization)
- Updates allocation table in real-time
- Clears old locations to prevent double-free

#### Stage 3: Update References

Scan all objects and fix pointers to moved objects:

```c
void gc_update_references(
    mp_state_mem_area_t *area,
    gc_forward_table_t *forward_table
)
```

**Algorithm:**

```
1. For each allocated block:
   
2. Get object pointer:
   obj = (mp_obj_t *)PTR_FROM_BLOCK(area, block)
   
3. Scan object for pointers:
   for each word in object:
       if word is pointer to GC heap:
           old_block = BLOCK_FROM_PTR(area, word)
           
           if old_block in forward_table:
               new_block = forward_table[old_block]
               new_address = PTR_FROM_BLOCK(area, new_block)
               word = new_address
```

**Reference types handled:**

1. **Object pointers**: `mp_obj_t` fields pointing to other GC objects
2. **Word pointers**: Explicit `mp_obj_t*` stored in objects
3. **Closure variables**: References in closure cells
4. **Attribute storage**: References in object dictionaries

**Complexity:**
- Must scan entire reachable graph
- O(n) where n = number of objects
- Linear in lifetime, not quadratic

**Trade-off:**
- Compaction is expensive (O(n) time and memory)
- But prevents out-of-memory situations
- Triggered lazily only when fragmentation is severe

### Pinned Object Handling

**Critical:** Pinned objects must never be moved during compaction.

```c
// From gc.c compaction
if (gc_is_block_pinned(block)) {
    // Skip this block - it's pinned
    // Don't add to forward table
    // Don't mark for compaction
    // Leave allocation table entry unchanged
}
```

**Why this is safe:**

1. Pinned objects allocated on right side
2. Compaction works left-to-right
3. Pinned objects in right zone remain untouched
4. Left-side compaction never affects pinned pointers

**Layout invariant during compaction:**

```
Before:  [Left moving objects][free][Right pinned objects]
               ↓ compaction
After:   [Left compacted][more free][Right pinned objects]
         
         Pinned objects never move!
```

## Pinned Object Tracking

### Data Structures

The system maintains a sorted table of pinned object ranges:

```c
typedef struct {
    void* obj;              // Pointer to pinned object
    size_t block_start;     // First block of object
    size_t block_count;     // Number of blocks object spans
} pinned_range_t;

typedef struct {
    pinned_range_t *ranges; // Array of ranges
    size_t count;           // Number of pinned objects
    size_t capacity;        // Allocated capacity
} pinned_table_t;
```

**Maintained as sorted array** (by block_start) for efficient lookup:

```
Block order:    10    25    50    75    100
Ranges:    [10-15][25-30][50-52][75-80][100-110]
              ^      ^      ^      ^       ^
           Sorted by start block
```

### Pin Operation

```c
void gc_pin(void* ptr) {
    // 1. Find which block this pointer belongs to
    size_t block = BLOCK_FROM_PTR(area, ptr);
    
    // 2. Find all blocks belonging to this object
    size_t first_block = block;
    while (first_block > 0 && 
           ATB_GET_KIND(area, first_block-1) == AT_TAIL) {
        first_block--;
    }
    
    size_t block_count = 1;
    size_t check_block = block + 1;
    while (ATB_GET_KIND(area, check_block) == AT_TAIL) {
        block_count++;
        check_block++;
    }
    
    // 3. Insert into pinned table (maintaining sort order)
    pinned_range_t range;
    range.obj = ptr;
    range.block_start = first_block;
    range.block_count = block_count;
    
    // Insert in sorted position
    insert_sorted(&gc_pinned_table, range);
    
    // 4. Mark blocks as pinned (in GC state)
    for (i = first_block; i < first_block + block_count; i++) {
        mark_block_pinned(i);
    }
}
```

**Time complexity:**
- Lookup blocks of object: O(object_size_in_blocks)
- Insert in table: O(n) where n = pinned objects
- Mark blocks: O(object_size_in_blocks)
- Total: O(n + object_size)

### Unpin Operation

```c
void gc_unpin(void* ptr) {
    // 1. Find pinned range containing this pointer
    ssize_t index = gc_pinned_find_range_by_ptr(ptr);
    
    if (index == -1) {
        return;  // Not pinned
    }
    
    // 2. Unmark blocks
    pinned_range_t *range = &gc_pinned_table.ranges[index];
    for (i = range->block_start; 
         i < range->block_start + range->block_count; i++) {
        unmark_block_pinned(i);
    }
    
    // 3. Remove from table (maintain sort order)
    remove_from_sorted(&gc_pinned_table, index);
}
```

### Checking If Pinned

```c
bool gc_is_pinned(void* ptr) {
    // Linear search (could optimize with binary search)
    ssize_t index = gc_pinned_find_range_by_ptr(ptr);
    return index != -1;
}

bool gc_is_block_pinned(size_t block) {
    // Check if block belongs to any pinned range
    ssize_t index = gc_pinned_find_range_by_block(block);
    return index != -1;
}
```

### Dynamic Capacity Management

The pinned table grows dynamically as needed:

```c
#define PINNED_TABLE_INITIAL_CAPACITY 32

// When inserting beyond capacity:
if (gc_pinned_table.count >= gc_pinned_table.capacity) {
    // Double capacity
    new_capacity = gc_pinned_table.capacity * 2;
    new_ranges = m_realloc(
        gc_pinned_table.ranges,
        gc_pinned_table.capacity * sizeof(pinned_range_t),
        new_capacity * sizeof(pinned_range_t)
    );
    gc_pinned_table.ranges = new_ranges;
    gc_pinned_table.capacity = new_capacity;
}
```

## Reference Map / Hash Table

For tracking object movements during compaction:

```c
typedef struct _gc_ref_entry_t {
    void *key;    // Old pointer
    void *value;  // New pointer
} gc_ref_entry_t;

typedef struct _gc_ref_map_t {
    gc_ref_entry_t *entries;  // Hash table entries
    size_t size;              // Number of slots
    size_t count;             // Number of entries
} gc_ref_map_t;
```

**Functions:**

```c
void gc_ref_map_init(gc_ref_map_t *map, size_t size);
void gc_ref_map_insert(gc_ref_map_t *map, void *old_ptr, void *new_ptr);
void *gc_ref_map_lookup(gc_ref_map_t *map, void *old_ptr);
void gc_ref_map_deinit(gc_ref_map_t *map);
```

**Use case:** When updating references, quickly determine new address of moved object

## GC Sweep Phase Integration

The existing sweep phase already distinguishes blocks:

```c
static void gc_sweep_free_blocks(void) {
    // Scan allocation table
    for each block:
        if block is HEAD and not MARK:
            // Object is unreachable
            
            if gc_is_block_pinned(block):
                // Pin prevents compaction movement
                // Exception: if unpinned, can go back to free
                clear_pinned_flag(block)
            
            // Free this block and tail blocks
            free_block_range(block)
```

**Key interaction:** Freeing a pinned object:
1. Unmark as pinned
2. Mark block as FREE
3. GC can reuse space after compaction

## Impact on VM Pointer Operations

The pointer system works seamlessly with compaction:

### Before Compaction

```python
x = [1, 2, 3]        # Allocated at block 5
ptr = &x             # Stores address of block 5
y = *ptr             # Dereferences block 5
```

### Compaction Occurs

```
GC computes: block 5 is at wrong location
GC moves: object from block 5 to block 2
GC forward table: 5 → 2
GC updates: all references to block 5 → block 2
```

### After Compaction

```python
x = [1, 2, 3]        # Now at block 2
ptr = &x             # Still points to x? NO!
                     # Original address is INVALID
```

**Danger:** If pointer stores old address, it's now dangling!

**Solution:** Pointer must be re-created after compaction.

```python
# Safe approach
x = [1, 2, 3]
ptr = &x             # Block 5
# ... compaction ...
ptr = &x             # Must capture new address (block 2)
y = *ptr             # Uses correct new address
```

### Pinning Prevents This

```python
x = [1, 2, 3]           # Pinned, allocated on right
ptr = &x                # Creates pointer, pins object
# ... compaction ...
                        # Object x NOT moved (pinned)
y = *ptr                # Still valid, points to same block
```

**Benefit:** Pinned objects simplify reference tracking by preventing moves.

## Pointer Arithmetic & Temporary Objects

With stack-based pointer dereference (MP_BC_POINTER_DEREF_STACK), pointer arithmetic creates temporary pointer objects:

```python
base_ptr = &arr[0]      # Permanent pointer, pins arr
offset_ptr = base_ptr + 1  # Temporary pointer object created
val = *offset_ptr       # Dereferenced immediately
```

### Temporary Pointer Optimization

To avoid GC overhead for short-lived arithmetic results, the system uses two allocation paths:

```c
// For user-created pointers (permanent, long-lived)
mp_obj_new_pointer(addr)       // Pins the target object
                               // Used for: ptr = &x

// For arithmetic results (temporary, immediate use)
mp_obj_new_pointer_fast(addr)  // No pinning
                               // Used for: ptr + 1, ptr - 2
```

**Performance impact:**
- `mp_obj_new_pointer()`: Pins object, adds to pinned table (slower)
- `mp_obj_new_pointer_fast()`: Skips pinning, minimal overhead (faster)

### Lifetime Pattern

```
┌─────────────────────────────────────┐
│ Pointer Arithmetic Stack Pattern    │
├─────────────────────────────────────┤
│ 1. base_ptr = &arr[0]               │
│    (permanent, pinned)              │
│                                     │
│ 2. offset = base_ptr + 5            │
│    (temporary, unpinned)            │
│    MP_BC_POINTER_DEREF_STACK        │
│    (automatically deref & discard)  │
│                                     │
│ 3. Stack has value, pointer gone    │
│    GC pressure: minimal             │
└─────────────────────────────────────┘
```

**Justification:** Temporary pointers from arithmetic stay on the stack only long enough to be dereferenced immediately, so they don't need pinning the target object.

## Pointer Arithmetic & Temporary Objects

With stack-based pointer dereference (MP_BC_POINTER_DEREF_STACK), pointer arithmetic creates temporary pointer objects:

```python
base_ptr = &arr[0]      # Permanent pointer, pins arr
offset_ptr = base_ptr + 1  # Temporary pointer object created
val = *offset_ptr       # Dereferenced immediately
```

### Temporary Pointer Optimization

To avoid GC overhead for short-lived arithmetic results, the system uses two allocation paths:

```c
// For user-created pointers (permanent, long-lived)
mp_obj_new_pointer(addr)       // Pins the target object
                               // Used for: ptr = &x

// For arithmetic results (temporary, immediate use)
mp_obj_new_pointer_fast(addr)  // No pinning
                               // Used for: ptr + 1, ptr - 2
```

**Performance impact:**
- `mp_obj_new_pointer()`: Pins object, adds to pinned table (slower)
- `mp_obj_new_pointer_fast()`: Skips pinning, minimal overhead (faster)

### Lifetime Pattern

```
┌─────────────────────────────────────┐
│ Pointer Arithmetic Stack Pattern    │
├─────────────────────────────────────┤
│ 1. base_ptr = &arr[0]               │
│    (permanent, pinned)              │
│                                     │
│ 2. offset = base_ptr + 5            │
│    (temporary, unpinned)            │
│    MP_BC_POINTER_DEREF_STACK        │
│    (automatically deref & discard)  │
│                                     │
│ 3. Stack has value, pointer gone    │
│    GC pressure: minimal             │
└─────────────────────────────────────┘
```

**Justification:** Temporary pointers from arithmetic stay on the stack only long enough to be dereferenced immediately, so they don't need pinning the target object.

## Performance Characteristics

### Time Complexity

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Pin object | O(n + s) | n=pinned objects, s=object size |
| Unpin object | O(n + s) | Search + removal + block marking |
| Compute forwarding | O(n) | n=total blocks in heap |
| Compact copy | O(n) | n=bytes moved |
| Update references| O(n) | n=heap objects, scan all refs |
| Total compaction | O(b + m + n) | Linear in heap content |

### Space Complexity

```
Pinned table:       O(p)        # p=pinned objects
Forward table:      O(m_blocks) # m=moved blocks
Reference map:      O(p)        # p=pointers
Total overhead:     O(p + m)
```

### Fragmentation Before/After

Example (1000-block heap):

**Before compaction:**
```
Allocation: 400 blocks used
Free space: 600 blocks
Fragmentation:
  - 10 separate free regions
  - Largest region: 80 blocks
  - Can't allocate 100-block object
```

**After compaction:**
```
Allocation: 400 blocks used
Free space: 600 blocks (contiguous)
Fragmentation: 
  - 1 free region
  - Size: 600 blocks
  - Can allocate anything up to 600 blocks
```

## Debugging & Validation

### Validation Functions

```c
// Validate pinned table consistency
static void gc_pinned_validate(void) {
    // Check no dangling pinned pointers
    // Verify blocks marked as pinned are actually pinned
    // Remove stale entries for freed objects
}
```


## Summary

The GC enhancements provide:

1. **Bidirectional allocation** prevents pointer corruption:
   - Regular objects move during compaction
   - Pinned objects stay fixed
   - No conflicts between pointer addresses and GC moves

2. **Three-stage compaction** efficiently defragments:
   - Compute forwarding: O(n) scan to find new positions
   - Copy objects: O(m) byte movement
   - Update references: O(n) pointer fixing

3. **Pinning system** tracks which objects are referenced:
   - Sorted table for fast lookup
   - Automatic on pointer creation
   - Prevents GC relocation

4. **Seamless integration** with pointer operations:
   - Pointers keep objects pinned
   - Compaction never invalidates pinned pointers
   - Automatic memory management preserves safety

This design maintains MicroPython's efficiency while supporting the new pointer feature safely on memory-constrained systems.
