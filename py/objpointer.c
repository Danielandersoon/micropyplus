#include "py/obj.h"
#include "py/runtime.h"
#include "py/gc.h"
#include <stdio.h>
#include <stdint.h>

typedef struct _mp_obj_pointer_t {
    mp_obj_base_t base;
    intptr_t addr;  // Store the address using intptr_t to preserve full pointer value
} mp_obj_pointer_t;

// Print function for pointer objects
static void pointer_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_obj_pointer_t *o = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "0x%lx", (unsigned long)o->addr);
}

// Type definition
MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_pointer,
    MP_QSTR_pointer,
    MP_TYPE_FLAG_NONE,
    print, pointer_print
);

// Create a new pointer object wrapping the given address
mp_obj_t mp_obj_new_pointer(mp_obj_t *addr) {
    mp_obj_pointer_t *o = m_new_obj(mp_obj_pointer_t);
    o->base.type = &mp_type_pointer;
    o->addr = (intptr_t)addr;  // Store the address using intptr_t
    
    // Pin the pointed-to object if it's a heap object
    if (addr != NULL && mp_obj_is_obj(*addr)) {
        gc_pin(MP_OBJ_TO_PTR(*addr));
    }
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
    return (mp_obj_t *)p->addr;  // Cast back from intptr_t to pointer
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
    return (mp_obj_t *)p->addr;  // Cast back from intptr_t to pointer
}
