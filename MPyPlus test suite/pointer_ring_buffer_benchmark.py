import time


class RingState:
    __slots__ = ("head", "mask")

    def __init__(self, size_pow2):
        self.head = 0
        self.mask = size_pow2 - 1


class DmaBuffer:
    __slots__ = ("samples",)

    def __init__(self, size):
        self.samples = [((i * 37) + 13) & 0x3FF for i in range(size)]


class RingDevice:
    __slots__ = ("state", "dma")

    def __init__(self, size_pow2):
        self.state = RingState(size_pow2)
        self.dma = DmaBuffer(size_pow2)


def standard_ring_read(dev, iterations):
    total = 0
    for _ in range(iterations):
        idx = dev.state.head & dev.state.mask
        total += dev.dma.samples[idx]
        dev.state.head = dev.state.head + 1

    return total, dev.state.head


def pointer_ring_read(dev_ptr, iterations):
    dev = *dev_ptr
    state = dev.state
    dma = dev.dma

    # Pointer-cached register/state object used in each poll iteration.
    pstate = &state

    samples = dma.samples
    total = 0

    for _ in range(iterations):
        idx = pstate->head & pstate->mask
        total += samples[idx]
        state.head = pstate->head + 1

    return total, state.head


def run_benchmark(size_pow2=64, iterations=1000000):
    dev_std = RingDevice(size_pow2)
    dev_ptr = RingDevice(size_pow2)
    ptr = &dev_ptr

    # Warm-up
    standard_ring_read(dev_std, 1000)
    pointer_ring_read(ptr, 1000)

    start = time.ticks_us()
    std_total, std_head = standard_ring_read(dev_std, iterations)
    std_time = time.ticks_diff(time.ticks_us(), start)

    start = time.ticks_us()
    ptr_total, ptr_head = pointer_ring_read(ptr, iterations)
    ptr_time = time.ticks_diff(time.ticks_us(), start)

    print("Ring Buffer Polling Benchmark")
    print("-----------------------------")
    print("Buffer size:", size_pow2)
    print("Iterations:", iterations)
    print("Standard (deep state + list index):", std_time, "us")
    print("Pointer  (cached pstate->head/mask):", ptr_time, "us")
    print("Speedup (std / ptr):    {:.2f}x".format(std_time / ptr_time))
    print()
    print("standard total:", std_total, "head:", std_head)
    print("pointer  total:", ptr_total, "head:", ptr_head)

    if std_total == ptr_total and std_head == ptr_head:
        print("Results match (correct).")
    else:
        print("Results mismatch - check implementation.")


run_benchmark()
