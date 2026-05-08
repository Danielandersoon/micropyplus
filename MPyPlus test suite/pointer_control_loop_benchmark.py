import time

# Embedded-style register model: nested objects, hot read/modify/write loop.
class AdcRegs:
    __slots__ = ("sample",)

    def __init__(self):
        self.sample = 17


class TimerRegs:
    __slots__ = ("counter",)

    def __init__(self):
        self.counter = 0


class Peripherals:
    __slots__ = ("adc", "timer")

    def __init__(self):
        self.adc = AdcRegs()
        self.timer = TimerRegs()


class Board:
    __slots__ = ("periph",)

    def __init__(self):
        self.periph = Peripherals()


def standard_control_loop(board, iterations):
    total = 0
    for _ in range(iterations):
        next_counter = (board.periph.timer.counter + board.periph.adc.sample) & 0xFFFF
        board.periph.timer.counter = next_counter
        total += next_counter
    return total, board.periph.timer.counter


def pointer_control_loop(board_ptr, iterations):
    board = *board_ptr

    # Pointer-cache deep register objects once (C-like "address capture" pattern).
    timer_regs = board.periph.timer
    adc_regs = board.periph.adc
    ptimer = &timer_regs
    padc = &adc_regs

    total = 0
    for _ in range(iterations):
        # Keep writes on the cached Python object; use pointers for hot-path reads.
        next_counter = (ptimer->counter + padc->sample) & 0xFFFF
        timer_regs.counter = next_counter
        total += next_counter
    return total, timer_regs.counter


def run_benchmark(iterations=1000000):
    board_std = Board()
    board_ptr = Board()
    ptr = &board_ptr

    # Warm-up
    standard_control_loop(board_std, 1000)
    pointer_control_loop(ptr, 1000)

    start = time.ticks_us()
    std_total, std_final = standard_control_loop(board_std, iterations)
    std_time = time.ticks_diff(time.ticks_us(), start)

    start = time.ticks_us()
    ptr_total, ptr_final = pointer_control_loop(ptr, iterations)
    ptr_time = time.ticks_diff(time.ticks_us(), start)

    print("Embedded Control Loop Benchmark")
    print("--------------------------------")
    print("Iterations:", iterations)
    print("Standard (deep attr read/write):", std_time, "us")
    print("Pointer  (cached reg pointers): ", ptr_time, "us")
    print("Speedup (std / ptr):            {:.2f}x".format(std_time / ptr_time))
    print()
    print("standard total:", std_total, "final counter:", std_final)
    print("pointer  total:", ptr_total, "final counter:", ptr_final)

    if std_total == ptr_total and std_final == ptr_final:
        print("Results match (correct).")
    else:
        print("Results mismatch - check implementation.")


run_benchmark()
