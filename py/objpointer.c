#include "py/obj.h"
#include "py/runtime.h"
#include "py/gc.h"
#include <stdio.h>

typedef struct _mp_obj_pointer_t {
    mp_obj_base_t base;
    mp_obj_t obj;  // The object this pointer refers to
} mp_obj_pointer_t;

// Type definition
MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_pointer,
    MP_QSTR_pointer,
    MP_TYPE_FLAG_NONE
);

// Create a new pointer object wrapping the given object
mp_obj_t mp_obj_new_pointer(mp_obj_t obj) {
    mp_obj_pointer_t *o = m_new_obj(mp_obj_pointer_t);
    o->base.type = &mp_type_pointer;
    o->obj = obj;
    
    // Only pin heap objects (not small ints, qstrs, etc)
    if (mp_obj_is_obj(obj)) {
        gc_pin(MP_OBJ_TO_PTR(obj));
    }
    return MP_OBJ_FROM_PTR(o);
}

// Extract the object from a pointer
mp_obj_t mp_obj_pointer_get(mp_obj_t ptr) {
    // Validate it's actually a pointer object
    if (!mp_obj_is_obj(ptr)) {
        mp_raise_TypeError(MP_ERROR_TEXT("expected pointer object"));
    }
    if (!mp_obj_is_type(ptr, &mp_type_pointer)) {
        mp_raise_TypeError(MP_ERROR_TEXT("expected pointer object"));
    }
    mp_obj_pointer_t *p = MP_OBJ_TO_PTR(ptr);
    return p->obj;
}
