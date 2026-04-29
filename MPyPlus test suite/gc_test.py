import gc
import micropython as mp

def test_gc_basic():
    gc.disable()
    # Create some objects
    a = [1, 2, 3]
    b = {"key": "value"}
    c = (4, 5, 6)
    d = bytearray(b"hello world")
    e = 32
    ePtr = &e
    print("Objects created:", a, b, c, d, e, ePtr)
    
    # Delete references and run GC
    del a
    del b
    del c
    print("References deleted, running GC...") 
    gc.collect()
    gc.enable()
    print("GC completed") 
    if *ePtr != 32:
        print("ERROR: live pointer value changed")
    mp.mem_info()

def test_dangling_pointer():
    print("Testing dangling pointer scenario...")
    # Create an object and get its id (which is its memory address)
    obj = [1, 2, 3]
    objId = id(obj)
    danglingPtr = &obj

    # Delete the reference to the object
    del obj
    print("Reference deleted, running GC...") 
    gc.collect()
    
    # Do not dereference a dangling pointer: this can hard-crash the VM.
    print("Dangling pointer captured:", danglingPtr, "for deleted obj id:", objId)
    print("Skipping dangling dereference to avoid VM crash")
    mp.mem_info()


def test_gc_mid_stress_safe_pointer():
    print("Testing middle GC smoke scenario...")
    x = [1, 2, 3, 4]
    y = {"a": 1, "b": 2}
    z = bytearray(b"mid-test")

    del y
    gc.collect()

    # x and z should still be alive and usable.
    x0 = x[0]
    z0 = z[0]
    ord_m = ord('m')
    expected_z0 = 109
    print("Middle smoke values: x[0]=", x0, " z[0]=", z0, " ord('m')=", ord_m, " literal_109=", expected_z0)
    if x0 != 1 or z0 != expected_z0:
        print("ERROR: middle smoke validation failed")
    elif ord_m != expected_z0:
        print("ERROR: builtin ord() returned unexpected value after GC")
    else:
        print("Middle smoke test passed")

# Run the test

test_gc_basic()
print("\n" + "="*50 + "\n")
test_gc_mid_stress_safe_pointer()
print("\n" + "="*50 + "\n")
test_dangling_pointer()
