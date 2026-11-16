# CH552 target USB profile: Logitech G815

Status: descriptor-derived target specification with user-reconstructed HID report-descriptor candidates; implementation and build verification pending.

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


## 2.1 Raw G815 configuration descriptor

The 59-byte configuration descriptor was captured in `DESCRIPTORs.pcapng`. The bytes below are copied from the descriptor response, not reconstructed from the table:

```text
09 02 3B 00 02 01 04 A0 FA
09 04 00 00 01 03 01 01 00
09 21 11 01 00 01 22 41 00
07 05 81 03 08 00 01
09 04 01 00 01 03 00 00 00
09 21 11 01 00 01 22 BC 00
07 05 82 03 40 00 01
```

Decoded HID report-descriptor lengths are `0x0041 = 65` bytes for interface 0 and `0x00BC = 188` bytes for interface 1. The capture contains the configuration descriptor and these length references; it does not contain the full report-descriptor byte arrays returned by the separate HID `GET_DESCRIPTOR(Report)` requests. Those arrays are still required before the application can implement the exact HID report formats.

## 2.1 Windows HID collection descriptor evidence

The user supplied a WinHidDump report for the physical G815. It exposes the following Windows HID collections:

| USB interface | Windows collection | WinHidDump descriptor length | Notable report ID / role |
|---|---|---:|---|
| 0 | `mi_00\...\kbd` | 50 B | Keyboard collection |
| 1 | `mi_01&col01` | 28 B | Report ID 0x01 |
| 1 | `mi_01&col02` | 74 B | Report ID 0x02; pointer-like collection |
| 1 | `mi_01&col03` | 56 B | Report ID 0x03; Consumer Control |
| 1 | `mi_01&col04` | 32 B | Vendor usage page 0xFF43, Report ID 0x11 |
| 1 | `mi_01&col05` | 33 B | Vendor usage page 0xFF43, Report ID 0x12 |

The WinHidDump output also contains a 460-byte descriptor under a Windows `HID VHF Driver` path. That is a separate virtual HID node, not the physical G815 USB interface descriptor; it must not be used as the G815's USB report descriptor.

These are Windows collection-level descriptors returned/reconstructed for individual HID collection handles. They are useful for mapping usages and Report IDs, but they are not necessarily byte-for-byte copies of the complete interface-level descriptors reported by the USB configuration descriptor (65 bytes for interface 0 and 188 bytes for interface 1). Do not concatenate the collection descriptors or substitute their lengths for the interface-level lengths.

The vendor collections declare:
- Report ID `0x11`: `0x98` bits = 19 data bytes; with Report ID, 20 bytes.
- Report ID `0x12`: `0x1F8` bits = 63 data bytes; with Report ID, 64 bytes.

This aligns with the observed 20-byte and 64-byte vendor report sizes, but actual endpoint routing must be cross-checked against USB transfers.

Exact collection descriptor bytes are stored in [G815 WinHidDump collections](captures/G815_WINHIDDUMP_COLLECTIONS.md). The raw 65-byte and 188-byte interface-level Report Descriptor streams still need to be extracted from USB `GET_DESCRIPTOR(Report)` responses before implementing the exact USB interface descriptors.

## 3. What the current QT Py sketch does and does not implement

`firmware/qt_py_memory_test/qt_py_memory_test.ino` remains a resource-baseline sketch. It currently includes the ch55xduino standard keyboard USB stack as a temporary stand-in, plus the P4↔CH552 SPI identity protocol and BOOT_REQ handoff.

The sketch does **not** yet expose the G815 identity, the two-interface configuration, the two endpoint descriptors, or the G815 HID report descriptors. The profile constants in the sketch are inventory metadata only; they do not alter the USB descriptors emitted by the current keyboard library.

The supplied configuration capture establishes descriptor lengths and endpoint properties. The user has also supplied provisional reconstructed report-descriptor candidates. Use those candidates for the memory-feasibility implementation while clearly marking them provisional; byte-exact raw capture remains a later fidelity refinement and may require corrections.

## 4. Memory-sizing gate

The next meaningful CH552 memory measurement must compile the intended two-interface G815 USB profile, not only the temporary single-interface keyboard profile.

The implementation must account for:

- EP0 control endpoint and control-transfer state;
- interface 0 keyboard HID descriptor and 8-byte interrupt-IN endpoint;
- interface 1 vendor-defined HID descriptor and 64-byte interrupt-IN endpoint;
- the current reconstructed 65-byte and 188-byte HID report-descriptor candidates, marked provisional;
- endpoint buffers and USB stack state;
- the expanded SPI report-path staging buffers (72-byte maximum request, 17-byte response, 8-byte keyboard report and 64-byte vendor report);
- CRC processing, BOOT_REQ handoff and application state;
- stack and interrupt-call depth.

Do not infer exact xRAM use by simply adding 8 + 64 bytes. The USB controller/stack's endpoint-buffer placement and allocation rules determine the actual memory cost.

## 5. Acceptance criteria

1. The G815 configuration and HID report descriptors are represented consistently; provisional reconstructed report descriptors are explicitly identified and can be replaced when raw capture evidence becomes available.
2. The PC enumerates two G815 HID interfaces with endpoint addresses 0x81 and 0x82.
3. Both interrupt-IN endpoints operate concurrently at their descriptor-defined intervals.
4. The vendor interface supports the required Logitech control and interrupt report traffic; descriptor enumeration alone is not G HUB compatibility.
5. Compiler ROM, xRAM, iRAM and stack figures are recorded from the actual selected ch55xduino package and settings.
6. USB keyboard, vendor traffic, SPI management and BOOT_REQ behavior are tested together on the QT Py.

No compilation, flash, USB enumeration, memory measurement or G HUB compatibility test is claimed by this specification.


## 2.2 Required raw interface-level report-descriptor capture

The next blocking input is the exact 65-byte interface-0 and 188-byte interface-1 HID Report Descriptor data stage. The Windows collection descriptors documented above are not substitutes.

The capture procedure, exact Wireshark display filter, SETUP-field checks, packet reassembly rules, export format and validation gates are documented in [G815 raw HID report-descriptor capture procedure](captures/G815_REPORT_DESCRIPTOR_CAPTURE.md).

Do not begin the final CH552 composite descriptor implementation until both byte arrays have been extracted and validated against their declared lengths. No report descriptor bytes are inferred or synthesized.
