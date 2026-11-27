# Logitech G815 – Deep USB Trace Analysis

## Scope and method

This analysis isolates the physical G815 from the unfiltered capture and separates it from the gateway's host-facing proxy. USBPcap records were parsed by bus/device address, transfer type, endpoint, control setup fields, report IDs, payload lengths, and timestamps. Other devices in the unfiltered capture were excluded from the G815 functional analysis.

Captures:
- `g815_ghub_direct_connect.pcapng` — direct physical G815; bus 2/device 21.
- `usb.pcap` — earlier direct capture of the same physical G815; bus 2/device 17.
- `g815_test_passthrough.pcapng` — host-facing gateway proxy; bus 2/device 20.

## 1. Key conclusions

1. The physical keyboard is identified by VID `0x046D`, PID `0xC33F`, product string `G815 RGB MECHANICAL GAMING KEYBOARD`, serial `017E30453836`. It appears at bus 2/device 21 (`0x15`) in the new direct capture.
2. `usb.pcap` is the same physical G815: device/configuration and both HID report descriptors match byte-for-byte; its dynamic USB address was 17 (`0x11`).
3. Device 20 in the passthrough capture is **the gateway proxy**, not the physical keyboard. Its serial is `G815-PROXY-S3`. Address 20 is still the correct address for analyzing what the PC sees from the proxy.
4. The proxy's device descriptor and both HID report descriptors match the physical keyboard. The configuration descriptor differs only at `iConfiguration` (physical `4`, proxy `0`). The proxy's product string is truncated.
5. The physical G815 uses interface 1 Report ID `0x11` as a 20-byte vendor channel: host `SET_REPORT` via Control OUT; keyboard replies as 20-byte Input reports on Interrupt-IN endpoint `0x82`. The direct trace contains 410 output reports and 414 input reports.
6. In the proxy capture, the host requests `GET_REPORT` IDs `0x11` (20 bytes) and `0x12` (64 bytes), but the proxy returns only the one-byte report ID. This is a concrete short response relative to the descriptor. The physical keyboard's own response to a direct GET_REPORT request was not tested.
7. A previously overlooked `0x11 0x5A` sequence is a six-block read: six 256-byte blocks, 16-byte chunks, each block read twice. Every repeated chunk is identical. The earlier direct capture has the same 194 request/response sequence using opcode `0x5B` instead of `0x5A`; all request fields and returned data match exactly.
8. The older direct capture contains a sustained `0x10 0x5B` / `0x10 0x7B` stream absent from the new short direct trace. It is periodic and has three stable response triplets. The exact function and mapping to a user action/key/zone remain unconfirmed.

## 2. Capture overview

| Capture | Records | Duration | Time range | G815 address |
|---|---:|---:|---|---|
| `g815_ghub_direct_connect.pcapng` | 5,382 | 9.810780 s | 2026-11-22 16:18:04.518752 – 2026-11-22 16:18:14.329532 | bus 2/device 21 (`0x15`) |
| `usb.pcap` | 14,944 | 195.713669 s | 2026-11-21 11:40:45.275882 – 2026-11-21 11:44:00.989551 | bus 2/device 17 (`0x11`) |
| `g815_test_passthrough.pcapng` | 148 | 22.407822 s | 2026-11-22 16:00:28.369789 – 2026-11-22 16:00:50.777611 | bus 2/device 20 (`0x14`), proxy |

## 3. Device identity and descriptor comparison

### Physical G815
- VID: `046D` (Logitech)
- PID: `C33F`
- `bcdDevice`: `0x3102`
- Product: `G815 RGB MECHANICAL GAMING KEYBOARD`
- Serial: `017E30453836`
- New direct capture address: bus 2/device 21 (`0x15`)

### Gateway proxy
- VID/PID and `bcdDevice` spoofed to match the G815.
- Product string: `G815 RGB Mechanical Gaming Keyb` (64-byte string descriptor, truncated relative to the physical 72-byte descriptor).
- Serial string: `G815-PROXY-S3`.
- Address: bus 2/device 20 (`0x14`).

### Descriptor fidelity
- Device descriptor: byte-for-byte identical between physical and proxy.
- HID report descriptors, interface 0 and interface 1: byte-for-byte identical.
- Configuration descriptor: byte-for-byte identical except `iConfiguration` byte 6: physical `0x04`, proxy `0x00`.
- USB addresses are dynamic and are not permanent device identities.

## 4. G815 HID topology

The configuration descriptor is 59 bytes and declares two HID interfaces. Both interrupt endpoints are IN; no interrupt OUT endpoint is declared.

| Interface | Type | Report Descriptor | Endpoint | Max packet | Interval |
|---:|---|---:|---|---:|---:|
| 0 | Keyboard / boot interface | 65 bytes | `0x81` Interrupt IN | 8 bytes | 1 |
| 1 | Vendor-defined interface (usage page `0xFF43`) | 188 bytes | `0x82` Interrupt IN | 64 bytes | 1 |

**Important:** Vendor Output reports are sent through Control `SET_REPORT`, not an interrupt OUT endpoint.

### Interface 0 reports
| Report | Direction | Length | Descriptor meaning |
|---|---|---:|---|
| ID 0 (no Report-ID prefix) | Input | 8 bytes | Standard keyboard: modifier byte, reserved byte, six key slots |
| ID 0 | Output | 1 byte | Three LED bits (Num/Caps/Scroll Lock) + padding |

### Interface 1 reports
| Report ID | Direction | Payload | Total size | Descriptor usage |
|---:|---|---:|---:|---|
| `0x01` | Input | 20 bytes | 21 bytes | Keyboard usages, page `0x07`, range `0x00–0xE7` |
| `0x02` | Input | 8 bytes | 9 bytes | Buttons 1–16, two 16-bit Generic Desktop fields (usage `0x31`), 8-bit usage `0x38`, Consumer usage `0x0238` |
| `0x03` | Input | 1 byte | 2 bytes | Consumer usage `0xE2` (likely Mute; usage-name inference) |
| `0x11` | Input + Output | 19 bytes | 20 bytes | Vendor page `0xFF43`, usage `0x02` |
| `0x12` | Input + Output | 63 bytes | 64 bytes | Vendor page `0xFF43`, usage `0x04` |
| — | Feature | — | — | No Feature reports found |

This resolves the earlier ambiguity: Report ID `0x12` is a genuine HID Report ID on interface 1. A separate USB descriptor beginning `12 01` or `12 03` is not itself a HID Report ID.

### Report Descriptor bytes (identical on physical and proxy)
**Interface 0 (65 bytes):**
```hex
05010906a101050719e029e71500250175019508810295017508810195037501050819012903910295057501910195067508150026ff00050719002aff008100c0
```

**Interface 1 (188 bytes):**
```hex
05010906a1010507190029e7150026e7007508951485018100c005010902a10185020901a10005091901291015002501951075018102050116018026ff7f751095020930093181061581257f7508950109388106050c0a380295018106c0c0050c0901a1018503150025017501950709b509b609b709cd09e909ea09e2810295018101c00643ff0a0206a101851175089513150026ff000902810009029100c00643ff0a0406a10185127508953f150026ff000904810009049100c0
```

## 5. Direct physical G815 traffic

### Counts, isolated to bus 2/device 21
| Item | Observation |
|---|---|
| Total G815 records | 1,768 |
| Vendor `SET_REPORT` ID `0x11`, 20 bytes | 410 |
| Input Report ID `0x11`, endpoint `0x82`, 20 bytes | 414 |
| Endpoint `0x82` zero-length records | 429 |
| Other non-empty endpoint `0x82` reports | 5: IDs 1/2/3 |
| Endpoint `0x81` non-empty keyboard input | 1 report, 8 bytes, all zero |
| Direct `GET_REPORT` requests | 0 |
| Direct `SET_REPORT` ID `0x12` | 0 |
| Keyboard LED Output `SET_REPORT` (ID 0, interface 0) | 2, payload `01` |
| HID `SET_IDLE` | 4 total: interface 0 three times, interface 1 once |
| `SET_CONFIGURATION` | 1 |

### Vendor channel ID `0x11`
All 410 vendor Output reports use Control OUT:
```text
bmRequestType = 0x21
bRequest     = 0x09 (SET_REPORT)
wValue      = 0x0211 (Output report type 2, Report ID 0x11)
wIndex      = 1 (interface 1)
wLength     = 20
```

The keyboard returns 20-byte Input reports on Interrupt-IN endpoint `0x82`. Report ID `0x11` has 19 payload bytes plus the report-ID byte. The endpoint's maximum packet size is 64 bytes, but the observed ID `0x11` reports are 20 bytes as defined by the descriptor.

Family-based FIFO association yields 408 matched output/input pairs across the report command families. For these pairs the median delay is 2.8185 ms (minimum 2.278 ms, maximum 5.700 ms). This is a timing correlation, not proof of individual command causality. The 194 table-read pairs are an especially strong ordered match.

### Other non-empty endpoint `0x82` reports
| Packet index | Relative time | Report ID | Captured size | Descriptor size | Raw bytes |
|---:|---:|---:|---:|---:|---|
| 2872 | 6.7358 s | `0x02` | 9 | 9 | `02 00 00 00 00 00 00 00 00` |
| 2874 | 6.7368 s | `0x01` | 9 | 9 | `01 00 00 00 00 00 00 00 00` |
| 2876 | 6.7377 s | `0x03` | 5 | 2 | `03 00 00 00 00` |
| 2884 | 6.7418 s | `0x01` | 21 | 21 | `01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` |
| 2886 | 6.7427 s | `0x03` | 2 | 2 | `03 00` |

ID `0x02` is 9 bytes and matches. ID `0x01` appears once at 21 bytes (matches) and once at 9 bytes (short). ID `0x03` appears once at 2 bytes (matches) and once at 5 bytes (long). These short/long reports are anomalies and should be investigated rather than silently accepted. The captured payloads are all zero-filled.

## 6. SET_REPORT and GET_REPORT: physical device vs proxy

### Direct physical G815
- `SET_REPORT` ID `0x11`: 410 requests, 20-byte reports.
- Keyboard LED `SET_REPORT` ID 0: two 1-byte payloads `01` on interface 0.
- No HID `GET_REPORT` requests appear in the direct trace.
- No `SET_REPORT` or input data for Report ID `0x12` appears.

### Host-facing proxy
Two host `GET_REPORT` requests are present:

| Packet | Setup bytes | Requested report | Interface | Requested length | Response packet | Returned data |
|---:|---|---:|---:|---:|---|
| 140 | `A1 01 11 01 01 00 14 00` | Input ID `0x11` | 1 | 20 | 141: `11` (1 byte) |
| 142 | `A1 01 12 01 01 00 40 00` | Input ID `0x12` | 1 | 64 | 143: `12` (1 byte) |

Responses contain only the Report-ID byte. They are short relative to the report descriptor. The physical G815's behavior for directly issued GET_REPORT requests has not been tested.

The proxy has six `SET_REPORT` ID `0x11` transfers: three of each of two distinct payloads:
```hex
11 FF 00 1F 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
11 FF 00 1A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```
Each distinct payload occurs once in the direct trace; the proxy sends each three times. There is no data-bearing vendor Input report on endpoint `0x82` in the proxy capture (only two zero-length records).

## 7. Six-block table read in the new direct capture (`0x11 0x5A`)

### Structure
- 194 Output `SET_REPORT` requests and 194 corresponding Input reports.
- Two selector `(byte 4, byte 5) = (0,0)` probes occur at packet indices 1832 and 2364—one before each six-block sweep. Their responses are identical; exact meaning is not established.
- The 192 data requests = 6 selectors × 16 offsets × 2 repetitions. The first chunk `(1,1, offset 0)` belongs to each sweep, not a separate probe.
- Each chunk is 16 bytes, requested at offsets `00,10,20,...,F0`.
- Each selector represents 256 bytes; six selectors = 1,536 bytes per sweep; two sweeps = 3,072 response bytes.
- Selector pairs, in order: `(1,1)`, `(0,1)`, `(1,2)`, `(0,2)`, `(1,3)`, `(0,3)`; then this order repeats.
- All 96 selector/offset chunk values are repeated twice and are byte-identical.
- Median request-to-input-response latency: 2.829 ms (mean 2.790 ms; range 2.278–3.248 ms).

This resembles a paged table/configuration read. Selector fields are not proven to be memory addresses or profile numbers.

### Probe exchange (occurs before each sweep)
| Request packet | Response packet | Request | Response |
|---:|---:|---|---|
| 1832 | 1834 | `11 FF 11 5A 00 00 00 00 ...` | `11 FF 11 5A 00 01 01 00 00 02 01 00 00 03 01 00 FF FF 00 00` |
| 2364 | 2366 | same request | same response |

The first chunk of page `(1,1)` follows each probe at request packet 1840 / response 1842 and 2372 / response 2374 respectively.

### Reconstructed six blocks (256 bytes each)
#### Selector `0,1`
```text
0000  01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0010  00 00 FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0020  80 02 00 3A 80 02 00 3B 80 02 00 3C 80 02 00 3D  ...:...;...<...=
0030  80 02 00 3E 90 0D FF 01 90 0D FF 02 90 0D FF 03  ...>............
0040  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0050  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0060  80 02 00 3A 80 02 00 3B 80 02 00 3C 80 02 00 3D  ...:...;...<...=
0070  80 02 00 3E 90 0D FF 01 90 0D FF 02 90 0D FF 03  ...>............
0080  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0090  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00A0  50 00 52 00 4F 00 46 00 49 00 4C 00 45 00 5F 00  P.R.O.F.I.L.E._.
00B0  4E 00 41 00 4D 00 45 00 5F 00 44 00 45 00 46 00  N.A.M.E._.D.E.F.
00C0  41 00 55 00 4C 00 54 00 00 00 00 00 00 00 00 00  A.U.L.T.........
00D0  03 00 00 00 00 00 08 34 64 00 00 04 00 00 00 00  .......4d.......
00E0  00 00 34 01 64 08 03 00 00 00 00 00 08 34 32 00  ..4.d........42.
00F0  00 04 FF 00 00 00 00 FF 34 01 32 08 00 FF 2A 70  ........4.2...*p
```
#### Selector `0,2`
```text
0000  01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0010  00 00 FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0020  00 04 00 04 90 00 00 00 80 02 00 3C 80 02 00 3D  ...........<...=
0030  80 02 00 3E 90 0D FF 01 90 0D FF 02 90 0D FF 03  ...>............
0040  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0050  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0060  00 04 00 06 90 00 00 00 80 02 00 3C 80 02 00 3D  ...........<...=
0070  80 02 00 3E 90 0D FF 01 90 0D FF 02 90 0D FF 03  ...>............
0080  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0090  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00A0  50 00 52 00 4F 00 46 00 49 00 4C 00 45 00 5F 00  P.R.O.F.I.L.E._.
00B0  4E 00 41 00 4D 00 45 00 5F 00 44 00 45 00 46 00  N.A.M.E._.D.E.F.
00C0  41 00 55 00 4C 00 54 00 00 00 00 00 00 00 00 00  A.U.L.T.........
00D0  01 60 60 60 02 00 00 00 00 00 00 01 60 60 60 02  .```........```.
00E0  00 00 00 00 00 00 0A 00 DC FF 0B B8 01 32 00 00  .............2..
00F0  00 0A 00 DC FF 0B B8 01 32 00 00 00 00 FF 3F 64  ........2.....?d
```
#### Selector `0,3`
```text
0000  01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0010  00 00 FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0020  00 04 00 08 90 00 00 00 80 02 00 3C 80 02 00 3D  ...........<...=
0030  80 02 00 3E 90 0D FF 01 90 0D FF 02 90 0D FF 03  ...>............
0040  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0050  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0060  00 04 00 0A 90 00 00 00 80 02 00 3C 80 02 00 3D  ...........<...=
0070  80 02 00 3E 90 0D FF 01 90 0D FF 02 90 0D FF 03  ...>............
0080  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0090  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00A0  50 00 52 00 4F 00 46 00 49 00 4C 00 45 00 5F 00  P.R.O.F.I.L.E._.
00B0  4E 00 41 00 4D 00 45 00 5F 00 44 00 45 00 46 00  N.A.M.E._.D.E.F.
00C0  41 00 55 00 4C 00 54 00 00 00 00 00 00 00 00 00  A.U.L.T.........
00D0  01 60 60 60 02 00 00 00 00 00 00 01 60 60 60 02  .```........```.
00E0  00 00 00 00 00 00 03 00 00 00 00 00 13 88 32 00  ..............2.
00F0  00 03 00 00 00 00 00 13 88 32 00 00 00 FF 0D BB  .........2......
```
#### Selector `1,1`
```text
0000  01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0010  00 00 FF FF FF FF FF FF FF FF FF FF 00 00 00 00  ................
0020  80 02 00 3A 80 02 00 3B 80 02 00 3C 80 02 00 3D  ...:...;...<...=
0030  80 02 00 3E 90 0D 00 01 90 0D 00 02 90 0D 00 03  ...>............
0040  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0050  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0060  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0070  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0080  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0090  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00A0  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00B0  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00C0  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00D0  03 00 00 00 00 00 08 34 64 00 00 04 FF 00 00 00  .......4d.......
00E0  00 FF 34 01 64 08 03 00 00 00 00 00 08 34 32 00  ..4.d........42.
00F0  00 04 FF 00 00 00 00 FF 34 01 32 08 FF FF FF 00  ........4.2.....
```
#### Selector `1,2`
```text
0000  01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0010  00 00 FF FF FF FF FF FF FF FF FF FF 00 00 00 00  ................
0020  80 02 00 1E 80 02 00 1F 80 02 00 20 80 02 00 21  ........... ...!
0030  80 02 00 22 90 0D 00 01 90 0D 00 02 90 0D 00 03  ..."............
0040  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0050  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0060  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0070  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0080  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0090  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00A0  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00B0  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00C0  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00D0  0A 00 DC FF 0B B8 01 64 00 00 00 0A 00 DC FF 0B  .......d........
00E0  B8 01 64 00 00 00 0A 00 DC FF 0B B8 01 32 00 00  ..d..........2..
00F0  00 0A 00 DC FF 0B B8 01 32 00 00 00 FF FF FF 00  ........2.......
```
#### Selector `1,3`
```text
0000  01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
0010  00 00 FF FF FF FF FF FF FF FF FF FF 00 00 00 00  ................
0020  80 02 00 23 80 02 00 24 80 02 00 25 80 02 00 26  ...#...$...%...&
0030  80 02 00 27 90 0D 00 01 90 0D 00 02 90 0D 00 03  ...'............
0040  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0050  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0060  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0070  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0080  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
0090  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00A0  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00B0  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00C0  FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF  ................
00D0  03 00 00 00 00 00 13 88 64 00 00 03 00 00 00 00  ........d.......
00E0  00 13 88 64 00 00 03 00 00 00 00 00 13 88 32 00  ...d..........2.
00F0  00 03 00 00 00 00 00 13 88 32 00 00 FF FF FF 00  .........2......
```

### Text fields found
The UTF-16LE string at offset `0xA0` in selectors `(0,1)`, `(0,2)`, `(0,3)` is **`PROFILE_NAME_DEFAULT`** (one continuous string, not separate labels). The corresponding `0xA0` region in selectors `(1,1)`, `(1,2)`, `(1,3)` is filled with `FF`. The string is likely a metadata key; its role and the binary structures remain semantically undecoded.


### `0x02 0x1A` indexed responses
The 47 exchanges query byte-4 indices 0–4. FIFO request/response pairing gives these observed labels:
| Request index | Count | Response index | Text in response |
|---:|---:|---:|---|
| 0 | 13 | 1 | `BOT` |
| 1 | 13 | 0 | `U1 1` |
| 2 | 7 | 9 | `RSV` |
| 3 | 7 | 8 | `EXT` |
| 4 | 7 | 8 | `EXT` |

The labels are literal response data; their semantic meaning is not established.

## 8. Comparison with the previous direct capture (`usb.pcap`)

Both direct captures are the same physical G815 (same VID/PID, serial, device descriptor, configuration descriptor, and report descriptors). The address changed from 17 to 21.

### The 194 table-read sequence
| Previous capture | New direct capture | Comparison |
|---|---|---|
| `11 FF 11 5B` | `11 FF 11 5A` | Opcode byte 3 differs by 1 |
| 194 requests | 194 requests | All request bytes 4–19 match |
| 194 responses | 194 responses | All response bytes 4–19 match |
| Six blocks | Six blocks | All 1,536 bytes identical; each capture reads each block twice |

This is not a table-data change. It is an opcode-byte variant with identical selector/offset fields and identical returned bytes. The reason for `0x5B` vs `0x5A` remains unknown.

### Other command-family comparisons (old opcode → new opcode)
| Family in `usb.pcap` | Family in new direct capture | Old count | New count | Request data match | Response data match |
|---|---|---:|---:|---:|---:|
| `0x11 0x5B` | `0x11 0x5A` | 194 | 194 | 194/194 | 194/194 |
| `0x02 0x1B` | `0x02 0x1A` | 47 | 47 | 47/47 | 47/47 |
| `0x0F 0x0B` | `0x0F 0x0A` | 28 | 42 | 28/28 | 28/28 |
| `0x00 0x1B` | `0x00 0x1A` | 24 | 24 | 24/24 | 24/24 |
| `0x01 0x1B` | `0x01 0x1A` | 20 | 20 | 20/20 | 20/20 |
| `0x0F 0x1B` | `0x0F 0x1A` | 12 | 2 | 0/2 | 2/2 |
| `0x10 0x0B` | `0x10 0x0A` | 6 | 6 | 6/6 | 6/6 |
| `0x11 0x4B` | `0x11 0x4A` | 6 | 6 | 6/6 | 5/6 |
| `0x0F 0x5B` | `0x0F 0x5A` | 5 | 3 | 3/3 | 3/3 |
| `0x11 0x2B` | `0x11 0x2A` | 5 | 5 | 5/5 | 4/5 |
| `0x0B 0x1B` | `0x0B 0x1A` | 4 | 1 | 1/1 | 1/1 |
| `0x11 0x1B` | `0x11 0x1A` | 4 | 3 | 3/3 | 3/3 |

Examples of exact normalized pairs: `02 1B → 02 1A` (47/47), `00 1B → 00 1A` (24/24), `01 1B → 01 1A` (20/20). `0F 0B → 0F 0A` has 28 matching old requests/responses but 42 new requests (14 additional); `0F 1B → 0F 1A` has 12 old vs 2 new.

## 9. Sustained stream in the older capture but absent from the new direct trace

The older capture has two command families that do not occur in the new short direct capture:
- `0x10 0x5B`: 2,454 Output reports and 2,454 Input reports. Three selectors (byte 4 = `0x01`, `0x27`, `0x26`) occur 818 times each. There are 1,383 unique outgoing payloads but only three unique incoming response payloads.
- `0x10 0x7B`: 818 Output reports and 818 Input reports. Request and response payloads are always all-zero.

### Periodic timing
The `0x10 0x7B` stream has median interval 47.01 ms. There is one long gap of 30.938652 s; excluding this gap, the stream is approximately 21 Hz. Both families divide into two active segments:

| Segment | `0x10 0x7B` count | Start–end relative to `usb.pcap` | Duration |
|---|---:|---|---:|
| 1 | 71 | 115.0755–118.4134 s | 3.3379 s |
| 2 | 747 | 149.3521–184.5058 s | 35.1537 s |

The `0x10 0x5B` stream sends exactly three reports per cycle (selectors `01`, `27`, `26`), with matching response counts. The stable responses are:

| Selector | Response bytes 5–7 | Decimal values |
|---:|---|---|
| `0x01` | `50 37 00` | 80, 55, 0 |
| `0x27` | `B4 9B 00` | 180, 155, 0 |
| `0x26` | `99 D2 00` | 153, 210, 0 |

These resemble color/state triplets (RGB is plausible), but the selectors are not mapped to physical keys or zones and the capture has no action markers. `0x10 0x7B` may be a heartbeat/keep-alive; that is a hypothesis.

## 10. Direct-trace timeline (relative to capture start)

| Phase | Time | Activity |
|---|---|---|
| Descriptor enumeration | 4.635–4.648 s | Device/config/HID/string descriptors |
| `0x02 0x1A` query family | 4.779–5.379 s | 47 query/response pairs |
| `0x11 0x5A` table read | 5.421–6.639 s | 194 exchanges, six blocks read twice |
| `0x0F 0x0A` query family | 6.648–6.994 s | 42 query/response pairs |

The direct capture is only 9.81 seconds long and has no action annotations. It documents G HUB initialization/query activity, but cannot label individual packets as brightness, static color, effect, M1/M2/M3, Game Mode, or G1–G5 changes.

## 11. Proxy capture – what is and is not working

### Descriptor fidelity
| Item | Physical G815 | Proxy | Result |
|---|---|---|---|
| VID/PID | `046D:C33F` | `046D:C33F` | Match |
| Device descriptor | 18 bytes | 18 bytes | Byte-identical |
| Configuration descriptor | 59 bytes, `iConfiguration=4` | 59 bytes, `iConfiguration=0` | One field differs |
| Interface 0 report descriptor | 65 bytes | 65 bytes | Exact match |
| Interface 1 report descriptor | 188 bytes | 188 bytes | Exact match |
| Endpoint `0x81` | Interrupt IN, 8 bytes | same | Match |
| Endpoint `0x82` | Interrupt IN, max 64 bytes | same | Match |
| Product string | 72 bytes, full name | 64 bytes, truncated | Not identical |
| Serial | `017E30453836` | `G815-PROXY-S3` | Proxy identity |

### Dynamic vendor traffic
- Proxy has six `SET_REPORT` ID `0x11` packets, versus 410 in the direct capture.
- Proxy has no data-bearing Interrupt-IN endpoint `0x82` vendor reports; direct trace has 414 20-byte reports.
- Proxy receives `GET_REPORT` for ID `0x11` (20 bytes) and ID `0x12` (64 bytes), but returns one byte (`11` or `12`).
- Proxy does not show the 194-exchange table-read sequence or its data responses.

The descriptors are not the main problem: they are structurally accurate. The principal gap is dynamic report/data handling and/or forwarding of the physical endpoint `0x82` Input stream.

## 12. Corrections to earlier interpretations

1. Device address 20 in the passthrough capture is the proxy (`G815-PROXY-S3`), not the physical keyboard. It is nevertheless the correct host-side address to analyze. The physical G815 in the new direct capture is address 21; the older direct capture used address 17.
2. `GET_REPORT` ID `0x11` and `0x12` in the passthrough capture are genuine HID class requests. Their responses are genuinely one byte.
3. Report ID `0x12` is valid according to the HID report descriptor (64-byte total report), but no ID `0x12` data is observed in the direct trace. The earlier 18-byte USB descriptor starting with `12 01` or `12 03` is a USB descriptor, not itself a HID Report ID `0x12`.
4. The direct capture reveals Input Report IDs 1, 2, and 3 in addition to the vendor Report ID `0x11` channel and the declared 64-byte vendor Report ID `0x12`.

## 13. Unknowns / remaining questions

- Meaning of the six selector pairs and contents of each 256-byte block.
- Why the table-read opcode changes from `0x5B` to `0x5A` while request fields and data are identical.
- Meaning of command families `0x0F 0x0A`, `0x00 0x1A`, `0x01 0x1A`, etc.
- Whether `0x10 0x5B` / `0x10 0x7B` is lighting animation, refresh, per-zone state, or another periodic mechanism.
- Mapping of selectors `0x01`, `0x27`, `0x26` to physical keys/lighting zones.
- Direct-device behavior for GET_REPORT IDs `0x11` and `0x12` (not requested in the direct trace).
- Cause of the short ID `0x01` and long ID `0x03` endpoint `0x82` packets.
- Actual key events for M1/M2/M3, Game Mode, and G1–G5. The direct trace has no annotated key presses; the standard keyboard report is all zero.

## 14. Recommended next experiments

1. **Direct GET_REPORT test:** on the physical G815, issue GET_REPORT Input ID `0x11` length 20 and ID `0x12` length 64; record exact response lengths and bytes. This establishes whether the proxy's one-byte responses are device-specific or a gateway defect.
2. **Vendor Input report test:** record the physical endpoint `0x82` stream with no action, then press one key at a time (M1/M2/M3, Game Mode, G1–G5, media/volume controls). Keep each action isolated and add timestamp markers.
3. **Proxy comparison:** repeat the exact same actions through the gateway and compare endpoint `0x82` data byte-for-byte, including latency/order.
4. **Lighting tests:** separate brightness, static RGB, and each effect into distinct captures; change only one variable per capture.
5. **Long stream test:** capture the `0x10 0x5B` / `0x10 0x7B` stream for a short interval, then change only brightness, color, or effect and compare the three selector response triplets and outgoing payloads.
6. **Gateway implementation:** preserve both interfaces, report IDs 1/2/3/0x11/0x12, their lengths, Control `SET_REPORT` routing, and Interrupt-IN endpoint `0x82` responses. Do not reduce vendor reports to just their Report-ID byte.

## 15. Final assessment

The G815 descriptor topology is now known and the proxy reproduces the HID report descriptors exactly. The direct capture reveals a repeatable vendor table-read operation that was previously overlooked: six 256-byte selector blocks, fetched twice in 16-byte chunks, with identical data across the two direct captures. The proxy capture, however, has no vendor Interrupt-IN data and returns only one byte to two GET_REPORT requests. This narrows the principal passthrough gap to runtime vendor report handling rather than descriptor construction. The sustained `0x10/0x5B` and `0x10/0x7B` stream from the older capture remains an additional behavior to reproduce and decode.
