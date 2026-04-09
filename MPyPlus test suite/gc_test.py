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

# Run the test

test_gc_basic()
