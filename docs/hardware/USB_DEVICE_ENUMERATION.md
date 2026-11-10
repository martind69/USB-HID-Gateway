# USB Device Enumeration – Logitech G502 HERO and G815

Capture date: 2026-11-09 (Windows "Last Arrival Date").  
Source: Windows USB device-tree and HID capability reports supplied for this project.

## 1. Scope and limitations

The captures identify two Logitech devices:

- G502 HERO Gaming Mouse, VID:PID `046D:C08B`, revision `0x2703`.
- G815 RGB Mechanical Gaming Keyboard, VID:PID `046D:C33F`, revision `0x3102`.

The reports contain USB device/configuration/interface/endpoint information and Windows HID capability information. They do **not** contain the actual HID report descriptor bytes: Windows reports descriptor lengths, but the descriptor read failed with `ERROR_INVALID_PARAMETER`.

They also contain no actual HID report payloads or USB control-transfer traces. Therefore, the report semantics and G HUB protocol cannot be fully reconstructed from these files alone. The facts below are observations from the supplied captures; interpretations and remaining work are identified separately.

## 2. Physical topology actually captured

| Device | Port chain | Companion port chain |
|---|---|---|
| G502 HERO | `2-7-3` | `2-23-3` |
| G815 | `2-7-4` | `2-23-4` |

Both devices are sibling ports below the same parent chain (`2-7`). This capture therefore shows the G502 and G815 connected independently to ports 3 and 4 of the same upstream hub. It does **not** show the G502 connected behind the USB hub integrated into the G815.

**Required follow-up:** connect the G502 to the G815's integrated hub and capture the full USB tree again. The G502's port chain should then identify the G815/hub as an intermediate parent.

The project previously named a G810 as the target keyboard. The supplied capture identifies a G815. Whether the G815 replaces the G810 as the target remains an open project decision.

## 3. Common USB properties

| Property | G502 HERO | G815 |
|---|---:|---:|
| VID:PID | `046D:C08B` | `046D:C33F` |
| Device revision | `2703` | `3102` |
| `bcdUSB` | `0x0200` | `0x0200` |
| Actual connection speed | Full-Speed (12 Mbit/s) | Full-Speed (12 Mbit/s) |
| Configurations | 1 | 1 |
| HID interfaces | 2 | 2 |
| Interrupt IN endpoints | `0x81`, `0x82` | `0x81`, `0x82` |
| Polling interval | 1 ms | 1 ms |
| Bus-powered maximum current | 300 mA | 500 mA |
| Configuration descriptor length | 59 bytes | 59 bytes |

Although `bcdUSB` is 2.00, both devices operated at Full-Speed in these captures. The companion-port entries refer to paired host-controller ports and do not indicate that the devices themselves are operating at SuperSpeed.

Both configuration descriptors report `bmAttributes = 0xA0` (bus-powered, remote-wakeup capable). The G502 reports `bMaxPower = 300 mA`; the G815 reports `500 mA`.

## 4. Logitech G502 HERO

### 4.1 USB interfaces

| Interface | Subclass | Protocol | HID report descriptor length | Interrupt IN endpoint | Max packet | Interval |
|---|---:|---:|---:|---|---:|---:|
| 0 | 1 (Boot) | 2 (Mouse) | 67 bytes | `0x81` | 8 bytes | 1 ms |
| 1 | 0 | 0 | 151 bytes | `0x82` | 20 bytes | 1 ms |

The configuration descriptor lists no interrupt OUT endpoint. Windows nevertheless reports Output reports for the vendor-defined HID collections. Their actual transfer path must be captured; do not assume an interrupt OUT endpoint.

### 4.2 Windows HID collections

| Usage page | Usage | Input length | Output length | Feature length |
|---|---|---:|---:|---:|
| Generic Desktop `0x01` | Mouse `0x02` | 9 bytes | 0 | 0 |
| Generic Desktop `0x01` | Keyboard `0x06` | 9 bytes | 0 | 0 |
| Consumer `0x0C` | Consumer Control `0x01` | 5 bytes | 0 | 0 |
| Generic Desktop `0x01` | System Control `0x80` | 2 bytes | 0 | 0 |
| Vendor `0xFF00` | Usage `0x0001` | 7 bytes | 7 bytes | 0 |
| Vendor `0xFF00` | Usage `0x0002` | 20 bytes | 20 bytes | 0 |

The mouse collection reports:

- Buttons: Usage Page `0x09`, Button 1 through Button 16.
- Y: Generic Desktop Usage `0x31`, signed relative 16-bit value, logical range -32767…+32767.
- X: Generic Desktop Usage `0x30`, signed relative 16-bit value, logical range -32767…+32767.
- Wheel: Usage `0x38`, signed relative 8-bit value, logical range -127…+127.
- AC Pan: Consumer Usage `0x0238`, signed relative 8-bit value, logical range -127…+127.

This is not a simple 3-button mouse with 8-bit X/Y fields. The proxy must parse the descriptor and must not hard-code byte offsets or movement widths.

Windows also exposes keyboard, consumer and system-control collections under the G502 device. Their exact relationship to the physical interface descriptors cannot be established without the raw report descriptor bytes. Do not assume that each Windows HID collection corresponds to a separate physical USB interface.

### 4.3 Vendor-defined collections

Two vendor-defined report collections are listed:

- Usage Page `0xFF00`, Usage `0x0001`: 7-byte Input and 7-byte Output.
- Usage Page `0xFF00`, Usage `0x0002`: 20-byte Input and 20-byte Output.

Their meaning and G HUB use are unknown. Report payloads, Report IDs, control requests and timing must be captured before implementing protocol-specific behavior.

The Windows tree also lists Logitech filter/child components (including `logi_lamparray`) and a Microsoft HID VHF virtual device. These are Windows driver-stack objects, not additional physical interfaces in the device's 59-byte USB configuration descriptor.

## 5. Logitech G815 keyboard

### 5.1 USB interfaces

| Interface | Subclass | Protocol | HID report descriptor length | Interrupt IN endpoint | Max packet | Interval |
|---|---:|---:|---:|---|---:|---:|
| 0 | 1 (Boot) | 1 (Keyboard) | 65 bytes | `0x81` | 8 bytes | 1 ms |
| 1 | 0 | 0 | 188 bytes | `0x82` | 64 bytes | 1 ms |

No interrupt OUT endpoint is listed. Windows reports Output reports for the keyboard and vendor-defined collections; the actual transfer path (for example, HID class control `SET_REPORT`) remains to be verified.

### 5.2 Windows HID collections

| Usage page | Usage | Input length | Output length | Feature length |
|---|---|---:|---:|---:|
| Generic Desktop `0x01` | Keyboard `0x06` | 9 bytes | 2 bytes | 0 |
| Generic Desktop `0x01` | Keyboard `0x06` | 21 bytes | 0 | 0 |
| Generic Desktop `0x01` | Mouse `0x02` | 9 bytes | 0 | 0 |
| Consumer `0x0C` | Consumer Control `0x01` | 2 bytes | 0 | 0 |
| Vendor `0xFF43` | Usage `0x0602` | 20 bytes | 20 bytes | 0 |
| Vendor `0xFF43` | Usage `0x0604` | 64 bytes | 64 bytes | 0 |

The boot keyboard collection contains modifier usages `0xE0`–`0xE7` and an array-style key field. Windows also exposes a second keyboard collection with a 21-byte input report length.

The mouse collection exposed by Windows contains:

- Buttons 1 through 16.
- Y: Generic Desktop Usage `0x31`, signed relative 16-bit value, logical range -32767…+32767.
- X: Generic Desktop Usage `0x30`, signed relative 16-bit value, logical range -32767…+32767.
- Wheel: Usage `0x38`, signed relative 8-bit value, logical range -127…+127.
- AC Pan: Consumer Usage `0x0238`, signed relative 8-bit value, logical range -127…+127.

The existence of this Windows mouse collection does not prove that the G815 has a physical mouse endpoint or an integrated mouse. Its origin must be confirmed from the raw HID report descriptor.

The Consumer Control collection lists seven controls: Mute, Volume Decrement, Volume Increment, Play/Pause, Stop, Scan Previous Track and Scan Next Track.

### 5.3 Vendor-defined collections

Two vendor-defined collections are listed:

- Usage Page `0xFF43`, Usage `0x0602`: 20-byte Input and 20-byte Output.
- Usage Page `0xFF43`, Usage `0x0604`: 64-byte Input and 64-byte Output.

Their protocol semantics and G HUB usage are not identified by the capability report. The Windows tree also lists Logitech filter/child components and a Microsoft HID VHF virtual device; these must not be confused with physical USB interfaces.

## 6. Interpretation notes for implementation

### 6.1 Report lengths are not endpoint packet sizes

For example, Windows reports a 9-byte mouse Input report while the corresponding interrupt endpoint has an 8-byte maximum packet size. Windows HID capability lengths may include the Report ID byte slot even when the report uses no Report ID. They are not a direct measurement of USB transaction payload length.

Keep these concepts separate:

1. HID report payload length and Report ID handling.
2. USB endpoint maximum packet size.
3. Actual transferred packet length.
4. Windows HID collection buffer length.

The actual descriptor bytes and bus transactions are needed to resolve the exact layouts and lengths.

### 6.2 Output reports and endpoint direction

Both devices list only interrupt IN endpoints in their configuration descriptors. Windows HID capabilities still report Output reports, including vendor-defined reports. Determine whether these are sent with HID class control `SET_REPORT` or another mechanism. The current captures do not establish the transfer path.

### 6.3 Design consequences

The current target devices demonstrate why the gateway must support:

- Multiple HID interfaces per physical device.
- Boot and non-boot HID interfaces.
- Interrupt IN packet sizes of 8, 20 and 64 bytes.
- Multiple top-level collections.
- Vendor-defined Usage Pages.
- Input and Output reports.
- Signed relative X/Y fields of 16 bits.
- Signed wheel and pan fields of 8 bits.
- Unknown fields that must remain intact during selective modification.

The G502 hotkey-mode modification must replace only the parsed X/Y fields. Buttons, wheel, AC Pan, vendor-defined fields and reserved bits must be preserved. Keyboard reports must continue to be forwarded.

## 7. Required follow-up captures

- [ ] Capture the G815 with the G502 connected through the G815 integrated hub; verify the port chain.
- [ ] Confirm whether G815 replaces G810 as the project target.
- [ ] Retrieve raw HID report descriptor bytes for all physical HID interfaces.
- [ ] Capture actual mouse Input reports while testing buttons, X/Y, wheel and pan.
- [ ] Capture keyboard typing, modifiers, media keys and secondary keyboard reports.
- [ ] Capture Output/Feature reports and USB control-transfer setup packets.
- [ ] Capture G HUB traffic and identify the required vendor-defined reports and control requests.
- [ ] Repeat the tests through the gateway after USB Host/Device transport is implemented.

## 8. Provenance

This document is derived from the two Windows USB device-tree/HID capability exports supplied on 2026-11-10. The original exports remain in the conversation attachments; this repository document is a structured analysis, not a byte-for-byte copy of those exports.
