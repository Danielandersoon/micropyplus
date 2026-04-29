# Garbage Collector Backend: Sliding Compaction and Bidirectional Allocation

## Overview

MicroPyPlus uses a phase-driven sliding compactor integrated with bidirectional allocation.

Core goals:
1. reduce fragmentation by packing movable live objects toward the left frontier,
2. keep pinned/runtime-critical objects stable on the right side,
3. preserve pointer correctness by explicitly rewriting references and roots.

## High-Level Pipeline

The GC flow is:

1. allocation and mark,
2. forwarding plan,
3. sliding move,
4. reference rewrite,
5. root rewrite,
6. cleanup and frontier finalization.

In short, compaction is not an isolated pass. It is a coordinated set of passes that plan movement, perform movement, and then repair every reachable pointer path.

## Heap Model and Bidirectional Allocation

The heap has two active allocation frontiers:
- left side for normal movable allocations,
- right side for pinned allocations.

Important boundaries:
- last used from left,
- last used from right,
- total block count.

Primary invariant 
- left frontier **NEVER** crosses right frontier!!!

Why this matters:
- movable objects can be compacted leftward,
- pinned objects remain right-side and are never selected as move targets,
- compaction can safely increase contiguous free space between compacted live prefix and right-side pinned region.

## Sliding Compaction Structure

### Phase 1: Forwarding Plan

The planner scans live objects and builds a forwarding table with entries: old_block, new_block, n_blocks.

This is a key design point: each mapping includes the full object span, not only the head block.

Planner behavior:
1. scans live objects from left to right,
2. chooses legal destinations in free runs before source objects,
3. respects right-side bounds so destinations never intrude into reserved or pinned territory,
4. records identity mappings for objects that must not move.

Psudocode:
```python
build_forward_table():
    write_ptr = left_start
    for each live HEAD in scan order:
        n = object_block_span(head)
        if is_pinned(head): map(head -> head, n)
        else:
            dst = first_fit_free_run_before(head, n, right_frontier)
            map(head -> dst, n)
            write_ptr = max(write_ptr, dst + n)
```

### Phase 2: Sliding Move

Objects are relocated using the forwarding plan.

Move behavior:
1. block-count aware memmove style copy,
2. ATB entries rewritten to match new head and tail layout,
3. old layout metadata cleared as appropriate.

The move phase preserves object bytes; only addresses change.

Psudocode:
```python
slide_move():
    for each entry (old, new, n):
        if old == new: continue
        memmove(block_addr(new), block_addr(old), n * BLOCK_SIZE)
        set_atb_object(new, n)   // HEAD + TAILs
        clear_atb_object(old, n)
```

### Phase 3: Reference Rewrite

All relevant in-heap references are rewritten after movement.

Critical mechanism:
- pointer remap is range-based using old_block and n_blocks,
- stale interior pointers are matched against old object ranges,
- new pointer is computed as new base plus original interior offset.

This avoids relying on post-move ATB backtracking for stale pointers and fixes interior-address rewrite failures.

Typed rewrite coverage includes:
- dict map tables and dict key/value object fields,
- list item arrays,
- module constant object tables,
- function bytecode object internals,
- closure function and captured values,
- str and bytes internal data pointer,
- bytearray internal items pointer.

Important safety rule:
- mp_obj containers are rewritten only when entries are true heap objects,
- immediates and tagged values are preserved.

Psudocode:
```python
rewrite_refs():
    for each live object:
        rewrite_typed_fields(dict/list/module/fun/closure/str/bytes/bytearray)
        for each pointer-like field p:
            p = remap_by_range(p)

remap_by_range(p):
    clean, tag = untag(p), tagbits(p)
    for each (old, new, n):
        if clean in [addr(old), addr(old)+n*B):
            return retag(addr(new) + (clean - addr(old)), tag)
    return p
```

## Root Rewrite Structure

After object-internal rewriting, GC rewrites root paths.

Coverage includes:
- root pointer section in runtime state,
- thread globals and locals roots,
- active code state fields and state slots,
- qstr-related roots and pool chains.

qstr traversal rule:
- traversal advances using rewritten links, not cached pre-rewrite links,
- this prevents stale-chain traversal after movement.
Psudocode:
```python
rewrite_roots():
    rewrite_root_range(mp_state_ctx roots)
    rewrite(thread.dict_locals, thread.dict_globals)
    rewrite_code_state_chain(fun_bc, ip, sp, old_globals, frame, state[])
    rewrite_qstr_roots(last_pool, qstr_last_chunk)
    for pool in qstr_chain:      // advance via rewritten prev
        rewrite(pool.prev, pool.hashes, pool.lengths, pool.qstrs[])
```

## Free Dirty Blocks and Finalise Frontier

The cleanup phase normalizes post-compaction allocation metadata and frees unreachable regions.

Outcomes:
- mark-state remnants converted to consistent allocation-state representation,
- garbage/dirty blocks released,
- compact live prefix + contiguous free tail restored,
- left and right frontier updated to current heap state.

Psudocode:
```python
finalize_heap():
    normalize_mark_state_to_final_atb()
    free_unreachable_blocks()
    gc_last_used_block_from_left  = compacted_left_end
    gc_last_used_block_from_right = pinned_right_start
    assert(left_frontier <= right_frontier)
```

## Pinned Object Safety Model

Pinned objects are preserved by design:
- planner does not target pinned ranges for relocation,
- mover does not move pinned objects,
- right-side allocation strategy reduces interference with left compaction.

This protects pointer-sensitive runtime allocations that require address stability.

## Correctness Issues Addressed by the Current Design

The current structure specifically addresses prior corruption classes:

1. stale interior pointer rewrite errors,
2. accidental rewrite of non-object immediates in mp_obj_t containers,
3. stale payload pointers in moved string-like objects,
4. stale qstr pool chain traversal due to pre-rewrite next pointer usage.

## Current Validation Status

## Practical Summary

The GC backend is a true sliding compactor with explicit planning and explicit repair passes.

What is new in practice:
- movement is planned with span-aware forwarding entries,
- movement and metadata rewrite are deterministic,
- pointer repair is typed and mp_obj aware,
- root repair covers runtime and qstr chains,
- cleanup returns a compacted heap with improved contiguous free capacity.
