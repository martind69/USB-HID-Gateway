# G815 raw HID report-descriptor capture procedure

Status: required capture evidence is still missing. This procedure obtains the exact interface-level HID Report Descriptor byte streams required by the CH552 G815 emulator.

## 1. Capture setup

1. Connect the physical Logitech G815 directly through the same USBPcap capture path used for the existing descriptor capture.
2. Start a new USBPcap capture before plugging in the keyboard, or disconnect/reconnect it while capture is running.
3. In Wireshark, enable USB control-transfer reassembly if the installed version exposes that preference. Preserve the complete capture as PCAPNG.
4. Locate the HID class-specific `GET_DESCRIPTOR(Report)` control requests. Do not use the 59-byte Configuration Descriptor response as a substitute.

Display filter:

```wireshark
usb.setup.bRequest == 0x06 && usb.setup.wValue == 0x2200
```

Here `bRequest=0x06` is standard `GET_DESCRIPTOR`; the high byte of `wValue=0x22` selects HID Report Descriptor and the low byte is descriptor index zero.

## 2. Required interface requests

The HID class descriptor embedded in the captured 59-byte configuration descriptor declares these lengths:

| Interface (`wIndex`) | Descriptor type (`wValue`) | Expected `wLength` | Required result |
|---:|---:|---:|---|
| 0 | `0x2200` | 65 (`0x0041`) | exactly 65 descriptor bytes |
| 1 | `0x2200` | 188 (`0x00BC`) | exactly 188 descriptor bytes |

For each request, verify the SETUP fields:

- `bmRequestType = 0x81` (Device-to-host, Standard, Interface)
- `bRequest = 0x06`
- `wValue = 0x2200`
- `wIndex = 0` or `1`
- `wLength = 65` or `188`

The data stage may span multiple USB packets. Reassemble the complete control transfer in packet order and take only the data-stage payload. Do not include the SETUP packet, USBPcap pseudo-header, packet headers, or the status stage. A short capture, truncated transfer, or payload length other than the declared `wLength` is not acceptable evidence.

## 3. Export format

Save two plain-text files (or provide the complete reassembled byte strings):

- `g815_interface0_report_descriptor.hex` — exactly 65 bytes.
- `g815_interface1_report_descriptor.hex` — exactly 188 bytes.

Use hexadecimal byte pairs separated by spaces or newlines. Preserve byte order. Do not append offsets, ASCII columns, packet headers, or annotations to the hex payload.

Also retain the original PCAPNG and note the frame numbers of each SETUP and completed data stage. If the request is split, record all contributing data packets.

## 4. Validation before implementation

For each interface:

1. Confirm the request's `wIndex` matches the interface number.
2. Confirm the completed data-stage length exactly matches `wLength`.
3. Confirm no bytes were copied from the SETUP or status stage.
4. Parse the HID short and long items without running beyond the descriptor boundary; reject truncated items.
5. Confirm every collection opened by `Collection` is closed by `End Collection`.
6. Compare the resulting usages and Report IDs with the independent WinHidDump collection evidence in [G815 WinHidDump collections](G815_WINHIDDUMP_COLLECTIONS.md). Treat differences as something to investigate, not something to normalize by editing the captured bytes.

The Windows collection-level descriptors are not substitutes for these interface-level byte streams. Do not concatenate them to construct either descriptor.

## 5. Implementation gate

Only after both raw byte arrays pass the checks above should the CH552 composite USB descriptor implementation begin. The implementation must use the exact captured arrays and preserve the two interfaces and endpoint properties recorded in [CH552 G815 USB profile](../CH552_G815_USB_PROFILE.md).

This capture step does not establish G HUB compatibility. Vendor control requests, interrupt report routing, endpoint behavior, memory use and enumeration must be validated separately.
