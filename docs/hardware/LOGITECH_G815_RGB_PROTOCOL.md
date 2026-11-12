# Logitech G815 RGB and Profile Control – Capture Findings

Capture date: 2026-11-12  
Device: Logitech G815 RGB Mechanical Gaming Keyboard, VID:PID `046D:C33F`  
USBPcap device address in the analyzed capture: `21` (capture-local; not a persistent device identifier)  
Source: User-supplied USBPcap captures and controlled G HUB actions.

## 1. Scope and confidence

This document records observed G815 control traffic and the user's exact test sequence. It distinguishes confirmed byte fields from report semantics that are not yet decoded.

The transfers described here are HID class control transfers on interface 1. They are not interrupt OUT endpoint transfers.

Do not treat this document as a complete Logitech protocol specification. Report variants, parameters and state dependencies that have not been independently established remain explicitly unresolved.

## 2. Control-transfer framing

A representative transfer (Wireshark frame 5181) contains:

| Field | Captured value | Interpretation |
|---|---:|---|
| USB device address | `21` | Address assigned during this capture only |
| Endpoint | `0x00` OUT | Default control endpoint (EP0) |
| URB function | `URB_FUNCTION_CLASS_INTERFACE` | Class/interface control request |
| `bmRequestType` | `0x21` | Host-to-device, Class, Interface |
| `bRequest` | `0x09` | HID `SET_REPORT` |
| `wValue` | `0x0211` | Output report type (`0x02`), Report ID `0x11` |
| `wIndex` | `1` | HID interface 1 |
| `wLength` | `20` | 20-byte report payload |

The captured Data Fragment begins with `11`, matching Report ID `0x11`. The observed report is therefore a 20-byte HID Output report sent using `SET_REPORT` on EP0.

The URB submission and its matching completion frame are two capture records for one control transfer, not two independent application commands.

## 3. Confirmed RGB channel fields

The following 20-byte report was captured while changing a key-zone color:

```text
11 FF 10 6E FF 0A 00 01 04 13 17 00 00 00 00 00 00 00 00 00
```

The corresponding report after changing the color to RGB(4,4,4) was:

```text
11 FF 10 6E 04 04 04 01 04 13 17 00 00 00 00 00 00 00 00 00
```

A separate single-channel test changed the red component to 100 while green and blue remained 4:

```text
11 FF 10 6E 64 04 04 01 04 13 17 00 00 00 00 00 00 00 00 00
```

The user confirmed that the RGB channels change independently in 8-bit steps.

For this observed `11 FF 10 6E` report variant:

| Report offset | Meaning | Confidence |
|---:|---|---|
| 0 | Report ID `0x11` | Confirmed |
| 1 | `0xFF` in captured examples | Observed constant |
| 2 | `0x10` in this report variant | Observed discriminator |
| 3 | `0x6E` in this report variant | Observed discriminator |
| 4 | Red, 8-bit | Confirmed |
| 5 | Green, 8-bit | Confirmed |
| 6 | Blue, 8-bit | Confirmed |
| 7–19 | Other parameters / key-selection data | Not fully decoded |

The first example is approximately red, but its captured value is RGB(255,10,0), not RGB(255,0,0). Do not normalize or rewrite these values when forwarding the original report.

**Implementation constraint:** RGB offsets 4–6 are confirmed only for the observed `11 FF 10 6E` variant. They must not be applied to every report with Report ID `0x11`, or to every report whose payload happens to be 20 bytes.

## 4. Global default color and individually configurable keys

The user confirmed the following behavior in G HUB:

- The default/global color RGB(0,255,255) applies to all keys.
- The following keys can be assigned custom colors independently:
  - W, S, A, D
  - E, R, F
  - Ctrl, Alt, Shift, Space
  - G1–G5

This means the lighting model is not simply one RGB triplet for the entire keyboard. It has a global/default color plus individually configurable keys.

A captured report during a full-keyboard green action was:

```text
11 FF 0F 1E 00 01 00 FF 00 02 00 00 00 00 00 01 00 00 00 00
```

The complete meaning of this `0F 1E` payload is not decoded. The global color behavior is based on the user's controlled G HUB observation; do not infer that bytes 4–6 in this different report have the same RGB meaning as offsets 4–6 in the `10 6E` variant.

## 5. Capture sequence and correct action classification

The frame groups in the latest capture must be interpreted according to the user's action notes:

| Frame range | User action / observation | Correct interpretation |
|---|---|---|
| 2xxx | Profile change is included among the captured operations | Contains a profile change and other configuration traffic |
| 55xx | RGB switched off | RGB-off traffic only; **not** a profile change |
| 6xxx | Selected keys addressed, including W, S, A, D, E, R, F, Ctrl, Alt, Shift and Space | Key-specific configuration/addressing traffic; the report group is similar to the 2xxx group |
| 8xxx | Switch back to Desktop/default profile | Profile switch to the default Desktop profile |

The 55xx group must not be described as a profile switch. Likewise, the 6xxx group must not be reduced to a simple color triplet update: it includes data that addresses individual keys.

The exact meaning and ordering of all report subtypes in these groups is still under analysis. Frame ranges are capture-specific and must not be used as protocol identifiers.

## 6. Report variants observed so far

The following prefixes have appeared in the supplied reports:

| Report bytes (after Report ID) | Observation | Semantic status |
|---|---|---|
| `0F 1E` | Appears in full-keyboard color/configuration traffic and profile/configuration sequences | Exact role and payload layout unresolved |
| `0F 5E` | Appeared in RGB-related traffic | Exact role unresolved |
| `10 5E` | Appeared during G-key and other zone-related lighting activity | Exact payload layout and key-selection semantics unresolved |
| `10 6E` | Used for confirmed RGB channel data and also appears in key-addressing traffic | This prefix alone is insufficient to identify a simple RGB-only payload |
| `10 7E` | Appeared adjacent to other lighting reports | Role unresolved |
| `08 1E`, `08 3E` | Appeared in profile/configuration sequences | Exact role and payload layout unresolved |

These are observed report discriminators, not a complete command list. Do not assign a definitive meaning to a prefix solely because it appears during one user action.

## 7. Consequences for gateway implementation

1. Preserve the complete original 20-byte report and its transfer context.
2. Identify the device, interface, HID report type and Report ID before interpreting payload bytes.
3. Do not modify reports solely because bytes 2–3 equal `10 6E`; the same observed variant family also occurs in key-addressing traffic.
4. Only modify offsets 4–6 when the report has been positively classified as the RGB-value form.
5. Preserve all key-addressing bytes, unknown fields, padding and ordering.
6. Forward profile changes and RGB-off operations unchanged unless a separately specified gateway feature requires otherwise.
7. Keep global/default color state separate from per-key overrides.
8. Unknown or unclassified report variants must pass through unchanged.

A future parser should separate:
- USB/HID transfer framing;
- G815 report classification;
- global/default color state;
- per-key color/address data;
- profile/configuration state.

Do not implement a Logitech G HUB emulator as part of the generic HID parser.

## 8. Evidence still required before protocol implementation

The following details remain unresolved and should only be added after a controlled capture establishes them:

- Exact byte layout and semantics of the global/default color report.
- Exact encoding of key identifiers and key groups.
- Whether the key-address list is a HID Usage ID list, Logitech-specific key codes, or a mixed encoding.
- Exact roles of `0F 5E`, `10 5E`, `10 7E`, `08 1E` and `08 3E`.
- Whether report ordering or prior profile state changes the interpretation of subsequent reports.
- Whether the same report forms and behavior apply to other G815 firmware/G HUB versions.

## 9. Provenance

Findings are based on user-supplied USBPcap captures `usb_pcap_v2.pcapng`, `usb_pcap_v5.pcapng` and `usb_pcap_v6.pcapng`, Wireshark packet details supplied in the conversation, and controlled G HUB actions described by the user on 2026-11-12.

The captures are retained as conversation evidence; this repository document contains the interpreted findings and selected payload examples, not the original PCAPNG files.
