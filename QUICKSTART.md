# MicroPyPlus Quickstart Guide

This guide provides essential commands and procedures for building and running MicroPyPlus on supported platforms. Currently, Unix and RP2 ports have been verified to work reliably.

## Building

### Clean Build

To remove all previously built artifacts:

```bash
make clean
```

### Standard Build

To build MicroPyPlus with default settings:

```bash
make -j4
```

### Build Variants

MicroPyPlus supports multiple build variants that affect garbage collection behavior:

- **Standard variant** (garbage collection with compaction):
  ```bash
  make VARIANT="standard"
  ```

- **Backward compatible variant** (garbage collection without compaction):
  ```bash
  make VARIANT="backward_compatible"
  ```

### Verbose Debug Build

To generate a build with verbose debug output enabled:

```bash
make CFLAGS_EXTRA="-DMICROPY_DEBUG_VERBOSE=1" -j4
```

### Readable Heap Dump

For a readable heap dump on garbage collection, set the ```READABLE_HEAP_DUMP``` constant to 1 in gc.c at line 66

### Building mpy-cross

To compile mpy-cross from the repository root:

```bash
make -C mpy-cross
```

## Running

### Unix Port

To run a Python file on the Unix port:

```bash
./<directory to build>/<build variant>/micropython <example.py>
```

**Example:**

```bash
./ports/unix/build-standard/micropython "./MPyPlus test suite/test_comprehensive_pointers.py"
```

## RP2 (Raspberry Pi Pico) Port

### Standard Build Procedure

1. Build mpy-cross:
   ```bash
   make -C mpy-cross
   ```

2. Navigate to the RP2 port directory:
   ```bash
   cd ports/rp2
   ```

3. Initialize submodules:
   ```bash
   make submodules
   ```

4. Clean any previous builds:
   ```bash
   make clean
   ```

5. Build with the recommended backward-compatible variant (which disables garbage collection compaction):
   ```bash
   make -j4 VARIANT="backward_compatible"
   ```

### Flashing to Device

After the build completes, you can deploy the firmware to your Raspberry Pi Pico using one of the following methods:

#### Method 1: BOOTSEL Mode

1. Set the Raspberry Pi Pico to BOOTSEL mode (hold the BOOTSEL button while powering on)
2. Copy the generated `.uf2` file from the build folder to the mounted Pico storage
3. The device will automatically reset and run the new firmware

#### Method 2: Picotool

Alternatively, use picotool to load the firmware directly:

```bash
picotool load -x firmware.elf
```

### Board-Specific Builds

To build for a Raspberry Pi Pico W or other non-standard boards, specify the board target:

```bash
make BOARD=RPI_PICO_W submodules
make BOARD=RPI_PICO_W clean
make BOARD=RPI_PICO_W
```

Replace `RPI_PICO_W` with the appropriate board identifier if targeting a different platform.

## Notes

- The `-j4` flag enables parallel compilation using 4 jobs for faster builds
- The backward-compatible variant is recommended for RP2 builds
- Ensure all submodules are initialized before building RP2 firmware
