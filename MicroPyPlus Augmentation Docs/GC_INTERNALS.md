# Garbage Collector Internals: Architecture & Design

## Heap Structure

### Logical Heap Layout

```
┌────────────────────────────────────────────────────────────┐
│                  TOTAL HEAP MEMORY                         │
├────────────────────────────────────────────────────────────┤
│  Allocation Table (ATB)                                    │
│  [metadata tracking block allocation states]               │
│  Size: proportional to pool size                           │
│  1 byte = 4 blocks (2 bits per block)                      │
├────────────────────────────────────────────────────────────┤
│  Finaliser Table (FTB) [optional]                          │
│  [tracks which objects have finalizers]                    │
│  Size: 1 bit per block                                     │
├────────────────────────────────────────────────────────────┤
│                    MEMORY POOL                             │
│  [actual Python objects and data]                          │
│  Size: remaining memory                                    │
│  Divided into fixed-size blocks                            │
│  Allocations are multiples of block size                   │
│  Typical block size: 16 or 32 bytes                        │
└────────────────────────────────────────────────────────────┘
```

### Block Organization

Each block tracks 4 allocation states using 2 bits:

```c
#define AT_FREE (0)    // 0b00 - Available for allocation
#define AT_HEAD (1)    // 0b01 - Start of object (may span multiple blocks)
#define AT_TAIL (2)    // 0b10 - Continuation of object started with HEAD
#define AT_MARK (3)    // 0b11 - HEAD that survived GC marking phase
```

**Example Block Chain:**
```
Object 1 (3 blocks):     Object 2 (1 block):   Free space:
┌─────────┬─────────┬─────────┬─────────┬─────────┬─────────┐
│ HEAD    │ TAIL    │ TAIL    │ HEAD    │ FREE    │ FREE    │
│ (obj1)  │ (obj1)  │ (obj1)  │ (obj2)  │         │         │
└─────────┴─────────┴─────────┴─────────┴─────────┴─────────┘
  Block 0   Block 1   Block 2   Block 3   Block 4   Block 5
```

**Encoding in Allocation Table (1 byte = 4 blocks):**
```
Byte[0] contains blocks 0-3:
  Bits 7-6: Block 3 state (2 bits)
  Bits 5-4: Block 2 state (2 bits)  
  Bits 3-2: Block 1 state (2 bits)
  Bits 1-0: Block 0 state (2 bits)

Example: 0b11011001 = 
  Block 3: 11 (MARK)
  Block 2: 01 (HEAD)
  Block 1: 10 (TAIL)
  Block 0: 01 (HEAD)
```

## Bi-Directional Allocation Strategy

### The Problem

Standard heap allocation from left-to-right creates fragmentation:

```
Initial:  [Block 0] [Block 1] [Block 2] [Block 3] ...

After obj1 (2 blocks):
          [USED    ] [USED    ] [FREE    ] [FREE    ] ...

After obj2 (2 blocks):
          [USED    ] [USED    ] [USED    ] [USED    ] ...

After obj1 freed:
          [FREE    ] [FREE    ] [USED    ] [USED    ] ...
          ^^^^ Hole! Cannot use for large objects
```

### The Solution: Allocate in Both Directions

MicroPythonPlus's GC allocates pinned objects from the RIGHT, allowing them to coexist with left-allocated objects:

```
Initial Heap:
┌─────────────────────────────────────────────────────┐
│ FREE | FREE | FREE | FREE | FREE | FREE | FREE ... │
└─────────────────────────────────────────────────────┘
  0     1     2     3     4     5     6

Regular allocation (from LEFT):
┌─────────────────────────────────────────────────────┐
│ OBJ1 | OBJ1 | OBJ2 | FREE | FREE | FREE | FREE ... │
└─────────────────────────────────────────────────────┘
  0     1     2     3     4     5     6
  ←─ Left frontier grows right

Pinned allocation (from RIGHT):
┌──────────────────────────────────────────────────────┐
│ OBJ1 | OBJ1 | OBJ2 | FREE | FREE | PIN1 | PIN1 ...    │
└──────────────────────────────────────────────────────┘
  0     1     2     3     4     5     6
                        Left ─→     ←─ Right frontier

After GC with compaction:
┌──────────────────────────────────────────────────────┐
│ OBJ1 | OBJ1 | OBJ2 | FREE | FREE | PIN1 | FREE ...   │
└──────────────────────────────────────────────────────┘
  0     1     2     3     4     5     6
  ←──────────────────────────────→      ←─ Pinned stays safe
```

### Implementation Details

Two pointers track the frontiers:

```c
area->gc_last_used_block_from_left   // Highest block used from left
area->gc_last_used_block_from_right  // Lowest block used from right
```

**Collision detection:**
```c
if (last_used_from_left >= last_used_from_right) {
    // No more space! Need GC or reallocation
    return allocation_failed;
}
```

### Allocation Functions

**Left allocation (regular objects):**
```c
// Scans from left to right
// Finds first n consecutive FREE blocks
// Marks first as HEAD, rest as TAIL
// Updates left frontier
```

**Right allocation (pinned objects):**
```c
// Scans from right to left
// Finds n consecutive FREE blocks
// Marks first as HEAD, rest as TAIL
// Updates right frontier
// CRITICAL: Searches backward to prevent fragmentation
```

### Why This Works for Pinned Objects

1. **Pinned objects don't move** - They stay at fixed addresses
2. **GC won't deallocate them** - They're explicitly protected
3. **Isolated from heap fragmentation** - Growing from opposite end
4. **Survives compaction** - Pinned section stays in place

```c
void *gc_alloc(size_t n_bytes, unsigned int alloc_flags) {
    bool is_pinned = alloc_flags & GC_ALLOC_FLAG_IS_PINNED;
    
    if (is_pinned) {
        // Allocate from right using gc_alloc_right()
        size_t block = gc_alloc_right(area, n_blocks);
        area->gc_last_used_block_from_right = alloc_start;
    } else {
        // Allocate from left (standard allocation)
        // Scan ATB from left to right, update gc_last_used_block_from_left
    }
}
```

## Garbage Collection Phases

### Phase 1: Collection Start & Root Marking

**Purpose:** Identify all reachable objects

```
gc_collect_start()
  ↓
gc_collect_start_common()
  - Acquire GC lock
  - Set GC_COLLECT_FLAG (prevents new allocations)
  - Zero allocation amount counter
  ↓
Scan Root Pointers
  - Stack/registers (Python stack)
  - Global variables
  - Module dictionaries
  ↓
gc_collect_root(ptrs, len)
  for each root pointer:
    - Verify pointer is in heap
    - Get block containing pointer
    - If block is HEAD (unmarked):
      - Mark it as MARK (change AT_HEAD → AT_MARK)
      - Push to GC mark stack
      - Recurse to children
```

**State during Phase 1:**
```
Block states:
  AT_FREE (0b00) - Not used
  AT_HEAD (0b01) - Unmarked object (not yet reachable)
  AT_MARK (0b11) - Marked object (reachable, scan children)
  
Example:
  Before: [HEAD] [TAIL] [HEAD] [TAIL] [TAIL] [FREE]
           0     1     2     3     4     5
           
  Root points to block 2, so mark it:
  After:  [HEAD] [TAIL] [MARK] [TAIL] [TAIL] [FREE]
           0     1     2     3     4     5
```

### Phase 2: Mark Subtree (Recursive Marking)

**Purpose:** Recursively mark all objects reachable from roots

```
gc_mark_subtree(area, block)
  
  Initialize stack (sp = 0)
  
  for each block to process:
    1. Count consecutive TAIL blocks following HEAD
    2. Read all pointers from block(s)
    3. For each pointer:
       - Verify it's a heap pointer
       - Get its block
       - If block is HEAD (unmarked):
         - Change HEAD → MARK
         - Push block to GC stack
    4. Pop next block from stack
    5. Repeat until stack empty
```

**Stack-based algorithm (prevents recursion depth issues):**
```c
size_t sp = 0;  // Stack pointer

for (;;) {
    // Process current block
    // ... check all pointers in block ...
    
    // Found unmarked block -> push to stack
    if (ATB_GET_KIND(ptr_area, ptr_block) == AT_HEAD) {
        ATB_HEAD_TO_MARK(ptr_area, ptr_block);
        if (sp < STACK_SIZE) {
            MP_STATE_MEM(gc_block_stack)[sp] = ptr_block;
            sp++;
        }
    }
    
    // Pop next block
    if (sp == 0) break;
    sp--;
    block = MP_STATE_MEM(gc_block_stack)[sp];
}
```

**Example Mark Propagation:**
```
Initial (after Phase 1, root points to obj1):
[MARK] [TAIL] [HEAD] [TAIL] [FREE]
  0      1      2      3       4

obj1 contains pointer to obj2:
Process block 0:
  - Found pointer to block 2 (HEAD)
  - Mark it: HEAD → MARK
  - Push block 2

[MARK] [TAIL] [MARK] [TAIL] [FREE]
   0      1     2      3      4

Process block 2:
  - No pointers to other objects
  - Pop stack (empty)
  
Final:
[MARK] [TAIL] [MARK] [TAIL] [FREE]
   0     1      2      3      4
```

### Phase 3: Sweep & Finalization

**Purpose:** Free unmarked objects, run finalizers

```
gc_deal_with_stack_overflow()
  - If mark stack overflowed: collect all unmarked blocks too
    (Conservative: assume overflow blocks are reachable)

gc_sweep_run_finalisers()
  for each block with finaliser (FTB set):
    - If block is HEAD and NOT MARK:
      - Get finaliser from object
      - Call finaliser (may allocate memory)

gc_sweep_free_blocks()
  for each HEAD block:
    - If block is MARK:
      - Change MARK → HEAD (prepare for next cycle)
    - Else (HEAD but not MARK):
      - Change HEAD/TAIL → FREE
      - Free blocks
      - Update gc_last_free_atb_index
```

**State Transitions During Sweep:**
```
Before sweep:
[MARK] [TAIL] [HEAD] [TAIL] [FREE] [MARK] [TAIL]
 0       1       2     3      4      5      6

Block 0-1: Reachable (MARK)
Block 2-3: Unreachable (HEAD not marked)
Block 5-6: Reachable (MARK)

After finalizers & sweep:
[HEAD] [TAIL] [FREE] [FREE] [FREE] [HEAD] [TAIL]
  0      1       2     3       4     5      6

Transitions:
  [MARK] → [HEAD]  (obj0, obj5 - survived, reset for next round)
  [HEAD] → [FREE]  (obj2 - freed)
```

### Phase 4: Compaction & Update References (if needed)

**Purpose:** Reduce fragmentation, move all live objects together

```
gc_collect_end()
  
  if gc_should_compact():
    for each memory area:
      1. gc_compute_forwarding_addresses(area, table)
         - Scan blocks left to right
         - Reachable blocks (MARK) get new addresses
         - Create map: old_block → new_block
      
      2. gc_compact_copy(area, table)
         - Copy block contents to new locations
         - Update block headers
      
      3. gc_update_references(area, table)
         - Scan all blocks
         - Update pointers to use new addresses
```

**Before Compaction:**
```
┌─────────┬─────────┬─────────┬─────────┬─────────┬─────────┐
│ OBJ1    │ OBJ1    │ [FREE]  │ OBJ2    │ [FREE]  │ [FREE]  │
│ MARK    │ TAIL    │ FREE    │ MARK    │ FREE    │ FREE    │
└─────────┴─────────┴─────────┴─────────┴─────────┴─────────┘
  0         1         2         3         4         5

Fragmentation: ~3 blocks free
```

**Forwarding Table:**
```
Block 0 → Block 0  (stay in place)
Block 1 → Block 1
Block 3 → Block 2  (shift down)
```

**After Compaction:**
```
┌─────────┬─────────┬─────────┬─────────┬─────────┬─────────┐
│ OBJ1    │ OBJ1    │ OBJ2    │ OBJ2    │ [FREE]  │ [FREE]  │
│ HEAD    │ TAIL    │ HEAD    │ TAIL    │ FREE    │ FREE    │
└─────────┴─────────┴─────────┴─────────┴─────────┴─────────┘
  0         1         2         3         4         5

Compaction benefit: Contiguous free space (~2 blocks)
```

## Complete GC Cycle

### Typical Cycle Timeline

```
Time →

NORMAL OPERATION
│
├─ Allocate objects
├─ Execute user code
├─ More allocations
│
└─ Heap > threshold → Trigger gc.collect()
   
   GC COLLECTION PHASE
   │
   ├─ PHASE 1: MARK START
   │  ├─ Acquire lock
   │  ├─ Set GC_COLLECT_FLAG (block new allocations)
   │  ├─ Scan root pointers (stack, globals)
   │  └─ Push reachable blocks to mark stack
   │
   ├─ PHASE 2: MARK SUBTREE
   │  ├─ Process mark stack
   │  ├─ For each block, scan child pointers
   │  ├─ Mark reachable child blocks
   │  └─ Continue until stack empty
   │
   ├─ PHASE 3: SWEEP
   │  ├─ Run finalizers on unreachable objects
   │  ├─ Free unreachable block chains
   │  └─ Change MARK → HEAD (prepare for next cycle)
   │
   ├─ PHASE 4: COMPACT (optional)
   │  ├─ Compute new block positions
   │  ├─ Copy blocks to new locations
   │  └─ Update all references
   │
   └─ Release lock
   
RESUME NORMAL OPERATION
│
├─ Allocate more objects
└─ Continue...
```

### Performance Characteristics

**Time complexity:**
```
Mark phase:    O(heap_size + pointers)
Sweep phase:   O(heap_size)
Compact phase: O(live_objects)
Total:         O(heap_size)
```

**Memory usage:**
```
Overhead:
  - ATB: heap_size / (BLOCKS_PER_ATB * 8)
  - FTB: heap_size / BLOCKS_PER_FTB / 8 (optional)
  - GC mark stack: configurable (typically 64 entries)
  - Pinned table: small (grows with pinned objects)

Example (1MB heap, 16-byte blocks):
  Heap blocks: 1MB / 16 = 65536 blocks
  ATB size: 65536 / 4 = 16384 bytes
  GC stack: 64 entries × 8 bytes = 512 bytes
  Total GC metadata: ~17KB (1.7% overhead)
```

### Pause Times

```
GC pause = Mark time + Sweep time + Compact time

Typical:
  Small heap (64KB):       1-5 ms
  Medium heap (1MB):       5-50 ms
  Large heap (16MB):       50-500 ms

Optimization strategies:
  1. Disable auto-collect during time-critical sections
     gc.disable()
     ... critical work ...
     gc.enable()
  
  2. Collect during idle time
     Can manually call gc.collect() when convenient
  
  3. Reduce fragmentation
     Pinned allocation from right prevents worst-case fragmentation
```

## Pinned Objects During GC

### Interaction with Collection

**Key principle:** Pinned objects are immune to GC

```c
bool gc_is_block_pinned(size_t block) {
    // Check if block is in pinned_table
    // Returns true even if block would be freed otherwise
}

gc_mark_subtree() {
    if (gc_is_block_pinned(ptr_block)) {
        // Always mark as reachable
        // Skip scanning (might contain old pointers)
    }
}
```

### Compaction with Pinned Objects

```
Before compaction (bi-directional):
┌─────────┬─────────┬─────────┬─────────┬─────────┬─────────┐
│ REG1    │ REG1    │ [FREE]  │ REG2    │ PIN1    │ PIN1    │
│ obj     │ obj     │ free    │ obj     │ pinned  │ pinned  │
└─────────┴─────────┴─────────┴─────────┴─────────┴─────────┘
    0         1         2         3         4         5

Compaction ONLY moves regular objects:
┌─────────┬─────────┬─────────┬─────────┬─────────┬─────────┐
│ REG1    │ REG1    │ REG2    │ [FREE]  │ PIN1    │ PIN1    │
│ obj     │ obj     │ obj     │ free    │ pinned  │ pinned  │
└─────────┴─────────┴─────────┴─────────┴─────────┴─────────┘
    0         1         2         3         4         5
  ←─ Left compaction     ┌───────────────────────────┘
                          Never touches pinned section
```

**Forwarding table only includes regular objects:**
```c
for (size_t block = 0; block < last_used_from_left; block++) {
    if (gc_is_block_pinned(block)) {
        continue;  // Don't compact pinned blocks
    }
    // Regular compaction logic
}
```

## Summary: GC Flow Diagram

```
┌─────────────────────────────────────────┐
│      START GC COLLECTION                │
└────────────────┬────────────────────────┘
                 │
                 ▼
    ┌────────────────────────────┐
    │ PHASE 1: MARK ROOT OBJECTS │
    │ - Acquire lock             │
    │ - Scan stack/globals       │
    │ - Mark reachable blocks    │
    └────────────┬───────────────┘
                 │
                 ▼
    ┌────────────────────────────┐
    │ PHASE 2: MARK SUBTREE      │
    │ - Recursively mark children│
    │ - Stack-based traversal    │
    │ - Find all reachable objs  │
    └────────────┬───────────────┘
                 │
                 ▼
    ┌────────────────────────────┐
    │ PHASE 3: SWEEP             │
    │ - Run finalizers           │
    │ - Free unmarked blocks     │
    │ - Reset MARK → HEAD        │
    └────────────┬───────────────┘
                 │
                 ▼
    ┌────────────────────────────┐
    │ PHASE 4: COMPACT (optional)│
    │ - Compute new positions    │
    │ - Copy live objects        │
    │ - Update references        │
    │ - Protect pinned objects   │
    └────────────┬───────────────┘
                 │
                 ▼
    ┌────────────────────────────┐
    │ Release lock               │
    │ Resume allocation          │
    └────────────────────────────┘
```
