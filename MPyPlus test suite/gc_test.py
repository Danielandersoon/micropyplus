import gc
import micropython as mp

def test_gc_basic():
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
    print("GC completed") 
    print((*ePtr, " (should still be accessible)"))

def test_dangling_pointer():
    # Create an object and get its id (which is its memory address)
    obj = [1, 2, 3]
    objId = id(obj)
    danglingPtr = &obj

    # Delete the reference to the object
    del obj
    print("Reference deleted, running GC...") 
    gc.collect()
    
    # Attempt to access the memory address of the deleted object
    try:
        # This is just for demonstration; in practice, you should not do this
        print("Attempting to access dangling pointer...")
        print("Dangling pointer accessed:", danglingPtr, " derefs to ", (*danglingPtr))
        mp.mem_info()  # This will show memory usage but won't directly access the dangling pointer
    except Exception as e:
        print("Error accessing dangling pointer:", e)

# Run the test

test_gc_basic()
test_dangling_pointer()
