"""
std_micropython.py

Baseline (standard MicroPython) benchmark for nested member access caching.

Hardware setup expected on Raspberry Pi Pico:
- OLED SDA -> GP6  (I2C1)
- OLED SCL -> GP7  (I2C1)
- MPU SDA  -> GP0  (I2C0)
- MPU SCL  -> GP1  (I2C0)

This script does two things:
1) Synthetic deep-chain benchmark: obj.next.next.next... access vs cached target.
2) Live IMU benchmark: repeated chained member access of IMU vector objects
	vs cached vector object references, using allocation-safe raw axis reads.
"""

from machine import I2C, Pin
from utime import sleep_ms, ticks_diff, ticks_us
import gc

from ssd1306 import SSD1306_I2C
from imu import MPU6050, MPUException


# I2C wiring as described in your message.
OLED_I2C_ID = 1
OLED_SDA_PIN = 6
OLED_SCL_PIN = 7

MPU_I2C_ID = 0
MPU_SDA_PIN = 0
MPU_SCL_PIN = 1

I2C_FREQ = 400_000
OLED_ADDR = 0x3C

DEEP_ITERATIONS = 300_000
IMU_ITERATIONS = 80
LOOP_DELAY_MS = 300


class L6:
	__slots__ = ("value",)

	def __init__(self):
		self.value = 42


class L5:
	__slots__ = ("next",)

	def __init__(self):
		self.next = L6()


class L4:
	__slots__ = ("next",)

	def __init__(self):
		self.next = L5()


class L3:
	__slots__ = ("next",)

	def __init__(self):
		self.next = L4()


class L2:
	__slots__ = ("next",)

	def __init__(self):
		self.next = L3()


class L1:
	__slots__ = ("next",)

	def __init__(self):
		self.next = L2()


def speedup(a_us, b_us):
	if b_us == 0:
		return 0.0
	return a_us / b_us


def deep_standard(obj, iterations):
	total = 0
	for _ in range(iterations):
		total += obj.next.next.next.next.next.value
	return total


def deep_cached(obj, iterations):
	target = obj.next.next.next.next.next
	total = 0
	for _ in range(iterations):
		total += target.value
	return total


def imu_standard(imu, iterations):
	total = 0
	for _ in range(iterations):
		imu.get_accel_irq()
		imu.get_gyro_irq()
		# No caching: keep full chained lookups in the hot path.
		total += imu.accel.ix + imu.accel.iy + imu.accel.iz
		total += imu.gyro.ix + imu.gyro.iy + imu.gyro.iz
	return total


def imu_cached(imu, iterations):
	# Match deep-cache semantics by caching final objects once, then reusing.
	accel = imu.accel
	gyro = imu.gyro
	total = 0
	for _ in range(iterations):
		imu.get_accel_irq()
		imu.get_gyro_irq()
		total += accel.ix + accel.iy + accel.iz
		total += gyro.ix + gyro.iy + gyro.iz
	return total


def imu_pointer_cached(imu_ptr, iterations):
	# MPyPlus pointer path: dereference once, then cache final objects.
	imu = *imu_ptr
	accel = imu.accel
	gyro = imu.gyro
	total = 0
	for _ in range(iterations):
		imu.get_accel_irq()
		imu.get_gyro_irq()
		total += accel.ix + accel.iy + accel.iz
		total += gyro.ix + gyro.iy + gyro.iz
	return total


def run_timed(func, *args):
	start = ticks_us()
	result = func(*args)
	elapsed = ticks_diff(ticks_us(), start)
	return result, elapsed


def oled_lines(oled, lines):
	oled.fill(0)
	y = 0
	for line in lines[:6]:
		oled.text(line[:21], 0, y)
		y += 10
	oled.show()


def init_devices():
	i2c_oled = I2C(
		OLED_I2C_ID,
		sda=Pin(OLED_SDA_PIN),
		scl=Pin(OLED_SCL_PIN),
		freq=I2C_FREQ,
	)
	i2c_mpu = I2C(
		MPU_I2C_ID,
		sda=Pin(MPU_SDA_PIN),
		scl=Pin(MPU_SCL_PIN),
		freq=I2C_FREQ,
	)

	oled = SSD1306_I2C(128, 64, i2c_oled, addr=OLED_ADDR)
	imu = MPU6050(i2c_mpu)
	return i2c_oled, i2c_mpu, oled, imu


def main():
	try:
		i2c_oled, i2c_mpu, oled, imu = init_devices()
	except Exception as exc:
		print("Init failed:", exc)
		raise

	print("OLED scan:", i2c_oled.scan())
	print("MPU scan:", i2c_mpu.scan())

	oled_lines(oled, ["MP test ready", "Running...", "Check REPL"]) 
	sleep_ms(400)

	root = L1()
	deep_expected = DEEP_ITERATIONS * 42
	_, _ = run_timed(deep_standard, root, 300)
	_, _ = run_timed(deep_cached, root, 300)

	deep_std_res, deep_std_us = run_timed(deep_standard, root, DEEP_ITERATIONS)
	deep_cache_res, deep_cache_us = run_timed(deep_cached, root, DEEP_ITERATIONS)

	print("\n=== Deep-chain benchmark ===")
	print("std us:", deep_std_us)
	print("cached us:", deep_cache_us)
	print("speedup:", "{:.2f}x".format(speedup(deep_std_us, deep_cache_us)))
	print("correct:", deep_std_res == deep_expected and deep_cache_res == deep_expected)

	oled_lines(
		oled,
		[
			"Deep cache test",
			"std:{}us".format(deep_std_us),
			"cache:{}us".format(deep_cache_us),
			"x{:.2f}".format(speedup(deep_std_us, deep_cache_us)),
		],
	)
	sleep_ms(5000)

	loop_count = 0
	std_total_us = 0
	cache_total_us = 0
	ptr_total_us = 0
	total_imu_iters = 0
	imu_ptr = &imu
	while True:
		loop_count += 1
		gc.collect()

		# Warm-up reduces first-sample jitter from initial cache/path setup.
		_, _ = run_timed(imu_standard, imu, 8)
		_, _ = run_timed(imu_cached, imu, 8)
		_, _ = run_timed(imu_pointer_cached, imu_ptr, 8)

		std_res, std_us = run_timed(imu_standard, imu, IMU_ITERATIONS)
		cache_res, cache_us = run_timed(imu_cached, imu, IMU_ITERATIONS)
		ptr_res, ptr_us = run_timed(imu_pointer_cached, imu_ptr, IMU_ITERATIONS)
		std_total_us += std_us
		cache_total_us += cache_us
		ptr_total_us += ptr_us
		total_imu_iters += IMU_ITERATIONS

		std_avg_us = std_us / IMU_ITERATIONS
		cache_avg_us = cache_us / IMU_ITERATIONS
		ptr_avg_us = ptr_us / IMU_ITERATIONS
		std_total_avg_us = std_total_us / total_imu_iters
		cache_total_avg_us = cache_total_us / total_imu_iters
		ptr_total_avg_us = ptr_total_us / total_imu_iters
		ratio = speedup(std_us, cache_us)
		ptr_ratio = speedup(std_us, ptr_us)

		# Read one sample for display context without extra allocations.
		imu.get_accel_irq()
		ax = imu.accel.ix
		ay = imu.accel.iy
		az = imu.accel.iz

		print("\n=== IMU benchmark #{} ===".format(loop_count))
		print("standard total us:", std_us)
		print("standard avg us/access:", "{:.2f}".format(std_avg_us))
		print("cached total us:", cache_us)
		print("cached avg us/access:", "{:.2f}".format(cache_avg_us))
		print("pointer total us:", ptr_us)
		print("pointer avg us/access:", "{:.2f}".format(ptr_avg_us))
		print("speedup:", "{:.2f}x".format(ratio))
		print("pointer speedup:", "{:.2f}x".format(ptr_ratio))
		print(
			"running totals us:",
			"std={} cache={} ptr={}".format(std_total_us, cache_total_us, ptr_total_us),
		)
		print(
			"running avg us/access:",
			"std={:.2f} cache={:.2f} ptr={:.2f}".format(
				std_total_avg_us,
				cache_total_avg_us,
				ptr_total_avg_us,
			),
		)
		print("checksum:", int(std_res), int(cache_res), int(ptr_res))
		print("accel raw:", ax, ay, az)

		oled_lines(
			oled,
			[
				"Loop {}".format(loop_count),
				"std a:{:.1f}us".format(std_avg_us),
				"cch a:{:.1f}us".format(cache_avg_us),
				"ptr a:{:.1f}us".format(ptr_avg_us),
				"c x{:.2f}".format(ratio),
				"p x{:.2f}".format(ptr_ratio),
			],
		)

		sleep_ms(LOOP_DELAY_MS)


if __name__ == "__main__":
	try:
		main()
	except MPUException as exc:
		print("MPU error:", exc)
	except KeyboardInterrupt:
		print("Stopped by user")
