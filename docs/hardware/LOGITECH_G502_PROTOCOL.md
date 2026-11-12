# Logitech G502 HERO – USB Control Protocol Findings

Capture date: 2026-11-12  
Device: Logitech G502 HERO, VID:PID `046D:C08B`  
USBPcap device address in the analyzed capture: `20` (capture-local; not a persistent device identifier)  
Source: User-supplied USBPcap setup packets and controlled G HUB actions.

## 1. Scope and confidence

This document records observed G502 control traffic for DPI configuration and lighting. It is based on controlled changes made in G HUB and the corresponding USBPcap captures.

The documented transfers are HID class control requests sent through endpoint 0 (EP0), targeting HID interface 1. They are not interrupt OUT endpoint transfers.

Only byte fields supported by controlled comparisons are assigned a semantic meaning. Other bytes remain explicitly unresolved. This is not a complete Logitech protocol specification, and the findings do not establish complete G HUB compatibility.

## 2. Common control-transfer framing

The captured Output reports use:

| Field | Captured value | Interpretation |
|---|---:|---|
| `bmRequestType` | `0x21` | Host-to-device, Class, Interface |
| `bRequest` | `0x09` | HID `SET_REPORT` |
| `wIndex` | `1` | HID interface 1 |
| `wValue` high byte | `0x02` | HID Output report type |
| `wValue` low byte | `0x10` or `0x11` | Report ID |
| Endpoint | `0x00` | Default control endpoint (EP0) |

Two report sizes have been observed:

- Report ID `0x10`: 7-byte payload.
- Report ID `0x11`: 20-byte payload.

The first payload byte repeats the Report ID. The USB device address `20` belongs only to this capture and must not be treated as a stable identifier.

## 3. DPI configuration

### 3.1 DPI value

The DPI value is transmitted in a 7-byte Report ID `0x10` payload. The controlled samples establish a 16-bit big-endian value in offsets 5–6 (zero-based offsets within the payload):

| Selected DPI | Captured payload | Bytes | Decoded value |
|---:|---|---|---:|
| 1200 | `10 FF 0A 3E 00 04 B0` | `04 B0` | 1200 |
| 1600 | `10 FF 0A 3E 00 06 40` | `06 40` | 1600 |
| 2400 | `10 FF 0A 3E 00 09 60` | `09 60` | 2400 |
| 3200 | `10 FF 0A 3E 00 0C 80` | `0C 80` | 3200 |

The observed command discriminator is `0A 3E`. The final two bytes are the DPI value in big-endian order.

### 3.2 Profile selection

A 20-byte Report ID `0x11` payload was observed during profile/DPI configuration:

```text
11 FF 05 5E 00 00 02 00 PP 00 00 00 00 00 00 00 00 00 00 00
```

The byte at offset 8 (`PP`) matches the selected profile in the supplied tests:

| DPI setting captured | Profile byte |
|---|---:|
| 1200 DPI | `01` |
| 1600 DPI | `02` |
| 2400 DPI | `02` |
| 3200 DPI | `03` |

This supports offset 8 as a profile identifier. The actual DPI value is separately transmitted by the `10 FF 0A 3E` report. The profile report's remaining fields are not decoded.

The user cannot assign the same DPI value to two profiles in the tested G HUB configuration; no assumption about duplicate DPI stages should be made.

### 3.3 Other observed DPI-sequence reports

The following reports appeared around DPI/profile changes:

- `10 FF 0B 2E 01 00 00`
- `10 FF 05 6E 00 00 00`

Their exact functions are unknown. Preserve them unchanged and in their observed order.

A report with prefix `10 FF 02 DE` was also seen in some captures. The user clarified that the `02 3E` followed by `02 DE` sequence occurs for lighting effects, not fixed-color lighting. It must therefore not be classified as a generic DPI completion, profile checksum or DPI command.

## 4. Fixed-color lighting

All lighting reports in this section are 20-byte Output reports using Report ID `0x11`, sent through HID `SET_REPORT` on interface 1.

### 4.1 Primary lighting zone

The primary lighting zone uses offset 4 = `00`.

| Action | Captured payload |
|---|---|
| Fixed cyan | `11 FF 02 3E 00 01 00 FF FF 02 00 00 00 00 00 00 00 00 00 00` |
| Fixed red | `11 FF 02 3E 00 01 FF 00 00 02 00 00 00 00 00 00 00 00 00 00` |
| Off | `11 FF 02 3E 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` |

### 4.2 Logo lighting zone

The logo zone uses offset 4 = `01`.

| Action | Captured payload |
|---|---|
| Logo cyan | `11 FF 02 3E 01 01 00 FF FF 02 00 00 00 00 00 00 00 00 00 00` |
| Logo red | `11 FF 02 3E 01 01 FF 00 00 02 00 00 00 00 00 00 00 00 00 00` |
| Logo off | `11 FF 02 3E 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` |

### 4.3 Confirmed fields for fixed-color reports

| Offset | Meaning | Confidence |
|---:|---|---|
| 0 | Report ID `0x11` | Confirmed |
| 1–3 | `FF 02 3E` in the captured report family | Observed discriminator |
| 4 | Zone: `00` primary, `01` logo | Confirmed by isolated zone changes |
| 5 | `00` off, `01` fixed color on | Confirmed for tested fixed-color operations |
| 6 | Red, 8-bit | Confirmed |
| 7 | Green, 8-bit | Confirmed |
| 8 | Blue, 8-bit | Confirmed |
| 9 | `02` in tested fixed-color-on reports | Meaning unknown |
| 10–19 | Other parameters / reserved fields | Not decoded |

The user confirmed that the fixed-color report changes only the selected zone. A fixed-color change is sent as one observed control transfer; it is not repeatedly transmitted like the G815 keyboard RGB traffic.

Do not interpret offset 5 as a universal Boolean. Effect reports use other values, including `02` and `03`.

## 5. Lighting effects

### 5.1 Effect mode report

Effect configuration uses a 20-byte Report ID `0x11` payload with the observed prefix `11 FF 02 3E`. The report is followed by a separate 7-byte Report ID `0x10` payload with prefix `10 FF 02 DE`.

Two controlled effect configurations have been captured:

**Effect sample A**

```text
11 FF 02 3E 00 03 00 00 00 00 13 88 64 00 00 00 00 00 00 00
10 FF 02 DE 00 F8 9E
```

**Effect sample B**

```text
11 FF 02 3E 00 02 00 FF FF 13 88 00 64 00 00 00 00 00 00 00
10 FF 02 DE 00 00 9A
```

The reports show that effect selection/configuration is not represented by a single byte. Both the 20-byte `0x11` report and the subsequent 7-byte `0x10` report must be retained as part of the observed operation.

### 5.2 Frequency parameter

Controlled frequency changes for the second effect sample established that offsets 9–10 of the 20-byte Report ID `0x11` payload contain a 16-bit big-endian frequency value in milliseconds.

| G HUB frequency | Captured bytes at offsets 9–10 | Hex value | Decimal |
|---:|---|---:|---:|
| 5000 ms | `13 88` | `0x1388` | 5000 |
| 13700 ms | `35 84` | `0x3584` | 13700 |
| 6500 ms | `19 64` | `0x1964` | 6500 |

The corresponding report payloads were:

- 5000 ms: `11 FF 02 3E 00 02 00 FF FF 13 88 00 64 00 00 00 00 00 00 00`
- 13700 ms: `11 FF 02 3E 00 02 00 FF FF 35 84 00 64 00 00 00 00 00 00 00`
- 6500 ms: `11 FF 02 3E 00 02 00 FF FF 19 64 00 64 00 00 00 00 00 00 00`

This is a confirmed field for the tested effect configuration. It is not yet proven that every G502 lighting effect uses the same offset, width or unit.

### 5.3 Additional effect report

The following 7-byte report changes between the captured effect configurations:

- Sample A: `10 FF 02 DE 00 F8 9E`
- Sample B: `10 FF 02 DE 00 00 9A`
- After changing the frequency to 6500 ms: `10 FF 02 DE 00 02 23`

Its exact semantics are unknown. The changing bytes must not be described as a checksum, frequency value or effect identifier without further controlled tests.

### 5.4 Effect fields – current confidence

| Offset / field | Observation | Status |
|---|---|---|
| Report ID `0x11`, offsets 1–3 | `FF 02 3E` | Observed report family |
| Offset 4 | `00` in supplied effect captures | Primary zone in these captures |
| Offset 5 | `02` or `03` across effect samples | Effect-related mode/selector; exact mapping unresolved |
| Offsets 6–8 | Vary with effect configuration | Meaning unresolved |
| Offsets 9–10 | Big-endian milliseconds for the tested effect | Confirmed for tested configuration |
| Offset 12 | `64` in supplied frequency samples | Meaning unresolved |
| Report ID `0x10`, prefix `02 DE` | Follows the 20-byte effect report | Meaning unresolved |

Do not apply the fixed-color interpretation of offset 5 to effect reports. In particular, `02` and `03` are observed effect-related values, not evidence of simple on/off states.

## 6. Implementation constraints

1. Match the physical device, interface, HID report type and Report ID before decoding a payload.
2. Preserve complete original payloads, including all unknown bytes.
3. Preserve the order of related control transfers.
4. Keep DPI configuration, profile selection, fixed-color lighting and effect configuration as separate command classes.
5. For DPI, decode the last two bytes of the observed `10 FF 0A 3E` 7-byte payload as a big-endian 16-bit value.
6. For the observed `11 FF 02 3E` fixed-color reports, only use the confirmed zone and RGB fields after positively classifying the report as fixed-color.
7. For the tested effect configuration, offsets 9–10 are a big-endian millisecond value. Do not generalize this layout to other effects without validation.
8. Treat the following `10 FF 02 DE` report as part of the observed effect transaction, but do not assign semantics to its trailing bytes yet.
9. Unknown or unclassified reports must pass through unchanged.
10. Do not assume that observed G HUB traffic is the complete set of reports required for full G HUB compatibility.

## 7. Evidence still required

- Identify the meaning of `10 FF 0B 2E` and `10 FF 05 6E` in the DPI sequence.
- Identify all fields in the `11 FF 05 5E` profile report.
- Determine the exact meaning of effect mode values `02` and `03`.
- Change one effect parameter at a time to decode offsets 6–8 and 11–19.
- Decode the trailing bytes of `10 FF 02 DE` without assuming they are a checksum.
- Test whether the frequency field at offsets 9–10 is shared by other effect types.
- Capture additional G502 button, wheel, report and G HUB control traffic as required for the gateway.

## 8. Provenance

Findings are based on user-supplied USBPcap setup-packet details and controlled G HUB actions described on 2026-11-12. The USB device address `20` is capture-local. This document records interpreted findings and selected payload examples; it is not a copy of the original PCAPNG capture.
