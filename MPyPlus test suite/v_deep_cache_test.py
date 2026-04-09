# deep_benchmark_12.py
import time

# Dynamically create 12 nested classes L1 to L12
class L12:
    __slots__ = ('value',)
    def __init__(self):
        self.value = 42

# Build the chain backwards: L11 -> L12, L10 -> L11, ..., L1 -> L2
prev_class = L12
for level in range(11, 0, -1):
    name = f'L{level}'
    # Create a class with a single attribute 'next' that points to the next level
    cls = type(name, (), {
        '__slots__': ('next',),
        '__init__': lambda self, nxt=prev_class: setattr(self, 'next', nxt())
    })
    # Store in globals so instances can be created
    globals()[name] = cls
    prev_class = cls

OuterClass = L1

def standard_deep(obj, iterations):
    total = 0
    for _ in range(iterations):
        # Traverse L1 -> L2 -> ... -> L12 -> value
        total += obj.next.next.next.next.next.next.next.next.next.next.next.value
    return total

def pointer_cached_deep(ptr, iterations):
    root = *ptr
    # Cache the target after 11 dot operations
    target = root.next.next.next.next.next.next.next.next.next.next.next
    total = 0
    for _ in range(iterations):
        total += target.value
    return total
 
root_obj = OuterClass()          # Create the object graph
ptr_to_root = &root_obj          # Pointer to the root

iterations = 1000000

# Warm‑up (optional)
standard_deep(root_obj, 1000)
pointer_cached_deep(ptr_to_root, 1000)

# Benchmark standard
start = time.ticks_us()
res_std = standard_deep(root_obj, iterations)
std_time = time.ticks_diff(time.ticks_us(), start)

# Benchmark pointer cached
start = time.ticks_us()
res_ptr = pointer_cached_deep(ptr_to_root, iterations)
ptr_time = time.ticks_diff(time.ticks_us(), start)

# Results
print(f"Standard (chained dots): {std_time} µs")
print(f"Pointer (cached):        {ptr_time} µs")
print(f"Speedup (std / ptr):     {std_time / ptr_time:.2f}x")

expected = iterations * 42
if res_std == expected and res_ptr == expected:
    print("Results match (correct).")
else:
    print("Results mismatch – check implementation.")