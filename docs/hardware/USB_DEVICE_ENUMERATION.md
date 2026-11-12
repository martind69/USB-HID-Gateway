# USB Device Enumeration – Logitech G502 HERO and G815

Capture date: 2026-11-09 (Windows "Last Arrival Date").  
Source: Windows USB device-tree and HID capability reports supplied for this project.

## 1. Scope and limitations

The complete dump identifies the monitor's Genesys Logic hub, an ASUS ROG Gaming Display Aura HID function, and two Logitech devices:

- Genesys Logic USB2.1 Hub, VID:PID `05E3:0610`, four downstream ports.
- ASUS ROG Gaming Display Aura Device, VID:PID `0B05:18DA`.
- G502 HERO Gaming Mouse, VID:PID `046D:C08B`, revision `0x2703`.
- G815 RGB Mechanical Gaming Keyboard, VID:PID `046D:C33F`, revision `0x3102`.

The dump contains USB device/configuration/interface/endpoint information and Windows HID capability information. It does **not** contain the actual HID report descriptor bytes: Windows reports descriptor lengths, but the descriptor reads failed (`ERROR_INVALID_PARAMETER` for the Logitech interfaces and `ERROR_GEN_FAILURE` for the ASUS Aura interface).

It also contains no actual HID report payloads or USB control-transfer traces. Therefore, the report semantics and G HUB protocol cannot be fully reconstructed from this dump alone. The facts below are observations from the supplied capture; interpretations and remaining work are identified separately.

## 2. Physical topology captured

The full Windows USB tree identifies the hub in the ASUS ROG monitor:

| Hub property | Captured value |
|---|---|
| Manufacturer | Genesys Logic |
| VID:PID | `05E3:0610` |
| Product | USB2.1 Hub |
| USB revision | `0x0210` |
| Hub port chain | `2-7` |
| Downstream ports | 4 |
| Operating speed | High-Speed (480 Mbit/s) |
| Power | Self-powered |
| Transaction translator | Multi-TT |
| Companion SuperSpeed hub | VID:PID `05E3:0620`, chain `2-23` |

The four downstream ports are reported as follows:

| Monitor hub port | Device | VID:PID | Port chain |
|---|---|---|---|
| 1 | ASUS ROG Gaming Display Aura Device | `0B05:18DA` | `2-7-1` |
| 2 | No device connected | – | `2-7-2` |
| 3 | Logitech G502 HERO | `046D:C08B` | `2-7-3` |
| 4 | Logitech G815 | `046D:C33F` | `2-7-4` |

The G502 and G815 are sibling devices below the monitor hub. This is consistent with the user's physical clarification: the G502 is connected through the G815's USB pass-through connection, while the G815 itself is connected to another monitor hub port. The G815 pass-through is a direct cable path, not a USB hub, so it does not create an additional hub tier in the USB tree.

The dump alone shows the logical USB topology, not the physical cable route. The pass-through route is established by the user's clarification.

The monitor's ASUS Aura function is a separate HID device on port 1. It must not be mistaken for a Logitech interface or part of the keyboard/mouse composite HID device.

The project documentation previously named a G810. This capture identifies the actual keyboard as a G815; the G815 should be treated as the current observed test device. Any remaining G810-specific requirement must be confirmed separately.

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

## 4. ASUS monitor hub HID function

The monitor also exposes an ASUS ROG Gaming Display Aura HID device on downstream hub port 1:

| Property | Captured value |
|---|---|
| VID:PID | `0B05:18DA` |
| Product | ROG Gaming Display Aura Device |
| USB revision | `0x0110` |
| Actual speed | Full-Speed (12 Mbit/s) |
| Configuration | 1 |
| HID interfaces | 1 |
| HID report descriptor length | 53 bytes |
| Interrupt IN endpoint | `0x82`, 64-byte maximum packet, 1 ms |
| Interrupt OUT endpoint | `0x03`, 64-byte maximum packet, 1 ms |
| Maximum current | 32 mA |

Windows reports a vendor-defined HID top-level collection (Usage Page `0xFFA0`, Usage `0x0001`). Its capability report lists Input and Output report buffer lengths of 513 bytes and a Feature report buffer length of 8 bytes. The detailed capabilities identify Report ID 5 for a 512-byte Input payload, Report ID 4 for a 512-byte Output payload and Report ID 3 for a 7-byte Feature payload.

The actual report descriptor bytes could not be read (`ERROR_GEN_FAILURE`). The report semantics and purpose are therefore unknown. Do not infer that this function controls the monitor's lighting solely from the product name, and do not route its reports as keyboard or mouse input.

The 513-byte Windows HID buffer length is not the endpoint packet size. The endpoint maximum packet size remains 64 bytes; actual report transfer behavior must be captured separately.

## 5. Logitech G502 HERO

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

## 6. Logitech G815 keyboard

### 5.1 USB interfaces

| Interface | Subclass | Protocol | HID report descriptor length | Interrupt IN endpoint | Max packet | Interval |
|---|---:|---:|---:|---|---:|---:|
| 0 | 1 (Boot) | 1 (Keyboard) | 65 bytes | `0x81` | 8 bytes | 1 ms |
| 1 | 0 | 0 | 188 bytes | `0x82` | 64 bytes | 1 ms |

No interrupt OUT endpoint is listed. A separate USBPcap capture now confirms that at least the observed G815 RGB Output reports are sent using HID class control `SET_REPORT` on interface 1 through EP0. See [G815 RGB and profile control findings](LOGITECH_G815_RGB_PROTOCOL.md).

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

### 5.4 Captured G815 RGB control traffic\n\nA USBPcap capture supplied on 2026-11-12 confirms G815 RGB Output reports sent as HID class `SET_REPORT` control transfers on interface 1 / EP0. The observed request uses Report ID `0x11` and a 20-byte payload. Independent RGB channel tests confirm 8-bit Red, Green and Blue values at offsets 4–6 for the positively identified `11 FF 10 6E` RGB report variant. The G815 also supports a global/default color and per-key custom colors; the tested custom-key set is W, S, A, D, E, R, F, Ctrl, Alt, Shift, Space and G1–G5.\n\nProfile changes, RGB-off operations and per-key addressing must be treated as distinct operations. The exact layouts of all report variants remain partially unresolved. See [G815 RGB and profile control findings](LOGITECH_G815_RGB_PROTOCOL.md) for payload examples, capture sequence and implementation constraints.\n\n## 7. Interpretation notes for implementation

### 6.1 Report lengths are not endpoint packet sizes

For example, Windows reports a 9-byte mouse Input report while the corresponding interrupt endpoint has an 8-byte maximum packet size. Windows HID capability lengths may include the Report ID byte slot even when the report uses no Report ID. They are not a direct measurement of USB transaction payload length.

Keep these concepts separate:

1. HID report payload length and Report ID handling.
2. USB endpoint maximum packet size.
3. Actual transferred packet length.
4. Windows HID collection buffer length.

The actual descriptor bytes and bus transactions are needed to resolve the exact layouts and lengths.

### 6.2 Output reports and endpoint direction

Both devices list only interrupt IN endpoints in their configuration descriptors. Windows HID capabilities still report Output reports, including vendor-defined reports. For the G815, a separate USBPcap capture confirms HID class control `SET_REPORT` on interface 1 / EP0 for the observed RGB reports. This does not establish the transfer path for every G815 Output/Feature report or for the G502.

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

## 8. Required follow-up captures

- [x] Capture the monitor hub, G815, G502 and ASUS Aura HID function in one complete USB tree.\n- [ ] Capture the same topology through the ESP32-P4 Host and verify multi-device hub enumeration.
- [ ] Confirm whether G815 replaces G810 as the project target.
- [ ] Retrieve raw HID report descriptor bytes for all physical HID interfaces.
- [ ] Capture actual mouse Input reports while testing buttons, X/Y, wheel and pan.
- [ ] Capture keyboard typing, modifiers, media keys and secondary keyboard reports.
- [ ] Capture Output/Feature reports and USB control-transfer setup packets.
- [ ] Capture G HUB traffic and identify the required vendor-defined reports and control requests.
- [ ] Repeat the tests through the gateway after USB Host/Device transport is implemented.

## 9. Provenance

This document is derived from the complete Windows USB device-tree/HID capability dump supplied on 2026-11-10. The original dump remains in the conversation attachment; this repository document is a structured analysis, not a byte-for-byte copy of the export.
