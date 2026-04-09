/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 * Copyright (c) 2014 Paul Sokolovsky
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "py/gc.h"
#include "py/runtime.h"
#include "py/bc.h"

#if MICROPY_DEBUG_VALGRIND
#include <valgrind/memcheck.h>
#endif

#if MICROPY_ENABLE_GC

#if MICROPY_DEBUG_VERBOSE // print debugging info
#define DEBUG_PRINT (1)
#define DEBUG_printf DEBUG_printf
#else // don't print debugging info
#define DEBUG_PRINT (0)
#define DEBUG_printf(...) (void)0
#endif

// make this 1 to dump the heap each time it changes
#define EXTENSIVE_HEAP_PROFILING (0)

// make this 1 to zero out swept memory to more eagerly
// detect untraced object still in use
#define CLEAR_ON_SWEEP (0)

#define WORDS_PER_BLOCK ((MICROPY_BYTES_PER_GC_BLOCK) / MP_BYTES_PER_OBJ_WORD)
#define BYTES_PER_BLOCK (MICROPY_BYTES_PER_GC_BLOCK)

// ATB = allocation table byte
// 0b00 = FREE -- free block
// 0b01 = HEAD -- head of a chain of blocks
// 0b10 = TAIL -- in the tail of a chain of blocks
// 0b11 = MARK -- marked head block

#define AT_FREE (0)
#define AT_HEAD (1)
#define AT_TAIL (2)
#define AT_MARK (3)

#define BLOCKS_PER_ATB (4)
#define ATB_MASK_0 (0x03)
#define ATB_MASK_1 (0x0c)
#define ATB_MASK_2 (0x30)
#define ATB_MASK_3 (0xc0)

#define ATB_0_IS_FREE(a) (((a) & ATB_MASK_0) == 0)
#define ATB_1_IS_FREE(a) (((a) & ATB_MASK_1) == 0)
#define ATB_2_IS_FREE(a) (((a) & ATB_MASK_2) == 0)
#define ATB_3_IS_FREE(a) (((a) & ATB_MASK_3) == 0)

#if MICROPY_GC_SPLIT_HEAP
#define NEXT_AREA(area) ((area)->next)
#else
#define NEXT_AREA(area) (NULL)
#endif

#define BLOCK_SHIFT(block) (2 * ((block) & (BLOCKS_PER_ATB - 1)))
#define ATB_GET_KIND(area, block) (((area)->gc_alloc_table_start[(block) / BLOCKS_PER_ATB] >> BLOCK_SHIFT(block)) & 3)
#define ATB_ANY_TO_FREE(area, block) do { area->gc_alloc_table_start[(block) / BLOCKS_PER_ATB] &= (~(AT_MARK << BLOCK_SHIFT(block))); } while (0)
#define ATB_FREE_TO_HEAD(area, block) do { area->gc_alloc_table_start[(block) / BLOCKS_PER_ATB] |= (AT_HEAD << BLOCK_SHIFT(block)); } while (0)
#define ATB_FREE_TO_TAIL(area, block) do { area->gc_alloc_table_start[(block) / BLOCKS_PER_ATB] |= (AT_TAIL << BLOCK_SHIFT(block)); } while (0)
#define ATB_HEAD_TO_MARK(area, block) do { area->gc_alloc_table_start[(block) / BLOCKS_PER_ATB] |= (AT_MARK << BLOCK_SHIFT(block)); } while (0)
#define ATB_MARK_TO_HEAD(area, block) do { area->gc_alloc_table_start[(block) / BLOCKS_PER_ATB] &= (~(AT_TAIL << BLOCK_SHIFT(block))); } while (0)
#define ATB_MARK_TO_TAIL(area, block) do { area->gc_alloc_table_start[(block) / BLOCKS_PER_ATB] &= (~(AT_HEAD << BLOCK_SHIFT(block))); } while (0)

#define BLOCK_FROM_PTR(area, ptr) (((byte *)(ptr) - area->gc_pool_start) / BYTES_PER_BLOCK)
#define PTR_FROM_BLOCK(area, block) (((block) * BYTES_PER_BLOCK + (uintptr_t)area->gc_pool_start))

// After the ATB, there must be a byte filled with AT_FREE so that gc_mark_tree
// cannot erroneously conclude that a block extends past the end of the GC heap
// due to bit patterns in the FTB (or first block, if finalizers are disabled)
// being interpreted as AT_TAIL.
#define ALLOC_TABLE_GAP_BYTE (1)

#if MICROPY_ENABLE_FINALISER
// FTB = finaliser table byte
// if set, then the corresponding block may have a finaliser

#define BLOCKS_PER_FTB (8)

#define FTB_GET(area, block) ((area->gc_finaliser_table_start[(block) / BLOCKS_PER_FTB] >> ((block) & 7)) & 1)
#define FTB_SET(area, block) do { area->gc_finaliser_table_start[(block) / BLOCKS_PER_FTB] |= (1 << ((block) & 7)); } while (0)
#define FTB_CLEAR(area, block) do { area->gc_finaliser_table_start[(block) / BLOCKS_PER_FTB] &= (~(1 << ((block) & 7))); } while (0)
#endif

#if MICROPY_PY_THREAD && !MICROPY_PY_THREAD_GIL
#define GC_MUTEX_INIT() mp_thread_recursive_mutex_init(&MP_STATE_MEM(gc_mutex))
#define GC_ENTER() mp_thread_recursive_mutex_lock(&MP_STATE_MEM(gc_mutex), 1)
#define GC_EXIT() mp_thread_recursive_mutex_unlock(&MP_STATE_MEM(gc_mutex))
#else
// Either no threading, or assume callers to gc_collect() hold the GIL
#define GC_MUTEX_INIT()
#define GC_ENTER()
#define GC_EXIT()
#endif

// Static functions for individual steps of the GC mark/sweep sequence
static void gc_collect_start_common(void);
static void *gc_get_ptr(void **ptrs, int i);
#if MICROPY_GC_SPLIT_HEAP
static void gc_mark_subtree(mp_state_mem_area_t *area, size_t block);
#else
static void gc_mark_subtree(size_t block);
#endif
static void gc_deal_with_stack_overflow(void);
static void gc_sweep_run_finalisers(void);
static void gc_sweep_free_blocks(void);
static void gc_compute_forwarding_addresses(mp_state_mem_area_t *area, gc_forward_table_t *forward_table);
static void gc_compact_copy(mp_state_mem_area_t *area, gc_forward_table_t *forward_table);
static void gc_update_references(mp_state_mem_area_t *area, gc_forward_table_t *forward_table);
static void gc_update_roots(mp_state_mem_area_t *area, gc_forward_table_t *forward_table);
static void gc_shift_left_frontier(mp_state_mem_area_t *area);
static void _gc_update_dict_values(mp_obj_dict_t *dict, mp_state_mem_area_t *area, gc_forward_table_t *forward_table);
//static size_t gc_count_marked_blocks(mp_state_mem_area_t *area);
static size_t gc_count_live_objects(mp_state_mem_area_t *area);
static bool gc_forward_table_prealloc(gc_forward_table_t *table, size_t size);


// Pinned objects tracking
static pinned_table_t gc_pinned_table = {NULL, 0, 0};

// Initial capacity for pinned table (will grow dynamically)
#define PINNED_TABLE_INITIAL_CAPACITY (32)

// Helper function to find pinned range by block (binary search would be optimal for larger tables)
static ssize_t gc_pinned_find_range_by_block(size_t block) {
    for (ssize_t i = 0; i < (ssize_t)gc_pinned_table.count; i++) {
        pinned_range_t *range = &gc_pinned_table.ranges[i];
        if (block >= range->block_start && block < range->block_start + range->block_count) {
            return i;
        }
    }
    return -1;
}

// Helper function to find range by pointer
static ssize_t gc_pinned_find_range_by_ptr(const void *ptr) {
    for (ssize_t i = 0; i < (ssize_t)gc_pinned_table.count; i++) {
        if (gc_pinned_table.ranges[i].obj == ptr) {
            return i;
        }
    }
    return -1;
}

// TODO waste less memory; currently requires that all entries in alloc_table have a corresponding block in pool
static void gc_setup_area(mp_state_mem_area_t *area, void *start, void *end) {
    // calculate parameters for GC (T=total, A=alloc table, F=finaliser table, P=pool; all in bytes):
    // T = A + F + P
    //     F = A * BLOCKS_PER_ATB / BLOCKS_PER_FTB
    //     P = A * BLOCKS_PER_ATB * BYTES_PER_BLOCK
    // => T = A * (1 + BLOCKS_PER_ATB / BLOCKS_PER_FTB + BLOCKS_PER_ATB * BYTES_PER_BLOCK)
    size_t total_byte_len = (byte *)end - (byte *)start;
    #if MICROPY_ENABLE_FINALISER
    area->gc_alloc_table_byte_len = (total_byte_len - ALLOC_TABLE_GAP_BYTE)
        * MP_BITS_PER_BYTE
        / (
            MP_BITS_PER_BYTE
            + MP_BITS_PER_BYTE * BLOCKS_PER_ATB / BLOCKS_PER_FTB
            + MP_BITS_PER_BYTE * BLOCKS_PER_ATB * BYTES_PER_BLOCK
            );
    #else
    area->gc_alloc_table_byte_len = (total_byte_len - ALLOC_TABLE_GAP_BYTE) / (1 + MP_BITS_PER_BYTE / 2 * BYTES_PER_BLOCK);
    #endif

    area->gc_alloc_table_start = (byte *)start;

    #if MICROPY_ENABLE_FINALISER
    size_t gc_finaliser_table_byte_len = (area->gc_alloc_table_byte_len * BLOCKS_PER_ATB + BLOCKS_PER_FTB - 1) / BLOCKS_PER_FTB;
    area->gc_finaliser_table_start = area->gc_alloc_table_start + area->gc_alloc_table_byte_len + ALLOC_TABLE_GAP_BYTE;
    #endif

    size_t gc_pool_block_len = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    area->gc_pool_start = (byte *)end - gc_pool_block_len * BYTES_PER_BLOCK;
    area->gc_pool_end = end;

    #if MICROPY_ENABLE_FINALISER
    assert(area->gc_pool_start >= area->gc_finaliser_table_start + gc_finaliser_table_byte_len);
    #endif

    #if MICROPY_ENABLE_FINALISER
    // clear ATB's and FTB's
    memset(area->gc_alloc_table_start, 0, gc_finaliser_table_byte_len + area->gc_alloc_table_byte_len + ALLOC_TABLE_GAP_BYTE);
    #else
    // clear ATB's
    memset(area->gc_alloc_table_start, 0, area->gc_alloc_table_byte_len + ALLOC_TABLE_GAP_BYTE);
    #endif

    area->gc_last_free_atb_index = 0;
    area->gc_last_used_block = 0;

    area->gc_last_used_block_from_left = (size_t)-1;
    area->gc_last_used_block_from_right = gc_pool_block_len;
    area->gc_num_blocks = gc_pool_block_len;

    #if MICROPY_GC_SPLIT_HEAP
    area->next = NULL;
    #endif

    DEBUG_printf("GC layout:\n");
    DEBUG_printf("  alloc table at %p, length " UINT_FMT " bytes, "
        UINT_FMT " blocks\n",
        area->gc_alloc_table_start, area->gc_alloc_table_byte_len,
        area->gc_alloc_table_byte_len * BLOCKS_PER_ATB);
    #if MICROPY_ENABLE_FINALISER
    DEBUG_printf("  finaliser table at %p, length " UINT_FMT " bytes, "
        UINT_FMT " blocks\n", area->gc_finaliser_table_start,
        gc_finaliser_table_byte_len,
        gc_finaliser_table_byte_len * BLOCKS_PER_FTB);
    #endif
    DEBUG_printf("  pool at %p, length " UINT_FMT " bytes, "
        UINT_FMT " blocks\n", area->gc_pool_start,
        gc_pool_block_len * BYTES_PER_BLOCK, gc_pool_block_len);
}

void gc_init(void *start, void *end) {
    // align end pointer on block boundary
    end = (void *)((uintptr_t)end & (~(BYTES_PER_BLOCK - 1)));
    DEBUG_printf("Initializing GC heap: %p..%p = " UINT_FMT " bytes\n", start, end, (byte *)end - (byte *)start);

    gc_setup_area(&MP_STATE_MEM(area), start, end);

    // set last free ATB index to start of heap
    #if MICROPY_GC_SPLIT_HEAP
    MP_STATE_MEM(gc_last_free_area) = &MP_STATE_MEM(area);
    #endif

    // unlock the GC
    MP_STATE_THREAD(gc_lock_depth) = 0;

    // allow auto collection
    MP_STATE_MEM(gc_auto_collect_enabled) = 1;

    #if MICROPY_GC_ALLOC_THRESHOLD
    // by default, maxuint for gc threshold, effectively turning gc-by-threshold off
    MP_STATE_MEM(gc_alloc_threshold) = (size_t)-1;
    MP_STATE_MEM(gc_alloc_amount) = 0;
    #endif

    // Initialize pinned table
    gc_pinned_table.ranges = NULL;
    gc_pinned_table.count = 0;
    gc_pinned_table.capacity = 0;

    GC_MUTEX_INIT();
}

#if MICROPY_GC_SPLIT_HEAP
void gc_add(void *start, void *end) {
    // Place the area struct at the start of the area.
    mp_state_mem_area_t *area = (mp_state_mem_area_t *)start;
    start = (void *)((uintptr_t)start + sizeof(mp_state_mem_area_t));

    end = (void *)((uintptr_t)end & (~(BYTES_PER_BLOCK - 1)));
    DEBUG_printf("Adding GC heap: %p..%p = " UINT_FMT " bytes\n", start, end, (byte *)end - (byte *)start);

    // Init this area
    gc_setup_area(area, start, end);

    // Find the last registered area in the linked list
    mp_state_mem_area_t *prev_area = &MP_STATE_MEM(area);
    while (prev_area->next != NULL) {
        prev_area = prev_area->next;
    }

    // Add this area to the linked list
    prev_area->next = area;
}

#if MICROPY_GC_SPLIT_HEAP_AUTO
// Try to automatically add a heap area large enough to fulfill 'failed_alloc'.
static bool gc_try_add_heap(size_t failed_alloc) {
    // 'needed' is the size of a heap large enough to hold failed_alloc, with
    // the additional metadata overheads as calculated in gc_setup_area().
    //
    // Rather than reproduce all of that logic here, we approximate that adding
    // (13/512) is enough overhead for sufficiently large heap areas (the
    // overhead converges to 3/128, but there's some fixed overhead and some
    // rounding up of partial block sizes).
    size_t needed = failed_alloc + MAX(2048, failed_alloc * 13 / 512);

    size_t avail = gc_get_max_new_split();

    DEBUG_printf("gc_try_add_heap failed_alloc " UINT_FMT ", "
        "needed " UINT_FMT ", avail " UINT_FMT " bytes \n",
        failed_alloc,
        needed,
        avail);

    if (avail < needed) {
        // Can't fit this allocation, or system heap has nearly run out anyway
        return false;
    }

    // Deciding how much to grow the total heap by each time is tricky:
    //
    // - Grow by too small amounts, leads to heap fragmentation issues.
    //
    // - Grow by too large amounts, may lead to system heap running out of
    //   space.
    //
    // Currently, this implementation is:
    //
    // - At minimum, aim to double the total heap size each time we add a new
    //   heap.  i.e. without any large single allocations, total size will be
    //   64KB -> 128KB -> 256KB -> 512KB -> 1MB, etc
    //
    // - If the failed allocation is too large to fit in that size, the new
    //   heap is made exactly large enough for that allocation. Future growth
    //   will double the total heap size again.
    //
    // - If the new heap won't fit in the available free space, add the largest
    //   new heap that will fit (this may lead to failed system heap allocations
    //   elsewhere, but some allocation will likely fail in this circumstance!)

    // Compute total number of blocks in the current heap.
    size_t total_blocks = 0;
    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area);
         area != NULL;
         area = NEXT_AREA(area)) {
        total_blocks += area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    }

    // Compute bytes needed to build a heap with total_blocks blocks.
    size_t total_heap =
        total_blocks / BLOCKS_PER_ATB
        #if MICROPY_ENABLE_FINALISER
        + total_blocks / BLOCKS_PER_FTB
        #endif
        + total_blocks * BYTES_PER_BLOCK
        + ALLOC_TABLE_GAP_BYTE
        + sizeof(mp_state_mem_area_t);

    // Round up size to the nearest multiple of BYTES_PER_BLOCK.
    total_heap = (total_heap + BYTES_PER_BLOCK - 1) & (~(BYTES_PER_BLOCK - 1));

    DEBUG_printf("total_heap " UINT_FMT " bytes\n", total_heap);

    size_t to_alloc = MIN(avail, MAX(total_heap, needed));

    mp_state_mem_area_t *new_heap = MP_PLAT_ALLOC_HEAP(to_alloc);

    DEBUG_printf("MP_PLAT_ALLOC_HEAP " UINT_FMT " = %p\n",
        to_alloc, new_heap);

    if (new_heap == NULL) {
        // This should only fail:
        // - In a threaded environment if another thread has
        //   allocated while this function ran.
        // - If there is a bug in gc_get_max_new_split().
        return false;
    }

    gc_add(new_heap, (void *)new_heap + to_alloc);

    return true;
}
#endif

#endif

void gc_lock(void) {
    // This does not need to be atomic or have the GC mutex because:
    // - each thread has its own gc_lock_depth so there are no races between threads;
    // - a hard interrupt will only change gc_lock_depth during its execution, and
    //   upon return will restore the value of gc_lock_depth.
    MP_STATE_THREAD(gc_lock_depth) += (1 << GC_LOCK_DEPTH_SHIFT);
}

void gc_unlock(void) {
    // This does not need to be atomic, See comment above in gc_lock.
    MP_STATE_THREAD(gc_lock_depth) -= (1 << GC_LOCK_DEPTH_SHIFT);
}

bool gc_is_locked(void) {
    return MP_STATE_THREAD(gc_lock_depth) != 0;
}

#if MICROPY_GC_SPLIT_HEAP
// Returns the area to which this pointer belongs, or NULL if it isn't
// allocated on the GC-managed heap.
static inline mp_state_mem_area_t *gc_get_ptr_area(const void *ptr) {
    if (((uintptr_t)(ptr) & (BYTES_PER_BLOCK - 1)) != 0) {   // must be aligned on a block
        return NULL;
    }
    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        if (ptr >= (void *)area->gc_pool_start   // must be above start of pool
            && ptr < (void *)area->gc_pool_end) {   // must be below end of pool
            return area;
        }
    }
    return NULL;
}
#endif

// ptr should be of type void*
#define VERIFY_PTR(ptr) ( \
    ((uintptr_t)(ptr) & (BYTES_PER_BLOCK - 1)) == 0          /* must be aligned on a block */ \
    && ptr >= (void *)MP_STATE_MEM(area).gc_pool_start      /* must be above start of pool */ \
    && ptr < (void *)MP_STATE_MEM(area).gc_pool_end         /* must be below end of pool */ \
    )

#ifndef TRACE_MARK
#if DEBUG_PRINT
#define TRACE_MARK(block, ptr) DEBUG_printf("gc_mark(%p)\n", ptr)
#else
#define TRACE_MARK(block, ptr)
#endif
#endif

void gc_collect_start(void) {
    gc_collect_start_common();
    #if MICROPY_GC_ALLOC_THRESHOLD
    MP_STATE_MEM(gc_alloc_amount) = 0;
    #endif

    // Pin all active execution frames. If a frame moves, the local C variables 
    // (stack ptr, prog counter) in mp_execute_bytecode will become invalid.
    #if MICROPY_PY_SYS_SETTRACE
    for (mp_code_state_t *cs = MP_STATE_THREAD(current_code_state); cs != NULL; ) {
        gc_pin(cs);
        #if MICROPY_STACKLESS
        cs = cs->prev;
        #else
        cs = cs->prev_state;
        #endif
    }
    #endif

    // Trace root pointers.  This relies on the root pointers being organised
    // correctly in the mp_state_ctx structure.  We scan nlr_top, dict_locals,
    // dict_globals, then the root pointer section of mp_state_vm.
    void **ptrs = (void **)(void *)&mp_state_ctx;
    size_t root_start = offsetof(mp_state_ctx_t, thread.dict_locals);
    size_t root_end = offsetof(mp_state_ctx_t, vm.qstr_last_chunk);
    gc_collect_root(ptrs + root_start / sizeof(void *), (root_end - root_start) / sizeof(void *));

    #if MICROPY_ENABLE_PYSTACK
    // Objects on the Python stack are often accessed via raw pointers in C functions.
    // These must be pinned during a compaction cycle to prevent stale stack references.
    ptrs = (void **)(void *)MP_STATE_THREAD(pystack_start);
    size_t pystack_len = (MP_STATE_THREAD(pystack_cur) - MP_STATE_THREAD(pystack_start)) / sizeof(void *);
    for (size_t i = 0; i < pystack_len; i++) {
        if (ptrs[i] >= (void*)MP_STATE_MEM(area).gc_pool_start && ptrs[i] < (void*)MP_STATE_MEM(area).gc_pool_end) {
            gc_pin(ptrs[i]);
        }
    }
    gc_collect_root(ptrs, pystack_len);
    #endif
}

static void gc_collect_start_common(void) {
    GC_ENTER();
    assert((MP_STATE_THREAD(gc_lock_depth) & GC_COLLECT_FLAG) == 0);
    MP_STATE_THREAD(gc_lock_depth) |= GC_COLLECT_FLAG;
    MP_STATE_MEM(gc_stack_overflow) = 0;
}

void gc_collect_root(void **ptrs, size_t len) {
    #if !MICROPY_GC_SPLIT_HEAP
    mp_state_mem_area_t *area = &MP_STATE_MEM(area);
    #endif
    for (size_t i = 0; i < len; i++) {
        MICROPY_GC_HOOK_LOOP(i);
        void *ptr = gc_get_ptr(ptrs, i);
        #if MICROPY_GC_SPLIT_HEAP
        mp_state_mem_area_t *area = gc_get_ptr_area(ptr);
        if (!area) {
            continue;
        }
        #else
        if (!VERIFY_PTR(ptr)) {
            continue;
        }
        #endif
        size_t block = BLOCK_FROM_PTR(area, ptr);
        if (ATB_GET_KIND(area, block) == AT_HEAD) {
            // An unmarked head: mark it, and mark all its children
            ATB_HEAD_TO_MARK(area, block);
            #if MICROPY_GC_SPLIT_HEAP
            gc_mark_subtree(area, block);
            #else
            gc_mark_subtree(block);
            #endif
        }
    }
}

// Take the given block as the topmost block on the stack. Check all it's
// children: mark the unmarked child blocks and put those newly marked
// blocks on the stack. When all children have been checked, pop off the
// topmost block on the stack and repeat with that one.
#if MICROPY_GC_SPLIT_HEAP
static void gc_mark_subtree(mp_state_mem_area_t *area, size_t block)
#else
static void gc_mark_subtree(size_t block)
#endif
{
    // Start with the block passed in the argument.
    size_t sp = 0;
    for (;;) {
        #if !MICROPY_GC_SPLIT_HEAP
        mp_state_mem_area_t *area = &MP_STATE_MEM(area);
        #endif

        // work out number of consecutive blocks in the chain starting with this one
        size_t n_blocks = 0;
        do {
            n_blocks += 1;
        } while (ATB_GET_KIND(area, block + n_blocks) == AT_TAIL);

        // check that the consecutive blocks didn't overflow past the end of the area
        assert(area->gc_pool_start + (block + n_blocks) * BYTES_PER_BLOCK <= area->gc_pool_end);

        // check this block's children
        void **ptrs = (void **)PTR_FROM_BLOCK(area, block);
        for (size_t i = n_blocks * BYTES_PER_BLOCK / sizeof(void *); i > 0; i--, ptrs++) {
            MICROPY_GC_HOOK_LOOP(i);
            void *ptr = *ptrs;
            // If this is a heap pointer that hasn't been marked, mark it and push
            // it's children to the stack.
            #if MICROPY_GC_SPLIT_HEAP
            mp_state_mem_area_t *ptr_area = gc_get_ptr_area(ptr);
            if (!ptr_area) {
                // Not a heap-allocated pointer (might even be random data).
                continue;
            }
            #else
            if (!VERIFY_PTR(ptr)) {
                continue;
            }
            mp_state_mem_area_t *ptr_area = area;
            #endif
            size_t ptr_block = BLOCK_FROM_PTR(ptr_area, ptr);
            if (ATB_GET_KIND(ptr_area, ptr_block) != AT_HEAD) {
                // This block is already marked.
                continue;
            }
            // An unmarked head. Mark it, and push it on gc stack.
            TRACE_MARK(ptr_block, ptr);
            ATB_HEAD_TO_MARK(ptr_area, ptr_block);
            if (sp < MICROPY_ALLOC_GC_STACK_SIZE) {
                MP_STATE_MEM(gc_block_stack)[sp] = ptr_block;
                #if MICROPY_GC_SPLIT_HEAP
                MP_STATE_MEM(gc_area_stack)[sp] = ptr_area;
                #endif
                sp += 1;
            } else {
                MP_STATE_MEM(gc_stack_overflow) = 1;
            }
        }

        // Are there any blocks on the stack?
        if (sp == 0) {
            break; // No, stack is empty, we're done.
        }

        // pop the next block off the stack
        sp -= 1;
        block = MP_STATE_MEM(gc_block_stack)[sp];
        #if MICROPY_GC_SPLIT_HEAP
        area = MP_STATE_MEM(gc_area_stack)[sp];
        #endif
    }
}

void gc_sweep_all(void) {
    gc_collect_start_common();
    gc_collect_end();
}

static bool gc_should_compact(void) {
    #if MICROPY_VARIANT_BACKWARD_COMPATIBLE
    return false;
    #else
    return true;
    #endif
}

// Shift the left frontier to reflect the actual compacted heap state
static void gc_shift_left_frontier(mp_state_mem_area_t *area) {
    DEBUG_printf("gc_shift_left_frontier: before shift, left frontier at block " UINT_FMT "\n", 
                 area->gc_last_used_block_from_left);
    
    // Scan from block 0 rightward, find the highest AT_HEAD or AT_TAIL block
    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    size_t new_frontier = 0;
    
    for (size_t block = 0; block < max_block; block++) {
        byte block_kind = ATB_GET_KIND(area, block);
        if (block_kind != AT_FREE) {
            new_frontier = block + 1;  // Frontier is one past the last used block
        }
    }
    
    area->gc_last_used_block_from_left = new_frontier;
    area->gc_last_used_block = new_frontier - 1;  
    DEBUG_printf("gc_shift_left_frontier: after shift, left frontier at block " UINT_FMT "\n", 
                 new_frontier);

    {
        size_t max_block_val = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
        size_t head_count = 0, tail_count = 0, free_count = 0, mark_count = 0;
        for (size_t block = 0; block < max_block_val; block++) {
            byte kind = ATB_GET_KIND(area, block);
            if (kind == AT_HEAD) head_count++;
            else if (kind == AT_TAIL) tail_count++;
            else if (kind == AT_FREE) free_count++;
            else if (kind == AT_MARK) mark_count++;
        }
        DEBUG_printf("gc_shift_left_frontier: ATB state - HEAD=" UINT_FMT ", TAIL=" UINT_FMT ", FREE=" UINT_FMT ", MARK=" UINT_FMT "\n",
                     head_count, tail_count, free_count, mark_count);
    }
}

// Function to free forward table
void gc_forward_table_free(gc_forward_table_t *table) {
    if (table->entries != NULL) {
        free(table->entries);
        table->entries = NULL;
    }
    table->count = 0;
    table->capacity = 0;
}

// Verification: Ensure no AT_MARK bits remain and all bits past frontier are AT_FREE
static void gc_verify_compacted_heap(mp_state_mem_area_t *area) {
    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    size_t frontier = area->gc_last_used_block;
    
    for (size_t block = 0; block < max_block; block++) {
        byte kind = ATB_GET_KIND(area, block);
        if (kind == AT_MARK) {
            DEBUG_printf("VERIFY ERROR: Block " UINT_FMT " is still AT_MARK after compaction!\n", block);
        }
        if (block > frontier && kind != AT_FREE && block < area->gc_last_used_block_from_right) {
            DEBUG_printf("VERIFY ERROR: Block " UINT_FMT " (past frontier " UINT_FMT ") is not FREE! (kind=%d)\n", block, frontier, kind);
        }
    }
}

// Dump all occupied blocks in the heap after compaction
static void gc_dump_occupied_blocks(mp_state_mem_area_t *area) {
    #if MICROPY_DEBUG_VERBOSE
    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    DEBUG_printf("\n=== HEAP DUMP AFTER COMPACTION ===\n");
    
    size_t block = 0;
    while (block < max_block) {
        byte kind = ATB_GET_KIND(area, block);
        if (kind == AT_HEAD) {
            // Found a HEAD block, count consecutive TAIL blocks
            void *addr = (void *)(area->gc_pool_start + block * BYTES_PER_BLOCK);
            size_t block_count = 1;
            size_t temp_block = block + 1;
            while (temp_block < max_block && ATB_GET_KIND(area, temp_block) == AT_TAIL) {
                block_count++;
                temp_block++;
            }
            DEBUG_printf("Block " UINT_FMT " at %p: " UINT_FMT " bytes (%d tail blocks)\n", 
                         block, addr, block_count * BYTES_PER_BLOCK, (int)(block_count - 1));
            block = temp_block;
        } else if (kind == AT_TAIL) {
            // Orphaned TAIL without HEAD (should not happen)
            void *addr = (void *)(area->gc_pool_start + block * BYTES_PER_BLOCK);
            DEBUG_printf("Block " UINT_FMT " at %p: ORPHAN TAIL!\n", block, addr);
            block++;
        } else if (kind == AT_MARK) {
            // Marked block (unused after compaction)
            void *addr = (void *)(area->gc_pool_start + block * BYTES_PER_BLOCK);
            DEBUG_printf("Block " UINT_FMT " at %p: MARK (should be 0!)\n", block, addr);
            block++;
        } else {
            // FREE block, skip to next occupied
            block++;
        }
    }
    DEBUG_printf("=== END HEAP DUMP ===\n\n");
    #endif
}

void gc_collect_end(void) {
    DEBUG_printf("gc_collect_end: ENTER\n");
    gc_deal_with_stack_overflow();
    DEBUG_printf("gc_collect_end: dealt with stack overflow\n");

    bool compaction_performed = gc_should_compact();
    if (compaction_performed) {
        DEBUG_printf("gc_collect_end: starting compaction\n");
        for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
            // Phase 0: Count live objects after sweep and pre-allocate forward table
            size_t live_count = gc_count_live_objects(area);
            DEBUG_printf("gc_collect_end: counted " UINT_FMT " live objects, pre-allocating forward table\n", live_count);
            
            gc_forward_table_t forward_table = {0};
            if (!gc_forward_table_prealloc(&forward_table, live_count)) {
                DEBUG_printf("gc_collect_end: failed to pre-allocate forward table\n");
                continue;
            }
            
            DEBUG_printf("gc_collect_end: Phase 1 - computing forwarding addresses\n");
            gc_compute_forwarding_addresses(area, &forward_table);
            DEBUG_printf("gc_collect_end: Phase 2 - copying objects\n");
            gc_compact_copy(area, &forward_table);
            DEBUG_printf("gc_collect_end: Phase 3 - updating references\n");
            gc_update_references(area, &forward_table);
            DEBUG_printf("gc_collect_end: Phase 4 - updating roots\n");            
            gc_update_roots(area, &forward_table);
            DEBUG_printf("gc_collect_end: Phase 5 - shifting left frontier\n");
            gc_shift_left_frontier(area);
            DEBUG_printf("gc_collect_end: compaction phases complete, freeing forward table\n");           
            gc_forward_table_free(&forward_table);
            
            // Dump all occupied blocks to see what's in heap after compaction
            gc_dump_occupied_blocks(area);
            gc_verify_compacted_heap(area);
        }
    }

    DEBUG_printf("gc_collect_end: running finalisers\n");
    gc_sweep_run_finalisers();
    
    DEBUG_printf("gc_collect_end: freeing blocks (Sweep)\n");
    gc_sweep_free_blocks();
    
    // Perform compaction on the "clean" heap
    if (compaction_performed) {
        for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
            // Shift the left frontier and update allocator state based on compacted positions
            gc_shift_left_frontier(area);

            // Set gc_last_free_atb_index to scan starting right after the compacted area
            size_t first_free_block = area->gc_last_used_block + 1;
            area->gc_last_free_atb_index = first_free_block / BLOCKS_PER_ATB;

            DEBUG_printf("gc_collect_end: set gc_last_free_atb_index to " UINT_FMT " (first free block " UINT_FMT ")\n", 
                         area->gc_last_free_atb_index, first_free_block);
            break;
        }
    }
    
    DEBUG_printf("gc_collect_end: blocks freed, clearing GC flag\n");
    
    // Clear the GC collect flag before exiting
    MP_STATE_THREAD(gc_lock_depth) &= ~GC_COLLECT_FLAG;
    
    DEBUG_printf("gc_collect_end: EXIT\n");
    GC_EXIT();
}

static void gc_deal_with_stack_overflow(void) {
    while (MP_STATE_MEM(gc_stack_overflow)) {
        MP_STATE_MEM(gc_stack_overflow) = 0;

        // scan entire memory looking for blocks which have been marked but not their children
        for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
            for (size_t block = 0; block < area->gc_alloc_table_byte_len * BLOCKS_PER_ATB; block++) {
                MICROPY_GC_HOOK_LOOP(block);
                // trace (again) if mark bit set
                if (ATB_GET_KIND(area, block) == AT_MARK) {
                    #if MICROPY_GC_SPLIT_HEAP
                    gc_mark_subtree(area, block);
                    #else
                    gc_mark_subtree(block);
                    #endif
                }
            }
        }
    }
}

// Run finalisers for all to-be-freed blocks
static void gc_sweep_run_finalisers(void) {
    #if MICROPY_ENABLE_FINALISER
    for (const mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        assert(area->gc_last_used_block <= area->gc_alloc_table_byte_len * BLOCKS_PER_ATB);
        // Small speed optimisation: skip over empty FTB blocks
        size_t ftb_end = area->gc_last_used_block / BLOCKS_PER_FTB; // index is inclusive
        for (size_t ftb_idx = 0; ftb_idx <= ftb_end; ftb_idx++) {
            byte ftb = area->gc_finaliser_table_start[ftb_idx];
            size_t block = ftb_idx * BLOCKS_PER_FTB;
            while (ftb) {
                MICROPY_GC_HOOK_LOOP(block);
                if (ftb & 1) { // FTB_GET(area, block) shortcut
                    if (ATB_GET_KIND(area, block) == AT_HEAD) {
                        mp_obj_base_t *obj = (mp_obj_base_t *)PTR_FROM_BLOCK(area, block);
                        if (obj->type != NULL) {
                            // if the object has a type then see if it has a __del__ method
                            mp_obj_t dest[2];
                            mp_load_method_maybe(MP_OBJ_FROM_PTR(obj), MP_QSTR___del__, dest);
                            if (dest[0] != MP_OBJ_NULL) {
                                // load_method returned a method, execute it in a protected environment
                                #if MICROPY_ENABLE_SCHEDULER
                                mp_sched_lock();
                                #endif
                                mp_call_function_1_protected(dest[0], dest[1]);
                                #if MICROPY_ENABLE_SCHEDULER
                                mp_sched_unlock();
                                #endif
                            }
                        }
                        // clear finaliser flag
                        FTB_CLEAR(area, block);
                    }
                }
                ftb >>= 1;
                block++;
            }
        }
    }
    #endif // MICROPY_ENABLE_FINALISER
}

// Free unmarked heads and their tails
static void gc_sweep_free_blocks(void) {
    #if MICROPY_PY_GC_COLLECT_RETVAL
    MP_STATE_MEM(gc_collected) = 0;
    #endif
    int free_tail = 0;
    #if MICROPY_GC_SPLIT_HEAP_AUTO
    mp_state_mem_area_t *prev_area = NULL;
    #endif

    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        DEBUG_printf("gc_sweep_free_blocks: ENTER area, gc_last_used_block=" UINT_FMT "\n", area->gc_last_used_block);
        size_t last_used_block = 0;
        assert(area->gc_last_used_block <= area->gc_alloc_table_byte_len * BLOCKS_PER_ATB);

        for (size_t block = 0; block <= area->gc_last_used_block; block++) {
            MICROPY_GC_HOOK_LOOP(block);
            switch (ATB_GET_KIND(area, block)) {
                case AT_HEAD:
                    // Check if this block is pinned - don't free pinned objects
                    if (gc_is_block_pinned(block)) {
                        free_tail = 0;
                        last_used_block = block;
                        break;
                    }
                    free_tail = 1;
                    DEBUG_printf("gc_sweep_free_blocks(%p)\n", (void *)PTR_FROM_BLOCK(area, block));
                    #if MICROPY_PY_GC_COLLECT_RETVAL
                    MP_STATE_MEM(gc_collected)++;
                    #endif
                    // fall through to free the head
                    MP_FALLTHROUGH

                case AT_TAIL:
                    if (free_tail) {
                        ATB_ANY_TO_FREE(area, block);
                        #if CLEAR_ON_SWEEP
                        memset((void *)PTR_FROM_BLOCK(area, block), 0, BYTES_PER_BLOCK);
                        #endif
                    } else {
                        last_used_block = block;
                    }
                    break;

                case AT_MARK:
                    ATB_MARK_TO_HEAD(area, block);
                    free_tail = 0;
                    last_used_block = block;
                    break;
            }
        }

        DEBUG_printf("gc_sweep_free_blocks: sweep loop done, last_used_block=" UINT_FMT ", was " UINT_FMT "\n", 
                     last_used_block, area->gc_last_used_block);
        area->gc_last_used_block = last_used_block;
        DEBUG_printf("gc_sweep_free_blocks: frontier set to " UINT_FMT "\n", area->gc_last_used_block);

        #if MICROPY_GC_SPLIT_HEAP_AUTO
        // Free any empty area, aside from the first one
        if (last_used_block == 0 && prev_area != NULL) {
            DEBUG_printf("gc_sweep_free_blocks free empty area %p\n", area);
            NEXT_AREA(prev_area) = NEXT_AREA(area);
            MP_PLAT_FREE_HEAP(area);
            area = prev_area;
        }
        prev_area = area;
        #endif
    }
}

// Address sanitizer needs to know that the access to ptrs[i] must always be
// considered OK, even if it's a load from an address that would normally be
// prohibited (due to being undefined, in a red zone, etc).
#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8))
__attribute__((no_sanitize_address))
#endif
static void *gc_get_ptr(void **ptrs, int i) {
    #if MICROPY_DEBUG_VALGRIND
    if (!VALGRIND_CHECK_MEM_IS_ADDRESSABLE(&ptrs[i], sizeof(*ptrs))) {
        return NULL;
    }
    #endif
    return ptrs[i];
}

void gc_info(gc_info_t *info) {
    GC_ENTER();
    info->total = 0;
    info->used = 0;
    info->free = 0;
    info->max_free = 0;
    info->num_1block = 0;
    info->num_2block = 0;
    info->max_block = 0;
    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        bool finish = false;
        info->total += area->gc_pool_end - area->gc_pool_start;
        for (size_t block = 0, len = 0, len_free = 0; !finish;) {
            MICROPY_GC_HOOK_LOOP(block);
            size_t kind = ATB_GET_KIND(area, block);
            switch (kind) {
                case AT_FREE:
                    info->free += 1;
                    len_free += 1;
                    len = 0;
                    break;

                case AT_HEAD:
                    info->used += 1;
                    len = 1;
                    break;

                case AT_TAIL:
                    info->used += 1;
                    len += 1;
                    break;

                case AT_MARK:
                    // shouldn't happen
                    break;
            }

            block++;
            finish = (block == area->gc_alloc_table_byte_len * BLOCKS_PER_ATB);
            // Get next block type if possible
            if (!finish) {
                kind = ATB_GET_KIND(area, block);
            }

            if (finish || kind == AT_FREE || kind == AT_HEAD) {
                if (len == 1) {
                    info->num_1block += 1;
                } else if (len == 2) {
                    info->num_2block += 1;
                }
                if (len > info->max_block) {
                    info->max_block = len;
                }
                if (finish || kind == AT_HEAD) {
                    if (len_free > info->max_free) {
                        info->max_free = len_free;
                    }
                    len_free = 0;
                }
            }
        }
    }

    info->used *= BYTES_PER_BLOCK;
    info->free *= BYTES_PER_BLOCK;

    #if MICROPY_GC_SPLIT_HEAP_AUTO
    info->max_new_split = gc_get_max_new_split();
    #endif

    GC_EXIT();
}

// Allocate from the right side of heap for pinned objects
// Returns block index where allocation starts, or (size_t)
static size_t gc_alloc_right(mp_state_mem_area_t *area, size_t n_blocks) {
    // Check for collision before attempting allocation
    if (area->gc_last_used_block_from_left == (size_t)-1) {
        // Left side free, safe to allocate
    } else if (area->gc_last_used_block_from_left + n_blocks >= area->gc_last_used_block_from_right) {
        // Collision detected
        return (size_t)-1;
    }

    // Scan from right frontier looking for n_blocks FREE blocks
    size_t n_free = 0;
    size_t alloc_start = (size_t)-1;

    for (size_t block = area->gc_last_used_block_from_right - 1; block > area->gc_last_used_block_from_left; block--) {
        if (ATB_GET_KIND(area, block) == AT_FREE) {
            n_free++;
            if (n_free == n_blocks) {
                alloc_start = block;
                break;
            }
        } else {
            n_free = 0;  // Reset counter when allocated block hit
        }
    }

    if (alloc_start == (size_t)-1) {
        // No consecutive free blocks
        return (size_t)-1;
    }

    // Mark allocated blocks
    size_t alloc_end = alloc_start + n_blocks - 1;
    ATB_FREE_TO_HEAD(area, alloc_start);
    for (size_t i = alloc_start + 1; i <= alloc_end; i++) {
        ATB_FREE_TO_TAIL(area, i);
    }

    // Update right frontier
    area->gc_last_used_block_from_right = alloc_start;

    return alloc_start;
}

void *gc_alloc(size_t n_bytes, unsigned int alloc_flags) {
    bool has_finaliser = alloc_flags & GC_ALLOC_FLAG_HAS_FINALISER;
    bool is_pinned = alloc_flags & GC_ALLOC_FLAG_IS_PINNED;
    size_t n_blocks = ((n_bytes + BYTES_PER_BLOCK - 1) & (~(BYTES_PER_BLOCK - 1))) / BYTES_PER_BLOCK;
    DEBUG_printf("gc_alloc(" UINT_FMT " bytes -> " UINT_FMT " blocks, pinned=%d)\n", n_bytes, n_blocks, is_pinned);

    // check for 0 allocation
    if (n_blocks == 0) {
        return NULL;
    }

    // check if GC is locked
    if (MP_STATE_THREAD(gc_lock_depth) > 0) {
        return NULL;
    }

    GC_ENTER();

    // Handle pinned object allocation from right
    if (is_pinned) {
        mp_state_mem_area_t *area;
        int collected = !MP_STATE_MEM(gc_auto_collect_enabled);

        for (;;) {
            #if MICROPY_GC_SPLIT_HEAP
            area = MP_STATE_MEM(gc_last_free_area);
            if (!area) area = &MP_STATE_MEM(area);
            #else
            area = &MP_STATE_MEM(area);
            #endif

            size_t block = gc_alloc_right(area, n_blocks);
            if (block != (size_t)-1) {
                // Successfully allocated from right
                size_t end_block = block + n_blocks - 1;
                area->gc_last_used_block = MAX(area->gc_last_used_block, end_block);
                
                void *ret_ptr = (void *)(area->gc_pool_start + block * BYTES_PER_BLOCK);
                DEBUG_printf("gc_alloc(pinned, block " UINT_FMT ", ptr %p)\n", block, ret_ptr);

                #if MICROPY_GC_ALLOC_THRESHOLD
                MP_STATE_MEM(gc_alloc_amount) += n_blocks;
                #endif

                GC_EXIT();

                // Clear memory
                #if MICROPY_GC_CONSERVATIVE_CLEAR
                memset((byte *)ret_ptr, 0, n_blocks * BYTES_PER_BLOCK);
                #else
                memset((byte *)ret_ptr + n_bytes, 0, n_blocks * BYTES_PER_BLOCK - n_bytes);
                #endif

                #if MICROPY_ENABLE_FINALISER
                if (has_finaliser) {
                    ((mp_obj_base_t *)ret_ptr)->type = NULL;
                    GC_ENTER();
                    FTB_SET(area, block);
                    GC_EXIT();
                }
                #endif

                return ret_ptr;
            }

            // Allocation failed, run GC
            GC_EXIT();
            if (collected) {
                return NULL;
            }
            DEBUG_printf("gc_alloc(pinned): no free mem, triggering GC\n");
            gc_collect();
            collected = 1;
            GC_ENTER();
        }
    }

    mp_state_mem_area_t *area;
    size_t i;
    size_t end_block;
    size_t start_block;
    size_t n_free;
    int collected = !MP_STATE_MEM(gc_auto_collect_enabled);
    #if MICROPY_GC_SPLIT_HEAP_AUTO
    bool added = false;
    #endif

    #if MICROPY_GC_ALLOC_THRESHOLD
    if (!collected && MP_STATE_MEM(gc_alloc_amount) >= MP_STATE_MEM(gc_alloc_threshold)) {
        GC_EXIT();
        gc_collect();
        collected = 1;
        GC_ENTER();
    }
    #endif

    for (;;) {

        #if MICROPY_GC_SPLIT_HEAP
        area = MP_STATE_MEM(gc_last_free_area);
        #else
        area = &MP_STATE_MEM(area);
        #endif

        // look for a run of n_blocks available blocks
        for (; area != NULL; area = NEXT_AREA(area), i = 0) {
            n_free = 0;
            for (i = area->gc_last_free_atb_index; i < area->gc_alloc_table_byte_len; i++) {
                MICROPY_GC_HOOK_LOOP(i);
                byte a = area->gc_alloc_table_start[i];
                // *FORMAT-OFF*
                if (ATB_0_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 0; goto found; } } else { n_free = 0; }
                if (ATB_1_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 1; goto found; } } else { n_free = 0; }
                if (ATB_2_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 2; goto found; } } else { n_free = 0; }
                if (ATB_3_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 3; goto found; } } else { n_free = 0; }
                // *FORMAT-ON*
            }

            // No free blocks found on this heap. Mark this heap as
            // filled, so we won't try to find free space here again until
            // space is freed.
            #if MICROPY_GC_SPLIT_HEAP
            if (n_blocks == 1) {
                area->gc_last_free_atb_index = (i + 1) / BLOCKS_PER_ATB; // or (size_t)-1
            }
            #endif
        }

        GC_EXIT();
        // nothing found!
        if (collected) {
            #if MICROPY_GC_SPLIT_HEAP_AUTO
            if (!added && gc_try_add_heap(n_bytes)) {
                added = true;
                continue;
            }
            #endif
            return NULL;
        }
        DEBUG_printf("gc_alloc(" UINT_FMT "): no free mem, triggering GC\n", n_bytes);
        gc_collect();
        collected = 1;
        GC_ENTER();
    }

    // found, ending at block i inclusive
found:
    // get starting and end blocks, both inclusive 
    end_block = i;
    start_block = i - n_free + 1;

    // TEST: Verify the block we found is within the valid pool range
    if (start_block >= area->gc_num_blocks || end_block >= area->gc_num_blocks) {
        DEBUG_printf("GC_ALLOC_CRITICAL_ERROR: Found blocks " UINT_FMT ".." UINT_FMT " exceed pool size " UINT_FMT "\n", 
                     start_block, end_block, area->gc_num_blocks);
        return NULL;
    }

    // TEST: Ensure no stale bits exist in the range we are about to use
    for (size_t bl = start_block; bl <= end_block; bl++) {
        assert(ATB_GET_KIND(area, bl) == AT_FREE);
    }

    // Set last free ATB index to block after last block we found, for start of
    // next scan.  To reduce fragmentation, we only do this if we were looking
    // for a single free block, which guarantees that there are no free blocks
    // before this one.  Also, whenever we free or shink a block we must check
    // if this index needs adjusting (see gc_realloc and gc_free).
    if (n_free == 1) {
        #if MICROPY_GC_SPLIT_HEAP
        MP_STATE_MEM(gc_last_free_area) = area;
        #endif
        area->gc_last_free_atb_index = (i + 1) / BLOCKS_PER_ATB;
    }

    area->gc_last_used_block = MAX(area->gc_last_used_block, end_block);

    // Track left frontier
    if (area->gc_last_used_block_from_left == (size_t)-1) {
        area->gc_last_used_block_from_left = start_block;
    } else {
        area->gc_last_used_block_from_left = MAX(area->gc_last_used_block_from_left, end_block);
    }

    // mark first block as used head
    ATB_FREE_TO_HEAD(area, start_block);

    // mark rest of blocks as used tail
    // TODO for a run of many blocks can make this more efficient
    for (size_t bl = start_block + 1; bl <= end_block; bl++) {
        ATB_FREE_TO_TAIL(area, bl);
    }

    // get pointer to first block
    // we must create this pointer before unlocking the GC so a collection can find it
    void *ret_ptr = (void *)(area->gc_pool_start + start_block * BYTES_PER_BLOCK);
    DEBUG_printf("gc_alloc(block " UINT_FMT ", ptr %p)\n", start_block, ret_ptr);

    // TEST: Attempt a volatile write to the first byte of the block.
    // If your interpretation is correct, the segfault will happen HERE.
    *(volatile byte *)ret_ptr = 0;

    #if MICROPY_GC_ALLOC_THRESHOLD
    MP_STATE_MEM(gc_alloc_amount) += n_blocks;
    #endif

    GC_EXIT();

    #if MICROPY_GC_CONSERVATIVE_CLEAR
    // be conservative and zero out all the newly allocated blocks
    memset((byte *)ret_ptr, 0, (end_block - start_block + 1) * BYTES_PER_BLOCK);
    #else
    // zero out the additional bytes of the newly allocated blocks
    // This is needed because the blocks may have previously held pointers
    // to the heap and will not be set to something else if the caller
    // doesn't actually use the entire block.  As such they will continue
    // to point to the heap and may prevent other blocks from being reclaimed.
    memset((byte *)ret_ptr + n_bytes, 0, (end_block - start_block + 1) * BYTES_PER_BLOCK - n_bytes);
    #endif

    #if MICROPY_ENABLE_FINALISER
    if (has_finaliser) {
        // clear type pointer in case it is never set
        ((mp_obj_base_t *)ret_ptr)->type = NULL;
        // set mp_obj flag only if it has a finaliser
        GC_ENTER();
        FTB_SET(area, start_block);
        GC_EXIT();
    }
    #else
    (void)has_finaliser;
    #endif

    #if EXTENSIVE_HEAP_PROFILING
    gc_dump_alloc_table(&mp_plat_print);
    #endif

    return ret_ptr;
}

// force the freeing of a piece of memory
// TODO: freeing here does not call finaliser
void gc_free(void *ptr) {
    // Cannot free while the GC is locked, unless we're only doing a gc sweep.
    // However free is an optimisation to reclaim the memory immediately, this
    // means it will now be left until the next collection.
    //
    // (We have the optimisation to free immediately from inside a gc sweep so
    // that finalisers can free more memory when trying to avoid MemoryError.)
    if (MP_STATE_THREAD(gc_lock_depth) & ~GC_COLLECT_FLAG) {
        return;
    }

    GC_ENTER();

    DEBUG_printf("gc_free(%p)\n", ptr);

    if (ptr == NULL) {
        // free(NULL) is a no-op
        GC_EXIT();
        return;
    }

    // get the GC block number corresponding to this pointer
    mp_state_mem_area_t *area;
    #if MICROPY_GC_SPLIT_HEAP
    area = gc_get_ptr_area(ptr);
    assert(area);
    #else
    assert(VERIFY_PTR(ptr));
    area = &MP_STATE_MEM(area);
    #endif

    size_t block = BLOCK_FROM_PTR(area, ptr);
    assert(ATB_GET_KIND(area, block) == AT_HEAD
        || (ATB_GET_KIND(area, block) == AT_MARK && (MP_STATE_THREAD(gc_lock_depth) & GC_COLLECT_FLAG)));

    // Check if this object is pinned - cannot free pinned objects
    if (gc_is_block_pinned(block)) {
        GC_EXIT();
        return;
    }

    #if MICROPY_ENABLE_FINALISER
    FTB_CLEAR(area, block);
    #endif

    #if MICROPY_GC_SPLIT_HEAP
    if (MP_STATE_MEM(gc_last_free_area) != area) {
        // We freed something but it isn't the current area. Reset the
        // last free area to the start for a rescan. Note that this won't
        // give much of a performance hit, since areas that are completely
        // filled will likely be skipped (the gc_last_free_atb_index
        // points to the last block).
        // The reason why this is necessary is because it is not possible
        // to see which area came first (like it is possible to adjust
        // gc_last_free_atb_index based on whether the freed block is
        // before the last free block).
        MP_STATE_MEM(gc_last_free_area) = &MP_STATE_MEM(area);
    }
    #endif

    // set the last_free pointer to this block if it's earlier in the heap
    if (block / BLOCKS_PER_ATB < area->gc_last_free_atb_index) {
        area->gc_last_free_atb_index = block / BLOCKS_PER_ATB;
    }

    // Count consecutive blocks for this object to check if we're freeing from frontier
    size_t block_start = block;
    size_t block_count = 1;
    size_t temp_block = block + 1;
    while (ATB_GET_KIND(area, temp_block) == AT_TAIL) {
        block_count++;
        temp_block++;
    }

    // Update left frontier if freeing at left edge
    if (block_start <= area->gc_last_used_block_from_left && 
        block_start + block_count - 1 == area->gc_last_used_block_from_left) {
        area->gc_last_used_block_from_left = block_start - 1;
    }

    // Update right frontier if freeing at right edge
    if (block_start == area->gc_last_used_block_from_right) {
        area->gc_last_used_block_from_right = block_start + block_count;
    }

    // free head and all of its tail blocks
    do {
        ATB_ANY_TO_FREE(area, block);
        block += 1;
    } while (ATB_GET_KIND(area, block) == AT_TAIL);

    GC_EXIT();

    #if EXTENSIVE_HEAP_PROFILING
    gc_dump_alloc_table(&mp_plat_print);
    #endif
}

size_t gc_nbytes(const void *ptr) {
    GC_ENTER();

    mp_state_mem_area_t *area;
    #if MICROPY_GC_SPLIT_HEAP
    area = gc_get_ptr_area(ptr);
    #else
    if (VERIFY_PTR(ptr)) {
        area = &MP_STATE_MEM(area);
    } else {
        area = NULL;
    }
    #endif

    if (area) {
        size_t block = BLOCK_FROM_PTR(area, ptr);
        if (ATB_GET_KIND(area, block) == AT_HEAD) {
            // work out number of consecutive blocks in the chain starting with this on
            size_t n_blocks = 0;
            do {
                n_blocks += 1;
            } while (ATB_GET_KIND(area, block + n_blocks) == AT_TAIL);
            GC_EXIT();
            return n_blocks * BYTES_PER_BLOCK;
        }
    }

    // invalid pointer
    GC_EXIT();
    return 0;
}

void *gc_realloc(void *ptr_in, size_t n_bytes, bool allow_move) {
    // check for pure allocation
    if (ptr_in == NULL) {
        return gc_alloc(n_bytes, false);
    }

    // check for pure free
    if (n_bytes == 0) {
        gc_free(ptr_in);
        return NULL;
    }

    if (MP_STATE_THREAD(gc_lock_depth) > 0) {
        return NULL;
    }

    void *ptr = ptr_in;

    GC_ENTER();

    // get the GC block number corresponding to this pointer
    mp_state_mem_area_t *area;
    #if MICROPY_GC_SPLIT_HEAP
    area = gc_get_ptr_area(ptr);
    assert(area);
    #else
    assert(VERIFY_PTR(ptr));
    area = &MP_STATE_MEM(area);
    #endif
    size_t block = BLOCK_FROM_PTR(area, ptr);
    assert(ATB_GET_KIND(area, block) == AT_HEAD);

    // compute number of new blocks that are requested
    size_t new_blocks = (n_bytes + BYTES_PER_BLOCK - 1) / BYTES_PER_BLOCK;

    // Get the total number of consecutive blocks that are already allocated to
    // this chunk of memory, and then count the number of free blocks following
    // it.  Stop if we reach the end of the heap, or if we find enough extra
    // free blocks to satisfy the realloc.  Note that we need to compute the
    // total size of the existing memory chunk so we can correctly and
    // efficiently shrink it (see below for shrinking code).
    size_t n_free = 0;
    size_t n_blocks = 1; // counting HEAD block
    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    for (size_t bl = block + n_blocks; bl < max_block; bl++) {
        byte block_type = ATB_GET_KIND(area, bl);
        if (block_type == AT_TAIL) {
            n_blocks++;
            continue;
        }
        if (block_type == AT_FREE) {
            n_free++;
            if (n_blocks + n_free >= new_blocks) {
                // stop as soon as we find enough blocks for n_bytes
                break;
            }
            continue;
        }
        break;
    }

    // return original ptr if it already has the requested number of blocks
    if (new_blocks == n_blocks) {
        GC_EXIT();
        return ptr_in;
    }

    // check if we can shrink the allocated area
    if (new_blocks < n_blocks) {
        // free unneeded tail blocks
        for (size_t bl = block + new_blocks, count = n_blocks - new_blocks; count > 0; bl++, count--) {
            ATB_ANY_TO_FREE(area, bl);
        }

        #if MICROPY_GC_SPLIT_HEAP
        if (MP_STATE_MEM(gc_last_free_area) != area) {
            // See comment in gc_free.
            MP_STATE_MEM(gc_last_free_area) = &MP_STATE_MEM(area);
        }
        #endif

        // set the last_free pointer to end of this block if it's earlier in the heap
        if ((block + new_blocks) / BLOCKS_PER_ATB < area->gc_last_free_atb_index) {
            area->gc_last_free_atb_index = (block + new_blocks) / BLOCKS_PER_ATB;
        }

        GC_EXIT();

        #if EXTENSIVE_HEAP_PROFILING
        gc_dump_alloc_table(&mp_plat_print);
        #endif

        return ptr_in;
    }

    // check if we can expand in place
    if (new_blocks <= n_blocks + n_free) {
        // mark few more blocks as used tail
        size_t end_block = block + new_blocks;
        for (size_t bl = block + n_blocks; bl < end_block; bl++) {
            assert(ATB_GET_KIND(area, bl) == AT_FREE);
            ATB_FREE_TO_TAIL(area, bl);
        }

        area->gc_last_used_block = MAX(area->gc_last_used_block, end_block);

        GC_EXIT();

        #if MICROPY_GC_CONSERVATIVE_CLEAR
        // be conservative and zero out all the newly allocated blocks
        memset((byte *)ptr_in + n_blocks * BYTES_PER_BLOCK, 0, (new_blocks - n_blocks) * BYTES_PER_BLOCK);
        #else
        // zero out the additional bytes of the newly allocated blocks (see comment above in gc_alloc)
        memset((byte *)ptr_in + n_bytes, 0, new_blocks * BYTES_PER_BLOCK - n_bytes);
        #endif

        #if EXTENSIVE_HEAP_PROFILING
        gc_dump_alloc_table(&mp_plat_print);
        #endif

        return ptr_in;
    }

    #if MICROPY_ENABLE_FINALISER
    bool ftb_state = FTB_GET(area, block);
    #else
    bool ftb_state = false;
    #endif

    GC_EXIT();

    if (!allow_move) {
        // not allowed to move memory block so return failure
        return NULL;
    }

    // can't resize inplace; try to find a new contiguous chain
    void *ptr_out = gc_alloc(n_bytes, ftb_state);

    // check that the alloc succeeded
    if (ptr_out == NULL) {
        return NULL;
    }

    DEBUG_printf("gc_realloc(%p -> %p)\n", ptr_in, ptr_out);
    memcpy(ptr_out, ptr_in, n_blocks * BYTES_PER_BLOCK);
    gc_free(ptr_in);
    return ptr_out;
}

void gc_dump_info(const mp_print_t *print) {
    gc_info_t info;
    gc_info(&info);
    mp_printf(print, "GC: total: %u, used: %u, free: %u",
        (uint)info.total, (uint)info.used, (uint)info.free);
    #if MICROPY_GC_SPLIT_HEAP_AUTO
    mp_printf(print, ", max new split: %u", (uint)info.max_new_split);
    #endif
    mp_printf(print, "\n No. of 1-blocks: %u, 2-blocks: %u, max blk sz: %u, max free sz: %u\n",
        (uint)info.num_1block, (uint)info.num_2block, (uint)info.max_block, (uint)info.max_free);
}

void gc_dump_alloc_table(const mp_print_t *print) {
    GC_ENTER();
    static const size_t DUMP_BYTES_PER_LINE = 64;
    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        #if !EXTENSIVE_HEAP_PROFILING
        // When comparing heap output we don't want to print the starting
        // pointer of the heap because it changes from run to run.
        mp_printf(print, "GC memory layout; from %p:", area->gc_pool_start);
        #endif
        for (size_t bl = 0; bl < area->gc_alloc_table_byte_len * BLOCKS_PER_ATB; bl++) {
            if (bl % DUMP_BYTES_PER_LINE == 0) {
                // a new line of blocks
                {
                    // check if this line contains only free blocks
                    size_t bl2 = bl;
                    while (bl2 < area->gc_alloc_table_byte_len * BLOCKS_PER_ATB && ATB_GET_KIND(area, bl2) == AT_FREE) {
                        bl2++;
                    }
                    if (bl2 - bl >= 2 * DUMP_BYTES_PER_LINE) {
                        // there are at least 2 lines containing only free blocks, so abbreviate their printing
                        mp_printf(print, "\n       (%u lines all free)", (uint)((bl2 - bl) / DUMP_BYTES_PER_LINE));
                        bl = bl2 & (~(DUMP_BYTES_PER_LINE - 1));
                        if (bl >= area->gc_alloc_table_byte_len * BLOCKS_PER_ATB) {
                            // got to end of heap
                            break;
                        }
                    }
                }
                // print header for new line of blocks
                // (the cast to uint32_t is for 16-bit ports)
                mp_printf(print, "\n%08x: ", (uint)(bl * BYTES_PER_BLOCK));
            }
            int c = ' ';
            switch (ATB_GET_KIND(area, bl)) {
                case AT_FREE:
                    c = '.';
                    break;
                /* this prints out if the object is reachable from BSS or STACK (for unix only)
                case AT_HEAD: {
                    c = 'h';
                    void **ptrs = (void**)(void*)&mp_state_ctx;
                    mp_uint_t len = offsetof(mp_state_ctx_t, vm.stack_top) / sizeof(mp_uint_t);
                    for (mp_uint_t i = 0; i < len; i++) {
                        mp_uint_t ptr = (mp_uint_t)ptrs[i];
                        if (gc_get_ptr_area(ptr) && BLOCK_FROM_PTR(ptr) == bl) {
                            c = 'B';
                            break;
                        }
                    }
                    if (c == 'h') {
                        ptrs = (void**)&c;
                        len = ((mp_uint_t)MP_STATE_THREAD(stack_top) - (mp_uint_t)&c) / sizeof(mp_uint_t);
                        for (mp_uint_t i = 0; i < len; i++) {
                            mp_uint_t ptr = (mp_uint_t)ptrs[i];
                            if (gc_get_ptr_area(ptr) && BLOCK_FROM_PTR(ptr) == bl) {
                                c = 'S';
                                break;
                            }
                        }
                    }
                    break;
                }
                */
                /* this prints the MicroPython object type of the head block */
                case AT_HEAD: {
                    void **ptr = (void **)(area->gc_pool_start + bl * BYTES_PER_BLOCK);
                    if (*ptr == &mp_type_tuple) {
                        c = 'T';
                    } else if (*ptr == &mp_type_list) {
                        c = 'L';
                    } else if (*ptr == &mp_type_dict) {
                        c = 'D';
                    } else if (*ptr == &mp_type_str || *ptr == &mp_type_bytes) {
                        c = 'S';
                    }
                    #if MICROPY_PY_BUILTINS_BYTEARRAY
                    else if (*ptr == &mp_type_bytearray) {
                        c = 'A';
                    }
                    #endif
                    #if MICROPY_PY_ARRAY
                    else if (*ptr == &mp_type_array) {
                        c = 'A';
                    }
                    #endif
                    #if MICROPY_PY_BUILTINS_FLOAT
                    else if (*ptr == &mp_type_float) {
                        c = 'F';
                    }
                    #endif
                    else if (*ptr == &mp_type_fun_bc) {
                        c = 'B';
                    } else if (*ptr == &mp_type_module) {
                        c = 'M';
                    } else {
                        c = 'h';
                        #if 0
                        // This code prints "Q" for qstr-pool data, and "q" for qstr-str
                        // data.  It can be useful to see how qstrs are being allocated,
                        // but is disabled by default because it is very slow.
                        for (qstr_pool_t *pool = MP_STATE_VM(last_pool); c == 'h' && pool != NULL; pool = pool->prev) {
                            if ((qstr_pool_t *)ptr == pool) {
                                c = 'Q';
                                break;
                            }
                            for (const byte **q = pool->qstrs, **q_top = pool->qstrs + pool->len; q < q_top; q++) {
                                if ((const byte *)ptr == *q) {
                                    c = 'q';
                                    break;
                                }
                            }
                        }
                        #endif
                    }
                    break;
                }
                case AT_TAIL:
                    c = '=';
                    break;
                case AT_MARK:
                    c = 'm';
                    break;
            }
            mp_printf(print, "%c", c);
        }
        mp_print_str(print, "\n");
    }
    GC_EXIT();
}

// Pin an object to prevent garbage collection
void gc_pin(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    // Get the area and block for this pointer
    mp_state_mem_area_t *area;
    #if MICROPY_GC_SPLIT_HEAP
    area = gc_get_ptr_area(ptr);
    if (!area) {
        return;
    }
    #else
    if (!VERIFY_PTR(ptr)) {
        return;
    }
    area = &MP_STATE_MEM(area);
    #endif

    size_t block = BLOCK_FROM_PTR(area, ptr);

    GC_ENTER();

    // Check if already pinned
    if (gc_pinned_find_range_by_ptr(ptr) != -1) {
        GC_EXIT();
        return;  // Already pinned
    }

    // Check if we need to expand the table
    bool needs_expansion = gc_pinned_table.count >= gc_pinned_table.capacity;
    if (needs_expansion) {
        GC_EXIT();  // Release lock before memory allocation
        
        size_t new_capacity = gc_pinned_table.capacity == 0 ? 
                               PINNED_TABLE_INITIAL_CAPACITY : 
                               gc_pinned_table.capacity * 2;
        size_t old_size = gc_pinned_table.capacity * sizeof(pinned_range_t);
        size_t new_size = new_capacity * sizeof(pinned_range_t);
        
        pinned_range_t *new_ranges = (pinned_range_t *)m_realloc(gc_pinned_table.ranges, 
                                                                  old_size, new_size);
        if (new_ranges == NULL) {
            return;  // Allocation failed
        }
        
        GC_ENTER();  // Re-acquire lock after memory allocation
        
        gc_pinned_table.ranges = new_ranges;
        gc_pinned_table.capacity = new_capacity;
        
        // Re-check if already pinned (another thread might have pinned it)
        if (gc_pinned_find_range_by_ptr(ptr) != -1) {
            GC_EXIT();
            return;  // Already pinned
        }
    }

    // Count the number of consecutive blocks for this object
    size_t block_count = 1;  // HEAD block
    for (size_t bl = block + 1; bl < area->gc_alloc_table_byte_len * BLOCKS_PER_ATB; bl++) {
        if (ATB_GET_KIND(area, bl) == AT_TAIL) {
            block_count++;
        } else {
            break;
        }
    }

    // Insert in sorted order by block_start (insertion sort)
    size_t insert_pos = gc_pinned_table.count;
    for (size_t i = 0; i < gc_pinned_table.count; i++) {
        if (block < gc_pinned_table.ranges[i].block_start) {
            insert_pos = i;
            break;
        }
    }

    // Shift entries to make room
    if (insert_pos < gc_pinned_table.count) {
        memmove(&gc_pinned_table.ranges[insert_pos + 1],
                &gc_pinned_table.ranges[insert_pos],
                (gc_pinned_table.count - insert_pos) * sizeof(pinned_range_t));
    }

    // Add the new pinned range
    gc_pinned_table.ranges[insert_pos].obj = ptr;
    gc_pinned_table.ranges[insert_pos].block_start = block;
    gc_pinned_table.ranges[insert_pos].block_count = block_count;
    gc_pinned_table.count++;

    DEBUG_printf("gc_pin(%p) at block " UINT_FMT " (count=" UINT_FMT ")\n", ptr, block, gc_pinned_table.count);

    GC_EXIT();
}

// Unpin an object to allow garbage collection
void gc_unpin(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    GC_ENTER();

    ssize_t idx = gc_pinned_find_range_by_ptr(ptr);
    if (idx != -1) {
        // Remove by shifting remaining entries
        if ((size_t)idx < gc_pinned_table.count - 1) {
            memmove(&gc_pinned_table.ranges[idx],
                    &gc_pinned_table.ranges[idx + 1],
                    (gc_pinned_table.count - idx - 1) * sizeof(pinned_range_t));
        }
        gc_pinned_table.count--;

        DEBUG_printf("gc_unpin(%p) count=" UINT_FMT "\n", ptr, gc_pinned_table.count);
    }

    GC_EXIT();
}

// Check if a pointer is pinned
bool gc_is_pinned(void* ptr) {
    if (ptr == NULL) {
        return false;
    }

    GC_ENTER();

    bool result = (gc_pinned_find_range_by_ptr(ptr) != -1);

    GC_EXIT();
    return result;
}

// Check if a specific block is pinned
bool gc_is_block_pinned(size_t block) {
    GC_ENTER();

    bool result = (gc_pinned_find_range_by_block(block) != -1);

    GC_EXIT();
    return result;
}

// Validate pinned table entries after GC - remove entries for freed objects
__attribute__((unused))
static void gc_pinned_validate(void) {
    for (ssize_t i = (ssize_t)gc_pinned_table.count - 1; i >= 0; i--) {
        pinned_range_t *range = &gc_pinned_table.ranges[i];
        
        // Find which area this range belongs to
        mp_state_mem_area_t *area = NULL;
        #if MICROPY_GC_SPLIT_HEAP
        area = gc_get_ptr_area(range->obj);
        if (!area) {
            // Object is no longer in heap, remove from pinned table
            goto remove_entry;
        }
        #else
        if (!VERIFY_PTR(range->obj)) {
            // Invalid pointer, remove from pinned table
            goto remove_entry;
        }
        area = &MP_STATE_MEM(area);
        #endif
        
        // Verify the block is still marked as allocated
        if (ATB_GET_KIND(area, range->block_start) != AT_HEAD) {
            // Block is no longer allocated, remove from pinned table
            goto remove_entry;
        }
        
        continue;
        
remove_entry:
        // Remove this entry by shifting
        if ((size_t)i < gc_pinned_table.count - 1) {
            memmove(&gc_pinned_table.ranges[i],
                    &gc_pinned_table.ranges[i + 1],
                    (gc_pinned_table.count - i - 1) * sizeof(pinned_range_t));
        }
        gc_pinned_table.count--;
    }
}

// Function to insert a forwarding entry 
static bool gc_forward_table_insert(gc_forward_table_t *table, size_t old_block, size_t new_block) {
    if (table->count >= table->capacity) {
        DEBUG_printf("gc_forward_table_insert: ERROR - table full, count=" UINT_FMT " capacity=" UINT_FMT "\n", 
                     table->count, table->capacity);
        return false;
    }

    table->entries[table->count].old_block = old_block;
    table->entries[table->count].new_block = new_block;
    table->count++;
    return true;
}

// Function to lookup forwarding address
static size_t gc_forward_table_lookup(gc_forward_table_t *table, size_t old_block) {
    for (size_t i = 0; i < table->count; i++) {
        if (table->entries[i].old_block == old_block) {
            return table->entries[i].new_block;
        }
    }
    return (size_t)-1;
}

static size_t gc_count_live_objects(mp_state_mem_area_t *area) {
    size_t count = 0;
    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;

    for (size_t block = 0; block < max_block; block++) {
        byte block_kind = ATB_GET_KIND(area, block);
        if (block_kind == AT_HEAD) {
            count++;
            size_t next_block = block + 1;
            while (next_block < max_block && ATB_GET_KIND(area, next_block) == AT_TAIL) {
                next_block++;
            }
            block = next_block - 1;
        }
    }
    return count;
}

static bool gc_forward_table_prealloc(gc_forward_table_t *table, size_t size) {
    if (size == 0) {
        table->entries = NULL;
        table->capacity = 0;
        table->count = 0;
        return true;
    }
    
    table->entries = (gc_forward_entry_t *)malloc(size * sizeof(gc_forward_entry_t));
    if (table->entries == NULL) {
        DEBUG_printf("gc_forward_table_prealloc: failed to allocate table\n");
        return false;
    }
    table->capacity = size;
    table->count = 0;
    return true;
}

// Maps old block positions to new compacted positions
static void gc_compute_forwarding_addresses(mp_state_mem_area_t *area, gc_forward_table_t *forward_table) { 
    if (forward_table->entries == NULL || forward_table->capacity == 0) {
        DEBUG_printf("gc_compute_forwarding_addresses: forward table not pre-allocated\n");
        return;
    }

    // Compute new addresses for all live objects using bi-directional compaction
    size_t compact_ptr = 0;
    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;

    for (size_t block = 0; block < max_block; block++) {
        byte block_kind = ATB_GET_KIND(area, block);

        // After sweep, live objects are AT_HEAD
        if (block_kind == AT_HEAD) {
            // Count the number of consecutive blocks
            size_t block_count = 1;
            size_t next_block = block + 1;
            while (next_block < max_block && ATB_GET_KIND(area, next_block) == AT_TAIL) {
                block_count++;
                next_block++;
            }

            // Determine if object is pinned (immovable)
            if (gc_is_block_pinned(block)) {
                // Pinned objects stay in place - record identity mapping
                if (!gc_forward_table_insert(forward_table, block, block)) {
                    DEBUG_printf("gc_compute_forwarding_addresses: failed to insert pinned object at block " UINT_FMT "\n", block);
                    gc_forward_table_free(forward_table);
                    return;
                }
                DEBUG_printf("gc_compute: pinned object at block " UINT_FMT " stays at " UINT_FMT "\n", block, block);
            } else {
                if (compact_ptr + block_count > max_block) {
                    DEBUG_printf("gc_compute: ERROR - compaction would exceed max_block! (" UINT_FMT " + " UINT_FMT " > " UINT_FMT ")\n",
                                 compact_ptr, block_count, max_block);
                    gc_forward_table_free(forward_table);
                    return;
                }

                // Check for collision with right frontier
                if (compact_ptr + block_count > area->gc_last_used_block_from_right) {
                    DEBUG_printf("gc_compute: collision at block " UINT_FMT ": left frontier (" UINT_FMT " + " UINT_FMT ") >= right frontier " UINT_FMT "\n",
                                 block, compact_ptr, block_count, area->gc_last_used_block_from_right);
                    gc_forward_table_free(forward_table);
                    return;
                }

                // Record forwarding address: old_block -> new_block
                if (!gc_forward_table_insert(forward_table, block, compact_ptr)) {
                    DEBUG_printf("gc_compute_forwarding_addresses: failed to insert mapping for block " UINT_FMT "\n", block);
                    gc_forward_table_free(forward_table);
                    return;
                }

                DEBUG_printf("gc_compute: block " UINT_FMT " (" UINT_FMT " blocks) -> " UINT_FMT " (left compact)\n", block, block_count, compact_ptr);
                compact_ptr += block_count;
            }

            // Skip to end of this object's blocks
            block = next_block - 1;
        }
    }

    DEBUG_printf("gc_compute_forwarding_addresses: left frontier compacted to block " UINT_FMT "\n", compact_ptr);
}

// Updates allocation table for new block positions
static void gc_compact_copy(mp_state_mem_area_t *area, gc_forward_table_t *forward_table) {
    // Physically copy live objects to their new locations
    DEBUG_printf("gc_compact_copy: starting copy phase\n");

    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;

    for (size_t block = 0; block < max_block; block++) {
        byte block_kind = ATB_GET_KIND(area, block);

        // Process all live objects identified after sweep
        if (block_kind == AT_HEAD) {
            // Count the number of consecutive blocks
            size_t block_count = 1;
            size_t next_block = block + 1;
            while (next_block < max_block && ATB_GET_KIND(area, next_block) == AT_TAIL) {
                block_count++;
                next_block++;
            }

            // Look up where this object should be moved to
            size_t new_block = gc_forward_table_lookup(forward_table, block);
            if (new_block == (size_t)-1) {
                // Should never happen!!! all AT_MARK blocks should be in forward table
                DEBUG_printf("gc_compact_copy: ERROR - block " UINT_FMT " not in forwarding table\n", block);
                block = next_block - 1;
                continue;
            }

            if (new_block == block) {
                DEBUG_printf("gc_compact_copy: block " UINT_FMT " pinned - no copy needed\n", block);
                // Object is already HEAD/TAIL from sweep, nothing to do
                block = next_block - 1;
                continue;
            }

            if (new_block + block_count > max_block) {
                DEBUG_printf("gc_compact_copy: ERROR - copy would exceed max_block! (" UINT_FMT " + " UINT_FMT " > " UINT_FMT ")\n",
                             new_block, block_count, max_block);
                return;
            }

            for (size_t i = 0; i < block_count; i++) {
                ATB_ANY_TO_FREE(area, new_block + i);
            }

            // Copy object data from old location to new
            byte *src = (byte *)PTR_FROM_BLOCK(area, block);
            byte *dst = (byte *)PTR_FROM_BLOCK(area, new_block);
            size_t copy_size = block_count * BYTES_PER_BLOCK;

            DEBUG_printf("gc_compact_copy: moving " UINT_FMT " blocks: block " UINT_FMT " -> block " UINT_FMT " (" UINT_FMT " bytes)\n",
                         block_count, block, new_block, copy_size);

            memmove(dst, src, copy_size);

            // Clear allocation table at old location FIRST.
            for (size_t i = 0; i < block_count; i++) {
                ATB_ANY_TO_FREE(area, block + i);
            }

            // Update Allocation Table at NEW location
            ATB_FREE_TO_HEAD(area, new_block);
            for (size_t i = new_block + 1; i < new_block + block_count; i++) {
                ATB_FREE_TO_TAIL(area, i);
            }

            #if CLEAR_ON_SWEEP
            memset(src, 0, copy_size);
            #endif

            block = next_block - 1;
        }
    }

    DEBUG_printf("gc_compact_copy: copy phase complete\n");

    // VALIDATION: Check ATB integrity after copy
    {
        size_t max_block_val = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
        size_t head_count = 0, tail_count = 0, free_count = 0, mark_count = 0;
        for (size_t block = 0; block < max_block_val; block++) {
            byte kind = ATB_GET_KIND(area, block);
            if (kind == AT_HEAD) head_count++;
            else if (kind == AT_TAIL) tail_count++;
            else if (kind == AT_FREE) free_count++;
            else if (kind == AT_MARK) mark_count++;
        }
        DEBUG_printf("gc_compact_copy: ATB state - HEAD=" UINT_FMT ", TAIL=" UINT_FMT ", FREE=" UINT_FMT ", MARK=" UINT_FMT "\n",
                     head_count, tail_count, free_count, mark_count);
        if (mark_count > 0) {
            DEBUG_printf("gc_compact_copy: ERROR - found " UINT_FMT " blocks still marked as AT_MARK!\n", mark_count);
        }
    }
}

static void gc_update_references(mp_state_mem_area_t *area, gc_forward_table_t *forward_table) {
    // Update all internal object references
    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;

    for (size_t block = 0; block < max_block; block++) {
        byte block_kind = ATB_GET_KIND(area, block);

        // Only process live objects after copy phase converts
        if (block_kind == AT_HEAD) {
            // Count blocks in this object chain
            size_t block_count = 1;
            size_t next_block = block + 1;
            while (next_block < max_block && ATB_GET_KIND(area, next_block) == AT_TAIL) {
                block_count++;
                next_block++;
            }

            // Get object's memory and scan each word
            void **obj_start = (void **)PTR_FROM_BLOCK(area, block);
            size_t words_count = (block_count * BYTES_PER_BLOCK) / sizeof(void*);

            for (size_t i = 0; i < words_count; i++) {
                void *ref_ptr = obj_start[i];
                
                // Determine which heap area this reference points to
                mp_state_mem_area_t *ref_area;
                #if MICROPY_GC_SPLIT_HEAP
                ref_area = gc_get_ptr_area(ref_ptr);
                #else
                // Single heap: validate pointer range
                if (VERIFY_PTR(ref_ptr)) {
                    ref_area = &MP_STATE_MEM(area);
                } else {
                    continue;
                }
                #endif

                if (ref_area) {
                    size_t old_block = BLOCK_FROM_PTR(ref_area, ref_ptr);
                    size_t new_block = gc_forward_table_lookup(forward_table, old_block);
                    if (new_block != (size_t)-1 && new_block != old_block) {
                        obj_start[i] = (void *)PTR_FROM_BLOCK(ref_area, new_block);
                        DEBUG_printf("gc_update_ref: obj block " UINT_FMT " word " UINT_FMT ": pointer block " UINT_FMT " -> block " UINT_FMT "\n",
                                     block, i, old_block, new_block);
                    }
                }
            }

            block = next_block - 1;
        }
    }
    
    DEBUG_printf("gc_update_references: all object references updated\n");

    {
        size_t max_block_val = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
        size_t head_count = 0, tail_count = 0, free_count = 0, mark_count = 0;
        for (size_t block = 0; block < max_block_val; block++) {
            byte kind = ATB_GET_KIND(area, block);
            if (kind == AT_HEAD) head_count++;
            else if (kind == AT_TAIL) tail_count++;
            else if (kind == AT_FREE) free_count++;
            else if (kind == AT_MARK) mark_count++;
        }
        DEBUG_printf("gc_update_references: ATB state - HEAD=" UINT_FMT ", TAIL=" UINT_FMT ", FREE=" UINT_FMT ", MARK=" UINT_FMT "\n",
                     head_count, tail_count, free_count, mark_count);
    }
}

static void *gc_update_ptr(void *ptr, gc_forward_table_t *forward_table) {
    uintptr_t p = (uintptr_t)ptr;
    if (p == 0) {
        return NULL;
    }

    void *clean_ptr = (void *)(p & ~3);
    mp_state_mem_area_t *area = NULL;
    #if MICROPY_GC_SPLIT_HEAP
    area = gc_get_ptr_area(clean_ptr);
    #else
    if (VERIFY_PTR(clean_ptr)) {
        area = &MP_STATE_MEM(area);
    }
    #endif

    if (area != NULL) {
        size_t old_block = BLOCK_FROM_PTR(area, clean_ptr);
        size_t new_block = gc_forward_table_lookup(forward_table, old_block);
        if (new_block != (size_t)-1) {
            // Calculate offset within the block if the pointer wasn't exactly at block start
            uintptr_t offset = (uintptr_t)clean_ptr - (uintptr_t)PTR_FROM_BLOCK(area, old_block);
            void *new_ptr = (void *)((uintptr_t)PTR_FROM_BLOCK(area, new_block) + offset);
           
            DEBUG_printf("gc_update_ptr: pointer block " UINT_FMT " -> block " UINT_FMT "\n", old_block, new_block);
            // Re-apply original tag bits
            return (void *)((uintptr_t)new_ptr | (p & 3));
        }
    }
    return ptr;
}



static void gc_update_roots(mp_state_mem_area_t *area, gc_forward_table_t *forward_table) {
    DEBUG_printf("gc_update_roots: ENTER\n");
    #if !MICROPY_GC_SPLIT_HEAP
    area = &MP_STATE_MEM(area);
    #endif

    void **ptrs = (void **)(void *)&mp_state_ctx;
    size_t root_start_idx = offsetof(mp_state_ctx_t, thread.nlr_top) / sizeof(void *);
    size_t root_end_idx = offsetof(mp_state_ctx_t, vm.qstr_last_chunk) / sizeof(void *);

    for (size_t i = root_start_idx; i < root_end_idx; i++) {
        ptrs[i] = gc_update_ptr(ptrs[i], forward_table);
    }

    // If dicts moved, their internal table pointers also need updating (VERY IMPORTANT, DO NOT TOUCH)
    if (MP_STATE_THREAD(dict_locals)) _gc_update_dict_values(MP_STATE_THREAD(dict_locals), area, forward_table);
    if (MP_STATE_THREAD(dict_globals)) _gc_update_dict_values(MP_STATE_THREAD(dict_globals), area, forward_table);

    DEBUG_printf("gc_update_roots: all root pointers updated\n");
    DEBUG_printf("gc_update_roots: EXIT\n");

    {
        size_t max_block_val = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
        size_t head_count = 0, tail_count = 0, free_count = 0, mark_count = 0;
        for (size_t block = 0; block < max_block_val; block++) {
            byte kind = ATB_GET_KIND(area, block);
            if (kind == AT_HEAD) head_count++;
            else if (kind == AT_TAIL) tail_count++;
            else if (kind == AT_FREE) free_count++;
            else if (kind == AT_MARK) mark_count++;
        }
        DEBUG_printf("gc_update_roots: ATB state - HEAD=" UINT_FMT ", TAIL=" UINT_FMT ", FREE=" UINT_FMT ", MARK=" UINT_FMT "\n",
                     head_count, tail_count, free_count, mark_count);
    }
}

static void _gc_update_dict_values(mp_obj_dict_t *dict, mp_state_mem_area_t *area, 
                                   gc_forward_table_t *forward_table) {
    DEBUG_printf("_gc_update_dict_values: ENTER dict=%p\n", dict);
    if (dict == NULL) {
        DEBUG_printf("_gc_update_dict_values: dict is NULL, EXIT\n");
        return;
    }

    mp_map_t *map = &dict->map;
    map->table = (mp_map_elem_t *)gc_update_ptr(map->table, forward_table);
    
    if (map->table == NULL || map->alloc == 0) {
        DEBUG_printf("_gc_update_dict_values: dict map table is NULL, EXIT\n");
        return;
    }
    for (size_t i = 0; i < map->alloc; i++) {
        mp_map_elem_t *elem = &map->table[i];
        
        if (elem->key != MP_OBJ_NULL && elem->key != MP_OBJ_SENTINEL) {
            elem->key = (mp_obj_t)gc_update_ptr(elem->key, forward_table);
        }
        if (elem->value != MP_OBJ_NULL && elem->value != MP_OBJ_SENTINEL) {
            elem->value = (mp_obj_t)gc_update_ptr(elem->value, forward_table);
        }
        
        DEBUG_printf("_gc_update_dict_values: updated dict key=%p value=%p\n", elem->key, elem->value);


    }
    DEBUG_printf("_gc_update_dict_values: EXIT\n");
}

#endif // MICROPY_ENABLE_GC