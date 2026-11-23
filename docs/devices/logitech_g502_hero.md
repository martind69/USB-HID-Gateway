# Reference device: Logitech G502 HERO

Source: USBPcap analysis `usbpcap_analyse_logitech_g502_hero.md` (2026-11-23).
Not compiled. All values are read from the device at runtime; this file
defines the acceptance criteria for the mirror.

## Configuration entry (only device-specific location)

`/sdcard/config/gateway.json`:

    { "match": { "vid": "046D", "pid": "C08B" }, "target": "p4_fs" }

## Identity

| Field | Value |
|---|---|
| idVendor / idProduct | 0x046D / 0xC08B |
| bcdUSB / bcdDevice | 0x0200 / 0x2703 |
| bMaxPacketSize0 | 64 |
| bmAttributes / bMaxPower | 0xA0 / 150 (300 mA) |

| Interface | Class / Sub / Proto | Report descriptor | Endpoint | wMaxPacketSize | bInterval |
|---|---|---|---|---|---|
| 0 | 03 / 01 / 02 boot mouse | 67 B | 0x81 IN | 8 | 1 |
| 1 | 03 / 00 / 00 HID++ | 151 B | 0x82 IN | 20 | 1 |

Interface 0 report: buttons (b0..b4), reserved, X int16 LE, Y int16 LE,
wheel int8, AC pan int8. Report interval 0.9996 ms.

## Mirror check (all within limits)

2 HID interfaces <= 4, interrupt endpoints only, wMaxPacketSize <= 64,
bMaxPacketSize0 = 64, string indices 1..3 < 8.

## Acceptance test

| # | Check | Expected |
|---|---|---|
| 1 | P4 log on plug-in | `addr=n 046D:C08B keyboard=0 mouse=1 -> p4_fs (gateway.json)` |
| 2 | P4 log | `Mirror connected to PC: 046D:C08B, 2 HID interface(s)` |
| 3 | Windows device manager / USBPcap PC side | 046D:C08B, identical descriptors and strings |
| 4 | USBPcap PC side, fast movement | X/Y values outside -127..+127 present |
| 5 | G HUB | mouse detected, DPI change works (HID++ via SET_REPORT + EP 0x82) |
| 6 | Unplug G502 | PC sees unplug, log `Mirror session ended` |
