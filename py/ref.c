/*
 * Provides:
 * - Pointer class with get/set/offset operations
 * - Dereference functions (*ptr semantics)
 * - Type-aware pointer operations
 * - Safe bounds checking
 */

#include "py/mpstate.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/objarray.h"
#include "ref.h"

#if MICROPY_ENABLE_GC

// Helper: Get address from Pointer object
uintptr_t ref_get_address(mp_obj_t obj) {
    if (mp_obj_is_type(obj, &mp_type_pointer)) {
        mp_obj_pointer_t *ptr_obj = MP_OBJ_TO_PTR(obj);
        return ptr_obj->address;
    }
    return 0;
}

// Helper: Get size from Pointer object
size_t ref_get_size(mp_obj_t obj) {
    if (mp_obj_is_type(obj, &mp_type_pointer)) {
        mp_obj_pointer_t *ptr_obj = MP_OBJ_TO_PTR(obj);
        return ptr_obj->size;
    }
    return 0;
}

// ############################## //
//                                //
// Pointer Class Implementation   //
//                                //
// ############################## //
// Pointer.__new__() - create from address with optional size
// Usage: Pointer(address) or Pointer(address, size)
static mp_obj_t pointer_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 2, false);
    uintptr_t addr = mp_obj_get_int(args[0]);
    
    // Validate the address is pinned
    if (!gc_ptr_validate(addr)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Address not pinned"));
    }
    
    // Allocate Pointer object first
    mp_obj_pointer_t *self = m_new_obj(mp_obj_pointer_t);
    self->base.type = &mp_type_pointer;
    self->address = addr;
    self->obj_address = addr;
    
    // If size is provided, use it directly
    if (n_args >= 2) {
        self->size = mp_obj_get_int(args[1]);
        return MP_OBJ_FROM_PTR(self);
    }
    
    // Try to detect if this is an array/bytearray object and extract buffer address
    // The address points to mp_obj_base_t, which for arrays contains type pointer at offset 0
    void *obj_ptr = (void *)addr;
    mp_obj_base_t *base = (mp_obj_base_t *)obj_ptr;
    
    // Check if it's an array/bytearray/memoryview type
    if (base->type == &mp_type_bytearray || base->type == &mp_type_array) {
        // This is an array object, extract the items buffer pointer
        mp_obj_array_t *arr = (mp_obj_array_t *)obj_ptr;
        uintptr_t buffer_addr = (uintptr_t)arr->items;
        // Note: arr->len is in elements, we want bytes
        size_t item_size = 1; // for bytearray it's always 1 byte per element
        
        self->address = buffer_addr;  // Buffer address for dereferencing
        self->obj_address = addr;      // Original object address for validation
        self->size = arr->len * item_size; // Size in bytes
        
        return MP_OBJ_FROM_PTR(self);
    }
    
    // For non-array objects, try to determine bounds
    void *base_ptr;
    size_t size;
    if (gc_ptr_get_range(addr, &base_ptr, &size)) {
        self->size = size;
    } else {
        // If we can't determine the size, default to a safe small size
        // This is a fallback - ideally the user should provide explicit size
        self->size = 1;  // Default to 1 byte
    }
    
    return MP_OBJ_FROM_PTR(self);
}

// Pointer.address property - get the raw address
static mp_obj_t pointer_address_get(mp_obj_t self_in) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int((mp_int_t)self->address);
}
MP_DEFINE_CONST_FUN_OBJ_1(pointer_address_get_obj, pointer_address_get);

// Pointer.size property - get the size of referenced object
static mp_obj_t pointer_size_get(mp_obj_t self_in) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->size);
}
MP_DEFINE_CONST_FUN_OBJ_1(pointer_size_get_obj, pointer_size_get);

// Pointer.get() - dereference operator (*ptr)
// Returns the byte value at the pointer address
static mp_obj_t pointer_get(mp_obj_t self_in) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    
    // Validate using the original object address (which is pinned)
    if (!gc_ptr_validate(self->obj_address)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Pointer is no longer valid"));
    }
    
    uint8_t value = *(uint8_t *)(uintptr_t)self->address;
    return MP_OBJ_NEW_SMALL_INT(value);
}
MP_DEFINE_CONST_FUN_OBJ_1(pointer_get_obj, pointer_get);

//Pointer.get_unsafe() - unsafe dereference without validation (faster but sacrifices safety)
// Returns the byte value at the pointer address without validating the pointer
static mp_obj_t pointer_get_unsafe(mp_obj_t self_in) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t value = *(uint8_t *)(uintptr_t)self->address;
    return MP_OBJ_NEW_SMALL_INT(value);
} 
MP_DEFINE_CONST_FUN_OBJ_1(pointer_get_unsafe_obj, pointer_get_unsafe);

// Pointer.set_unsafe(value) - unsafe dereference assignment without validation (faster but sacrifices safety)
// Sets the byte value at the pointer address without validating the pointer
static mp_obj_t pointer_set_unsafe(mp_obj_t self_in, mp_obj_t value_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t value = mp_obj_get_int(value_obj);
    *(uint8_t *)(uintptr_t)self->address = (uint8_t)value;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_set_unsafe_obj, pointer_set_unsafe);


// Pointer.set(value) - dereference assignment (*ptr = value)
static mp_obj_t pointer_set(mp_obj_t self_in, mp_obj_t value_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t value = mp_obj_get_int(value_obj);
    
    // Validate using the original object address (which is pinned)
    if (!gc_ptr_validate(self->obj_address)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Pointer is no longer valid"));
    }
    
    *(uint8_t *)(uintptr_t)self->address = (uint8_t)value;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_set_obj, pointer_set);

// Pointer.offset(n) - pointer arithmetic (ptr + n)
// Returns a new Pointer object offset by n bytes
static mp_obj_t pointer_offset(mp_obj_t self_in, mp_obj_t offset_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t offset = mp_obj_get_int(offset_obj);
    
    // Calculate new address by simple addition (safer for buffer pointers)
    uintptr_t new_addr = self->address + offset;
    
    // Check bounds: new address should be within size from original address
    if (offset < 0 || (size_t)offset >= self->size) {
        mp_raise_ValueError(MP_ERROR_TEXT("Offset out of bounds"));
    }
    
    // Create new Pointer object at offset address
    mp_obj_pointer_t *new_ptr = m_new_obj(mp_obj_pointer_t);
    new_ptr->base.type = &mp_type_pointer;
    new_ptr->address = new_addr;
    new_ptr->obj_address = self->obj_address;  // Keep same object address
    new_ptr->size = self->size - offset;  // Remaining size from offset
    
    return MP_OBJ_FROM_PTR(new_ptr);
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_offset_obj, pointer_offset);

// Pointer.is_valid() - check if pointer is still valid
static mp_obj_t pointer_is_valid(mp_obj_t self_in) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(gc_ptr_validate(self->obj_address));
}
MP_DEFINE_CONST_FUN_OBJ_1(pointer_is_valid_obj, pointer_is_valid);

// Pointer.read_byte(offset=0) - read byte at pointer + offset
static mp_obj_t pointer_read_byte(size_t n_args, const mp_obj_t *args) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t offset = (n_args > 1) ? mp_obj_get_int(args[1]) : 0;
    
    // Check bounds manually if we have a size
    if (offset < 0 || (size_t)offset >= self->size) {
        mp_raise_ValueError(MP_ERROR_TEXT("Read offset out of bounds"));
    }
    
    // If obj_address is set, validate the original object, otherwise validate the buffer address
    if (self->obj_address != 0) {
        if (!gc_ptr_validate(self->obj_address)) {
            mp_raise_ValueError(MP_ERROR_TEXT("Object no longer pinned"));
        }
    } else {
        if (!gc_ptr_validate(self->address)) {
            mp_raise_ValueError(MP_ERROR_TEXT("Address not pinned"));
        }
    }
    
    uintptr_t addr = self->address + offset;
    uint8_t value = *(uint8_t *)addr;
    return MP_OBJ_NEW_SMALL_INT(value);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pointer_read_byte_obj, 1, 2, pointer_read_byte);

// ****************************** //
//                                //
//     Typed pointer get + set    //
//                                //
// ****************************** //

static mp_obj_t pointer_get_int16(mp_obj_t self_in) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    if (!gc_ptr_validate(self->obj_address)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Pointer is no longer valid"));
    }
    int16_t value = *(int16_t *)(uintptr_t)self->address;
    return mp_obj_new_int((mp_int_t)value);
}
MP_DEFINE_CONST_FUN_OBJ_1(pointer_get_int16_obj, pointer_get_int16);

static mp_obj_t pointer_set_int16(mp_obj_t self_in, mp_obj_t value_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t value = mp_obj_get_int(value_obj);
    if (!gc_ptr_validate(self->obj_address)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Pointer is no longer valid"));
    }
    *(int16_t *)(uintptr_t)self->address = (int16_t)value;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_set_int16_obj, pointer_set_int16);

static mp_obj_t pointer_get_int16_unsafe(mp_obj_t self_in) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    int16_t value = *(int16_t *)(uintptr_t)self->address;
    return mp_obj_new_int((mp_int_t)value);
}
MP_DEFINE_CONST_FUN_OBJ_1(pointer_get_int16_unsafe_obj, pointer_get_int16_unsafe);

static mp_obj_t pointer_set_int16_unsafe(mp_obj_t self_in, mp_obj_t value_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t value = mp_obj_get_int(value_obj);
    *(int16_t *)(uintptr_t)self->address = (int16_t)value;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_set_int16_unsafe_obj, pointer_set_int16_unsafe);

static mp_obj_t pointer_get_int64(mp_obj_t self_in) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    if (!gc_ptr_validate(self->obj_address)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Pointer is no longer valid"));
    }
    int64_t value = *(int64_t *)(uintptr_t)self->address;
    return mp_obj_new_int((mp_int_t)value);
}
MP_DEFINE_CONST_FUN_OBJ_1(pointer_get_int64_obj, pointer_get_int64);

static mp_obj_t pointer_set_int64(mp_obj_t self_in, mp_obj_t value_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t value = mp_obj_get_int(value_obj);
    if (!gc_ptr_validate(self->obj_address)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Pointer is no longer valid"));
    }
    *(int64_t *)(uintptr_t)self->address = (int64_t)value;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_set_int64_obj, pointer_set_int64);

static mp_obj_t pointer_get_int64_unsafe(mp_obj_t self_in) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    int64_t value = *(int64_t *)(uintptr_t)self->address;
    return mp_obj_new_int((mp_int_t)value);
}
MP_DEFINE_CONST_FUN_OBJ_1(pointer_get_int64_unsafe_obj, pointer_get_int64_unsafe);

static mp_obj_t pointer_set_int64_unsafe(mp_obj_t self_in, mp_obj_t value_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t value = mp_obj_get_int(value_obj);
    *(int64_t *)(uintptr_t)self->address = (int64_t)value;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_set_int64_unsafe_obj, pointer_set_int64_unsafe);

static mp_obj_t pointer_get_int(mp_obj_t self_in) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    if (!gc_ptr_validate(self->obj_address)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Object no longer pinned"));
    }
    int32_t value = *(int32_t *)(uintptr_t)self->address;
    return mp_obj_new_int((mp_int_t)value);
}
MP_DEFINE_CONST_FUN_OBJ_1(pointer_get_int_obj, pointer_get_int);

static mp_obj_t pointer_set_int(mp_obj_t self_in, mp_obj_t value_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t value = mp_obj_get_int(value_obj);
    if (!gc_ptr_validate(self->obj_address)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Object no longer pinned"));
    }
    *(int32_t *)(uintptr_t)self->address = (int32_t)value;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_set_int_obj, pointer_set_int);

static mp_obj_t pointer_get_int_unsafe(mp_obj_t self_in) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    int32_t value = *(int32_t *)(uintptr_t)self->address;
    return mp_obj_new_int((mp_int_t)value);
}
MP_DEFINE_CONST_FUN_OBJ_1(pointer_get_int_unsafe_obj, pointer_get_int_unsafe);

static mp_obj_t pointer_set_int_unsafe(mp_obj_t self_in, mp_obj_t value_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t value = mp_obj_get_int(value_obj);
    *(int32_t *)(uintptr_t)self->address = (int32_t)value;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_set_int_unsafe_obj, pointer_set_int_unsafe);

// float access
static mp_obj_t pointer_get_float(mp_obj_t self_in) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    if (!gc_ptr_validate(self->obj_address)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Object no longer pinned"));
    }
    float value = *(float *)(uintptr_t)self->address;
    return mp_obj_new_float((mp_float_t)value);
}
MP_DEFINE_CONST_FUN_OBJ_1(pointer_get_float_obj, pointer_get_float);

static mp_obj_t pointer_set_float(mp_obj_t self_in, mp_obj_t value_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    mp_float_t value = mp_obj_get_float(value_obj);
    if (!gc_ptr_validate(self->obj_address)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Object no longer pinned"));
    }
    *(float *)(uintptr_t)self->address = (float)value;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_set_float_obj, pointer_set_float);

static mp_obj_t pointer_get_float_unsafe(mp_obj_t self_in) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    float value = *(float *)(uintptr_t)self->address;
    return mp_obj_new_float((mp_float_t)value);
}
MP_DEFINE_CONST_FUN_OBJ_1(pointer_get_float_unsafe_obj, pointer_get_float_unsafe);

static mp_obj_t pointer_set_float_unsafe(mp_obj_t self_in, mp_obj_t value_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    mp_float_t value = mp_obj_get_float(value_obj);
    *(float *)(uintptr_t)self->address = (float)value;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_set_float_unsafe_obj, pointer_set_float_unsafe);

// In-place arithmetic operations (int)
static mp_obj_t pointer_add_int(mp_obj_t self_in, mp_obj_t delta_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    int32_t delta = mp_obj_get_int(delta_obj);
    *(int32_t *)(uintptr_t)self->address += delta;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_add_int_obj, pointer_add_int);

static mp_obj_t pointer_sub_int(mp_obj_t self_in, mp_obj_t delta_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    int32_t delta = mp_obj_get_int(delta_obj);
    *(int32_t *)(uintptr_t)self->address -= delta;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_sub_int_obj, pointer_sub_int);

// In-place arithmetic operations (int16)
static mp_obj_t pointer_add_int16(mp_obj_t self_in, mp_obj_t delta_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    int16_t delta = mp_obj_get_int(delta_obj);
    *(int16_t *)(uintptr_t)self->address += delta;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_add_int16_obj, pointer_add_int16);

static mp_obj_t pointer_sub_int16(mp_obj_t self_in, mp_obj_t delta_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    int16_t delta = mp_obj_get_int(delta_obj);
    *(int16_t *)(uintptr_t)self->address -= delta;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_sub_int16_obj, pointer_sub_int16);

// In-place arithmetic operations (float)
static mp_obj_t pointer_add_float(mp_obj_t self_in, mp_obj_t delta_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    mp_float_t delta = mp_obj_get_float(delta_obj);
    float *fptr = (float *)(uintptr_t)self->address;
    *fptr = *fptr + (float)delta;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_add_float_obj, pointer_add_float);

static mp_obj_t pointer_sub_float(mp_obj_t self_in, mp_obj_t delta_obj) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(self_in);
    mp_float_t delta = mp_obj_get_float(delta_obj);
    float *fptr = (float *)(uintptr_t)self->address;
    *fptr = *fptr - (float)delta;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(pointer_sub_float_obj, pointer_sub_float);

// Pointer.write_byte(value, offset=0) - write byte at pointer + offset
static mp_obj_t pointer_write_byte(size_t n_args, const mp_obj_t *args) {
    mp_obj_pointer_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t value = mp_obj_get_int(args[1]);
    mp_int_t offset = (n_args > 2) ? mp_obj_get_int(args[2]) : 0;
    
    // Check bounds manually if we have a size
    if (offset < 0 || (size_t)offset >= self->size) {
        mp_raise_ValueError(MP_ERROR_TEXT("Write offset out of bounds"));
    }
    
    // If obj_address is set, validate the original object, otherwise validate the buffer address
    if (self->obj_address != 0) {
        if (!gc_ptr_validate(self->obj_address)) {
            mp_raise_ValueError(MP_ERROR_TEXT("Object no longer pinned"));
        }
    } else {
        if (!gc_ptr_validate(self->address)) {
            mp_raise_ValueError(MP_ERROR_TEXT("Address not pinned"));
        }
    }
    
    uintptr_t addr = self->address + offset;
    *(uint8_t *)addr = (uint8_t)value;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pointer_write_byte_obj, 2, 3, pointer_write_byte);

// Pointer.deref() - alias for get() using * operator concept
static mp_obj_t pointer_deref(mp_obj_t self_in) {
    return pointer_get(self_in);
}
MP_DEFINE_CONST_FUN_OBJ_1(pointer_deref_obj, pointer_deref);

// Pointer locals dict
static const mp_rom_map_elem_t pointer_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_address), MP_ROM_PTR(&pointer_address_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_size), MP_ROM_PTR(&pointer_size_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&pointer_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_unsafe), MP_ROM_PTR(&pointer_get_unsafe_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_unsafe), MP_ROM_PTR(&pointer_set_unsafe_obj) },
    { MP_ROM_QSTR(MP_QSTR_set), MP_ROM_PTR(&pointer_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_offset), MP_ROM_PTR(&pointer_offset_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_valid), MP_ROM_PTR(&pointer_is_valid_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_byte), MP_ROM_PTR(&pointer_read_byte_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_byte), MP_ROM_PTR(&pointer_write_byte_obj) },
    { MP_ROM_QSTR(MP_QSTR_deref), MP_ROM_PTR(&pointer_deref_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_int), MP_ROM_PTR(&pointer_get_int_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_int), MP_ROM_PTR(&pointer_set_int_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_int_unsafe), MP_ROM_PTR(&pointer_get_int_unsafe_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_int_unsafe), MP_ROM_PTR(&pointer_set_int_unsafe_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_int16), MP_ROM_PTR(&pointer_get_int16_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_int16), MP_ROM_PTR(&pointer_set_int16_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_int16_unsafe), MP_ROM_PTR(&pointer_get_int16_unsafe_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_int16_unsafe), MP_ROM_PTR(&pointer_set_int16_unsafe_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_int64), MP_ROM_PTR(&pointer_get_int64_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_int64), MP_ROM_PTR(&pointer_set_int64_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_int64_unsafe), MP_ROM_PTR(&pointer_get_int64_unsafe_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_int64_unsafe), MP_ROM_PTR(&pointer_set_int64_unsafe_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_float), MP_ROM_PTR(&pointer_get_float_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_float), MP_ROM_PTR(&pointer_set_float_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_float_unsafe), MP_ROM_PTR(&pointer_get_float_unsafe_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_float_unsafe), MP_ROM_PTR(&pointer_set_float_unsafe_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_int), MP_ROM_PTR(&pointer_add_int_obj) },
    { MP_ROM_QSTR(MP_QSTR_sub_int), MP_ROM_PTR(&pointer_sub_int_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_int16), MP_ROM_PTR(&pointer_add_int16_obj) },
    { MP_ROM_QSTR(MP_QSTR_sub_int16), MP_ROM_PTR(&pointer_sub_int16_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_float), MP_ROM_PTR(&pointer_add_float_obj) },
    { MP_ROM_QSTR(MP_QSTR_sub_float), MP_ROM_PTR(&pointer_sub_float_obj) },
};
static MP_DEFINE_CONST_DICT(pointer_locals_dict, pointer_locals_dict_table);

// Pointer type definition
MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_pointer,
    MP_QSTR_Pointer,
    MP_TYPE_FLAG_NONE,
    make_new, pointer_make_new,
    locals_dict, &pointer_locals_dict
    );

// ########################## //
//                            //
// Module-level functions     //
//                            //
// ########################## //

// ref.deref(addr) - get value at address
static mp_obj_t ref_deref(mp_obj_t addr_obj) {
    uintptr_t addr = mp_obj_get_int(addr_obj);
    
    if (!gc_ptr_validate(addr)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Address not pinned"));
    }
    
    uint8_t value = *(uint8_t *)addr;
    return MP_OBJ_NEW_SMALL_INT(value);
}
MP_DEFINE_CONST_FUN_OBJ_1(ref_deref_obj, ref_deref);

// ref.set_deref(addr, value) - set value at address
static mp_obj_t ref_set_deref(mp_obj_t addr_obj, mp_obj_t value_obj) {
    uintptr_t addr = mp_obj_get_int(addr_obj);
    mp_int_t value = mp_obj_get_int(value_obj);
    
    if (!gc_ptr_validate(addr)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Address not pinned"));
    }
    
    *(uint8_t *)addr = (uint8_t)value;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(ref_set_deref_obj, ref_set_deref);

// ref.Pointer(addr) - create new Pointer object
static const mp_rom_map_elem_t mp_module_ref_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ref) },
    { MP_ROM_QSTR(MP_QSTR_Pointer), MP_ROM_PTR(&mp_type_pointer) },
    { MP_ROM_QSTR(MP_QSTR_deref), MP_ROM_PTR(&ref_deref_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_deref), MP_ROM_PTR(&ref_set_deref_obj) },
};

static MP_DEFINE_CONST_DICT(mp_module_ref_globals, mp_module_ref_globals_table);

const mp_obj_module_t mp_module_ref = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_ref_globals,
};

MP_REGISTER_MODULE(MP_QSTR_ref, mp_module_ref);

#endif // MICROPY_ENABLE_GC
