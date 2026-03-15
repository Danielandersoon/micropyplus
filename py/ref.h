/*
 * Pointer Reference Module Header
 * 
 * Provides C++-like pointer reference functionality for pinned objects.
 * Wraps raw memory pointers with bounds checking and dereference operations.
 */

#ifndef MICROPYTHON_REF_H
#define MICROPYTHON_REF_H

#include "py/obj.h"
#include "py/runtime.h"

// Pointer object structure
typedef struct {
    mp_obj_base_t base;
    uintptr_t address;          // Raw memory address (may be buffer address if from array so would need offset when dereferencing)
    uintptr_t obj_address;      // Original pinned object address
    size_t size;                // Size of referenced object (in bytes)
} mp_obj_pointer_t;

// Forward declarations for functions
extern const mp_obj_type_t mp_type_pointer;
extern const mp_obj_module_t mp_module_ref;

// Helper function to validate and extract pointer from object
uintptr_t ref_get_address(mp_obj_t obj);
size_t ref_get_size(mp_obj_t obj);

#endif // MICROPYTHON_REF_H
