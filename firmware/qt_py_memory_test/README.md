# QT Py CH552 memory baseline and G815 target

This sketch is the first resource baseline for the Adafruit QT Py CH552. The selected device role is now the Logitech G815 keyboard (`046D:C33F`), while the ESP32-P4 emulates the G502 HERO (`046D:C08B`).

## Current sketch contents

- Temporary ch55xduino standard HID keyboard USB stack;
- SPI0 slave request/response protocol used by the ESP32-P4 manager;
- 8-byte request and 17-byte identity response;
- CRC-16/CCITT-FALSE;
- application-assisted P3.2/INT0 BOOT_REQ handoff;
- compile-time inventory constants for the intended G815 USB profile.

**Important:** the current sketch still enumerates using the temporary single-interface ch55xduino keyboard profile. The G815 constants are documentation/inventory only. They do not create the final composite USB configuration.

## G815 target profile from `DESCRIPTORs.pcapng`

The supplied capture summary identifies a 59-byte configuration descriptor with two HID interfaces:

| Interface | Role | HID report descriptor | IN endpoint | Max packet | bInterval |
|---|---|---:|---|---:|---:|
| 0 | Boot keyboard | 65 bytes | 0x81 | 8 bytes | 1 |
| 1 | Vendor-defined HID | 188 bytes | 0x82 | 64 bytes | 1 |

Both interfaces must be active concurrently. The vendor endpoint's 64-byte maximum packet size is a key USB endpoint-memory constraint. For Full-Speed interrupt endpoints, `bInterval=1` requests a 1 ms polling interval.

The descriptor summary provides the report-descriptor lengths and endpoint properties, but not the complete HID report-descriptor byte arrays. Do not invent those bytes. Extract the exact descriptors from the capture before implementing the final composite USB profile.

See [CH552 G815 USB profile](../../docs/CH552_G815_USB_PROFILE.md) for the complete target specification and acceptance criteria.

## Build settings

In Arduino IDE with the DeqingSun ch55xduino board package:

- Board: **CH552 Board**
- Clock: **16 MHz (internal), 3.3 V**
- Bootloader pin: **P3.6 (D+) pull-up**
- USB settings: **user148**

Open `qt_py_memory_test.ino` and select Verify. Do not upload as part of the memory-only measurement.

Record the compiler summary:
- program storage used / maximum;
- global variables / dynamic memory used;
- local variable / stack margin reported by the toolchain;
- generated `.map` and `.mem` files.

The ch55xduino `user148` profile documents 148 bytes reserved for USB endpoint memory and 876 bytes remaining xRAM under that profile. The CH552 has 256 bytes internal iRAM and 1 KiB xRAM. The sketch also allocates 25 bytes for the explicit SPI request/response buffers. These are architectural figures, not measured results.

## Memory-sizing stages

1. Compile the current temporary keyboard + SPI + BOOT_REQ baseline and record ROM, xRAM, iRAM and stack figures.
2. Implement the exact G815 two-interface composite profile, including EP0, 8-byte endpoint 0x81, 64-byte endpoint 0x82 and the exact 65-byte / 188-byte HID report descriptors.
3. Compile again and compare the resource reports. The second report is the meaningful CH552 capacity gate.
4. Add Logitech vendor control and interrupt-report handling only after the descriptor-level composite profile fits with a defensible stack margin.

Do not estimate endpoint memory by merely adding 8 + 64 bytes. The USB controller and stack determine endpoint-buffer placement and allocation overhead.

## Limitations and safety

- No measured ROM/RAM result is claimed until this exact sketch is compiled with the selected package version and settings.
- The ROM application area depends on bootloader/configuration layout; do not assume all 16 KiB are available.
- No firmware update or configuration-memory write is part of this sketch.
- The BOOT_REQ handoff and SPI timing still require bench validation.
- The current USB VID/PID and descriptors are those of the temporary ch55xduino keyboard profile, not the Logitech G815.
- Descriptor enumeration is not proof of Logitech G HUB compatibility.
- No compilation, flashing, hardware test or G HUB test is claimed.


## Additional G815 HID collection evidence

The supplied WinHidDump output is documented in [G815 WinHidDump collections](../../docs/captures/G815_WINHIDDUMP_COLLECTIONS.md). It identifies the physical G815's Windows HID collections, including vendor Report ID `0x11` (20 bytes including ID) and Report ID `0x12` (64 bytes including ID).

These are collection-level descriptors returned/reconstructed by Windows, not necessarily the raw interface-level report descriptor streams. The USB configuration capture still identifies 65 bytes for interface 0 and 188 bytes for interface 1. The separate 460-byte `HID VHF Driver` descriptor is not the physical Logitech G815 descriptor.

The exact raw 65-byte and 188-byte interface descriptors must still be captured from USB `GET_DESCRIPTOR(Report)` before implementing the final composite USB descriptors. No descriptor code or memory estimate should be based on concatenating the Windows collection descriptors.


## Exact raw descriptor capture is the next gate

The raw interface-level Report Descriptor byte streams are still missing. Capture and export exactly 65 bytes for interface 0 and 188 bytes for interface 1 as described in [G815 raw HID report-descriptor capture procedure](../../docs/captures/G815_REPORT_DESCRIPTOR_CAPTURE.md). The Windows collection-level descriptors are supporting evidence only and must not be concatenated or substituted.

Do not replace the temporary keyboard USB stack with a guessed G815 composite profile. Implement the final two-interface profile only after both captured byte arrays pass the documented length and HID-item-boundary checks.
