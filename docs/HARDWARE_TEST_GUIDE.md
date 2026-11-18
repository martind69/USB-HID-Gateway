# Hardware and end-to-end test guide

## A. Feedback hardware (point 5)

1. Boot with OLED disconnected: gateway must continue, Error LED may latch.
2. Verify active-high LEDs: GPIO1 Busy, GPIO4 Success, GPIO5 Error.
3. Exercise READY, LEARNING, EXPORT_WRITING, EXPORT_SUCCESS, EXPORT_ERROR and ANALOG_ERROR.
4. Attach OLED and verify reset pulse on GPIO23, CS GPIO37, D/C GPIO38 and 10 MHz clock GPIO6.
5. Generate continuous S3 traffic while refreshing OLED; no frame loss or watchdog is permitted.

Pass: LEDs are independent of OLED; OLED updates only on state change and no faster than 5 Hz.

## B. P4-S3 link test (points 8 and 11)

Wiring:

| P4 | S3 Super Mini |
|---:|---:|
| GPIO6 SCLK | GPIO12 SCLK |
| GPIO7 MOSI | GPIO11 MOSI |
| GPIO8 MISO | GPIO13 MISO |
| GPIO36 CS | GPIO10 CS |
| optional IRQ input | GPIO9 IRQ output |
| GND | GND |

Run in phases:

1. 100 NOP frames and verify STATUS/sequence.
2. Corrupt one CRC every 100 frames; each must be rejected.
3. Run sequence 0..255 through at least four wraparounds.
4. Send 8-byte and 64-byte patterns: all 00, all FF, walking-one, counter.
5. Run at least 1,000,000 frames at 10 MHz.
6. Reset S3 during traffic and verify bounded recovery without stale reports.
7. Force queue overflow and verify explicit counter/status.

Record: sent, received, CRC error, sequence error, timeout, queue overflow, recovery count, maximum latency.

## C. USB Input end-to-end (point 9)

1. Enumerate physical G815 on P4.
2. Capture the complete raw Interrupt-IN payload before semantic processing.
3. Forward via `usb_proxy_send_input()`.
4. Capture S3 upstream packets with USBPcap/Wireshark.
5. Compare interface, endpoint, Report ID, length and every byte.
6. Test press/release ordering and simultaneous typing/vendor traffic.
7. Disconnect/reconnect physical G815; no stale key may appear.

Pass: byte-identical payloads and zero unexplained drops for a 30-minute test.

## D. JSON/cache/export round-trip (point 5)

1. Start with a 1440-cell profile containing unique deterministic values.
2. Delete `.bin`; boot and verify JSON parse plus cache creation.
3. Reboot and verify cache path is used.
4. Change one JSON tuning value; verify SHA-256 mismatch and cache regeneration.
5. Trigger Ctrl+Alt+F12 export.
6. Reload exported JSON and compare every channel, 720 envelope values, 1440 cells and tuning field.
7. Interrupt power during `.tmp`, `.bak` and rename phases; one valid JSON must remain recoverable.

Float acceptance: absolute error <= 1e-6 for normalized/tuning values.

## E. Logitech evidence (point 13)

Use USBPcap plus Wireshark on Windows:

1. Capture enumeration with physical G815 directly connected.
2. Save `GET_DESCRIPTOR(Report)` responses per interface; verify 65 and 188 bytes.
3. Capture G1 press alone, G1 release alone and a control key for comparison.
4. Toggle G1 illumination in G HUB and capture Output, Feature and vendor-control traffic.
5. Repeat for G502 buttons, X/Y, wheel, pan, DPI/profile/RGB operations.
6. Document device address, interface, endpoint, transfer type, Report ID, length and full payload.

Do not merge WinHidDump collection descriptors into interface Report Descriptors.
