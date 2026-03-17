/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
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

#include "py/mpstate.h"
#include "py/obj.h"
#include "py/gc.h"

#if MICROPY_PY_GC && MICROPY_ENABLE_GC

// collect(): run a garbage collection
static mp_obj_t py_gc_collect(void) {
    gc_collect();
    #if MICROPY_PY_GC_COLLECT_RETVAL
    return MP_OBJ_NEW_SMALL_INT(MP_STATE_MEM(gc_collected));
    #else
    return mp_const_none;
    #endif
}
MP_DEFINE_CONST_FUN_OBJ_0(gc_collect_obj, py_gc_collect);

static mp_obj_t py_gc_pin(mp_obj_t obj) {
    gc_pin(MP_OBJ_TO_PTR(obj));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_pin_obj, py_gc_pin);

static mp_obj_t py_gc_unpin(mp_obj_t obj) {
    gc_unpin(MP_OBJ_TO_PTR(obj));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_unpin_obj, py_gc_unpin);

static mp_obj_t py_gc_is_pinned(mp_obj_t obj) {
    return mp_obj_new_bool(gc_is_pinned(MP_OBJ_TO_PTR(obj)));
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_is_pinned_obj, py_gc_is_pinned);

// pin_ptr(): pin an object and return its raw address as an integer
static mp_obj_t py_gc_pin_ptr(mp_obj_t obj) {
    void *ptr = MP_OBJ_TO_PTR(obj);
    gc_pin(ptr);
    return mp_obj_new_int((mp_int_t)(uintptr_t)ptr);
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_pin_ptr_obj, py_gc_pin_ptr);

//********************//
//                    //
// Type aware pinning //
//                    //
//********************//

static mp_obj_t gc_pin_int16(mp_obj_t value_obj) {
    // Create a bytearray container for the integer
    mp_int_t value = mp_obj_get_int(value_obj);
    
    // Allocate 2 bytes of memory for the integer
    uint8_t *data = (uint8_t *)m_malloc(2);
    
    // Store integer in little-endian format
    data[0] = (uint8_t)(value & 0xFF);
    data[1] = (uint8_t)((value >> 8) & 0xFF);
    
    // Pin the memory
    gc_pin(data);
    
    // Return pointer
    return mp_obj_new_int((mp_int_t)(uintptr_t)data);
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_pin_int16_obj, gc_pin_int16);

static mp_obj_t gc_py_unpin_int16(mp_obj_t ptr_obj) {
    mp_int_t addr = mp_obj_get_int(ptr_obj);
    void *ptr = (void *)(uintptr_t)addr;
    gc_unpin(ptr);
    m_free(ptr, 2);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_unpin_int16_obj, gc_py_unpin_int16);

static mp_obj_t gc_pin_int64(mp_obj_t value_obj) {
    // Create a bytearray container for the integer
    mp_int_t value = mp_obj_get_int(value_obj);
    
    // Allocate 8 bytes of memory for the integer
    uint8_t *data = (uint8_t *)m_malloc(8);
    
    // Store integer in little-endian format
    data[0] = (uint8_t)(value & 0xFF);
    data[1] = (uint8_t)((value >> 8) & 0xFF);
    data[2] = (uint8_t)((value >> 16) & 0xFF);
    data[3] = (uint8_t)((value >> 24) & 0xFF);
    data[4] = (uint8_t)((value >> 32) & 0xFF);
    data[5] = (uint8_t)((value >> 40) & 0xFF);
    data[6] = (uint8_t)((value >> 48) & 0xFF);
    data[7] = (uint8_t)((value >> 56) & 0xFF);
    
    // Pin the memory
    gc_pin(data);
    
    // Return pointer
    return mp_obj_new_int((mp_int_t)(uintptr_t)data);
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_pin_int64_obj, gc_pin_int64);

static mp_obj_t gc_py_unpin_int64(mp_obj_t ptr_obj) {
    mp_int_t addr = mp_obj_get_int(ptr_obj);
    void *ptr = (void *)(uintptr_t)addr;
    gc_unpin(ptr);
    m_free(ptr, 8);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_unpin_int64_obj, gc_py_unpin_int64);

static mp_obj_t gc_pin_int(mp_obj_t value_obj) {
    // Create a 4-byte buffer for the integer
    mp_int_t value = mp_obj_get_int(value_obj);
    
    // Allocate 4 bytes of memory for the integer
    uint8_t *data = (uint8_t *)m_malloc(4);
    
    // Store integer in little-endian format
    data[0] = (uint8_t)(value & 0xFF);
    data[1] = (uint8_t)((value >> 8) & 0xFF);
    data[2] = (uint8_t)((value >> 16) & 0xFF);
    data[3] = (uint8_t)((value >> 24) & 0xFF);
    
    // Pin the memory
    gc_pin(data);
    
    // Return pointer
    return mp_obj_new_int((mp_int_t)(uintptr_t)data);
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_pin_int_obj, gc_pin_int);

static mp_obj_t gc_py_unpin_int(mp_obj_t ptr_obj) {
    mp_int_t addr = mp_obj_get_int(ptr_obj);
    void *ptr = (void *)(uintptr_t)addr;
    gc_unpin(ptr);
    m_free(ptr, 4);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_unpin_int_obj, gc_py_unpin_int);

static mp_obj_t gc_pin_float(mp_obj_t value_obj) {
    // Create a 4-byte buffer for the float
    mp_float_t value = mp_obj_get_float(value_obj);
    
    // Allocate 4 bytes of memory for the float
    uint8_t *data = (uint8_t *)m_malloc(4);
    
    // Store float safely using memcpy to avoid strict-aliasing issues
    float f = (float)value;
    memcpy(data, &f, sizeof(float));
    
    // Pin the memory
    gc_pin(data);
    
    // Return pointer
    return mp_obj_new_int((mp_int_t)(uintptr_t)data);
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_pin_float_obj, gc_pin_float);

static mp_obj_t gc_py_unpin_float(mp_obj_t ptr_obj) {
    mp_int_t addr = mp_obj_get_int(ptr_obj);
    void *ptr = (void *)(uintptr_t)addr;
    gc_unpin(ptr);
    m_free(ptr, sizeof(float));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_unpin_float_obj, gc_py_unpin_float);

// ptr_validate(): check if a raw pointer address is valid and pinned
static mp_obj_t py_gc_ptr_validate(mp_obj_t addr_obj) {
    mp_int_t addr = mp_obj_get_int(addr_obj);
    return mp_obj_new_bool(gc_ptr_validate((uintptr_t)addr));
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_ptr_validate_obj, py_gc_ptr_validate);

// ptr_offset(): safely offset a pointer within its pinned object bounds
// Returns new address, or -1 if out of bounds
static mp_obj_t py_gc_ptr_offset(mp_obj_t addr_obj, mp_obj_t offset_obj) {
    mp_int_t addr = mp_obj_get_int(addr_obj);
    mp_int_t offset = mp_obj_get_int(offset_obj);
    uintptr_t new_addr = gc_ptr_offset((uintptr_t)addr, offset);
    if (new_addr == 0) {
        return MP_OBJ_NEW_SMALL_INT(-1);  // Out of bounds
    }
    return mp_obj_new_int((mp_int_t)new_addr);
}
MP_DEFINE_CONST_FUN_OBJ_2(gc_ptr_offset_obj, py_gc_ptr_offset);

// ptr_read_byte(): safely read a byte from a pinned address
static mp_obj_t py_gc_ptr_read_byte(mp_obj_t addr_obj) {
    mp_int_t addr = mp_obj_get_int(addr_obj);
    if (!gc_ptr_validate((uintptr_t)addr)) {
        return MP_OBJ_NEW_SMALL_INT(-1);  // Invalid address
    }
    uint8_t value = *(uint8_t *)(uintptr_t)addr;
    return MP_OBJ_NEW_SMALL_INT(value);
}
MP_DEFINE_CONST_FUN_OBJ_1(gc_ptr_read_byte_obj, py_gc_ptr_read_byte);

// ptr_write_byte(): safely write a byte to a pinned address
static mp_obj_t py_gc_ptr_write_byte(mp_obj_t addr_obj, mp_obj_t value_obj) {
    mp_int_t addr = mp_obj_get_int(addr_obj);
    mp_int_t value = mp_obj_get_int(value_obj);
    if (!gc_ptr_validate((uintptr_t)addr)) {
        return MP_OBJ_NEW_SMALL_INT(-1); 
    }
    *(uint8_t *)(uintptr_t)addr = (uint8_t)value;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(gc_ptr_write_byte_obj, py_gc_ptr_write_byte);

// disable(): disable the garbage collector
static mp_obj_t gc_disable(void) {
    MP_STATE_MEM(gc_auto_collect_enabled) = 0;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(gc_disable_obj, gc_disable);

// enable(): enable the garbage collector
static mp_obj_t gc_enable(void) {
    MP_STATE_MEM(gc_auto_collect_enabled) = 1;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(gc_enable_obj, gc_enable);

static mp_obj_t gc_isenabled(void) {
    return mp_obj_new_bool(MP_STATE_MEM(gc_auto_collect_enabled));
}
MP_DEFINE_CONST_FUN_OBJ_0(gc_isenabled_obj, gc_isenabled);

// mem_free(): return the number of bytes of available heap RAM
static mp_obj_t gc_mem_free(void) {
    gc_info_t info;
    gc_info(&info);
    #if MICROPY_GC_SPLIT_HEAP_AUTO
    // Include max_new_split value here as a more useful heuristic
    return MP_OBJ_NEW_SMALL_INT(info.free + info.max_new_split);
    #else
    return MP_OBJ_NEW_SMALL_INT(info.free);
    #endif
}
MP_DEFINE_CONST_FUN_OBJ_0(gc_mem_free_obj, gc_mem_free);

// mem_alloc(): return the number of bytes of heap RAM that are allocated
static mp_obj_t gc_mem_alloc(void) {
    gc_info_t info;
    gc_info(&info);
    return MP_OBJ_NEW_SMALL_INT(info.used);
}
MP_DEFINE_CONST_FUN_OBJ_0(gc_mem_alloc_obj, gc_mem_alloc);

// obj_header_size(): return the size of MicroPython object headers  
static mp_obj_t gc_obj_header_size(void) {
    return MP_OBJ_NEW_SMALL_INT(sizeof(mp_obj_base_t));
}
MP_DEFINE_CONST_FUN_OBJ_0(gc_obj_header_size_obj, gc_obj_header_size);

#if MICROPY_GC_ALLOC_THRESHOLD
static mp_obj_t gc_threshold(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        if (MP_STATE_MEM(gc_alloc_threshold) == (size_t)-1) {
            return MP_OBJ_NEW_SMALL_INT(-1);
        }
        return mp_obj_new_int(MP_STATE_MEM(gc_alloc_threshold) * MICROPY_BYTES_PER_GC_BLOCK);
    }
    mp_int_t val = mp_obj_get_int(args[0]);
    if (val < 0) {
        MP_STATE_MEM(gc_alloc_threshold) = (size_t)-1;
    } else {
        MP_STATE_MEM(gc_alloc_threshold) = val / MICROPY_BYTES_PER_GC_BLOCK;
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(gc_threshold_obj, 0, 1, gc_threshold);
#endif

static const mp_rom_map_elem_t mp_module_gc_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_gc) },
    { MP_ROM_QSTR(MP_QSTR_collect), MP_ROM_PTR(&gc_collect_obj) },
    { MP_ROM_QSTR(MP_QSTR_disable), MP_ROM_PTR(&gc_disable_obj) },
    { MP_ROM_QSTR(MP_QSTR_enable), MP_ROM_PTR(&gc_enable_obj) },
    { MP_ROM_QSTR(MP_QSTR_isenabled), MP_ROM_PTR(&gc_isenabled_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem_free), MP_ROM_PTR(&gc_mem_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem_alloc), MP_ROM_PTR(&gc_mem_alloc_obj) },
    { MP_ROM_QSTR(MP_QSTR_pin), MP_ROM_PTR(&gc_pin_obj) },
    { MP_ROM_QSTR(MP_QSTR_pin_int16), MP_ROM_PTR(&gc_pin_int16_obj) },
    { MP_ROM_QSTR(MP_QSTR_unpin_int16), MP_ROM_PTR(&gc_unpin_int16_obj) },
    { MP_ROM_QSTR(MP_QSTR_pin_int), MP_ROM_PTR(&gc_pin_int_obj) },
    { MP_ROM_QSTR(MP_QSTR_unpin_int), MP_ROM_PTR(&gc_unpin_int_obj) },
    { MP_ROM_QSTR(MP_QSTR_pin_int64), MP_ROM_PTR(&gc_pin_int64_obj) },
    { MP_ROM_QSTR(MP_QSTR_unpin_int64), MP_ROM_PTR(&gc_unpin_int64_obj) },
    { MP_ROM_QSTR(MP_QSTR_pin_float), MP_ROM_PTR(&gc_pin_float_obj) },
    { MP_ROM_QSTR(MP_QSTR_unpin_float), MP_ROM_PTR(&gc_unpin_float_obj) },
    { MP_ROM_QSTR(MP_QSTR_unpin), MP_ROM_PTR(&gc_unpin_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_pinned), MP_ROM_PTR(&gc_is_pinned_obj) },
    { MP_ROM_QSTR(MP_QSTR_pin_ptr), MP_ROM_PTR(&gc_pin_ptr_obj) },
    { MP_ROM_QSTR(MP_QSTR_ptr_validate), MP_ROM_PTR(&gc_ptr_validate_obj) },
    { MP_ROM_QSTR(MP_QSTR_ptr_offset), MP_ROM_PTR(&gc_ptr_offset_obj) },
    { MP_ROM_QSTR(MP_QSTR_ptr_read_byte), MP_ROM_PTR(&gc_ptr_read_byte_obj) },
    { MP_ROM_QSTR(MP_QSTR_ptr_write_byte), MP_ROM_PTR(&gc_ptr_write_byte_obj) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_obj_header_size), MP_ROM_PTR(&gc_obj_header_size_obj) },
    #if MICROPY_GC_ALLOC_THRESHOLD
    { MP_ROM_QSTR(MP_QSTR_threshold), MP_ROM_PTR(&gc_threshold_obj) },
    #endif
};

static MP_DEFINE_CONST_DICT(mp_module_gc_globals, mp_module_gc_globals_table);

const mp_obj_module_t mp_module_gc = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_gc_globals,
};

MP_REGISTER_MODULE(MP_QSTR_gc, mp_module_gc);

#endif
