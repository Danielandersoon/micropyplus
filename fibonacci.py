import gc
import ref
import time

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
    
if __name__ == "__main__":

    performance_metrics = [[], [], [], [], [], []] # ptr time, standard time, ptr result, standard result , time dif, time ratio

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
        ptr_n = ref.Pointer(gcptr_n, 2)          # int16 = 2 bytes
        ptr_val_1 = ref.Pointer(gcptr_val_1, 8)  # int64 = 8 bytes
        ptr_val_2 = ref.Pointer(gcptr_val_2, 8)  # int64 = 8 bytes
        ptr_temp = ref.Pointer(gcptr_temp, 8)    # int64 = 8 bytes

        start_time = time.time()
        for x in range (100):
            fibonacci_ref(ptr_n, ptr_val_1, ptr_val_2, ptr_temp)
            n = 49
            val_1 = 1
            val_2 = 0
            temp = 0
        end_time = time.time()
        run_time_ptr = end_time - start_time

        performance_metrics[0].append(run_time_ptr)
        performance_metrics[2].append(ptr_val_2.get_int64_unsafe())
        
        n2 = 49
        val_1_2 = 1
        n2 = 10
        val_1_2 = 1
        val_2_2 = 0
        temp_2 = 0

        start_time = time.time()
        for x in range (100):
            result = fibonacci(n2, val_1_2, val_2_2, temp_2)
            n2 = 49
            val_1_2 = 1
            val_2_2 = 0
            temp_2 = 0
        end_time = time.time()
        run_time = end_time - start_time
        performance_metrics[1].append(run_time)
        performance_metrics[3].append(result)

        performance_metrics[4].append(run_time - run_time_ptr)
        performance_metrics[5].append(run_time/run_time_ptr)

    print(f"Fibonacci result with pointers: {performance_metrics[2][0]}")
    print(f"Fibonacci result without pointers: {performance_metrics[3][0]}")
    print(f"Average time with pointers: {sum(performance_metrics[0])/len(performance_metrics[0])} seconds")
    print(f"Average time without pointers: {sum(performance_metrics[1])/len(performance_metrics[1])} seconds")
    print(f"Average time difference: {sum(performance_metrics[4])/len(performance_metrics[4])} seconds")
    print(f"Average time ratio: {sum(performance_metrics[5])/len(performance_metrics[5])}")

