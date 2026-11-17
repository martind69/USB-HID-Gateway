# ESP32-S3 Mini secondary-controller board

## Decision

The secondary USB Device controller is based on an ESP32-S3 Mini Development Board. The ESP32-P4 remains the gateway controller and downstream USB Host owner. The secondary ESP32-S3 presents the G815 as an independent PC-facing USB Device.

This replaces the previously explored small 8051-class controller architecture. The secondary controller is now a programmable ESP32-S3 running ESP-IDF and TinyUSB.

## USB capability

The ESP32-S3 provides a programmable Full-Speed USB OTG controller, separate from its fixed-function USB Serial/JTAG controller. Native USB OTG uses GPIO19 for USB D− and GPIO20 for USB D+.

The board is acceptable only if its USB-C connector routes D−/D+ to GPIO19/GPIO20 without an intervening USB-UART bridge and those pins are not otherwise consumed. The supplied board schematic appears to show a direct USB connection; exact board revision and net-to-GPIO continuity remain a bring-up check.

Do not use GPIO19/GPIO20 for SPI or general-purpose signals when the native USB Device port is active.

## Provisional secondary-link pin assignment

The following allocation is the current wiring proposal, not a verified board-level pinout:

| Signal | ESP32-S3 GPIO | Direction at S3 | Notes |
|---|---:|---|---|
| SPI SCLK | 6 | Input | P4 SPI master clock |
| SPI MOSI | 7 | Input | P4 to S3 |
| SPI MISO | 8 | Output | S3 to P4 |
| SPI CS_N | 9 | Input | Active low |
| LINK_IRQ | 10 | Output | Optional event notification |
| RESET_N | 21 | Input | External reset control; polarity must be checked |

The pin assignment must be checked against the exact purchased board's schematic, module pin availability, strapping requirements and any onboard peripherals before PCB routing. The P4-side SPI pin allocation is not yet fixed.

## Runtime responsibilities

- ESP32-P4: sole downstream USB Host, G502 emulation, transaction scheduling, device registry and gateway policy.
- ESP32-S3: independent USB Device for G815, keyboard/vendor HID endpoints and local USB control-request handling.
- SPI: framed runtime transport between P4 and S3. The link is not a USB bus and must preserve request/response correlation and raw HID payload bytes.
- Firmware updates: S3 is programmed through its own native USB/ROM download path during development. Automatic P4-controlled S3 updates are not part of the initial framework.

## G815 USB target

The PC must enumerate the G815 and G502 as two separate USB devices. The S3 exposes the G815 identity (046D:C33F); the P4 exposes the G502 identity (046D:C08B). Each is connected to a separate downstream port of the PC-facing hub.

The observed G815 configuration has two HID interfaces:
- Interface 0: keyboard, interrupt IN 0x81, 8-byte maximum packet, bInterval=1.
- Interface 1: vendor-defined HID, interrupt IN 0x82, 64-byte maximum packet, bInterval=1.

The exact raw G815 report-descriptor streams remain provisional and must be replaced by byte-exact USB capture data before claiming full enumeration fidelity or G HUB compatibility.

## Initial validation gates

1. Confirm the exact board model/revision and USB-C-to-GPIO19/20 routing.
2. Build the ESP-IDF project for esp32s3.
3. Verify native USB Device enumeration independently of the SPI link.
4. Verify two HID interfaces and endpoint addresses/sizes.
5. Verify SPI slave communication with the P4 at a conservative clock rate.
6. Verify raw keyboard and vendor reports end-to-end.
7. Verify Logitech G HUB functions; VID/PID preservation alone is not proof of compatibility.

## References

- ESP32-S3 datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
- ESP32-S3-MINI-1 datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-s3-mini-1_mini-1u_datasheet_en.pdf
- ESP-IDF USB Device Stack: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_device.html
