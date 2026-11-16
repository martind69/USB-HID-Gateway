# QT Py CH552 memory baseline

This sketch measures the resource cost of the minimum required application
infrastructure on the Adafruit QT Py CH552.

## Included

- ch55xduino USB HID keyboard device stack, compiled with USB RAM profile
  `user148`;
- SPI0 slave request/response protocol used by the ESP32-P4 manager;
- 8-byte request and 17-byte identity response;
- CRC-16/CCITT-FALSE;
- application-assisted BOOT_REQ handoff to the ROM bootloader;
- fixed firmware version and build ID.

This is a resource baseline only. It does not yet implement Logitech G815/G502
descriptors, vendor-specific control requests, vendor HID reports, RGB/DPI
commands, or the final choice of which of the two devices the QT Py emulates.
The keyboard HID profile is a temporary standard-HID stand-in so that the
USB device stack is present during the first measurement.

## Build settings

In Arduino IDE with the DeqingSun ch55xduino board package:

- Board: **CH552 Board**
- Clock: **16 MHz (internal), 3.3 V**
- Bootloader pin: **P3.6 (D+) pull-up**
- USB settings: **user148**

Open `qt_py_memory_test.ino` and select Verify. Do not upload as part of the
memory-only measurement.

Record the compiler summary:
- program storage used / maximum;
- global variables / dynamic memory used;
- local variable / stack margin reported by the toolchain;
- generated `.map` and `.mem` files.

The ch55xduino default USB profile reserves 148 bytes for USB endpoint memory;
the documented remaining xRAM budget is 876 bytes. The CH552 has 256 bytes of
internal iRAM, used in part by the stack, and 1 KiB xRAM. The application adds
25 bytes of explicit SPI frame buffers, plus compiler-generated globals and
stack use. These figures are architectural limits, not measured results.

## Important limitations

- No measured ROM/RAM result is claimed until this exact sketch is compiled
  with the selected ch55xduino package version and settings.
- The ROM application area depends on the bootloader/configuration layout.
  Do not assume all 16 KiB are available to the application.
- No firmware update or configuration-memory write is part of this sketch.
- The BOOT_REQ handoff and SPI timing must be bench-tested on the actual QT Py.
- The USB VID/PID and descriptors are those of the temporary ch55xduino
  keyboard profile, not Logitech identities.

## Expected next step

Compile and capture the real memory report before adding vendor protocol code.
Then replace the temporary keyboard profile with the intended Logitech USB
descriptor/interface layout and repeat the measurement. The second report is
the meaningful capacity gate for the final firmware.
