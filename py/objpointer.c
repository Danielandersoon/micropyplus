#include "py/obj.h"
#include "py/runtime.h"
#include "py/gc.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

// Helper function to get address from pointer object
static inline intptr_t pointer_get_addr(mp_obj_pointer_t *p) {
    return p->addr;
}

// Helper function to set address on pointer object
static inline void pointer_set_addr(mp_obj_pointer_t *p, intptr_t addr) {
    p->addr = addr;
}

// Print function for pointer objects
static void pointer_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_pointer_t *o = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "0x%lx", (unsigned long)pointer_get_addr(o));
}

// Binary operations on pointers (addition, subtraction, comparison)
static mp_obj_t pointer_binary_op(mp_binary_op_t op, mp_obj_t lhs_in, mp_obj_t rhs_in) {
    // Extract left hand side pointer address
    if (!mp_obj_is_type(lhs_in, &mp_type_pointer)) {
        return MP_OBJ_NULL; // Not a pointer on LHS
    }
    mp_obj_pointer_t *lhs = MP_OBJ_TO_PTR(lhs_in);
    intptr_t lhs_addr = pointer_get_addr(lhs);

    switch (op) {
        case MP_BINARY_OP_ADD:
        case MP_BINARY_OP_INPLACE_ADD: {
            // ptr + int → new pointer
            return mp_obj_new_pointer((mp_obj_t *)(lhs_addr + mp_obj_get_int(rhs_in)));
        }

        case MP_BINARY_OP_SUBTRACT:
        case MP_BINARY_OP_INPLACE_SUBTRACT: {
            if (mp_obj_is_type(rhs_in, &mp_type_pointer)) {
                // ptr - ptr → int (difference in bytes)
                mp_obj_pointer_t *rhs = MP_OBJ_TO_PTR(rhs_in);
                return mp_obj_new_int(lhs_addr - pointer_get_addr(rhs));
            } else {
                // ptr - int → new pointer
                return mp_obj_new_pointer((mp_obj_t *)(lhs_addr - mp_obj_get_int(rhs_in)));
            }
        }

        case MP_BINARY_OP_EQUAL: {
            if (mp_obj_is_type(rhs_in, &mp_type_pointer)) {
                mp_obj_pointer_t *rhs = MP_OBJ_TO_PTR(rhs_in);
                return mp_obj_new_bool(lhs_addr == pointer_get_addr(rhs));
            }
            return mp_obj_new_bool(false);
        }

        case MP_BINARY_OP_NOT_EQUAL: {
            if (mp_obj_is_type(rhs_in, &mp_type_pointer)) {
                mp_obj_pointer_t *rhs = MP_OBJ_TO_PTR(rhs_in);
                return mp_obj_new_bool(lhs_addr != pointer_get_addr(rhs));
            }
            return mp_obj_new_bool(true);
        }

        case MP_BINARY_OP_LESS: {
            if (mp_obj_is_type(rhs_in, &mp_type_pointer)) {
                mp_obj_pointer_t *rhs = MP_OBJ_TO_PTR(rhs_in);
                return mp_obj_new_bool(lhs_addr < pointer_get_addr(rhs));
            }
            return MP_OBJ_NULL; // Can't compare pointer to non-pointer
        }

        case MP_BINARY_OP_LESS_EQUAL: {
            if (mp_obj_is_type(rhs_in, &mp_type_pointer)) {
                mp_obj_pointer_t *rhs = MP_OBJ_TO_PTR(rhs_in);
                return mp_obj_new_bool(lhs_addr <= pointer_get_addr(rhs));
            }
            return MP_OBJ_NULL;
        }

        case MP_BINARY_OP_MORE: {
            if (mp_obj_is_type(rhs_in, &mp_type_pointer)) {
                mp_obj_pointer_t *rhs = MP_OBJ_TO_PTR(rhs_in);
                return mp_obj_new_bool(lhs_addr > pointer_get_addr(rhs));
            }
            return MP_OBJ_NULL;
        }

        case MP_BINARY_OP_MORE_EQUAL: {
            if (mp_obj_is_type(rhs_in, &mp_type_pointer)) {
                mp_obj_pointer_t *rhs = MP_OBJ_TO_PTR(rhs_in);
                return mp_obj_new_bool(lhs_addr >= pointer_get_addr(rhs));
            }
            return MP_OBJ_NULL;
        }

        default:
            return MP_OBJ_NULL; // Operation not supported
    }
}

// Type definition
MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_pointer,
    MP_QSTR_pointer,
    MP_TYPE_FLAG_NONE,
    print, pointer_print,
    binary_op, pointer_binary_op
);

// Create a new pointer object wrapping the given address
mp_obj_t mp_obj_new_pointer(mp_obj_t *addr) {
    // Allocate the pointer object itself with normal GC allocation
    mp_obj_pointer_t *o = m_new_obj(mp_obj_pointer_t);
    o->base.type = &mp_type_pointer;
    pointer_set_addr(o, (intptr_t)addr);
    return MP_OBJ_FROM_PTR(o);
}

// Fast pointer creation for temporary arithmetic results (NO GC PINNING)
// WARNING: Use only for intermediate arithmetic results that are immediately dereferenced
// Do NOT use for pointers that will be stored or survive GC cycles
// The caller must ensure the object at addr is pinned elsewhere
mp_obj_t mp_obj_new_pointer_fast(mp_obj_t *addr) {
    mp_obj_pointer_t *o = m_new_obj(mp_obj_pointer_t);
    o->base.type = &mp_type_pointer;
    pointer_set_addr(o, (intptr_t)addr);
    // NOTE: Intentionally skipping gc_pin() for speed - safe only for ephemeral arithm
    return MP_OBJ_FROM_PTR(o);
}

// Extract the pointer from a pointer object  
mp_obj_t *mp_obj_pointer_get_addr(mp_obj_t ptr) {
    // Validate it's actually a pointer object
    if (!mp_obj_is_obj(ptr)) {
        mp_raise_TypeError(MP_ERROR_TEXT("expected pointer object"));
    }
    if (!mp_obj_is_type(ptr, &mp_type_pointer)) {
        mp_raise_TypeError(MP_ERROR_TEXT("expected pointer object"));
    }
    mp_obj_pointer_t *p = MP_OBJ_TO_PTR(ptr);
    return (mp_obj_t *)pointer_get_addr(p);
}

// Extract the pointer from a pointer object  
mp_obj_t *mp_obj_pointer_get(mp_obj_t ptr) {
    // Validate it's actually a pointer object
    if (!mp_obj_is_obj(ptr)) {
        mp_raise_TypeError(MP_ERROR_TEXT("expected pointer object"));
    }
    if (!mp_obj_is_type(ptr, &mp_type_pointer)) {
        mp_raise_TypeError(MP_ERROR_TEXT("expected pointer object"));
    }
    mp_obj_pointer_t *p = MP_OBJ_TO_PTR(ptr);
    return (mp_obj_t *)pointer_get_addr(p);
}
