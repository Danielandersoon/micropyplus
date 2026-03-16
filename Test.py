import sys
import gc

class Test:
    def __init__(self):
        self.int = 16
        self.array = bytearray([1,2,3,4,5])
        self.string = "Hello"

def pass_by_ref(ptr):
    print("ptr current data = ", gc.ptr_read_byte(ptr))

test_class = Test()
int_ptr = gc.pin_ptr(test_class.int)
print(f"ptr location = 0x{int_ptr:x}")

pass_by_ref(int_ptr)