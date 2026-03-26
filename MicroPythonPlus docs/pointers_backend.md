# Pointer System Backend Implementation

## Overview

This document describes the internal implementation of the pointer reference system in MicroPython Plus. This is a technical reference for developers working on the MicroPython compiler, virtual machine, and runtime systems.

## Architecture Overview

The pointer system consists of five main layers:

1. **Lexer & Parser** - Tokenize and build AST for pointer syntax
2. **Compiler** - Generate bytecode opcodes from AST
3. **Bytecode** - Define opcodes for pointer operations
4. **Virtual Machine** - Execute pointer operations at runtime
5. **Runtime** - Object representation and memory management

```
Source Code (ptr_x = &x//ptr_x = 42)
         ↓
    [Lexer/Parser]
         ↓
[AST with Pointer Nodes]
         ↓
    [Compiler]
         ↓
 [Bytecode Opcodes]
         ↓
 [Virtual Machine]
         ↓
 [Memory/GC System]
```

## Lexer & Parser (`py/lexer.h`, `py/grammar.h`)

### Tokens

The lexer recognizes three pointer-related tokens:

| Token | Symbol | Use |
|-------|--------|-----|
| `OP_STAR` | `*` | Dereference (context-dependent: multiply vs dereference) |
| `OP_AMPERSAND` | `&` | Address-of operator |
| `DEL_MINUS_MORE` | `->` | Member access through pointer |

### Grammar Rules

The grammar integrates pointer operations into the expression hierarchy:

```c
// Factor level (unary operations)
DEF_RULE_NC(factor, or(3), rule(factor_2), rule(ptr_unary), rule(power))

// Pointer-specific branch
DEF_RULE_NC(ptr_unary, or(2), rule(ptr_deref), rule(ptr_addr_of))

// Individual operations
DEF_RULE(ptr_deref, c(ptr_deref), and(2), tok(OP_STAR), rule(factor))
DEF_RULE(ptr_addr_of, c(ptr_addr_of), and(2), tok(OP_AMPERSAND), rule(factor))

// Member access in trailers
DEF_RULE(trailer_ptr_member, c(trailer_ptr_member), and(2), tok(DEL_MINUS_MORE), tok(NAME))
```

### Parse Node Structure

Parser generates nodes with these structures:

**Dereference (`*expr`):**
```
ptr_deref node
├── nodes[0]: inner expression (factor)
└── implicit: OP_STAR token
```

**Address-of (`&expr`):**
```
ptr_addr_of node
├── nodes[0]: identifier (NAME) or factor
├── nodes[1]: factor
└── implicit: OP_AMPERSAND token
```

**Member Access (`ptr->member`):**
```
trailer_ptr_member node
├── nodes[0]: DEL_MINUS_MORE token
├── nodes[1]: NAME (member identifier)
```

### Operator Precedence

Pointers are at the **factor level**, same as unary operators:

```
Assignment  (lowest precedence)
   ↓
Or/And
   ↓
Comparisons
   ↓
Arithmetic (+, -)
   ↓
Term (*, /, %)
   ↓
Unary (+, -, ~) ← Pointer ops here (&, *)
   ↓
Power (**)
   ↓
Trailer/Subscript (highest precedence)
```

This means `&x + 1` parses as `(&x) + 1`, not `&(x + 1)`.

## Bytecode Opcodes (`py/bc0.h`)

### Opcode Format Reference

Pointer opcodes use two encoding formats:

| Format | Size | Used For |
|--------|------|----------|
| `VINT_O` | Variable-length int arg | Local variable indices (small) |
| `QSTR_O` | Interned string arg | Global variable names |
| `VINT_E` | Variable-length int arg | Extended operations |

### Address-of Opcodes

```c
#define MP_BC_ADDRESS_OF_FAST     (MP_BC_BASE_VINT_O + 0x08)
// Arg: uint - local variable index
// Stack: ... → ... ptr
// Create pointer to local variable

#define MP_BC_ADDRESS_OF_GLOBAL   (MP_BC_BASE_QSTR_O + 0x0d)
// Arg: qstr - global variable name
// Stack: ... → ... ptr
// Create pointer to global variable
```

### Dereference Opcodes

```c
#define MP_BC_POINTER_DEREF_FAST   (MP_BC_BASE_VINT_O + 0x09)
// Arg: uint - local variable index
// Stack: ... → ... value
// Dereference local variable containing pointer

#define MP_BC_POINTER_DEREF_GLOBAL (MP_BC_BASE_QSTR_O + 0x0e)
// Arg: qstr - global variable name  
// Stack: ... → ... value
// Dereference global variable containing pointer
```

### Member Access Opcodes

```c
#define MP_BC_POINTER_MEMBER_ACCESS (MP_BC_BASE_QSTR_O + 0x0f)
// Arg: qstr - member name (attribute)
// Stack: ... ptr → ... value
// Pop pointer, dereference, load member attribute
```

### Pointer Assignment Opcodes

```c
#define MP_BC_POINTER_ASSIGN_FAST  (MP_BC_BASE_VINT_E + 0x0a)
// Arg: uint - local variable index
// Stack: ... ptr value → ...
// Pop value, pop pointer from local, assign through pointer

#define MP_BC_POINTER_ASSIGN_GLOBAL (MP_BC_BASE_VINT_O + 0x0a)
// Arg: qstr - global variable name
// Stack: ... value → ...
// Pop value, load pointer from global, assign through pointer

#define MP_BC_POINTER_MEMBER_ASSIGN (MP_BC_BASE_VINT_O + 0x0b)
// Arg: qstr - member name
// Stack: ... ptr value → ...
// Pop value, pop pointer, set member attribute
```

### Combined Opcode Table

| Operation | Local | Global | Notes |
|-----------|-------|--------|-------|
| Address-of | `ADDRESS_OF_FAST` (VINT) | `ADDRESS_OF_GLOBAL` (QSTR) | Two variants per scope |
| Dereference | `POINTER_DEREF_FAST` (VINT) | `POINTER_DEREF_GLOBAL` (QSTR) | Load value through pointer |
| Member Access | N/A | `POINTER_MEMBER_ACCESS` (QSTR) | Access field on dereferenced object |
| Assign | `POINTER_ASSIGN_FAST` (VINT) | `POINTER_ASSIGN_GLOBAL` (QSTR) | Write value through pointer |
| Member Assign | N/A | `POINTER_MEMBER_ASSIGN` (QSTR) | Write field on dereferenced object |

## Compiler Implementation (`py/compile.c`)

### Compile Functions

The compiler generates bytecode through dedicated compile functions for each pointer construct:

#### `compile_ptr_addr_of()`

**Input:** Parse node for `&expr`

**Algorithm:**
```
1. Extract operand from parse node
2. Check if operand is simple identifier:
   - If YES:
     a. Get qstr from identifier
     b. Call mp_emit_common_id_op() with address_of method
     c. This emits ADDRESS_OF_FAST or ADDRESS_OF_GLOBAL
   - If NO:
     a. Compile error: complex expressions unsupported
     b. Must be simple variable reference
3. Two passes:
   PASS_SCOPE: Register identifier for lookup
   PASS_EMIT: Emit actual bytecode
```

**Example emission:**
```python
x = 42
ptr_x = &x
```

Compiles to:
```
LOAD_FAST 0        # Load local 'ptr_x' (will be assigned)
ADDRESS_OF_FAST 0  # Push &x (where 0 is local index of x)
STORE_FAST 0       # Store pointer in ptr_x
```

#### `compile_ptr_deref()`

**Input:** Parse node for `*expr`

**Algorithm:**
```
1. Extract operand from parse node
2. Check if operand is simple identifier:
   - If YES:
     a. Get qstr from identifier  
     b. Call mp_emit_common_id_op() with pointer_deref method
     c. Emits POINTER_DEREF_FAST or POINTER_DEREF_GLOBAL
   - If NO:
     a. Compile error: complex expressions unsupported
```

**Example emission:**
```python
ptr_x = &x
val = *ptr_x
```

Compiles to:
```
ADDRESS_OF_FAST 0  # &x
STORE_FAST 1       # ptr_x = ...
LOAD_FAST 1        # ptr_x
POINTER_DEREF_FAST 1  # Dereference
STORE_FAST 2       # val = ...
```

**Note:** The deref opcode takes local index as argument (index 1 for `ptr_x`), not index 0.

#### `compile_ptr_member_access()`

**Input:** Parse node for `ptr->member`

**Algorithm:**
```
1. Compile the pointer expression (left side of ->)
2. Extract member name from parse node
3. Emit POINTER_MEMBER_ACCESS with member qstr
4. Result: pointer dereferenced and member loaded
```

**Example emission:**
```python
p = &point
x = p->x
```

Compiles to:
```
ADDRESS_OF_FAST 0     # &point
STORE_FAST 1          # p = ...
LOAD_FAST 1           # p
POINTER_MEMBER_ACCESS x  # Load 'x' attribute from dereferenced object
STORE_FAST 2          # x = ...
```

**VM Implementation:** The single opcode handles both dereference and attribute load:
```c
ENTRY(MP_BC_POINTER_MEMBER_ACCESS): {
    mp_obj_t ptr_obj = TOP();
    mp_obj_t *ptr = mp_obj_pointer_get(ptr_obj);
    SET_TOP(mp_load_attr(*ptr, qst));  // Dereference then load attr
    DISPATCH();
}
```

#### Assignment Compilation

**Parse Node:** `stmt_expr_stmt` containing assignment with pointer dereference/member access

**Algorithm for `*ptr = value`:**
```
1. Parse left side as assignment target
2. Check if target is ptr_deref node:
   - Extract operand (must be simple identifier)
   - Compile right-side expression (value)
   - Emit POINTER_ASSIGN_FAST or POINTER_ASSIGN_GLOBAL
3. Stack effects:
   - Before: [TOS = value]
   - After: [] (value consumed by assignment)
```

**Algorithm for `ptr->member = value`:**
```
1. Extract member name from trailer_ptr_member
2. Compile pointer expression
3. Compile value expression  
4. Emit POINTER_MEMBER_ASSIGN with member qstr
5. Stack: [... ptr value] → [...]
```

**Special handling for augmented assignment (`*ptr += 1`):**
```
For assignment kind ASSIGN_AUG_LOAD:
  - Preserve pointer on stack
  - DUP_TOP
  - POINTER_DEREF to load current value
  - Apply binary op

For assignment kind ASSIGN_AUG_STORE:
  - MOD rotation of stack
  - POINTER_ASSIGN to write back
```

### Emit Methods (`py/emit.h`)

The emit interface defines three operation tables for pointer operations:

```c
typedef struct _emit_method_table_t {
    // ... other fields
    mp_emit_method_table_id_ops_t address_of;
    mp_emit_method_table_id_ops_t pointer_deref;
    mp_emit_method_table_id_ops_t pointer_assign;
    void (*pointer_member_access)(emit_t *emit, qstr member);
    // ... other fields
} emit_method_table_t;
```

Each `id_ops` table contains:
```c
typedef struct {
    // fast: local variable operation
    // global: global variable operation
    // deref: closure variable operation
} mp_emit_method_table_id_ops_t;
```

## Virtual Machine Execution (`py/vm.c`)

### VM Entry Points

The VM dispatch loop contains entries for each pointer opcode:

#### `MP_BC_ADDRESS_OF_FAST`

```c
ENTRY(MP_BC_ADDRESS_OF_FAST): {
    DECODE_UINT;  // unum = local variable index
    mp_obj_t *ptr = &fastn[-unum];  // Get address of local var
    PUSH(mp_obj_new_pointer(ptr));  // Create pointer object
    DISPATCH();
}
```

**Stack effect:** `... → ... pointer_obj`

**Key points:**
- `fastn` points to the local variable array (base address)
- `fastn[-unum]` is the variable at index `unum` (negative indexing from end)
- `&fastn[-unum]` gets its address
- `mp_obj_new_pointer()` wraps address in pointer object

**Memory view:**
```
Stack: [... local_x ...]
                ↓
fastn: [... | x | ...]
            ↑
        &fastn[-unum]
            ↓
    Returns address
            ↓
    pointer_obj created
```

#### `MP_BC_ADDRESS_OF_GLOBAL`

```c
ENTRY(MP_BC_ADDRESS_OF_GLOBAL): {
    MARK_EXC_IP_SELECTIVE();  // Track exception info
    DECODE_QSTR;  // qst = global variable name
    mp_map_elem_t *elem = mp_map_lookup(
        &mp_globals_get()->map,
        MP_OBJ_NEW_QSTR(qst),
        MP_MAP_LOOKUP
    );
    if (elem == NULL) {
        goto local_name_error;  // Global not found
    }
    mp_obj_t *ptr = &elem->value;  // Address of global value
    PUSH(mp_obj_new_pointer(ptr));
    DISPATCH();
}
```

**Stack effect:** `... → ... pointer_obj`

**Key points:**
- Look up global in globals dictionary
- Get address of the value storage in map element
- Wrap in pointer object

#### `MP_BC_POINTER_DEREF_FAST`

```c
ENTRY(MP_BC_POINTER_DEREF_FAST): {
    DECODE_UINT;  // unum = pointer local variable index
    mp_obj_t ptr_obj = fastn[-unum];  // Load pointer object
    mp_obj_t *ptr = mp_obj_pointer_get(ptr_obj);  // Extract address
    PUSH(*ptr);  // Dereference: push value at address
    DISPATCH();
}
```

**Stack effect:** `... → ... value`

**Key points:**
- Load pointer object from local variable
- Validate it's actually a pointer type
- Extract the stored address
- Dereference to get the value
- Push value onto stack

**Type checking in `mp_obj_pointer_get()`:**
```c
mp_obj_t *mp_obj_pointer_get(mp_obj_t ptr) {
    if (!mp_obj_is_obj(ptr)) {
        mp_raise_TypeError("expected pointer object");
    }
    if (!mp_obj_is_type(ptr, &mp_type_pointer)) {
        mp_raise_TypeError("expected pointer object");
    }
    mp_obj_pointer_t *p = MP_OBJ_TO_PTR(ptr);
    return (mp_obj_t *)p->addr;
}
```

#### `MP_BC_POINTER_MEMBER_ACCESS`

```c
ENTRY(MP_BC_POINTER_MEMBER_ACCESS): {
    MARK_EXC_IP_SELECTIVE();
    DECODE_QSTR;  // qst = member name
    mp_obj_t ptr_obj = TOP();  // Peek at pointer (don't pop)
    mp_obj_t *ptr = mp_obj_pointer_get(ptr_obj);  // Extract address
    SET_TOP(mp_load_attr(*ptr, qst));  // Replace with member value
    DISPATCH();
}
```

**Stack effect:** `... ptr → ... member_value`

**Key points:**
- Dereference pointer to get object
- Load attribute from that object
- Replace pointer with attribute value on stack
- Uses existing `mp_load_attr()` machinery

#### `MP_BC_POINTER_ASSIGN_FAST`

```c
ENTRY(MP_BC_POINTER_ASSIGN_FAST): {
    MARK_EXC_IP_SELECTIVE();
    DECODE_UINT;  // unum = pointer local variable index
    mp_obj_t ptr_obj = fastn[-unum];  // Load pointer
    
    // Type validation
    if (!MP_OBJ_IS_TYPE(ptr_obj, &mp_type_pointer)) {
        RAISE(TypeError("expected pointer"));
    }
    
    mp_obj_t *ptr = mp_obj_pointer_get(ptr_obj);
    
    // Bounds checking
    if (ptr == NULL || (uintptr_t)ptr < 0x1000 || 
        (uintptr_t)ptr > 0x7fffffff0000ULL) {
        RAISE(ValueError("invalid pointer value"));
    }
    
    mp_obj_t value = POP();  // Get value to assign
    *ptr = value;  // Write through pointer
    DISPATCH();
}
```

**Stack effect:** `... value → ...`

**Key points:**
- Validate pointer object type
- Check pointer is within valid range (safety check)
- Prevent NULL and far-out-of-range pointers
- Pop value from stack
- Write (dereference) to memory location

**Safety features:**
- Null pointer check
- Range check (basic bounds)
- Type validation

#### `MP_BC_POINTER_MEMBER_ASSIGN`

```c
ENTRY(MP_BC_POINTER_MEMBER_ASSIGN): {
    MARK_EXC_IP_SELECTIVE();
    DECODE_QSTR;  // qst = member name
    mp_obj_t value = POP();  // Get value
    mp_obj_t ptr_obj = POP();  // Get pointer
    
    if (!MP_OBJ_IS_TYPE(ptr_obj, &mp_type_pointer)) {
        RAISE(TypeError("expected pointer"));
    }
    
    mp_obj_t *ptr = mp_obj_pointer_get(ptr_obj);
    mp_store_attr(*ptr, qst, value);  // Dereference and store
    DISPATCH();
}
```

**Stack effect:** `... ptr value → ...`

**Key points:**
- Pop value then pointer (FILO order)
- Dereference pointer to get object
- Call `mp_store_attr()` to set member
- Uses existing attribute storage machinery

## Pointer Object Type (`py/objpointer.c`)

### Object Structure

```c
typedef struct _mp_obj_pointer_t {
    mp_obj_base_t base;      // Standard object header
    intptr_t addr;           // Stored address (64-bit signed)
} mp_obj_pointer_t;
```

**Why `intptr_t`?**
- Portable: Defined to be same size as pointer on any platform
- Signed: Can detect certain invalid values
- Allows arithmetic interpretation if needed

### Type Definition

```c
MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_pointer,
    MP_QSTR_pointer,
    MP_TYPE_FLAG_NONE
);
```

**Fields:**
- `mp_type_pointer`: Type object itself
- `MP_QSTR_pointer`: Name as interned string
- `MP_TYPE_FLAG_NONE`: No special flags (not heap-allocated type, etc.)

### Core Functions

#### `mp_obj_new_pointer(mp_obj_t *addr)`

**Purpose:** Create new pointer object

```c
mp_obj_t mp_obj_new_pointer(mp_obj_t *addr) {
    // Allocate object
    mp_obj_pointer_t *o = m_new_obj(mp_obj_pointer_t);
    
    // Initialize
    o->base.type = &mp_type_pointer;
    o->addr = (intptr_t)addr;
    
    // GC tracking: Pin pointed-to object
    if (addr != NULL && mp_obj_is_obj(*addr)) {
        gc_pin(MP_OBJ_TO_PTR(*addr));
    }
    
    return MP_OBJ_FROM_PTR(o);
}
```

**GC Interaction:**
- When pointer created, automatically pin target object
- Prevents GC from relocating pointed-to object
- Reduces but doesn't eliminate copying (heap may compact)

**Safety:**
- NULL check before pinning
- Only pin objects, not small integers/constants

#### `mp_obj_pointer_get(mp_obj_t ptr)`

**Purpose:** Extract address from pointer object with validation

```c
mp_obj_t *mp_obj_pointer_get(mp_obj_t ptr) {
    // Type checking
    if (!mp_obj_is_obj(ptr)) {
        mp_raise_TypeError("expected pointer object");
    }
    if (!mp_obj_is_type(ptr, &mp_type_pointer)) {
        mp_raise_TypeError("expected pointer object");
    }
    
    // Extract
    mp_obj_pointer_t *p = MP_OBJ_TO_PTR(ptr);
    return (mp_obj_t *)p->addr;
}
```

**Error handling:**
- Rejects non-objects (small ints, None, etc.)
- Validates type is actually `mp_type_pointer`
- Raises `TypeError` if validation fails

### Pointer Lifetime

**Creation:**
```
    &variable
       ↓
Address computed
       ↓
Pointer object 
   allocated
       ↓
 Target object 
     pinned
       ↓
Pointer object returned
```

**Usage:**
```
    *ptr
      ↓
Pointer object 
 dereferenced
      ↓
Address extracted
      ↓
  Value at 
address accessed
```

**Cleanup:**
```
Pointer object 
  no longer 
  referenced
      ↓
GC collects 
pointer object
      ↓
Target object unpinned 
  (if needed)
      ↓
Target can move/be collected
```

## Garbage Collection Integration

### Pinning Mechanism

When a pointer is created to an object, that object is "pinned" to prevent GC relocation:

```c
// From mp_obj_new_pointer()
if (addr != NULL && mp_obj_is_obj(*addr)) {
    gc_pin(MP_OBJ_TO_PTR(*addr));
}
```

**Why pinning is necessary:**

In a moving garbage collector:
1. Object allocated at address 0x1000
2. Pointer stores 0x1000
3. GC runs, compacts heap
4. Object moved to 0x2000
5. Pointer still points to 0x1000 → **CRASH, not good**

Pinning prevents this:
1. Object allocated at address 0x1000
2. Pointer stores 0x1000
3. GC marks object as "pinned" (don't move)
4. GC runs, skips pinned objects
5. Object stays at 0x1000 → **SAFE**

### GC module API (`py/modgc.c`)

Exposed Python functions for GC control:

```python
gc.pin(obj)          # Pin object
gc.unpin(obj)        # Unpin object
gc.is_pinned(obj)    # Check if pinned
gc.obj_header_size() # Get object header size
```

**Implementation:**
```c
static mp_obj_t py_gc_pin(mp_obj_t obj) {
    gc_pin(MP_OBJ_TO_PTR(obj));
    return mp_const_none;
}

static mp_obj_t py_gc_is_pinned(mp_obj_t obj) {
    return mp_obj_new_bool(gc_is_pinned(MP_OBJ_TO_PTR(obj)));
}
```

### GC State Tracking

The GC maintains a set of pinned objects. From earlier analysis:

- Dynamic pinned table (entries tracked in `pinned_table_t`)
- Sorted insertion/removal for efficiency
- GC collection phase recognizes pinned objects
- Pinned objects allocated to right side of heap (avoid fragmentation)

## Type System Integration (`py/obj.h`, `py/mpstate.h`)

### Type Declaration

```c
// In obj.h header
extern const mp_obj_type_t mp_type_pointer;
```

**Type features:**
- No special methods (no `__add__`, `__str__`, etc.)
- Cannot be instantiated directly from Python
- Only created via `&` operator in compiled code

### VM State

```c
// In mpstate.h
typedef struct {
    // ... other fields
    // Pointer-related state can be added here
    // (currently integrated with GC system)
} mp_state_t;
```

## Scope Analysis

### Scope Tracking in Compiler

The compiler tracks variable scope to choose correct opcode:

```
Variable lookup:
  1. Local scope (function locals) → FAST opcodes (VINT)
  2. Global scope (module vars) → GLOBAL opcodes (QSTR)
  3. Closure scope (nested functions) → DEREF opcodes (VINT)
```

**For pointers:**
```c
// In compile_ptr_addr_of()
if (comp->pass == MP_PASS_SCOPE) {
    // First pass: register variable for scope analysis
    mp_emit_common_get_id_for_load(comp->scope_cur, qst);
} else {
    // Second pass: emit actual opcode based on scope
    mp_emit_common_id_op(
        comp->emit,
        &mp_emit_bc_method_table_address_of_ops,
        comp->scope_cur,
        qst
    );
}
```

### Two-Pass Compilation

```
Pass 1 (PASS_SCOPE):
  - Parse tree traversal
  - Identify all variable references
  - Build scope map
  
Pass 2 (PASS_EMIT):
  - Parse tree traversal again
  - Use scope info to choose opcode variant
  - Generate bytecode
```

This two-pass approach is necessary because:
- Opcode choice depends on variable location
- Variable location determined during scope analysis
- Can't emit code until scope is known

## Bytecode Encoding

### Argument Encoding

Pointer opcodes use variable-length argument encoding:

**VINT format (local indices):**
```
Single byte argument:        [OPCODE | ARG]
Multi-byte argument:         [OPCODE | ARG_LO | ARG_HI | ...]
```

Example: `ADDRESS_OF_FAST 100`
- Low 7 bits fit in one byte: `ADDRESS_OF_FAST | 100`
- Larger indices use continuation bytes

**QSTR format (string names):**
```
[OPCODE | QSTR_ID_LO | QSTR_ID_HI | ...]
```

Maps global variable names to interned strings for space efficiency.

## Stack Effects Summary

| Opcode | Input Stack | Output Stack | Notes |
|--------|-------------|--------------|-------|
| `ADDRESS_OF_FAST` | `...` | `... ptr` | Create pointer |
| `ADDRESS_OF_GLOBAL` | `...` | `... ptr` | Create pointer |
| `POINTER_DEREF_FAST` | `...` | `... val` | Load through ptr |
| `POINTER_DEREF_GLOBAL` | `...` | `... val` | Load through ptr |
| `POINTER_MEMBER_ACCESS` | `... ptr` | `... val` | Deref + load attr |
| `POINTER_ASSIGN_FAST` | `... val` | `...` | Store through ptr |
| `POINTER_ASSIGN_GLOBAL` | `... val` | `...` | Store through ptr |
| `POINTER_MEMBER_ASSIGN` | `... ptr val` | `...` | Store attr via ptr |

## Limitations & Design Decisions

### Why Only Simple Identifiers?

Pointers only work with simple variable references:
```python
x = 42
ptr = &x      # OK - simple variable

y = [1, 2, 3]
ptr = &y[0]   # ERROR - complex expression
```

**Reason:** Need to address variable storage location

- Simple variables have fixed locations (stack/globals)
- Expressions have temporary values (stack slots)
- No stable address for temporary values
- Would require complex copy-to-stack-frame logic

### Why Pinning in GC?

Alternative: Unmovable objects
- Less efficient GC
- Fragments heap
- Can't compact

Alternative: Update all pointers on move
- Complex GC implementation
- Runtime overhead tracking pointers

Chosen: Pinning
- Simple: Just mark object
- Efficient: Only affects pointed-to objects
- Backwards compatible: Standard GC still works

### Why Type Checking in VM?

Could skip validation for performance, but:
- Prevents crashes from corrupted memory
- Catches programmer errors early
- Minimal overhead (one type comparison)

## Performance Characteristics

### Operation Costs

| Operation | Cost | Notes |
|-----------|------|-------|
| `&var` | O(1) | Create pointer object, pin target |
| `*ptr` | O(1) | Type check + dereference |
| `ptr->member` | O(1) | Dereference + attr lookup |
| `*ptr = val` | O(1) | Type check + write + pinning |

### Memory Overhead

```
Per pointer object:
  - Object header: ~16 bytes (platform dependent)
  - Address field: 8 bytes (64-bit)
  - Total: ~24 bytes

Per pinned object:
  - GC pinning table entry: ~8 bytes per pinned object
```

## Debugging & Introspection

### Internal Functions

Available to VM developers:

```c
// Check if object is a pointer
bool mp_obj_is_type(mp_obj_t obj, const mp_obj_type_t *type)

// Get raw address from pointer
mp_obj_t *mp_obj_pointer_get(mp_obj_t ptr)

// Create pointer object
mp_obj_t mp_obj_new_pointer(mp_obj_t *addr)

// GC pinning
void gc_pin(void *ptr)
void gc_unpin(void *ptr)  
bool gc_is_pinned(void *ptr)
```

### Compiler Debug Output

Enable with `-DDEBUG` flag to see:
- Parse node structure
- Opcode emission
- Variable scope analysis
- Assignment kind determination

Example output:
```
DEBUG: Compiling PN_ptr_deref assignment, assign_kind=0
DEBUG: qst=123 (variable name index)
```

## Summary

The pointer system architecture separates concerns into:

1. **Syntax layer** - Lexer/parser recognize syntax, build AST
2. **IR layer** - Compiler converts AST to bytecode instructions
3. **Execution layer** - VM executes bytecode with safety checks
4. **Type layer** - Pointer object type with GC integration
5. **Memory layer** - GC manages pinning and lifetime

This modular design allows each layer to be tested independently while maintaining the contracts between layers.
