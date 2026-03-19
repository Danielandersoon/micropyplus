import gc
import ref
import time
import sys

max_heap_ptr = 0
max_heap_std = 0
max_heap_mut = 0

def fibonacci_ref(ptr_n, ptr_val_1, ptr_val_2, ptr_temp):
    """Calculate the nth Fibonacci number using pointer arithmetic"""
    
    if ptr_n.get_int16_unsafe() <= 0:
        return
    else:      
        ptr_temp.set_int64_unsafe(ptr_val_1.get_int64_unsafe() + ptr_val_2.get_int64_unsafe())
        ptr_val_1.set_int64_unsafe(ptr_val_2.get_int64_unsafe())
        ptr_val_2.set_int64_unsafe(ptr_temp.get_int64_unsafe())
        ptr_n.sub_int16(1) 
        fibonacci_ref(ptr_n, ptr_val_1, ptr_val_2, ptr_temp)

    return

def fibonacci(n, val_1, val_2, temp):
    """Calculate the nth Fibonacci number"""
    
    if n <= 0:
        return val_2
    else:
        temp = val_1 + val_2
        val_1 = val_2
        val_2 = temp
        n -= 1
        return fibonacci(n, val_1, val_2, temp) 

def fibonacci_mutable(n, val_1, val_2, temp):
    """Calculate the nth Fibonacci number using mutable objects"""
    
    if n[0] <= 0:
        return val_2[0]
    else:
        temp[0] = val_1[0] + val_2[0]
        val_1[0] = val_2[0]
        val_2[0] = temp[0]
        n[0] -= 1
        return fibonacci_mutable(n, val_1, val_2, temp)
    
if __name__ == "__main__":

    performance_metrics = [[], [], [], [], [], [], [], [], [], []] # 0: ptr time, 1: standard time, 2: mutable time, 3: ptr result, 4: standard result, 5: mutable result, 6: time dif ptr std, 7: time ratio std/ptr, 8: time ratio std/mutable, 9: time ratio ptr/mutable
    memory_metrics = [[], [], []]  # 0: ptr memory, 1: standard memory, 2: mutable memory

    for i in range (1000):
        n = 49
        val_1 = 1
        val_2 = 0
        temp = 0

        gcptr_val_1 = gc.pin_int64(val_1)
        gcptr_val_2 = gc.pin_int64(val_2)
        gcptr_temp = gc.pin_int64(temp)
        gcptr_n = gc.pin_int16(n)

        # Pass explicit size (in bytes) as second parameter
        ptr_n = ref.Pointer(gcptr_n)          # int16 = 2 bytes
        ptr_val_1 = ref.Pointer(gcptr_val_1)  # int64 = 8 bytes
        ptr_val_2 = ref.Pointer(gcptr_val_2)  # int64 = 8 bytes
        ptr_temp = ref.Pointer(gcptr_temp)    # int64 = 8 bytes

        # Memory usage for pointer version
        start_time = time.time()
        for x in range (100):
            fibonacci_ref(ptr_n, ptr_val_1, ptr_val_2, ptr_temp)
            if x < 99:  # Don't reset after last iteration
                ptr_n.set_int16_unsafe(49)
                ptr_val_1.set_int64_unsafe(1)
                ptr_val_2.set_int64_unsafe(0)
                ptr_temp.set_int64_unsafe(0)
        end_time = time.time()
        run_time_ptr = end_time - start_time
        max_heap_ptr = gc.mem_alloc()  # Capture at end of batch

        performance_metrics[0].append(run_time_ptr)
        performance_metrics[3].append(ptr_val_2.get_int64_unsafe())
        memory_metrics[0].append(max_heap_ptr)
        
        n2 = 49
        val_1_2 = 1
        val_2_2 = 0
        temp_2 = 0

        # Memory usage for standard version
        start_time = time.time()
        for x in range (100):
            result = fibonacci(n2, val_1_2, val_2_2, temp_2)
            n2 = 49
            val_1_2 = 1
            val_2_2 = 0
            temp_2 = 0
        end_time = time.time()
        run_time = end_time - start_time
        max_heap_std = gc.mem_alloc()  # Capture at end of batch

        performance_metrics[1].append(run_time)
        performance_metrics[4].append(result)
        memory_metrics[1].append(max_heap_std)
        
        n3 = [49]
        val_1_3 = [1]
        val_2_3 = [0]
        temp_3 = [0]

        # Memory usage for mutable version
        start_time = time.time()
        for x in range (100):
            result = fibonacci_mutable(n3, val_1_3, val_2_3, temp_3)
            n3[0] = 49
            val_1_3[0] = 1
            val_2_3[0] = 0
            temp_3[0] = 0

        end_time = time.time()
        run_time_mutable = end_time - start_time
        max_heap_mut = gc.mem_alloc()  # Capture at end of batch

        performance_metrics[2].append(run_time_mutable)
        performance_metrics[5].append(result)
        memory_metrics[2].append(max_heap_mut)

        performance_metrics[6].append(run_time - run_time_ptr)
        performance_metrics[7].append(run_time / run_time_ptr)
        performance_metrics[8].append(run_time / run_time_mutable)
        performance_metrics[9].append(run_time_mutable / run_time_ptr)

        


    print(f"Fibonacci result with pointers: {performance_metrics[3][0]}")
    print(f"Fibonacci result without pointers: {performance_metrics[4][0]}")
    print(f"Fibonacci result with mutable objects: {performance_metrics[5][0]}")
    print(f"\nTiming Results:")
    print(f"Average time with pointers: {sum(performance_metrics[0])/len(performance_metrics[0])} seconds")
    print(f"Average time without pointers: {sum(performance_metrics[1])/len(performance_metrics[1])} seconds")
    print(f"Average time with mutable objects: {sum(performance_metrics[2])/len(performance_metrics[2])} seconds")
    print(f"Average time difference (standard - ptr): {sum(performance_metrics[6])/len(performance_metrics[6])} seconds")
    print(f"Average time ratio (standard / ptr): {sum(performance_metrics[7])/len(performance_metrics[7])}")
    print(f"Average time ratio (standard / mutable): {sum(performance_metrics[8])/len(performance_metrics[8])}")
    print(f"Average time ratio (ptr / mutable): {sum(performance_metrics[9])/len(performance_metrics[9])}")
    print(f"Average time ratio (mutable / ptr): {sum(performance_metrics[9])/len(performance_metrics[9])}")
    print(f"Average time ratio (mutable / standard): {1 / (sum(performance_metrics[8])/len(performance_metrics[8]))}")
    
    print(f"\nMemory Usage Results:")
    print(f"Average memory used with pointers: {sum(memory_metrics[0])/len(memory_metrics[0])} bytes")
    print(f"Average memory used without pointers: {sum(memory_metrics[1])/len(memory_metrics[1])} bytes")
    print(f"Average memory used with mutable objects: {sum(memory_metrics[2])/len(memory_metrics[2])} bytes")
    print(f"Memory ratio (standard / ptr): {(sum(memory_metrics[1])/len(memory_metrics[1])) / (sum(memory_metrics[0])/len(memory_metrics[0]))}")
    print(f"Memory ratio (mutable / ptr): {(sum(memory_metrics[2])/len(memory_metrics[2])) / (sum(memory_metrics[0])/len(memory_metrics[0]))}")
    
    print(f"\nPeak Heap Usage During Recursion:")
    print(f"Max heap with pointers: {max_heap_ptr} bytes")
    print(f"Max heap without pointers: {max_heap_std} bytes")
    print(f"Max heap with mutable objects: {max_heap_mut} bytes")
    print(f"Peak ratio (standard / ptr): {max_heap_std / max_heap_ptr if max_heap_ptr > 0 else 0}")
    print(f"Peak ratio (mutable / ptr): {max_heap_mut / max_heap_ptr if max_heap_ptr > 0 else 0}")
    
    # Debug: Show memory info
    print(f"\nDebug Info:")
    print(f"Current heap free: {gc.mem_free()} bytes")
    print(f"Current heap used: {gc.mem_alloc()} bytes")
    print(f"Total heap: {gc.mem_free() + gc.mem_alloc()} bytes")