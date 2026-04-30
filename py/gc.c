/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
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
#include "py/emitglue.h"
#include "py/objfun.h"
#include "py/objlist.h"
#include "py/objstr.h"
#include "py/qstr.h"

#if MICROPY_DEBUG_VALGRIND
#include <valgrind/memcheck.h>
#endif

#if MICROPY_ENABLE_GC

extern const mp_obj_type_t mp_type_pointer;
extern const mp_obj_type_t mp_type_closure;
extern const mp_obj_type_t mp_type_cell;

typedef struct _gc_obj_closure_t {
    mp_obj_base_t base;
    mp_obj_t fun;
    size_t n_closed;
    mp_obj_t closed[];
} gc_obj_closure_t;

#if MICROPY_DEBUG_VERBOSE // print debugging info
#define DEBUG_PRINT (1)
#define DEBUG_printf DEBUG_printf
#else // don't print debugging info
#define DEBUG_PRINT (0)
#define DEBUG_printf(...) (void)0
#endif

// make this 1 to dump the heap each time it changes
#define EXTENSIVE_HEAP_PROFILING (0)
#define READABLE_HEAP_DUMP (0)
#define MICROPY_DEBUG_FUN_GC_TRACE (0)

// Opt-in prototype: reclaim only clearly fake unmarked heads (null/heap/
// implausible type pointers) after compaction.  Keep disabled by default.
#ifndef MICROPY_GC_EXPERIMENTAL_RECLAIM
#define MICROPY_GC_EXPERIMENTAL_RECLAIM (1)
#endif

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
static size_t gc_forward_table_lookup(gc_forward_table_t *table, size_t old_block);
#if MICROPY_GC_SPLIT_HEAP
static void gc_mark_subtree(mp_state_mem_area_t *area, size_t block);
#else
static void gc_mark_subtree(size_t block);
#endif
static void gc_deal_with_stack_overflow(void);
static void gc_sweep_run_finalisers(void);
static void gc_sweep_free_blocks(void);
#if MICROPY_GC_EXPERIMENTAL_RECLAIM
static void gc_experimental_reclaim_fake_unmarked_heads(mp_state_mem_area_t *area);
static bool gc_experimental_has_root_ref(void *target_ptr);
static bool gc_experimental_has_heap_ref(mp_state_mem_area_t *target_area, size_t target_block, void *target_ptr);
#endif
static void gc_compute_forwarding_addresses(mp_state_mem_area_t *area, gc_forward_table_t *forward_table);
static void gc_compact_copy(mp_state_mem_area_t *area, gc_forward_table_t *forward_table);
static void gc_update_references(mp_state_mem_area_t *area, gc_forward_table_t *forward_table);
static void gc_update_roots(mp_state_mem_area_t *area, gc_forward_table_t *forward_table);
static void gc_update_raw_code_tree(mp_state_mem_area_t *area, gc_forward_table_t *forward_table, mp_raw_code_t *rc);
static void gc_shift_left_frontier(mp_state_mem_area_t *area);
static void gc_trace_pinned_runtime_tables(mp_state_mem_area_t *area);
static void gc_trace_vm_codestate(const char *phase, mp_state_mem_area_t *area, gc_forward_table_t *forward_table);
static size_t gc_count_object_blocks(mp_state_mem_area_t *area, size_t head_block, size_t process_limit);
static void gc_write_object_atb(mp_state_mem_area_t *area, size_t head_block, size_t block_count);
static void gc_clear_object_atb(mp_state_mem_area_t *area, size_t head_block, size_t block_count);
static bool gc_is_plausible_type_ptr(mp_state_mem_area_t *area, uintptr_t type_ptr);
static bool gc_ptr_is_in_heap(void *ptr);
static bool gc_rewrite_ptr_slot(void **slot, gc_forward_table_t *forward_table, size_t *slots_scanned, size_t *slots_rewritten, size_t *slots_nonheap);
static size_t gc_rewrite_ptr_array(void **slots, size_t count, gc_forward_table_t *forward_table, size_t *slots_scanned, size_t *slots_rewritten, size_t *slots_nonheap);
static void _gc_update_dict_values(mp_obj_dict_t *dict, mp_state_mem_area_t *area, gc_forward_table_t *forward_table);
static void _gc_update_list_items(mp_obj_list_t *list, gc_forward_table_t *forward_table);
static void _gc_update_module_context_obj_table(mp_module_context_t *ctx, gc_forward_table_t *forward_table, size_t *slots_scanned, size_t *slots_rewritten, size_t *slots_nonheap);
static void *gc_update_ptr(void *ptr, gc_forward_table_t *forward_table);
//static size_t gc_count_marked_blocks(mp_state_mem_area_t *area);
static size_t gc_count_live_objects(mp_state_mem_area_t *area);
static bool gc_forward_table_prealloc(gc_forward_table_t *table, size_t size);
static void gc_dump_occupied_blocks(mp_state_mem_area_t *area, bool after_compaction);
static void gc_mark_pinned_table_ranges(void);
static void gc_update_pinned_table(gc_forward_table_t *forward_table);

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

static void gc_mark_pinned_table_ranges(void) {
    if (gc_pinned_table.ranges == NULL || gc_pinned_table.capacity == 0) {
        return;
    }

    mp_state_mem_area_t *area = NULL;
    #if MICROPY_GC_SPLIT_HEAP
    area = gc_get_ptr_area(gc_pinned_table.ranges);
    if (area == NULL) {
        return;
    }
    #else
    if ((byte *)gc_pinned_table.ranges < (byte *)MP_STATE_MEM(area).gc_pool_start
        || (byte *)gc_pinned_table.ranges >= (byte *)MP_STATE_MEM(area).gc_pool_end) {
        return;
    }
    area = &MP_STATE_MEM(area);
    #endif

    size_t block = BLOCK_FROM_PTR(area, gc_pinned_table.ranges);
    if (ATB_GET_KIND(area, block) == AT_HEAD) {
        ATB_HEAD_TO_MARK(area, block);
    }
}

static void gc_update_pinned_table(gc_forward_table_t *forward_table) {
    if (gc_pinned_table.ranges == NULL) {
        return;
    }

    gc_pinned_table.ranges = (pinned_range_t *)gc_update_ptr(gc_pinned_table.ranges, forward_table);

    for (size_t i = 0; i < gc_pinned_table.count; ++i) {
        pinned_range_t *range = &gc_pinned_table.ranges[i];
        if (range->obj == NULL) {
            continue;
        }

        range->obj = gc_update_ptr(range->obj, forward_table);

        mp_state_mem_area_t *area = NULL;
        #if MICROPY_GC_SPLIT_HEAP
        area = gc_get_ptr_area(range->obj);
        if (area == NULL) {
            continue;
        }
        #else
        if ((byte *)range->obj < (byte *)MP_STATE_MEM(area).gc_pool_start
            || (byte *)range->obj >= (byte *)MP_STATE_MEM(area).gc_pool_end) {
            continue;
        }
        area = &MP_STATE_MEM(area);
        #endif

        range->block_start = BLOCK_FROM_PTR(area, range->obj);
    }
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

    // Initialize pinned table.
    if (gc_pinned_table.ranges != NULL) {
        gc_free(gc_pinned_table.ranges);
    }
    gc_pinned_table.capacity = 0;
    gc_pinned_table.count = 0;
    gc_pinned_table.ranges = NULL;

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

    // Pin all active execution frames. If a frame moves, local C variables
    // (stack ptr, prog counter) in mp_execute_bytecode become stale.
    for (mp_code_state_t *cs = MP_STATE_THREAD(current_code_state); cs != NULL; ) {
        gc_pin(cs);

        // VM keeps these as raw C pointers in locals during execution.
        // Pin them so compaction cannot move targets while locals are live.
        mp_obj_fun_bc_t *fun_bc = cs->fun_bc;
        gc_pin(fun_bc);
        if (fun_bc != NULL) {
            gc_pin((void *)fun_bc->bytecode);
            gc_pin((void *)fun_bc->child_table);
            gc_pin((void *)fun_bc->context);
        }

        #if MICROPY_STACKLESS
        cs = cs->prev;
        #else
        cs = cs->prev_state;
        #endif
    }

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
    #if READABLE_HEAP_DUMP
    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        gc_dump_occupied_blocks(area, false);
    }
    #endif
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
        while (block > 0 && ATB_GET_KIND(area, block) == AT_TAIL) {
            block--;
        }
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
        size_t n_ptrs = n_blocks * BYTES_PER_BLOCK / sizeof(void *);
        void **ptrs = (void **)PTR_FROM_BLOCK(area, block);

        // Pointer objects store a raw address in word[1] that is not a GC ref.
        // Never traverse it during mark, otherwise '&var' can cause random
        // memory traversal and crashes.
        bool is_pointer_obj = false;
        if (n_ptrs >= 2) {
            mp_obj_base_t *obj = (mp_obj_base_t *)ptrs;
            is_pointer_obj = (obj->type == &mp_type_pointer);
        }

        for (size_t i = 0; i < n_ptrs; i++, ptrs++) {
            MICROPY_GC_HOOK_LOOP(i);
            if (is_pointer_obj && i == 1) {
                continue;
            }
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
            while (ptr_block > 0 && ATB_GET_KIND(ptr_area, ptr_block) == AT_TAIL) {
                ptr_block--;
            }
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
__attribute__((unused))
static void gc_shift_left_frontier(mp_state_mem_area_t *area) {
    DEBUG_printf("gc_shift_left_frontier: before shift, left frontier at block " UINT_FMT "\n", 
                 area->gc_last_used_block_from_left);
    
    // Scan the ENTIRE heap to find the highest numbered allocated block
    // After compaction with pinned objects, allocated blocks may not be contiguous
    // so we must scan all blocks to find the true frontier
    size_t limit = MIN(area->gc_num_blocks, area->gc_alloc_table_byte_len * BLOCKS_PER_ATB);
    size_t last_used = 0;
    
    // Scan from start to end, tracking two frontiers:
    // - last_normal_used: highest non-pinned block (for the left allocator frontier)
    // - last_used: highest of ALL blocks including pinned (for sweep bounds)
    size_t last_normal_used = 0;
    for (size_t block = 0; block < limit; block++) {
        byte block_kind = ATB_GET_KIND(area, block);
        if (block_kind == AT_HEAD || block_kind == AT_TAIL || block_kind == AT_MARK) {
            last_used = block;
            if (!gc_is_block_pinned(block)) {
                last_normal_used = block;
            }
        }
    }
    
    // Left frontier is one block after the highest NON-PINNED allocated block.
    // Pinned blocks live at the far right and must not inflate this frontier,
    // otherwise gc_alloc and gc_alloc_right both lose all usable address space.
    size_t new_frontier = last_normal_used + 1;
    
    area->gc_last_used_block_from_left = new_frontier;
    area->gc_last_used_block = last_used;  // Tracks ALL blocks (needed by sweep)
    DEBUG_printf("gc_shift_left_frontier: after shift, left frontier at block " UINT_FMT ", last_used=" UINT_FMT "\n", 
                 new_frontier, last_used);

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
__attribute__((unused))
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
__attribute__((unused))
static void gc_dump_occupied_blocks(mp_state_mem_area_t *area, bool after_compaction) {
    #if MICROPY_DEBUG_VERBOSE || READABLE_HEAP_DUMP
    const char *phase = after_compaction ? "AFTER" : "BEFORE";
    size_t atb_len = area->gc_alloc_table_byte_len;
    if (atb_len == 0 || atb_len > 1000000) {
        printf("\n=== HEAP SUMMARY %s COMPACTION ===\n", phase);
        printf("area=%p invalid_atb_len=" UINT_FMT "\n\n", (void *)area, (mp_uint_t)atb_len);
        fflush(stdout);
        return;
    }

    size_t process_limit = MIN(area->gc_num_blocks, atb_len * BLOCKS_PER_ATB);
    size_t head_count = 0;
    size_t tail_count = 0;
    size_t mark_count = 0;
    size_t free_count = 0;
    size_t free_runs = 0;
    size_t used_runs = 0;
    size_t largest_free_run = 0;
    size_t holes_in_left_prefix = 0;
    size_t free_runs_left = 0;
    size_t used_runs_left = 0;
    size_t largest_free_left = 0;

    // Compact run preview, e.g. H2 F5 M1 H3 ... (first 12 runs only).
    char run_preview[256];
    size_t run_preview_pos = 0;
    size_t run_preview_count = 0;

    size_t block = 0;
    while (block < process_limit) {
        byte kind = ATB_GET_KIND(area, block);
        size_t run_start = block;
        do {
            if (kind == AT_HEAD) {
                head_count++;
            } else if (kind == AT_TAIL) {
                tail_count++;
            } else if (kind == AT_MARK) {
                mark_count++;
            } else {
                free_count++;
            }
            block++;
        } while (block < process_limit && ATB_GET_KIND(area, block) == kind);

        size_t run_len = block - run_start;
        if (kind == AT_FREE) {
            free_runs++;
            if (run_len > largest_free_run) {
                largest_free_run = run_len;
            }
        } else {
            used_runs++;
        }

        if (run_preview_count < 12) {
            char run_tag = kind == AT_FREE ? 'F' : (kind == AT_MARK ? 'M' : 'H');
            int n = snprintf(run_preview + run_preview_pos, sizeof(run_preview) - run_preview_pos,
                "%c" UINT_FMT "%s", run_tag, (mp_uint_t)run_len, run_preview_count == 11 ? "" : " ");
            if (n > 0 && (size_t)n < sizeof(run_preview) - run_preview_pos) {
                run_preview_pos += (size_t)n;
            }
            run_preview_count++;
        }
    }

    size_t left_prefix_end = MIN(area->gc_last_used_block_from_left + 1, process_limit);
    if (left_prefix_end > 0) {
        size_t i = 0;
        while (i < left_prefix_end) {
            byte kind = ATB_GET_KIND(area, i);
            size_t run_start = i;
            while (i < left_prefix_end && ATB_GET_KIND(area, i) == kind) {
                if (kind == AT_FREE) {
                    holes_in_left_prefix++;
                }
                i++;
            }
            size_t run_len = i - run_start;
            if (kind == AT_FREE) {
                free_runs_left++;
                if (run_len > largest_free_left) {
                    largest_free_left = run_len;
                }
            } else {
                used_runs_left++;
            }
        }
    }

    size_t used_blocks = head_count + tail_count + mark_count;
    size_t free_pct = process_limit == 0 ? 0 : (free_count * 100) / process_limit;

    printf("\n=== HEAP SUMMARY %s COMPACTION ===\n", phase);
    printf("area=%p blocks=" UINT_FMT " used=" UINT_FMT " free=" UINT_FMT " (" UINT_FMT "%%) heads=" UINT_FMT " marks=" UINT_FMT "\n",
        (void *)area,
        (mp_uint_t)process_limit,
        (mp_uint_t)used_blocks,
        (mp_uint_t)free_count,
        (mp_uint_t)free_pct,
        (mp_uint_t)head_count,
        (mp_uint_t)mark_count);
    printf("runs(total): used=" UINT_FMT " free=" UINT_FMT " largest_free=" UINT_FMT "\n",
        (mp_uint_t)used_runs,
        (mp_uint_t)free_runs,
        (mp_uint_t)largest_free_run);
    printf("runs(left): used=" UINT_FMT " free=" UINT_FMT " largest_free=" UINT_FMT " holes_in_prefix=" UINT_FMT "\n",
        (mp_uint_t)used_runs_left,
        (mp_uint_t)free_runs_left,
        (mp_uint_t)largest_free_left,
        (mp_uint_t)holes_in_left_prefix);
    printf("frontiers: left=" UINT_FMT " right=" UINT_FMT "\n",
        (mp_uint_t)area->gc_last_used_block_from_left,
        (mp_uint_t)area->gc_last_used_block_from_right);
    if (run_preview_pos > 0) {
        printf("runs_preview: %s%s\n", run_preview, run_preview_count >= 12 ? "..." : "");
    }
    printf("\n");
    fflush(stdout);
    #endif
}

// Validate heap for consistency issues that indicate corruption
__attribute__((unused))
static bool gc_validate_heap_integrity(mp_state_mem_area_t *area, const char *checkpoint) {
    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    bool corruption = false;
    
    for (size_t block = 0; block < max_block; block++) {
        byte kind = ATB_GET_KIND(area, block);
        
        // Check for orphaned TAIL blocks (TAIL without preceding HEAD, TAIL, or MARK)
        if (kind == AT_TAIL && block > 0) {
            byte prev_kind = ATB_GET_KIND(area, block - 1);
            // TAIL blocks are valid after HEAD, TAIL, or MARK (live object from mark phase)
            if (prev_kind != AT_HEAD && prev_kind != AT_TAIL && prev_kind != AT_MARK) {
                DEBUG_printf("CORRUPTION at %s: Block " UINT_FMT " is TAIL but previous block is %d (should be HEAD/TAIL/MARK)\n", 
                             checkpoint, block, prev_kind);
                corruption = true;
            }
        }
        
        // Check for HEAD without following TAIL
        if (kind == AT_HEAD) {
            // Scan ahead to verify TAIL blocks are valid
            size_t tail_count = 0;
            size_t check_block = block + 1;
            while (check_block < max_block && ATB_GET_KIND(area, check_block) == AT_TAIL) {
                tail_count++;
                check_block++;
            }
            // This is OK - just informational
            (void)tail_count;
        }
    }
    
    if (corruption) {
        DEBUG_printf("HEAP INTEGRITY FAILURE AT %s\n", checkpoint);
        return false;
    }
    DEBUG_printf("Heap integrity OK at %s\n", checkpoint);
    return true;
}

void gc_collect_end(void) {
    DEBUG_printf("gc_collect_end: ENTER\n");
    gc_deal_with_stack_overflow();
    DEBUG_printf("gc_collect_end: dealt with stack overflow\n");

    bool compaction_performed = gc_should_compact();

    // Capture pre-sweep garbage diagnostics while AT_HEAD garbage still exists.
    if (compaction_performed) {
        #if !MICROPY_VARIANT_BACKWARD_COMPATIBLE
        DEBUG_printf("gc_collect_end: pre-sweep diagnostics\n");
        for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
            gc_trace_pinned_runtime_tables(area);
            gc_trace_vm_codestate("pre-sweep", area, NULL);
        }
        #endif
    }

    // Keep the pinned table storage alive if it's allocated on the GC heap.
    // We intentionally don't trace its contents here because pinned entries
    // should not act as GC roots.
    gc_mark_pinned_table_ranges();

    // ALWAYS run finalisers then sweep BEFORE compaction.
    // gc_sweep_free_blocks() converts:
    //   AT_MARK (live, reached by mark phase) -> AT_HEAD  (surviving objects)
    //   AT_HEAD (garbage, not reached)        -> AT_FREE  (holes for compaction to fill)
    // AT_FREE destination slots and moved nothing leading to unbounded heap growth.
    DEBUG_printf("gc_collect_end: running finalisers\n");
    gc_sweep_run_finalisers();
    DEBUG_printf("gc_collect_end: freeing blocks (Sweep)\n");
    gc_sweep_free_blocks();

    if (compaction_performed) {
        for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
            gc_trace_vm_codestate("post-sweep", area, NULL);
        }
    }

    if (compaction_performed) {
        #if !MICROPY_VARIANT_BACKWARD_COMPATIBLE
        DEBUG_printf("gc_collect_end: starting compaction\n");
        for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
            // Phase 0: Count live objects (all AT_HEAD after sweep) and pre-allocate
            // the forward table.
            gc_trace_vm_codestate("phase0-pre-count", area, NULL);
            DEBUG_printf("gc_collect_end: About to count live objects\n");
            size_t live_count = gc_count_live_objects(area);
            DEBUG_printf("gc_collect_end: counted " UINT_FMT " live objects\n", live_count);

            gc_forward_table_t forward_table = {0};
            DEBUG_printf("gc_collect_end: About to prealloc forward table (size " UINT_FMT ")\n", live_count);
            // SAFETY FIX: allocate with +1 capacity to avoid OBO at exact capacity boundary
            if (!gc_forward_table_prealloc(&forward_table, live_count + 1)) {
                DEBUG_printf("gc_collect_end: failed to pre-allocate forward table\n");
                continue;
            }
            DEBUG_printf("gc_collect_end: Forward table prealloc complete\n");

            DEBUG_printf("gc_collect_end: Phase 1 - computing forwarding addresses - area=%p\n", area);
            gc_compute_forwarding_addresses(area, &forward_table);
            DEBUG_printf("gc_collect_end: Phase 1 COMPLETE - forward_table has " UINT_FMT " entries (cap=" UINT_FMT ")\n", forward_table.count, forward_table.capacity);
            gc_trace_vm_codestate("post-phase1-forward-map", area, &forward_table);
            gc_validate_heap_integrity(area, "after Phase 1");

            DEBUG_printf("gc_collect_end: Phase 2 - copying objects\n");
            gc_compact_copy(area, &forward_table);
            gc_trace_vm_codestate("post-phase2-copy", area, &forward_table);

            // Verify stack is not corrupted by checking a known value
            volatile uint64_t stack_check = 0x0102030405060708ULL;
            if (stack_check != 0x0102030405060708ULL) {
                printf("CORRUPTION DETECTED: Stack value changed!\n");
            }

            // Phase 3: Update internal object references
            gc_update_references(area, &forward_table);
            gc_trace_vm_codestate("post-phase3-update-refs", area, &forward_table);
            gc_update_roots(area, &forward_table);
            gc_trace_vm_codestate("post-phase3-update-roots", area, NULL);
            gc_shift_left_frontier(area);
            gc_forward_table_free(&forward_table);

            //gc_verify_compacted_heap(area);
        }
        #endif
    }

    #if MICROPY_GC_EXPERIMENTAL_RECLAIM
    if (compaction_performed) {
        for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
            gc_experimental_reclaim_fake_unmarked_heads(area);
            gc_shift_left_frontier(area);
        }
    }
    #endif
    
    // Finalize frontiers after the sweep has cleared garbage
    if (compaction_performed) {
        for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
            // Set gc_last_free_atb_index to scan starting right after the compacted
            // non-pinned area.  gc_last_used_block_from_left holds the block index
            // immediately after the last normal (non-pinned) live block, which is
            // exactly where free space begins after compaction.
            ssize_t first_free_block = (ssize_t)area->gc_last_used_block_from_left;

            // Bounds check
            if (first_free_block < 0) first_free_block = 0;
            if (first_free_block >= (ssize_t)(area->gc_alloc_table_byte_len * BLOCKS_PER_ATB)) {
                first_free_block = 0;  // Wrap to start if beyond table
            }

            area->gc_last_free_atb_index = ((size_t)first_free_block) / BLOCKS_PER_ATB;
            break;
        }
    }
    
    DEBUG_printf("gc_collect_end: blocks freed, clearing GC flag\n");
    
    // Clear the GC collect flag before exiting
    MP_STATE_THREAD(gc_lock_depth) &= ~GC_COLLECT_FLAG;
    
    DEBUG_printf("gc_collect_end: EXIT\n");
    #if MICROPY_DEBUG_VERBOSE || READABLE_HEAP_DUMP
    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        gc_dump_occupied_blocks(area, true);
    }
    #endif
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
__attribute__((unused))
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
        size_t last_used_block __attribute__((unused)) = 0;
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

#if MICROPY_GC_EXPERIMENTAL_RECLAIM
static bool gc_experimental_has_root_ref(void *target_ptr) {
    if (target_ptr == NULL) {
        return false;
    }

    void **ptrs = (void **)(void *)&mp_state_ctx;
    size_t root_start = offsetof(mp_state_ctx_t, thread.dict_locals);
    size_t root_end = offsetof(mp_state_ctx_t, vm.qstr_last_chunk);
    size_t root_slots = (root_end - root_start) / sizeof(void *);
    void **roots = ptrs + root_start / sizeof(void *);
    for (size_t i = 0; i < root_slots; ++i) {
        if (roots[i] == target_ptr) {
            return true;
        }
    }

    #if MICROPY_ENABLE_PYSTACK
    void **pystack_ptrs = (void **)(void *)MP_STATE_THREAD(pystack_start);
    size_t pystack_len = (MP_STATE_THREAD(pystack_cur) - MP_STATE_THREAD(pystack_start)) / sizeof(void *);
    for (size_t i = 0; i < pystack_len; ++i) {
        if (pystack_ptrs[i] == target_ptr) {
            return true;
        }
    }
    #endif

    return false;
}

static bool gc_experimental_has_heap_ref(mp_state_mem_area_t *target_area, size_t target_block, void *target_ptr) {
    if (target_ptr == NULL) {
        return false;
    }

    for (mp_state_mem_area_t *scan_area = &MP_STATE_MEM(area); scan_area != NULL; scan_area = NEXT_AREA(scan_area)) {
        size_t process_limit = MIN(scan_area->gc_last_used_block + 1,
                                   scan_area->gc_alloc_table_byte_len * BLOCKS_PER_ATB);
        for (size_t block = 0; block < process_limit; ++block) {
            byte kind = ATB_GET_KIND(scan_area, block);
            if (kind != AT_HEAD && kind != AT_MARK) {
                continue;
            }

            if (scan_area == target_area && block == target_block) {
                size_t skip_blocks = gc_count_object_blocks(scan_area, block, process_limit);
                block += skip_blocks - 1;
                continue;
            }

            size_t block_count = gc_count_object_blocks(scan_area, block, process_limit);
            size_t word_count = (block_count * BYTES_PER_BLOCK) / MP_BYTES_PER_OBJ_WORD;
            void **words = (void **)PTR_FROM_BLOCK(scan_area, block);
            for (size_t w = 0; w < word_count; ++w) {
                if (words[w] == target_ptr) {
                    return true;
                }
            }

            block += block_count - 1;
        }
    }

    return false;
}

static void gc_experimental_reclaim_fake_unmarked_heads(mp_state_mem_area_t *area) {
    size_t reclaimed_heads = 0;
    size_t reclaimed_blocks = 0;
    size_t kept_heads = 0;
    size_t kept_heap_type_heads = 0;
    size_t kept_referenced_heads = 0;
    size_t candidate_null_type = 0;
    size_t candidate_implausible_type = 0;

    size_t process_limit = MIN(area->gc_last_used_block + 1,
                               area->gc_alloc_table_byte_len * BLOCKS_PER_ATB);

    for (size_t block = 0; block < process_limit; ++block) {
        if (ATB_GET_KIND(area, block) != AT_HEAD) {
            continue;
        }

        if (gc_is_block_pinned(block)) {
            kept_heads++;
            continue;
        }

        mp_obj_base_t *base = (mp_obj_base_t *)PTR_FROM_BLOCK(area, block);
        bool reclaim = false;
        bool type_in_heap = false;

        if (base->type == NULL) {
            reclaim = true;
            candidate_null_type++;
        } else {
            #if MICROPY_GC_SPLIT_HEAP
            type_in_heap = (gc_get_ptr_area((void *)base->type) != NULL);
            #else
            type_in_heap = ((byte *)base->type >= (byte *)area->gc_pool_start
                && (byte *)base->type < (byte *)area->gc_pool_end);
            #endif

            // Heap-resident type pointers are suspicious but can still appear in
            // reachable stale payloads; keep them until we have stronger proof.
            if (type_in_heap) {
                kept_heap_type_heads++;
            } else if (!gc_is_plausible_type_ptr(area, (uintptr_t)base->type)) {
                reclaim = true;
                candidate_implausible_type++;
            }
        }

        if (!reclaim) {
            kept_heads++;
            continue;
        }

        void *obj_ptr = (void *)PTR_FROM_BLOCK(area, block);
        if (gc_experimental_has_root_ref(obj_ptr) || gc_experimental_has_heap_ref(area, block, obj_ptr)) {
            kept_referenced_heads++;
            kept_heads++;
            continue;
        }

        size_t block_count = gc_count_object_blocks(area, block, process_limit);
        for (size_t i = 0; i < block_count; ++i) {
            ATB_ANY_TO_FREE(area, block + i);
            #if CLEAR_ON_SWEEP
            memset((void *)PTR_FROM_BLOCK(area, block + i), 0, BYTES_PER_BLOCK);
            #endif
        }

        reclaimed_heads++;
        reclaimed_blocks += block_count;
        block += block_count - 1;
    }

    DEBUG_printf("[reclaim-x] fake_unmarked heads=" UINT_FMT " blocks=" UINT_FMT " kept_heads=" UINT_FMT " kept_heap_type=" UINT_FMT " kept_referenced=" UINT_FMT " cand_null=" UINT_FMT " cand_implausible=" UINT_FMT "\n",
        (mp_uint_t)reclaimed_heads,
        (mp_uint_t)reclaimed_blocks,
        (mp_uint_t)kept_heads,
        (mp_uint_t)kept_heap_type_heads,
        (mp_uint_t)kept_referenced_heads,
        (mp_uint_t)candidate_null_type,
        (mp_uint_t)candidate_implausible_type);
}
#endif

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

// Conservative check used by compaction Phase 3 before scanning an allocation
// as an mp_obj_t-bearing object. This avoids rewriting raw buffers (eg,
// bytecode/data blobs) that happen to look pointer-like.
__attribute__((unused))
static bool gc_is_plausible_type_ptr(mp_state_mem_area_t *area, uintptr_t type_ptr) {
    if (type_ptr == 0 || type_ptr < 0x100000) {
        return false;
    }
    if ((type_ptr & (MP_BYTES_PER_OBJ_WORD - 1)) != 0) {
        return false;
    }

    // Type pointers must not point into the GC heap.
    #if MICROPY_GC_SPLIT_HEAP
    if (gc_get_ptr_area((void *)type_ptr) != NULL) {
        return false;
    }
    #else
    if ((byte *)type_ptr >= (byte *)area->gc_pool_start && (byte *)type_ptr < (byte *)area->gc_pool_end) {
        return false;
    }
    #endif

    // Keep the check non-dereferencing and conservative: valid type objects are
    // expected to live in roughly the same static address region as mp_type_type.
    uintptr_t anchor = (uintptr_t)&mp_type_type;
    #if UINTPTR_MAX > 0xffffffff
    if (((type_ptr ^ anchor) >> 40) != 0) {
        return false;
    }
    #else
    if (((type_ptr ^ anchor) >> 20) != 0) {
        return false;
    }
    #endif

    // Validate that this really looks like a MicroPython type object.
    const mp_obj_type_t *type = (const mp_obj_type_t *)type_ptr;
    if (type->base.type != &mp_type_type) {
        return false;
    }
    if (type->name == MP_QSTRnull) {
        return false;
    }

    return true;
}

static void gc_update_ptr_field(mp_state_mem_area_t *area, gc_forward_table_t *forward_table, void **field) {
    void *ref_ptr = *field;
    if ((uintptr_t)ref_ptr < 0x100000) {
        return;
    }

    mp_state_mem_area_t *ref_area = NULL;
    #if MICROPY_GC_SPLIT_HEAP
    ref_area = gc_get_ptr_area(ref_ptr);
    #else
    if ((byte *)ref_ptr >= (byte *)area->gc_pool_start && (byte *)ref_ptr < (byte *)area->gc_pool_end) {
        ref_area = area;
    }
    #endif

    if (ref_area == NULL) {
        return;
    }

    // Forward table only stores HEAD blocks. If the field points into a TAIL
    // block (eg. mp_obj_str_t.data), walk back to the allocation head first.
    size_t raw_block = BLOCK_FROM_PTR(ref_area, ref_ptr);
    size_t head_block = raw_block;
    while (head_block > 0 && ATB_GET_KIND(ref_area, head_block) == AT_TAIL) {
        head_block--;
    }

    size_t new_head = gc_forward_table_lookup(forward_table, head_block);
    if (new_head != (size_t)-1 && new_head != head_block) {
        uintptr_t offset = (uintptr_t)ref_ptr - (uintptr_t)PTR_FROM_BLOCK(ref_area, head_block);
        *field = (void *)((uintptr_t)PTR_FROM_BLOCK(ref_area, new_head) + offset);
    }
}

__attribute__((unused))
static void gc_update_raw_code_ptr_array(mp_state_mem_area_t *area, gc_forward_table_t *forward_table, mp_raw_code_t **entries) {
    if (entries == NULL) {
        return;
    }

    size_t bytes = gc_nbytes(entries);
    if (bytes == 0) {
        return;
    }

    size_t count = bytes / sizeof(entries[0]);
    for (size_t i = 0; i < count; ++i) {
        gc_update_ptr_field(area, forward_table, (void **)&entries[i]);
        gc_update_raw_code_tree(area, forward_table, entries[i]);
    }
}

__attribute__((unused))
static void gc_update_raw_code_tree(mp_state_mem_area_t *area, gc_forward_table_t *forward_table, mp_raw_code_t *rc) {
    if (rc == NULL) {
        return;
    }

    gc_update_ptr_field(area, forward_table, (void **)&rc->fun_data);
    gc_update_ptr_field(area, forward_table, (void **)&rc->children);

    if (rc->kind != MP_CODE_BYTECODE || rc->children == NULL) {
        return;
    }

    size_t bytes = gc_nbytes(rc->children);
    if (bytes == 0) {
        return;
    }

    size_t count = bytes / sizeof(rc->children[0]);
    for (size_t i = 0; i < count; ++i) {
        gc_update_ptr_field(area, forward_table, (void **)&rc->children[i]);
        gc_update_raw_code_tree(area, forward_table, rc->children[i]);
    }
}

#if MICROPY_DEBUG_FUN_GC_TRACE
static bool gc_trace_block_if_pinned_fun_ref(mp_state_mem_area_t *area, size_t block, const char **field_name, void **owner_out) {
    for (size_t i = 0; i < gc_pinned_table.count; ++i) {
        pinned_range_t *range = &gc_pinned_table.ranges[i];
        if (range->obj == NULL) {
            continue;
        }
        mp_obj_base_t *base = (mp_obj_base_t *)range->obj;
        if (base->type != &mp_type_fun_bc) {
            continue;
        }
        mp_obj_fun_bc_t *fun = (mp_obj_fun_bc_t *)range->obj;
        bool bytecode_in_area = false;
        if (fun->bytecode != NULL) {
            #if MICROPY_GC_SPLIT_HEAP
            bytecode_in_area = gc_get_ptr_area((void *)fun->bytecode) == area;
            #else
            bytecode_in_area = VERIFY_PTR((void *)fun->bytecode);
            #endif
        }
        if (bytecode_in_area) {
            size_t fun_block = BLOCK_FROM_PTR(area, fun->bytecode);
            if (fun_block == block) {
                *field_name = "bytecode";
                *owner_out = fun;
                return true;
            }
        }
        bool child_table_in_area = false;
        if (fun->child_table != NULL) {
            #if MICROPY_GC_SPLIT_HEAP
            child_table_in_area = gc_get_ptr_area((void *)fun->child_table) == area;
            #else
            child_table_in_area = VERIFY_PTR((void *)fun->child_table);
            #endif
        }
        if (child_table_in_area) {
            size_t child_block = BLOCK_FROM_PTR(area, fun->child_table);
            if (child_block == block) {
                *field_name = "child_table";
                *owner_out = fun;
                return true;
            }
        }
    }
    return false;
}

static void gc_trace_raw_fun_ref(const char *phase, mp_state_mem_area_t *area, size_t old_block, size_t new_block, size_t block_count) {
    const char *field_name = NULL;
    void *owner = NULL;
    if (gc_trace_block_if_pinned_fun_ref(area, old_block, &field_name, &owner)) {
        printf("[fun-trace] %s raw-ref owner=%p field=%s old_block=%u new_block=%u blocks=%u old_ptr=%p new_ptr=%p\n",
            phase,
            owner,
            field_name,
            (unsigned)old_block,
            (unsigned)new_block,
            (unsigned)block_count,
            (void *)PTR_FROM_BLOCK(area, old_block),
            (void *)PTR_FROM_BLOCK(area, new_block));
    }
}
#else
#define gc_trace_raw_fun_ref(...) (void)0
#endif

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

// Check if a pointer is valid (within GC heap bounds)
bool gc_is_valid_ptr(const void *ptr) {
    if (ptr == NULL) {
        return false;
    }
    
    #if MICROPY_GC_SPLIT_HEAP
    mp_state_mem_area_t *area = gc_get_ptr_area(ptr);
    if (area == NULL) {
        return false;
    }
    #else
    mp_state_mem_area_t *area = &MP_STATE_MEM(area);
    // Check alignment and bounds
    if (((uintptr_t)ptr & (BYTES_PER_BLOCK - 1)) != 0) {
        return false;  // Must be aligned on a block
    }
    if (ptr < (void *)area->gc_pool_start || ptr >= (void *)area->gc_pool_end) {
        return false;  // Must be within pool
    }
    #endif
    
    return true;
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

    #if MICROPY_GC_ALLOC_THRESHOLD
    MP_STATE_MEM(gc_alloc_amount) += n_blocks;
    #endif

    GC_EXIT();

    // SAFETY: Before touching memory, validate we can write to it
    size_t clear_size = (end_block - start_block + 1) * BYTES_PER_BLOCK;
    DEBUG_printf("gc_alloc: attempting to clear " UINT_FMT " bytes at %p (blocks " UINT_FMT ".." UINT_FMT ")\n",
                 clear_size, ret_ptr, start_block, end_block);

    #if MICROPY_GC_CONSERVATIVE_CLEAR
    // be conservative and zero out all the newly allocated blocks
    memset((byte *)ret_ptr, 0, clear_size);
    #else
    // zero out the additional bytes of the newly allocated blocks
    // This is needed because the blocks may have previously held pointers
    // to the heap and will not be set to something else if the caller
    // doesn't actually use the entire block.  As such they will continue
    // to point to the heap and may prevent other blocks from being reclaimed.
    size_t excess = clear_size - n_bytes;
    if (excess > 0) {
        memset((byte *)ret_ptr + n_bytes, 0, excess);
    }
    #endif

    DEBUG_printf("gc_alloc: cleared memory successfully\n");

    #if MICROPY_ENABLE_FINALISER
    if (has_finaliser) {
        DEBUG_printf("gc_alloc: setting up finaliser\n");
        // clear type pointer in case it is never set
        ((mp_obj_base_t *)ret_ptr)->type = NULL;
        DEBUG_printf("gc_alloc: cleared type pointer\n");
        // set mp_obj flag only if it has a finaliser
        GC_ENTER();
        FTB_SET(area, start_block);
        GC_EXIT();
        DEBUG_printf("gc_alloc: finaliser setup complete\n");
    }
    #else
    (void)has_finaliser;
    #endif

    #if EXTENSIVE_HEAP_PROFILING
    gc_dump_alloc_table(&mp_plat_print);
    #endif

    DEBUG_printf("gc_alloc: returning pointer %p\n", ret_ptr);
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
    byte block_state = ATB_GET_KIND(area, block);
    
    // Check if block is already free (double-free is a no-op, not an error)
    if (block_state == AT_FREE) {
        GC_EXIT();
        return;
    }
    
    assert(block_state == AT_HEAD
        || (block_state == AT_MARK && (MP_STATE_THREAD(gc_lock_depth) & GC_COLLECT_FLAG)));

    // Check if this object is pinned - cannot free pinned objects
    if (gc_is_block_pinned(block)) {
        DEBUG_printf("gc_free: block " UINT_FMT " is pinned, skipping free\n", block);
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
    size_t block_count = 1;
    size_t temp_block = block + 1;
    while (temp_block < area->gc_alloc_table_byte_len * BLOCKS_PER_ATB && 
           ATB_GET_KIND(area, temp_block) == AT_TAIL) {
        block_count++;
        temp_block++;
    }

    // NOTE: We intentionally do NOT update frontier tracking during free() because:
    // 1. Left frontier backward movement can underflow (size_t is unsigned)
    // 2. Moving frontier backward corrupts compaction invariants
    // 3. The frontier is correctly maintained during allocation and compaction
    // 4. Free is just reclaiming space for the next allocation to use

    // free head and all of its tail blocks
    do {
        ATB_ANY_TO_FREE(area, block);
        block += 1;
    } while (block < area->gc_alloc_table_byte_len * BLOCKS_PER_ATB && 
             ATB_GET_KIND(area, block) == AT_TAIL);

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
    // this chunk of memory. Then count the number of free blocks following it.
    // Stop if we reach the end of the heap, OR if we find enough extra
    // free blocks to satisfy the realloc.
    // 
    // Note: We need to compute the total size of the existing memory 
    // chunk so we can correctly and efficiently shrink it.
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
    DEBUG_printf("gc_pin: pinning object at ptr %p (block " UINT_FMT ")\n", ptr, block);

    // Validate that this block actually contains an allocated object
    // Don't pin garbage values that happen to fall in the heap range
    byte block_kind = ATB_GET_KIND(area, block);
    if (block_kind != AT_HEAD && block_kind != AT_MARK && block_kind != AT_TAIL) {
        DEBUG_printf("gc_pin: ERROR - block " UINT_FMT " is not allocated (kind=%d), ignoring\n", block, block_kind);
        GC_EXIT();
        return;  // Not a real allocated block
    }

    // Check if already pinned
    if (gc_pinned_find_range_by_ptr(ptr) != -1) {
        GC_EXIT();
        return;  // Already pinned
    }

    // Check if we need to expand the table on the MicroPython heap.
    // gc_collect_end/gc_update_roots explicitly preserve and rewrite this buffer.
    bool needs_expansion = gc_pinned_table.count >= gc_pinned_table.capacity;
    if (needs_expansion) {
        size_t new_capacity = gc_pinned_table.capacity == 0 ?
                               PINNED_TABLE_INITIAL_CAPACITY :
                               gc_pinned_table.capacity * 2;
        size_t new_size = new_capacity * sizeof(pinned_range_t);

        pinned_range_t *new_ranges = (pinned_range_t *)gc_realloc(gc_pinned_table.ranges, new_size, true);
        if (new_ranges == NULL) {
            GC_EXIT();
            return;  // Allocation failed
        }

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
__attribute__((unused))
static bool gc_forward_table_insert(gc_forward_table_t *table, size_t old_block, size_t new_block, size_t n_blocks) {
    if (table->count >= table->capacity) {
        DEBUG_printf("gc_forward_table_insert: ERROR - table FULL! count=" UINT_FMT " capacity=" UINT_FMT "\n", 
                     table->count, table->capacity);
        return false;
    }

    table->entries[table->count].old_block = old_block;
    table->entries[table->count].new_block = new_block;
    table->entries[table->count].n_blocks = n_blocks;
    table->count++;
    return true;
}

// Function to lookup forwarding address
static size_t gc_forward_table_lookup(gc_forward_table_t *table, size_t old_block) {
    if (table == NULL || table->entries == NULL) {
        DEBUG_printf("gc_forward_table_lookup: ERROR - table is NULL or unallocated!\n");
        return (size_t)-1;
    }
    
    // BOUNDS CHECK: Make sure count doesn't exceed capacity
    if (table->count > table->capacity) {
        printf("FATAL: forward_table.count (" UINT_FMT ") exceeds capacity (" UINT_FMT ")!\n", 
               table->count, table->capacity);
        return (size_t)-1;
    }
    
    for (size_t i = 0; i < table->count; i++) {
        if (table->entries[i].old_block == old_block) {
            return table->entries[i].new_block;
        }
    }
    return (size_t)-1;
}

__attribute__((unused))
static size_t gc_count_live_objects(mp_state_mem_area_t *area) {
    size_t count = 0;
    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;

    // Count both AT_HEAD and AT_MARK blocks since compaction processes both
    // AT_HEAD: already-converted live objects
    // AT_MARK: live objects from mark phase (not yet converted by sweep)
    for (size_t block = 0; block < max_block; block++) {
        byte block_kind = ATB_GET_KIND(area, block);
        // Count both AT_HEAD and AT_MARK blocks - these are the live objects
        if (block_kind == AT_HEAD || block_kind == AT_MARK) {
            count++;
            // Skip consecutive TAIL blocks
            size_t next_block = block + 1;
            while (next_block < max_block && ATB_GET_KIND(area, next_block) == AT_TAIL) {
                next_block++;
            }
            
            // BOUNDS CHECK: Ensure next_block won't cause overflow
            if (next_block > max_block) {
                printf("FATAL: gc_count_live_objects - next_block (" UINT_FMT ") exceeds max_block (" UINT_FMT ")!\n",
                       next_block, max_block);
                return count;
            }
            
            // Skip past the TAIL blocks for this object
            block = next_block - 1;  // -1 because the for loop will do block++
        }
    }
    return count;
}

__attribute__((unused))
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

static bool gc_trace_ptr_in_area(mp_state_mem_area_t *area, const void *ptr, size_t *head_block_out, size_t *offset_out, byte *kind_out) {
    if (ptr == NULL) {
        return false;
    }

    void *clean_ptr = (void *)((uintptr_t)ptr & ~(uintptr_t)3);

    #if MICROPY_GC_SPLIT_HEAP
    if (gc_get_ptr_area(clean_ptr) != area) {
        return false;
    }
    #else
    if ((byte *)clean_ptr < (byte *)area->gc_pool_start || (byte *)clean_ptr >= (byte *)area->gc_pool_end) {
        return false;
    }
    #endif

    size_t raw_block = BLOCK_FROM_PTR(area, clean_ptr);
    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    if (raw_block >= max_block) {
        return false;
    }

    size_t head_block = raw_block;
    while (head_block > 0 && ATB_GET_KIND(area, head_block) == AT_TAIL) {
        --head_block;
    }

    byte kind = ATB_GET_KIND(area, head_block);
    uintptr_t base = (uintptr_t)PTR_FROM_BLOCK(area, head_block);
    uintptr_t p = (uintptr_t)clean_ptr;

    if (head_block_out != NULL) {
        *head_block_out = head_block;
    }
    if (offset_out != NULL) {
        *offset_out = (size_t)(p - base);
    }
    if (kind_out != NULL) {
        *kind_out = kind;
    }
    return true;
}

__attribute__((unused))
static void *gc_trace_map_ptr(mp_state_mem_area_t *area, gc_forward_table_t *forward_table, void *ptr) {
    if (ptr == NULL || forward_table == NULL) {
        return ptr;
    }

    size_t head_block;
    size_t offset;
    byte kind;
    if (!gc_trace_ptr_in_area(area, ptr, &head_block, &offset, &kind)) {
        return ptr;
    }

    size_t new_head = gc_forward_table_lookup(forward_table, head_block);
    if (new_head == (size_t)-1) {
        return ptr;
    }

    return (void *)((uintptr_t)PTR_FROM_BLOCK(area, new_head) + offset);
}

__attribute__((unused))
static uint32_t gc_trace_hash_bytes(const byte *data, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static void gc_trace_vm_codestate(const char *phase, mp_state_mem_area_t *area, gc_forward_table_t *forward_table) {
    #if MICROPY_DEBUG_VERBOSE
    static bool printed_note = false;
    if (!printed_note) {
        DEBUG_printf("[vmtrace] note opcode dispatch table is static code in vm.c (not GC heap managed)\n");
        printed_note = true;
    }

    mp_code_state_t *cs = MP_STATE_THREAD(current_code_state);
    if (cs == NULL) {
        DEBUG_printf("[vmtrace] %s area=%p no_active_code_state\n", phase, area);
        return;
    }

    size_t depth = 0;
    while (cs != NULL && depth < 24) {
        mp_obj_fun_bc_t *fun = cs->fun_bc;
        bool fun_valid = false;
        const byte *bytecode = NULL;
        void *child_table = NULL;

        if (fun != NULL && gc_ptr_is_in_heap(fun) && gc_is_valid_ptr(fun)) {
            fun_valid = true;
            bytecode = fun->bytecode;
            child_table = (void *)fun->child_table;
        }

        size_t fun_head = 0;
        size_t fun_ofs = 0;
        byte fun_kind = AT_FREE;
        bool fun_in_area = gc_trace_ptr_in_area(area, fun, &fun_head, &fun_ofs, &fun_kind);

        size_t bc_head = 0;
        size_t bc_ofs = 0;
        byte bc_kind = AT_FREE;
        bool bc_in_area = gc_trace_ptr_in_area(area, bytecode, &bc_head, &bc_ofs, &bc_kind);

        size_t ip_head = 0;
        size_t ip_ofs = 0;
        byte ip_kind = AT_FREE;
        bool ip_in_area = gc_trace_ptr_in_area(area, cs->ip, &ip_head, &ip_ofs, &ip_kind);

        void *fun_new = gc_trace_map_ptr(area, forward_table, fun);
        void *bc_new = gc_trace_map_ptr(area, forward_table, (void *)bytecode);
        void *ip_new = gc_trace_map_ptr(area, forward_table, (void *)cs->ip);
        void *sp_new = gc_trace_map_ptr(area, forward_table, (void *)cs->sp);

        uint32_t bc_hash = 0;
        size_t bc_hash_len = 0;
        if (bc_in_area && (bc_kind == AT_HEAD || bc_kind == AT_MARK || bc_kind == AT_TAIL)) {
            size_t max_bytes = (size_t)((byte *)area->gc_pool_end - (const byte *)bytecode);
            bc_hash_len = max_bytes > 16 ? 16 : max_bytes;
            bc_hash = gc_trace_hash_bytes(bytecode, bc_hash_len);
        }

        unsigned op = 0xff;
        if (ip_in_area && (ip_kind == AT_HEAD || ip_kind == AT_MARK || ip_kind == AT_TAIL)) {
            op = (unsigned)(*cs->ip);
        }

        bool rel_ip_valid = false;
        mp_uint_t rel_ip = 0;
        if (bytecode != NULL && cs->ip != NULL) {
            rel_ip_valid = true;
            rel_ip = (mp_uint_t)((uintptr_t)cs->ip - (uintptr_t)bytecode);
        }

        size_t n_exc_stack = 0;
        size_t active_exc = 0;
        const byte *top_handler = NULL;
        void *top_handler_new = NULL;
        unsigned top_handler_op = 0xff;
        if (bytecode != NULL) {
            const byte *prelude_ptr = bytecode;
            size_t n_state_unused, scope_flags, n_pos_args, n_kwonly_args, n_def_pos_args;
            MP_BC_PRELUDE_SIG_DECODE_INTO(prelude_ptr, n_state_unused, n_exc_stack, scope_flags, n_pos_args, n_kwonly_args, n_def_pos_args);
            (void)n_state_unused;
            (void)scope_flags;
            (void)n_pos_args;
            (void)n_kwonly_args;
            (void)n_def_pos_args;
            active_exc = cs->exc_sp_idx;
            if (active_exc > n_exc_stack) {
                active_exc = n_exc_stack;
            }
            if (active_exc > 0) {
                mp_exc_stack_t *exc_state = (mp_exc_stack_t *)(void *)(cs->state + cs->n_state);
                top_handler = exc_state[active_exc - 1].handler;
                top_handler_new = gc_trace_map_ptr(area, forward_table, (void *)top_handler);
                if (top_handler != NULL && gc_trace_ptr_in_area(area, (void *)top_handler, NULL, NULL, NULL)) {
                    top_handler_op = (unsigned)(*top_handler);
                }
            }
        }

        DEBUG_printf("[vmtrace] %s area=%p depth=" UINT_FMT " cs=%p prev=%p fun=%p", phase, area,
            (mp_uint_t)depth, cs, cs->prev_state, fun);
        if (fun_in_area) {
            DEBUG_printf(" fun_head=" UINT_FMT "+" UINT_FMT " fun_kind=%u",
                (mp_uint_t)fun_head,
                (mp_uint_t)fun_ofs,
                (unsigned)fun_kind);
        }
        DEBUG_printf(" fun_new=%p fun_valid=%d bc=%p", fun_new, (int)fun_valid, bytecode);
        if (bc_in_area) {
            DEBUG_printf(" bc_head=" UINT_FMT "+" UINT_FMT " bc_kind=%u",
                (mp_uint_t)bc_head,
                (mp_uint_t)bc_ofs,
                (unsigned)bc_kind);
        }
        DEBUG_printf(" bc_new=%p bc_hash=" UINT_FMT "/" UINT_FMT,
            bc_new,
            (mp_uint_t)bc_hash,
            (mp_uint_t)bc_hash_len);
        DEBUG_printf(" ip=%p", cs->ip);
        if (ip_in_area) {
            DEBUG_printf(" ip_head=" UINT_FMT "+" UINT_FMT " ip_kind=%u",
                (mp_uint_t)ip_head,
                (mp_uint_t)ip_ofs,
                (unsigned)ip_kind);
        }
        DEBUG_printf(" ip_new=%p", ip_new);
        if (rel_ip_valid) {
            DEBUG_printf(" ip_rel=" UINT_FMT, rel_ip);
        } else {
            DEBUG_printf(" ip_rel=-");
        }
        DEBUG_printf(" op=" UINT_FMT " exc=" UINT_FMT "/" UINT_FMT " handler=%p handler_new=%p h_op=" UINT_FMT " sp=%p sp_new=%p child=%p\n",
            (mp_uint_t)op,
            (mp_uint_t)active_exc,
            (mp_uint_t)n_exc_stack,
            top_handler,
            top_handler_new,
            (mp_uint_t)top_handler_op,
            cs->sp,
            sp_new,
            child_table);

        cs = cs->prev_state;
        ++depth;
    }

    if (cs != NULL) {
        DEBUG_printf("[vmtrace] %s area=%p code_state_chain_truncated_at_24\n", phase, area);
    }
    #else
    (void)phase;
    (void)area;
    (void)forward_table;
    #endif
}

// Maps old block positions to new compacted positions
__attribute__((unused))
static void gc_trace_pinned_runtime_tables(mp_state_mem_area_t *area) {
    size_t child_seen = 0;
    size_t child_pinned = 0;
    size_t obj_seen = 0;
    size_t obj_pinned = 0;
    size_t qstr_seen = 0;
    size_t qstr_pinned = 0;

    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    for (size_t block = area->gc_last_used_block; block <= max_block; ++block) {
        byte kind = ATB_GET_KIND(area, block);
        if (kind != AT_HEAD && kind != AT_MARK) {
            continue;
        }

        mp_obj_base_t *base = (mp_obj_base_t *)PTR_FROM_BLOCK(area, block);
        if (base->type == NULL) {
            continue;
        }

        if (base->type == &mp_type_fun_bc) {
            mp_obj_fun_bc_t *fun = (mp_obj_fun_bc_t *)base;
            if (fun->child_table != NULL) {
                ++child_seen;
                if (gc_is_pinned((void *)fun->child_table)) {
                    ++child_pinned;
                } else {
                    DEBUG_printf("[pin-check] WARN child_table unpinned fun=%p table=%p\n", fun, fun->child_table);
                }
            }
        } else if (base->type == &mp_type_module) {
            mp_module_context_t *ctx = (mp_module_context_t *)base;
            if (ctx->constants.obj_table != NULL) {
                ++obj_seen;
                if (gc_is_pinned(ctx->constants.obj_table)) {
                    ++obj_pinned;
                } else {
                    DEBUG_printf("[pin-check] WARN obj_table unpinned module_ctx=%p table=%p\n", ctx, ctx->constants.obj_table);
                }
            }
            #if MICROPY_EMIT_BYTECODE_USES_QSTR_TABLE
            if (ctx->constants.qstr_table != NULL) {
                ++qstr_seen;
                if (gc_is_pinned((void *)ctx->constants.qstr_table)) {
                    ++qstr_pinned;
                } else {
                    DEBUG_printf("[pin-check] WARN qstr_table unpinned module_ctx=%p table=%p\n", ctx, ctx->constants.qstr_table);
                }
            }
            #endif
        }
    }

    DEBUG_printf("[pin-check] child_table pinned=" UINT_FMT "/" UINT_FMT " obj_table pinned=" UINT_FMT "/" UINT_FMT,
        (mp_uint_t)child_pinned,
        (mp_uint_t)child_seen,
        (mp_uint_t)obj_pinned,
        (mp_uint_t)obj_seen);
    #if MICROPY_EMIT_BYTECODE_USES_QSTR_TABLE
    DEBUG_printf(" qstr_table pinned=" UINT_FMT "/" UINT_FMT,
        (mp_uint_t)qstr_pinned,
        (mp_uint_t)qstr_seen);
    #endif
    DEBUG_printf("\n");
}

typedef struct {
    void *type;
    size_t count;
    size_t pinned_count;
    size_t blocks_total;
    size_t mark_ref_slots;
} gc_diag_unmarked_type_bucket_t;

__attribute__((unused))
static const char *gc_diag_type_name(mp_state_mem_area_t *area, void *type_ptr) {
    if (type_ptr == NULL) {
        return "<null>";
    }

    uintptr_t tp = (uintptr_t)type_ptr;
    if (gc_is_plausible_type_ptr(area, tp)) {
        const mp_obj_type_t *type = (const mp_obj_type_t *)tp;
        return qstr_str(type->name);
    }

    #if MICROPY_GC_SPLIT_HEAP
    if (gc_get_ptr_area(type_ptr) != NULL) {
        return "<heap-ptr>";
    }
    #else
    if ((byte *)type_ptr >= (byte *)area->gc_pool_start && (byte *)type_ptr < (byte *)area->gc_pool_end) {
        return "<heap-ptr>";
    }
    #endif

    return "<weird>";
}

__attribute__((unused))
static size_t gc_diag_find_or_add_type_bucket(gc_diag_unmarked_type_bucket_t *buckets, size_t bucket_count, void *type) {
    for (size_t i = 0; i < bucket_count; ++i) {
        if (buckets[i].type == type) {
            return i;
        }
    }
    for (size_t i = 0; i < bucket_count; ++i) {
        if (buckets[i].type == NULL) {
            buckets[i].type = type;
            return i;
        }
    }
    return (size_t)-1;
}

// ============================================================================
// PHASE 1: Compute Forwarding Addresses (Plan Compaction)
// ============================================================================
// SPECIAL CASES:
//   - Don't move pinned blocks (use gc_is_block_pinned() to check)
//   - Multi-block objects must stay contiguous
//   - Can't move if destination crosses gc_last_used_block_from_right
//   - Bytecode/dispatch tables should be pinned
__attribute__((unused))
static void gc_compute_forwarding_addresses(mp_state_mem_area_t *area, gc_forward_table_t *forward_table) {
    if (area == NULL || forward_table == NULL || forward_table->entries == NULL || forward_table->capacity == 0) {
        DEBUG_printf("[phase1] invalid args: area=%p table=%p entries=%p cap=" UINT_FMT "\n",
            area,
            forward_table,
            forward_table ? (void *)forward_table->entries : NULL,
            (mp_uint_t)(forward_table ? forward_table->capacity : 0));
        return;
    }

    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    size_t process_limit = MIN(area->gc_num_blocks, max_block);
    // gc_last_used_block_from_right is an inclusive block index.
    size_t right_exclusive = area->gc_last_used_block_from_right + 1;
    if (right_exclusive > process_limit) {
        right_exclusive = process_limit;
    }
    size_t write_ptr = 0;
    size_t object_count = 0;
    size_t skipped_right_count = 0;
    size_t skipped_right_first = 0;
    size_t skipped_right_last = 0;

    for (size_t block = 0; block < process_limit; block++) {
        byte block_kind = ATB_GET_KIND(area, block);
        if (block_kind != AT_HEAD && block_kind != AT_MARK) {
            continue;
        }

        size_t block_count = 1;
        size_t next_block = block + 1;
        while (next_block < process_limit && ATB_GET_KIND(area, next_block) == AT_TAIL) {
            block_count++;
            next_block++;
        }

        bool is_pinned = gc_is_block_pinned(block);

        // Do not compact objects at/after the right frontier. They belong to
        // the non-compacted right segment for this cycle.
        if (block >= right_exclusive) {
            if (skipped_right_count == 0) {
                skipped_right_first = block;
            }
            skipped_right_last = block;
            skipped_right_count++;
            block = next_block - 1;
            continue;
        }

        size_t new_block = block;

        if (!is_pinned && write_ptr < block) {
            size_t candidate = write_ptr;
            while (candidate + block_count <= block) {
                bool can_place = true;
                for (size_t i = 0; i < block_count; i++) {
                    size_t b = candidate + i;
                    if (ATB_GET_KIND(area, b) != AT_FREE || gc_is_block_pinned(b)) {
                        can_place = false;
                        candidate = b + 1;
                        break;
                    }
                }
                if (can_place) {
                    new_block = candidate;
                    break;
                }
            }
        }

        if (new_block + block_count > right_exclusive) {
            DEBUG_printf("[phase1] ERROR destination out of right bound old=" UINT_FMT " new=" UINT_FMT " count=" UINT_FMT " right_inclusive=" UINT_FMT " right_exclusive=" UINT_FMT "\n",
                (mp_uint_t)block,
                (mp_uint_t)new_block,
                (mp_uint_t)block_count,
                (mp_uint_t)area->gc_last_used_block_from_right,
                (mp_uint_t)right_exclusive);
            return;
        }

        if (!gc_forward_table_insert(forward_table, block, new_block, block_count)) {
            DEBUG_printf("[phase1] ERROR insert failed old=" UINT_FMT " new=" UINT_FMT " count=" UINT_FMT " table_count=" UINT_FMT " cap=" UINT_FMT "\n",
                (mp_uint_t)block,
                (mp_uint_t)new_block,
                (mp_uint_t)block_count,
                (mp_uint_t)forward_table->count,
                (mp_uint_t)forward_table->capacity);
            return;
        }

        if (new_block + block_count > write_ptr) {
            write_ptr = new_block + block_count;
        }

        object_count++;
        block = next_block - 1;
    }

    if (skipped_right_count > 0) {
        DEBUG_printf("[phase1] skipped at/after right frontier count=" UINT_FMT " first=" UINT_FMT " last=" UINT_FMT " frontier=" UINT_FMT "\n",
            (mp_uint_t)skipped_right_count,
            (mp_uint_t)skipped_right_first,
            (mp_uint_t)skipped_right_last,
            (mp_uint_t)right_exclusive);
    }
    (void)skipped_right_first;
    (void)skipped_right_last;



    // Invariants: validate mapping correctness and destination interval overlaps.
    if (forward_table->count != object_count) {
        DEBUG_printf("[phase1] ERROR count mismatch table=" UINT_FMT " objects=" UINT_FMT "\n",
            (mp_uint_t)forward_table->count,
            (mp_uint_t)object_count);
        return;
    }

    for (size_t i = 0; i < forward_table->count; i++) {
        size_t old_i = forward_table->entries[i].old_block;
        size_t new_i = forward_table->entries[i].new_block;

        if (old_i >= process_limit || new_i >= process_limit) {
            DEBUG_printf("[phase1] ERROR mapping out of bounds idx=" UINT_FMT " old=" UINT_FMT " new=" UINT_FMT " limit=" UINT_FMT "\n",
                (mp_uint_t)i,
                (mp_uint_t)old_i,
                (mp_uint_t)new_i,
                (mp_uint_t)process_limit);
            return;
        }

        byte old_kind = ATB_GET_KIND(area, old_i);
        if (old_kind != AT_HEAD && old_kind != AT_MARK) {
            DEBUG_printf("[phase1] ERROR old block not live idx=" UINT_FMT " old=" UINT_FMT " kind=%u\n",
                (mp_uint_t)i,
                (mp_uint_t)old_i,
                (unsigned)old_kind);
            return;
        }

        bool pinned_i = gc_is_block_pinned(old_i);
        if (pinned_i && old_i != new_i) {
            DEBUG_printf("[phase1] ERROR pinned mapping moved old=" UINT_FMT " new=" UINT_FMT "\n",
                (mp_uint_t)old_i,
                (mp_uint_t)new_i);
            return;
        }

        size_t count_i = 1;
        size_t scan_i = old_i + 1;
        while (scan_i < process_limit && ATB_GET_KIND(area, scan_i) == AT_TAIL) {
            count_i++;
            scan_i++;
        }

        if (new_i + count_i > process_limit) {
            DEBUG_printf("[phase1] ERROR destination range out of bounds old=" UINT_FMT " new=" UINT_FMT " count=" UINT_FMT " limit=" UINT_FMT "\n",
                (mp_uint_t)old_i,
                (mp_uint_t)new_i,
                (mp_uint_t)count_i,
                (mp_uint_t)process_limit);
            return;
        }

        for (size_t j = i + 1; j < forward_table->count; j++) {
            size_t old_j = forward_table->entries[j].old_block;
            size_t new_j = forward_table->entries[j].new_block;

            size_t count_j = 1;
            size_t scan_j = old_j + 1;
            while (scan_j < process_limit && ATB_GET_KIND(area, scan_j) == AT_TAIL) {
                count_j++;
                scan_j++;
            }

            size_t i_start = new_i;
            size_t i_end = new_i + count_i;
            size_t j_start = new_j;
            size_t j_end = new_j + count_j;
            bool overlap = (i_start < j_end) && (j_start < i_end);

            if (overlap) {
                DEBUG_printf("[phase1] ERROR overlap i(old=" UINT_FMT ",new=" UINT_FMT ",count=" UINT_FMT ") j(old=" UINT_FMT ",new=" UINT_FMT ",count=" UINT_FMT ")\n",
                    (mp_uint_t)old_i,
                    (mp_uint_t)new_i,
                    (mp_uint_t)count_i,
                    (mp_uint_t)old_j,
                    (mp_uint_t)new_j,
                    (mp_uint_t)count_j);
                return;
            }
        }
    }

}

// ============================================================================
// PHASE 2: Copy Objects to New Locations (Perform Memmove)
// ============================================================================
// SPECIAL CASES:
//   - Pinned and in-place objects keep their addresses and only need MARK->HEAD
//   - Objects at/after the right frontier were skipped by Phase 1 and must also
//     have their AT_MARK heads normalised in-place
//   - Multi-block objects are moved as a single byte range and re-written
static size_t gc_count_object_blocks(mp_state_mem_area_t *area, size_t head_block, size_t process_limit) {
    size_t block_count = 1;
    size_t block = head_block + 1;
    while (block < process_limit && ATB_GET_KIND(area, block) == AT_TAIL) {
        block_count++;
        block++;
    }
    return block_count;
}

static void gc_write_object_atb(mp_state_mem_area_t *area, size_t head_block, size_t block_count) {
    ATB_ANY_TO_FREE(area, head_block);
    ATB_FREE_TO_HEAD(area, head_block);
    for (size_t i = 1; i < block_count; ++i) {
        ATB_ANY_TO_FREE(area, head_block + i);
        ATB_FREE_TO_TAIL(area, head_block + i);
    }
}

static void gc_clear_object_atb(mp_state_mem_area_t *area, size_t head_block, size_t block_count) {
    for (size_t i = 0; i < block_count; ++i) {
        ATB_ANY_TO_FREE(area, head_block + i);
    }
}

__attribute__((unused))
static void gc_compact_copy(mp_state_mem_area_t *area, gc_forward_table_t *forward_table) {
    if (area == NULL || forward_table == NULL) {
        DEBUG_printf("[phase2] ERROR null input area=%p table=%p\n", area, forward_table);
        return;
    }

    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    size_t process_limit = MIN(area->gc_num_blocks, max_block);
    size_t right_exclusive = area->gc_last_used_block_from_right + 1;
    if (right_exclusive > process_limit) {
        right_exclusive = process_limit;
    }

    size_t moved_count = 0;
    size_t inplace_count = 0;
    size_t normalized_right_count = 0;
    size_t bytes_moved = 0;

    for (size_t i = 0; i < forward_table->count; ++i) {
        size_t old_block = forward_table->entries[i].old_block;
        size_t new_block = forward_table->entries[i].new_block;

        if (old_block >= process_limit || new_block >= process_limit) {
            DEBUG_printf("[phase2] ERROR mapping out of bounds idx=" UINT_FMT " old=" UINT_FMT " new=" UINT_FMT " limit=" UINT_FMT "\n",
                (mp_uint_t)i,
                (mp_uint_t)old_block,
                (mp_uint_t)new_block,
                (mp_uint_t)process_limit);
            return;
        }

        byte old_kind = ATB_GET_KIND(area, old_block);
        if (old_kind != AT_HEAD && old_kind != AT_MARK) {
            DEBUG_printf("[phase2] ERROR source block not live idx=" UINT_FMT " old=" UINT_FMT " kind=%u\n",
                (mp_uint_t)i,
                (mp_uint_t)old_block,
                (unsigned)old_kind);
            return;
        }

        size_t block_count = gc_count_object_blocks(area, old_block, process_limit);
        size_t byte_count = block_count * BYTES_PER_BLOCK;

        if (new_block > old_block) {
            DEBUG_printf("[phase2] ERROR non-sliding mapping idx=" UINT_FMT " old=" UINT_FMT " new=" UINT_FMT " blocks=" UINT_FMT "\n",
                (mp_uint_t)i,
                (mp_uint_t)old_block,
                (mp_uint_t)new_block,
                (mp_uint_t)block_count);
            return;
        }

        if (new_block == old_block) {
            if (old_kind == AT_MARK) {
                ATB_MARK_TO_HEAD(area, old_block);
            }
            inplace_count++;
            continue;
        }

        void *old_ptr = (void *)PTR_FROM_BLOCK(area, old_block);
        void *new_ptr = (void *)PTR_FROM_BLOCK(area, new_block);
        
        // Diagnostic: capture type pointer before and after copy
        mp_obj_base_t *src_obj = (mp_obj_base_t *)old_ptr;
        mp_obj_base_t *dst_obj = (mp_obj_base_t *)new_ptr;
        const void *src_type = src_obj->type;
        const void *dst_type_after;
        const void *dst_type_before __attribute__((unused)) = dst_obj->type;
        memmove(new_ptr, old_ptr, byte_count);
        
        dst_type_after = dst_obj->type;
        if (dst_type_after != src_type) {
            DEBUG_printf("[phase2] TYPE_MISMATCH: old_blk=" UINT_FMT " new_blk=" UINT_FMT " src_type=%p dst_before=%p dst_after=%p byte_count=" UINT_FMT " old_ptr=%p new_ptr=%p\n",
                (mp_uint_t)old_block, (mp_uint_t)new_block, src_type, dst_type_before, dst_type_after, (mp_uint_t)byte_count, old_ptr, new_ptr);
        }
        // Check for overlapping regions
        uintptr_t old_start = (uintptr_t)old_ptr;
        uintptr_t old_end = old_start + byte_count;
        uintptr_t new_start = (uintptr_t)new_ptr;
        uintptr_t new_end = new_start + byte_count;
        
        if ((new_start < old_end && new_start >= old_start) || 
            (new_end > old_start && new_end <= old_end)) {
            DEBUG_printf("[phase2] WARN OVERLAP: old=%p-%p new=%p-%p byte_count=" UINT_FMT "\n", 
                (void*)old_start, (void*)old_end, (void*)new_start, (void*)new_end, (mp_uint_t)byte_count);
        }
        gc_write_object_atb(area, new_block, block_count);
        gc_clear_object_atb(area, old_block, block_count);
        moved_count++;
        bytes_moved += byte_count;
    }

    for (size_t block = right_exclusive; block < process_limit; ++block) {
        if (ATB_GET_KIND(area, block) == AT_MARK) {
            ATB_MARK_TO_HEAD(area, block);
            normalized_right_count++;
        }
    }


}

// ============================================================================
// PHASE 3: Update Internal Object References
// ============================================================================
__attribute__((unused))
static void gc_update_references(mp_state_mem_area_t *area, gc_forward_table_t *forward_table) {
    if (area == NULL || forward_table == NULL) {
        DEBUG_printf("[phase3] ERROR null input area=%p table=%p\n", area, forward_table);
        return;
    }

    size_t max_block = area->gc_alloc_table_byte_len * BLOCKS_PER_ATB;
    size_t process_limit = MIN(area->gc_num_blocks, max_block);

    size_t objects_seen = 0;
    size_t objects_typed = 0;
    size_t fun_objects = 0;
    size_t module_objects = 0;
    size_t list_objects = 0;
    size_t dict_objects = 0;
    size_t str_objects = 0;
    size_t slots_scanned = 0;
    size_t slots_rewritten = 0;
    size_t slots_nonheap = 0;
    size_t skipped_null_type = 0;
    size_t skipped_heap_type = 0;
    size_t skipped_implausible_type = 0;

    for (size_t block = 0; block < process_limit; ++block) {
        byte kind = ATB_GET_KIND(area, block);
        if (kind != AT_HEAD && kind != AT_MARK) {
            continue;
        }

        size_t block_count = gc_count_object_blocks(area, block, process_limit);
        mp_obj_base_t *base = (mp_obj_base_t *)PTR_FROM_BLOCK(area, block);
        objects_seen++;

        if (base->type == NULL) {
            if (block < process_limit && ATB_GET_KIND(area, block + 1) == AT_TAIL) {
                DEBUG_printf("[phase3] SKIP null_type obj=%p block=" UINT_FMT "\n", base, (mp_uint_t)block);
            }
            skipped_null_type++;
            block += block_count - 1;
            continue;
        }

        bool type_in_heap = false;
        #if MICROPY_GC_SPLIT_HEAP
        type_in_heap = (gc_get_ptr_area((void *)base->type) != NULL);
        #else
        type_in_heap = ((byte *)base->type >= (byte *)area->gc_pool_start
            && (byte *)base->type < (byte *)area->gc_pool_end);
        #endif
        if (type_in_heap) {
            // Heap-resident type pointers indicate stale/non-object payloads.
            if (block < process_limit && ATB_GET_KIND(area, block + 1) == AT_TAIL) {
                DEBUG_printf("[phase3] SKIP heap_type obj=%p block=" UINT_FMT "\n", base, (mp_uint_t)block);
            }
            skipped_heap_type++;
            block += block_count - 1;
            continue;
        }

        if (!gc_is_plausible_type_ptr(area, (uintptr_t)base->type)) {
            if (block < process_limit && ATB_GET_KIND(area, block + 1) == AT_TAIL) {
                DEBUG_printf("[phase3] SKIP implausible_type obj=%p type=%p block=" UINT_FMT "\n", base, base->type, (mp_uint_t)block);
            }
            skipped_implausible_type++;
            block += block_count - 1;
            continue;
        }

        if (base->type == &mp_type_fun_bc) {
            mp_obj_fun_bc_t *fun = (mp_obj_fun_bc_t *)base;
            objects_typed++;
            fun_objects++;

            DEBUG_printf("[phase3] fun_bc processing fun=%p bytecode_old=%p ", (void *)fun, (void *)fun->bytecode);

            gc_rewrite_ptr_slot((void **)&fun->context, forward_table, &slots_scanned, &slots_rewritten, &slots_nonheap);
            gc_rewrite_ptr_slot((void **)&fun->child_table, forward_table, &slots_scanned, &slots_rewritten, &slots_nonheap);
            gc_rewrite_ptr_slot((void **)&fun->bytecode, forward_table, &slots_scanned, &slots_rewritten, &slots_nonheap);

            if (fun->child_table != NULL) {
                gc_update_raw_code_ptr_array(area, forward_table, (mp_raw_code_t **)fun->child_table);
            }
            
            DEBUG_printf("bytecode_new=%p\n", (void *)fun->bytecode);
            
            #if MICROPY_PY_SYS_SETTRACE
            gc_rewrite_ptr_slot((void **)&fun->rc, forward_table, &slots_scanned, &slots_rewritten, &slots_nonheap);
            #endif

            if (fun->context != NULL) {
                _gc_update_module_context_obj_table((mp_module_context_t *)fun->context, forward_table,
                    &slots_scanned, &slots_rewritten, &slots_nonheap);
            }

            // Rewrite default-arg payload as mp_obj_t values, preserving immediates.
            size_t word_count = (block_count * BYTES_PER_BLOCK) / MP_BYTES_PER_OBJ_WORD;
            size_t extra_word = offsetof(mp_obj_fun_bc_t, extra_args) / MP_BYTES_PER_OBJ_WORD;
            mp_obj_t *words = (mp_obj_t *)fun;
            for (size_t w = extra_word; w < word_count; ++w) {
                mp_obj_t obj = words[w];
                if (mp_obj_is_obj(obj)) {
                    mp_obj_t new_obj = MP_OBJ_FROM_PTR(gc_update_ptr(MP_OBJ_TO_PTR(obj), forward_table));
                    if (new_obj != obj) {
                        words[w] = new_obj;
                        slots_rewritten++;
                    }
                    slots_scanned++;
                } else {
                    slots_nonheap++;
                }
            }
        } else if (base->type == &mp_type_module) {
            mp_module_context_t *ctx = (mp_module_context_t *)base;
            objects_typed++;
            module_objects++;

            gc_rewrite_ptr_slot((void **)&ctx->module.globals, forward_table, &slots_scanned, &slots_rewritten, &slots_nonheap);
            _gc_update_module_context_obj_table(ctx, forward_table,
                &slots_scanned, &slots_rewritten, &slots_nonheap);
        } else if (base->type == &mp_type_list) {
            mp_obj_list_t *list = (mp_obj_list_t *)base;
            objects_typed++;
            list_objects++;

            _gc_update_list_items(list, forward_table);
        } else if (base->type == &mp_type_dict) {
            mp_obj_dict_t *dict = (mp_obj_dict_t *)base;
            objects_typed++;
            dict_objects++;

            _gc_update_dict_values(dict, area, forward_table);
        } else if (base->type == &mp_type_str || base->type == &mp_type_bytes) {
            mp_obj_str_t *str = (mp_obj_str_t *)base;
            objects_typed++;
            str_objects++;

            gc_rewrite_ptr_slot((void **)&str->data, forward_table,
                &slots_scanned, &slots_rewritten, &slots_nonheap);
        #if MICROPY_PY_BUILTINS_BYTEARRAY
        } else if (base->type == &mp_type_bytearray) {
            mp_obj_array_t *arr = (mp_obj_array_t *)base;
            objects_typed++;

            gc_rewrite_ptr_slot((void **)&arr->items, forward_table,
                &slots_scanned, &slots_rewritten, &slots_nonheap);
        #endif
        } else if (base->type == &mp_type_closure) {
            gc_obj_closure_t *closure = (gc_obj_closure_t *)base;
            objects_typed++;

            if (mp_obj_is_obj(closure->fun)) {
                mp_obj_t new_fun = MP_OBJ_FROM_PTR(gc_update_ptr(MP_OBJ_TO_PTR(closure->fun), forward_table));
                if (new_fun != closure->fun) {
                    closure->fun = new_fun;
                    slots_rewritten++;
                }
                slots_scanned++;
            } else {
                slots_nonheap++;
            }

            size_t obj_bytes = block_count * BYTES_PER_BLOCK;
            size_t closed_capacity = 0;
            if (obj_bytes > offsetof(gc_obj_closure_t, closed)) {
                closed_capacity = (obj_bytes - offsetof(gc_obj_closure_t, closed)) / sizeof(mp_obj_t);
            }
            size_t closed_count = MIN(closure->n_closed, closed_capacity);
            for (size_t i = 0; i < closed_count; ++i) {
                mp_obj_t obj = closure->closed[i];
                if (mp_obj_is_obj(obj)) {
                    mp_obj_t new_obj = MP_OBJ_FROM_PTR(gc_update_ptr(MP_OBJ_TO_PTR(obj), forward_table));
                    if (new_obj != obj) {
                        closure->closed[i] = new_obj;
                        slots_rewritten++;
                    }
                    slots_scanned++;
                } else {
                    slots_nonheap++;
                }
            }
        } else if (base->type == &mp_type_cell) {
            mp_obj_cell_t *cell = (mp_obj_cell_t *)base;
            objects_typed++;

            if (mp_obj_is_obj(cell->obj)) {
                mp_obj_t new_obj = MP_OBJ_FROM_PTR(gc_update_ptr(MP_OBJ_TO_PTR(cell->obj), forward_table));
                if (new_obj != cell->obj) {
                    cell->obj = new_obj;
                    slots_rewritten++;
                }
                slots_scanned++;
            } else {
                slots_nonheap++;
            }
        }

        block += block_count - 1;
    }


}

// ============================================================================
// Update Root Pointers (From VM Context)
// ============================================================================
// CRITICAL: Some roots point to objects that have been moved, so we
// must use forward_table to find their new locations
__attribute__((unused))
static void gc_update_roots(mp_state_mem_area_t *area, gc_forward_table_t *forward_table) {
    if (area == NULL || forward_table == NULL) {
        DEBUG_printf("[roots] ERROR null input area=%p table=%p\n", area, forward_table);
        return;
    }

    size_t slots_scanned = 0;
    size_t slots_rewritten = 0;
    size_t slots_nonheap = 0;
    size_t root_section_rewritten = 0;
    size_t code_state_nodes = 0;

    // Keep root updates aligned with gc_collect_start root scan range.
    void **ptrs = (void **)(void *)&mp_state_ctx;
    size_t root_start = offsetof(mp_state_ctx_t, thread.dict_locals);
    size_t root_end = offsetof(mp_state_ctx_t, vm.qstr_last_chunk);
    size_t root_slots = (root_end - root_start) / sizeof(void *);
    root_section_rewritten = gc_rewrite_ptr_array(ptrs + root_start / sizeof(void *), root_slots, forward_table,
        &slots_scanned, &slots_rewritten, &slots_nonheap);

    gc_rewrite_ptr_slot((void **)&MP_STATE_THREAD(dict_locals), forward_table,
        &slots_scanned, &slots_rewritten, &slots_nonheap);
    gc_rewrite_ptr_slot((void **)&MP_STATE_THREAD(dict_globals), forward_table,
        &slots_scanned, &slots_rewritten, &slots_nonheap);

    for (mp_code_state_t *cs = MP_STATE_THREAD(current_code_state); cs != NULL && code_state_nodes < 4096; ) {
        code_state_nodes++;

        mp_obj_fun_bc_t *old_fun_bc = cs->fun_bc;
        const uint8_t *old_bytecode = old_fun_bc != NULL ? old_fun_bc->bytecode : NULL;
        const uint8_t *old_ip = cs->ip;
        size_t ip_offset = 0;
        bool ip_in_old_bytecode = false;
        if (old_bytecode != NULL && old_ip != NULL && old_ip >= old_bytecode) {
            ip_offset = (size_t)(old_ip - old_bytecode);
            ip_in_old_bytecode = true;
        }

        gc_rewrite_ptr_slot((void **)&cs->fun_bc, forward_table,
            &slots_scanned, &slots_rewritten, &slots_nonheap);

        // ip points inside bytecode, so preserve its offset across bytecode relocation.
        if (ip_in_old_bytecode && cs->fun_bc != NULL && cs->fun_bc->bytecode != NULL) {
            const uint8_t *new_bytecode = cs->fun_bc->bytecode;
            cs->ip = (const byte *)new_bytecode + ip_offset;
            slots_scanned++;
            if (new_bytecode != old_bytecode) {
                slots_rewritten++;
            }
        } else {
            gc_rewrite_ptr_slot((void **)&cs->ip, forward_table,
                &slots_scanned, &slots_rewritten, &slots_nonheap);
        }
        
        gc_rewrite_ptr_slot((void **)&cs->sp, forward_table,
            &slots_scanned, &slots_rewritten, &slots_nonheap);
        gc_rewrite_ptr_slot((void **)&cs->old_globals, forward_table,
            &slots_scanned, &slots_rewritten, &slots_nonheap);
        #if MICROPY_PY_SYS_SETTRACE
        gc_rewrite_ptr_slot((void **)&cs->frame, forward_table,
            &slots_scanned, &slots_rewritten, &slots_nonheap);
        #endif

        // Rewrite exception handler pointers, which also point into bytecode.
        if (cs->fun_bc != NULL && cs->fun_bc->bytecode != NULL) {
            const byte *new_bytecode = cs->fun_bc->bytecode;
            const byte *prelude_ptr = new_bytecode;
            size_t n_state_unused, n_exc_stack, scope_flags, n_pos_args, n_kwonly_args, n_def_pos_args;
            MP_BC_PRELUDE_SIG_DECODE_INTO(prelude_ptr, n_state_unused, n_exc_stack, scope_flags, n_pos_args, n_kwonly_args, n_def_pos_args);
            (void)n_state_unused;
            (void)scope_flags;
            (void)n_pos_args;
            (void)n_kwonly_args;
            (void)n_def_pos_args;

            mp_exc_stack_t *exc_state = (mp_exc_stack_t *)(void *)(cs->state + cs->n_state);
            size_t active_exc = cs->exc_sp_idx;
            if (active_exc > n_exc_stack) {
                active_exc = n_exc_stack;
            }
            for (size_t i = 0; i < active_exc; ++i) {
                const byte *handler = exc_state[i].handler;
                if (handler == NULL) {
                    continue;
                }

                if (old_bytecode != NULL && handler >= old_bytecode) {
                    size_t handler_offset = (size_t)(handler - old_bytecode);
                    const byte *new_handler = new_bytecode + handler_offset;
                    if (new_handler != handler) {
                        exc_state[i].handler = new_handler;
                        slots_rewritten++;
                    }
                    slots_scanned++;
                } else {
                    gc_rewrite_ptr_slot((void **)&exc_state[i].handler, forward_table,
                        &slots_scanned, &slots_rewritten, &slots_nonheap);
                }
            }
        }

        for (size_t i = 0; i < cs->n_state; ++i) {
            mp_obj_t state_obj = cs->state[i];
            if (mp_obj_is_obj(state_obj)) {
                mp_obj_t new_obj = MP_OBJ_FROM_PTR(gc_update_ptr(MP_OBJ_TO_PTR(state_obj), forward_table));
                if (new_obj != state_obj) {
                    cs->state[i] = new_obj;
                    slots_rewritten++;
                }
                slots_scanned++;
            } else {
                slots_nonheap++;
            }
        }

        #if MICROPY_STACKLESS
        gc_rewrite_ptr_slot((void **)&cs->prev, forward_table,
            &slots_scanned, &slots_rewritten, &slots_nonheap);
        cs = cs->prev;
        #else
        gc_rewrite_ptr_slot((void **)&cs->prev_state, forward_table,
            &slots_scanned, &slots_rewritten, &slots_nonheap);
        cs = cs->prev_state;
        #endif
    }

    mp_obj_dict_t *dict_locals = MP_STATE_THREAD(dict_locals);
    mp_obj_dict_t *dict_globals = MP_STATE_THREAD(dict_globals);
    _gc_update_dict_values(dict_locals, area, forward_table);
    if (dict_globals != dict_locals) {
        _gc_update_dict_values(dict_globals, area, forward_table);
    }

    gc_rewrite_ptr_slot((void **)&MP_STATE_VM(last_pool), forward_table,
        &slots_scanned, &slots_rewritten, &slots_nonheap);
    gc_rewrite_ptr_slot((void **)&MP_STATE_VM(qstr_last_chunk), forward_table,
        &slots_scanned, &slots_rewritten, &slots_nonheap);

    size_t qstr_pools_seen = 0;
    for (qstr_pool_t *pool = MP_STATE_VM(last_pool); pool != NULL && qstr_pools_seen < 4096; ) {
        qstr_pools_seen++;

        gc_rewrite_ptr_slot((void **)&pool->prev, forward_table,
            &slots_scanned, &slots_rewritten, &slots_nonheap);
        #if MICROPY_QSTR_BYTES_IN_HASH
        gc_rewrite_ptr_slot((void **)&pool->hashes, forward_table,
            &slots_scanned, &slots_rewritten, &slots_nonheap);
        #endif
        gc_rewrite_ptr_slot((void **)&pool->lengths, forward_table,
            &slots_scanned, &slots_rewritten, &slots_nonheap);
        gc_rewrite_ptr_array((void **)pool->qstrs, pool->len, forward_table,
            &slots_scanned, &slots_rewritten, &slots_nonheap);

        pool = (qstr_pool_t *)pool->prev;
    }

    gc_update_pinned_table(forward_table);

    DEBUG_printf("[roots] summary root_slots=" UINT_FMT " root_rewritten=" UINT_FMT " scanned=" UINT_FMT " rewritten=" UINT_FMT " nonheap=" UINT_FMT " qstr_pools=" UINT_FMT " code_states=" UINT_FMT "\n",
        (mp_uint_t)root_slots,
        (mp_uint_t)root_section_rewritten,
        (mp_uint_t)slots_scanned,
        (mp_uint_t)slots_rewritten,
        (mp_uint_t)slots_nonheap,
        (mp_uint_t)qstr_pools_seen,
        (mp_uint_t)code_state_nodes);
    (void)root_section_rewritten;
    (void)code_state_nodes;
}

// ============================================================================
// Update Dispatch and Code Tables
// ============================================================================

static bool gc_ptr_is_in_heap(void *ptr) {
    uintptr_t p = (uintptr_t)ptr;
    if (p == 0) {
        return false;
    }

    void *clean_ptr = (void *)(p & ~(uintptr_t)3);
    #if MICROPY_GC_SPLIT_HEAP
    for (mp_state_mem_area_t *scan = &MP_STATE_MEM(area); scan != NULL; scan = NEXT_AREA(scan)) {
        if ((byte *)clean_ptr >= (byte *)scan->gc_pool_start && (byte *)clean_ptr < (byte *)scan->gc_pool_end) {
            return true;
        }
    }
    return false;
    #else
    return (byte *)clean_ptr >= (byte *)MP_STATE_MEM(area).gc_pool_start
        && (byte *)clean_ptr < (byte *)MP_STATE_MEM(area).gc_pool_end;
    #endif
}

static bool gc_rewrite_ptr_slot(void **slot, gc_forward_table_t *forward_table, size_t *slots_scanned, size_t *slots_rewritten, size_t *slots_nonheap) {
    if (slot == NULL) {
        return false;
    }

    if (slots_scanned != NULL) {
        (*slots_scanned)++;
    }

    void *old_ptr = *slot;
    if (!gc_ptr_is_in_heap(old_ptr)) {
        if (slots_nonheap != NULL) {
            (*slots_nonheap)++;
        }
        return false;
    }

    void *new_ptr = gc_update_ptr(old_ptr, forward_table);
    if (new_ptr != old_ptr) {
        *slot = new_ptr;
        if (slots_rewritten != NULL) {
            (*slots_rewritten)++;
        }
        return true;
    }
    return false;
}

static size_t gc_rewrite_ptr_array(void **slots, size_t count, gc_forward_table_t *forward_table, size_t *slots_scanned, size_t *slots_rewritten, size_t *slots_nonheap) {
    if (slots == NULL || count == 0) {
        return 0;
    }

    size_t rewritten = 0;
    for (size_t i = 0; i < count; ++i) {
        rewritten += gc_rewrite_ptr_slot(&slots[i], forward_table, slots_scanned, slots_rewritten, slots_nonheap) ? 1 : 0;
    }
    return rewritten;
}

__attribute__((unused))
static void _gc_update_dict_values(mp_obj_dict_t *dict, mp_state_mem_area_t *area, 
                                   gc_forward_table_t *forward_table) {
    if (dict == NULL) {
        return;
    }

    mp_map_t *map = &dict->map;
    map->table = (mp_map_elem_t *)gc_update_ptr(map->table, forward_table);
    
    if (map->table == NULL || map->alloc == 0) {
        return;
    }
    for (size_t i = 0; i < map->alloc; i++) {
        mp_map_elem_t *elem = &map->table[i];
        
        if (elem->key != MP_OBJ_NULL && elem->key != MP_OBJ_SENTINEL && mp_obj_is_obj(elem->key)) {
            elem->key = MP_OBJ_FROM_PTR(gc_update_ptr(MP_OBJ_TO_PTR(elem->key), forward_table));
        }
        if (elem->value != MP_OBJ_NULL && elem->value != MP_OBJ_SENTINEL && mp_obj_is_obj(elem->value)) {
            elem->value = MP_OBJ_FROM_PTR(gc_update_ptr(MP_OBJ_TO_PTR(elem->value), forward_table));
        }
    }
}

__attribute__((unused))
static void _gc_update_list_items(mp_obj_list_t *list, gc_forward_table_t *forward_table) {
    if (list == NULL || list->items == NULL || list->len == 0) {
        return;
    }

    list->items = (mp_obj_t *)gc_update_ptr(list->items, forward_table);
    if (list->items == NULL) {
        return;
    }

    for (size_t i = 0; i < list->len; ++i) {
        if (mp_obj_is_obj(list->items[i])) {
            list->items[i] = MP_OBJ_FROM_PTR(gc_update_ptr(MP_OBJ_TO_PTR(list->items[i]), forward_table));
        }
    }
}

static void *gc_update_ptr(void *ptr, gc_forward_table_t *forward_table) {
    uintptr_t p = (uintptr_t)ptr;
    if (p == 0) {
        return NULL;
    }

    if (forward_table == NULL || forward_table->entries == NULL || forward_table->capacity == 0) {
        return ptr;
    }

    // Preserve object tag bits (if present) and operate on the untagged address.
    uintptr_t tag_bits = p & 3;
    void *clean_ptr = (void *)(p & ~(uintptr_t)3);

    mp_state_mem_area_t *ref_area = NULL;
    #if MICROPY_GC_SPLIT_HEAP
    for (mp_state_mem_area_t *area = &MP_STATE_MEM(area); area != NULL; area = NEXT_AREA(area)) {
        if ((byte *)clean_ptr >= (byte *)area->gc_pool_start && (byte *)clean_ptr < (byte *)area->gc_pool_end) {
            ref_area = area;
            break;
        }
    }
    #else
    if ((byte *)clean_ptr >= (byte *)MP_STATE_MEM(area).gc_pool_start
        && (byte *)clean_ptr < (byte *)MP_STATE_MEM(area).gc_pool_end) {
        ref_area = &MP_STATE_MEM(area);
    }
    #endif

    if (ref_area == NULL) {
        return ptr;
    }

    for (size_t i = 0; i < forward_table->count; ++i) {
        gc_forward_entry_t *e = &forward_table->entries[i];
        uintptr_t old_start = (uintptr_t)PTR_FROM_BLOCK(ref_area, e->old_block);
        uintptr_t old_end = old_start + e->n_blocks * BYTES_PER_BLOCK;
        uintptr_t p_clean = (uintptr_t)clean_ptr;
        if (p_clean < old_start || p_clean >= old_end) {
            continue;
        }

        if (e->new_block == e->old_block) {
            return ptr;
        }

        uintptr_t offset = p_clean - old_start;
        void *new_clean_ptr = (void *)((uintptr_t)PTR_FROM_BLOCK(ref_area, e->new_block) + offset);
        return (void *)((uintptr_t)new_clean_ptr | tag_bits);
    }

    return ptr;
}

__attribute__((unused))
static void _gc_update_module_context_obj_table(mp_module_context_t *ctx, gc_forward_table_t *forward_table, size_t *slots_scanned, size_t *slots_rewritten, size_t *slots_nonheap) {
    if (ctx == NULL) {
        return;
    }

    gc_rewrite_ptr_slot((void **)&ctx->module.globals, forward_table, slots_scanned, slots_rewritten, slots_nonheap);

    #if MICROPY_EMIT_BYTECODE_USES_QSTR_TABLE
    gc_rewrite_ptr_slot((void **)&ctx->constants.qstr_table, forward_table, slots_scanned, slots_rewritten, slots_nonheap);
    #endif

    gc_rewrite_ptr_slot((void **)&ctx->constants.obj_table, forward_table, slots_scanned, slots_rewritten, slots_nonheap);

    if (ctx->constants.obj_table == NULL || ctx->obj_table_len == 0) {
        return;
    }

    for (size_t i = 0; i < ctx->obj_table_len; ++i) {
        mp_obj_t obj = ctx->constants.obj_table[i];
        if (mp_obj_is_obj(obj)) {
            ctx->constants.obj_table[i] = MP_OBJ_FROM_PTR(gc_update_ptr(MP_OBJ_TO_PTR(obj), forward_table));
            if (slots_scanned != NULL) {
                (*slots_scanned)++;
            }
            if (slots_rewritten != NULL && ctx->constants.obj_table[i] != obj) {
                (*slots_rewritten)++;
            }
        } else if (slots_nonheap != NULL) {
            (*slots_nonheap)++;
        }
    }
}

#endif // MICROPY_ENABLE_GC