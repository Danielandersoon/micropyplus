import time

class Level3:
    __slots__ = ('value',)
    def __init__(self):
        self.value = 42

class Level2:
    __slots__ = ('next',)
    def __init__(self):
        self.next = Level3()

class Level1:
    __slots__ = ('next',)
    def __init__(self):
        self.next = Level2()

# Standard Python
def standard_nested(obj, iterations):
    total = 0
    for i in range(iterations):
        total += obj.next.next.value
    return total

# Pointer with caching (dereference once)
def pointer_cached(ptr, iterations):
    obj = *ptr
    target = obj.next.next  # Cache after dereference
    total = 0
    for i in range(iterations):
        total += target.value
    return total

# Pointer with deep cache (create pointer to target)
def pointer_deep_cache(deep_ptr, iterations):
    total = 0
    for i in range(iterations):
        total += deep_ptr->value  # Direct arrow access
    return total

# Setup - need intermediate variables because & only works on variables
obj = Level1()

# Create pointer to the deep target using intermediate variables
middle = obj.next
deep = middle.next
deep_ptr = &deep

# Alternative: create pointer to obj and dereference
ptr = &obj

iterations = 1000000

print("Running tests...")

start = time.ticks_us()
res_std = standard_nested(obj, iterations)
std_time = time.ticks_diff(time.ticks_us(), start)
print(f"Standard: {std_time} us")

start = time.ticks_us()
res_ptr = pointer_cached(ptr, iterations)
cached_time = time.ticks_diff(time.ticks_us(), start)
print(f"Pointer (cached): {cached_time} us")

start = time.ticks_us()
pointer_deep_cache(deep_ptr, iterations)
arrow_time = time.ticks_diff(time.ticks_us(), start)
print(f"Pointer (deep arrow): {arrow_time} us")

print(f"\nSpeedup (cached vs standard): {std_time/cached_time:.2f}x")
print(f"Speedup (deep arrow vs standard): {std_time/arrow_time:.2f}x")

expected = iterations * 42
if res_std == expected and res_ptr == expected:
    print("Results match (correct).")
else:
    print("Results mismatch – check implementation.")