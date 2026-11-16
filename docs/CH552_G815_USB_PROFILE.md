# CH552 target USB profile: Logitech G815

Status: descriptor-derived target specification; implementation and build verification pending.

Source capture: `DESCRIPTORs.pcapng`, supplied in the USB-HID-Gateway project conversation.

## 1. Device allocation

The PC must continue to enumerate the G815 keyboard and G502 HERO mouse as two separate USB devices.

- ESP32-P4 USB Device: emulate the G502 HERO (`046D:C08B`).
- CH552 QT Py USB Device: emulate the G815 keyboard (`046D:C33F`).
- ESP32-P4 remains the only USB Host for the physical G815 and G502.
- The P4-to-CH552 runtime link remains SPI; UART1 remains reserved for the CH552 ROM ISP programming path.

The G502 is assigned to the P4 because its regular mouse interrupt-IN endpoint was observed delivering 8-byte reports at approximately 1,000 reports/s in the supplied capture excerpt. Avoid adding the CH552/SPI hop to this latency-sensitive report path.

## 2. G815 configuration descriptor facts

The G815 configuration descriptor is 59 bytes and contains two HID interfaces:

| Interface | HID subclass | HID protocol | Role | HID report descriptor length |
|---|---:|---:|---|---:|
| 0 | 0x01 | 0x01 | Boot keyboard | 65 bytes |
| 1 | 0x00 | 0x00 | Vendor-defined HID | 188 bytes |

Interrupt-IN endpoints:

| Endpoint address | Interface | Maximum packet size | bInterval |
|---|---:|---:|---:|
| 0x81 | 0 | 8 bytes | 1 |
| 0x82 | 1 | 64 bytes | 1 |

For a Full-Speed interrupt endpoint, `bInterval=1` requests a 1 ms polling interval. This is a descriptor scheduling interval, not a measured guarantee of application-level report delivery latency.

The vendor endpoint's 64-byte maximum packet size is the important CH552 endpoint-buffer and USB-stack design constraint. Both IN endpoints must be represented concurrently, with EP0 retained for control transfers. The CH552 endpoint-memory layout and ch55xduino USB implementation must be checked before selecting a concrete buffer allocation.

## 3. What the current QT Py sketch does and does not implement

`firmware/qt_py_memory_test/qt_py_memory_test.ino` remains a resource-baseline sketch. It currently includes the ch55xduino standard keyboard USB stack as a temporary stand-in, plus the P4↔CH552 SPI identity protocol and BOOT_REQ handoff.

The sketch does **not** yet expose the G815 identity, the two-interface configuration, the two endpoint descriptors, or the G815 HID report descriptors. The profile constants in the sketch are inventory metadata only; they do not alter the USB descriptors emitted by the current keyboard library.

The supplied descriptor summary establishes descriptor lengths and endpoint properties, but not the complete byte contents of the two HID report descriptors. Do not synthesize or guess those report descriptors. Extract the exact report-descriptor bytes from the capture before implementing the composite device.

## 4. Memory-sizing gate

The next meaningful CH552 memory measurement must compile the intended two-interface G815 USB profile, not only the temporary single-interface keyboard profile.

The implementation must account for:

- EP0 control endpoint and control-transfer state;
- interface 0 keyboard HID descriptor and 8-byte interrupt-IN endpoint;
- interface 1 vendor-defined HID descriptor and 64-byte interrupt-IN endpoint;
- exact 65-byte and 188-byte HID report descriptors;
- endpoint buffers and USB stack state;
- the existing 8-byte SPI request and 17-byte SPI response;
- CRC processing, BOOT_REQ handoff and application state;
- stack and interrupt-call depth.

Do not infer exact xRAM use by simply adding 8 + 64 bytes. The USB controller/stack's endpoint-buffer placement and allocation rules determine the actual memory cost.

## 5. Acceptance criteria

1. The exact G815 configuration and HID report descriptors are represented byte-for-byte.
2. The PC enumerates two G815 HID interfaces with endpoint addresses 0x81 and 0x82.
3. Both interrupt-IN endpoints operate concurrently at their descriptor-defined intervals.
4. The vendor interface supports the required Logitech control and interrupt report traffic; descriptor enumeration alone is not G HUB compatibility.
5. Compiler ROM, xRAM, iRAM and stack figures are recorded from the actual selected ch55xduino package and settings.
6. USB keyboard, vendor traffic, SPI management and BOOT_REQ behavior are tested together on the QT Py.

No compilation, flash, USB enumeration, memory measurement or G HUB compatibility test is claimed by this specification.
