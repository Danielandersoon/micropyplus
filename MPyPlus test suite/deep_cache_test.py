# deep_benchmark.py
import time

class L6:
    __slots__ = ('value',)
    def __init__(self):
        self.value = 42

class L5:
    __slots__ = ('next',)
    def __init__(self):
        self.next = L6()

class L4:
    __slots__ = ('next',)
    def __init__(self):
        self.next = L5()

class L3:
    __slots__ = ('next',)
    def __init__(self):
        self.next = L4()

class L2:
    __slots__ = ('next',)
    def __init__(self):
        self.next = L3()

class L1:
    __slots__ = ('next',)
    def __init__(self):
        self.next = L2()

def standard_deep(obj, iterations):
    total = 0
    for _ in range(iterations):
        total += obj.next.next.next.next.next.value
    return total

def pointer_cached_deep(ptr, iterations):
    # Dereference once to get the root object
    root = *ptr
    # Cache the final target (after 5 dot operations)
    target = root.next.next.next.next.next
    total = 0
    for _ in range(iterations):
        total += target.value
    return total

root_obj = L1()          # Create the object graph
ptr_to_root = &root_obj  # Pointer to the root

iterations = 1000000

standard_deep(root_obj, 1000)
pointer_cached_deep(ptr_to_root, 1000)

# Standard
start = time.ticks_us()
res_std = standard_deep(root_obj, iterations)
std_time = time.ticks_diff(time.ticks_us(), start)

# Pointer cached
start = time.ticks_us()
res_ptr = pointer_cached_deep(ptr_to_root, iterations)
ptr_time = time.ticks_diff(time.ticks_us(), start)

print(f"Standard (chained dots):        {std_time} µs")
print(f"Pointer (cached):               {ptr_time} µs")
print(f"Speedup (std / ptr):            {std_time / ptr_time:.2f}x")

expected = iterations * 42
if res_std == expected and res_ptr == expected:
    print("Results match (correct).")
else:
    print("Results mismatch – check implementation.")