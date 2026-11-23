# M4 - Transparent mirror on the S3

## Result

No device-specific code is left in either firmware. Both PC ports use the
same mirror implementation (component `components/usb_mirror`):

| File | Content |
|---|---|
| `p4_s3_protocol.h/.c` | single copy of the SPI protocol (P4 and S3) |
| `usb_mirror_descriptor.h/.c` | descriptor analysis, host-tested |
| `usb_mirror_device.h/.c` | TinyUSB mirror: runtime descriptors, raw IN path, SET_REPORT with restored Report ID, GET_REPORT statistics |

| Side | Module | Task |
|---|---|---|
| P4 | `usb_device.c` | USB1P1 mirror (PHY routing, EP0 to the real device) |
| P4 | `s3_mirror.c` | session transfer to the S3, resync, S3 statistics |
| S3 | `s3_mirror_session.c` | staging of the received descriptors, commit to the mirror |
| S3 | `spi_link.c` | protocol dispatcher |

## Protocol additions (version byte unchanged, SPI OTA stays compatible)

| Type | Direction | Payload |
|---|---|---|
| 0x10 + interface | P4 -> S3 | raw input report |
| 0x20 MIRROR_BEGIN | P4 -> S3 | session = host address |
| 0x21 MIRROR_END | P4 -> S3 | - |
| 0x22 MIRROR_DESCRIPTOR | P4 -> S3 | kind, index, offset, total, data (56 B per chunk) |
| 0x23 MIRROR_COMMIT | P4 -> S3 | - (S3 connects to the PC) |
| 0x24 MIRROR_STATS | P4 -> S3 | STATUS carries p4_s3_mirror_stats_t |
| 0x42 MIRROR_REQUEST | S3 -> P4 | - (sent once after S3 boot) |

`p4_s3_link_command()` now matches the STATUS by sequence number. Before,
the first STATUS in the queue was taken, which could be the response to a
concurrently sent input report.

## Session lifecycle

1. P4 boot: `s3_mirror` sends MIRROR_END (clears a session left from before).
2. Keyboard plugged in, target `s3`: descriptors collected, session sent,
   S3 connects. Input reports are forwarded only after COMMIT succeeded.
3. S3 restart (reset, OTA): S3 sends MIRROR_REQUEST, P4 re-sends the session.
4. Every 2 s the P4 polls the S3 statistics; if the S3 has lost the session,
   it is re-sent.
5. Keyboard removed: MIRROR_END, the PC sees an unplug.

## Acceptance test (risk: GET_REPORT is not forwarded)

Preconditions: `CONFIG_USB_GATEWAY_CAPTURE_ENABLE=y`, P4 monitor open,
G HUB closed at the start.

Relevant log lines every `CAPTURE_DIAG_PERIOD_S`:

    s3_mirror:  S3 MIRROR session=.. state=2 mounted=1 046D:C33F in=.. drop=.. set=.. setdrop=.. get_stall=N last_get=ifI/idX/tT ...
    usb_device: P4 MIRROR session=.. state=2 mounted=1 046D:C08B in=.. drop=.. set=.. setdrop=.. ctldrop=.. get_stall=N ...

| # | Step | Pass criterion | Record |
|---|---|---|---|
| T1 | Plug G815 | log `S3 session <n> committed: 046D:C33F, 2 HID interface(s)`; Windows shows 046D:C33F with original product string and serial | strings |
| T2 | Type text, toggle Caps Lock | text correct; Caps LED follows (`set` increases) | - |
| T3 | Start G HUB | keyboard detected | `get_stall` before / after |
| T4 | Change lighting, record and play a G-key macro | works | `set`, `get_stall`, `last_get` |
| T5 | Reset S3 only (EN button) | log `S3 requested a session` and new commit; keyboard back without P4 reset, G HUB detects it again | time to recovery |
| T6 | Reset P4 only | S3 receives MIRROR_END, then new session; keyboard back | - |
| T7 | Unplug / replug G815 | PC sees unplug / plug | - |
| T8 | Plug a different keyboard into the same port (no reboot) | PC shows the identity of the new keyboard | VID:PID |
| T9 | Repeat T1-T4 with the G502 on USB1P1 (`P4 MIRROR` line) | mouse moves, G HUB detects it, DPI change works | `get_stall` |

Evaluation of the GET_REPORT risk:

| Observation | Conclusion |
|---|---|
| G HUB works, `get_stall = 0` | GET_REPORT not used, risk closed |
| G HUB works, `get_stall > 0` | GET_REPORT used but not required, risk closed; note `last_get` |
| G HUB fails, `get_stall > 0` | GET_REPORT required: implement a generic per-Report-ID cache (last SET_REPORT / input report) answered synchronously |
| G HUB fails, `get_stall = 0` | other cause: compare USBPcap PC side vs. device side |

T8 additionally verifies that esp_tinyusb keeps references to the descriptor
buffers (descriptor swap without reinstall).
