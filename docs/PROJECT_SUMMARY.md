# Project summary

- ESP32-P4 owns physical G815/G502 USB Host, G502 Device, mapping, learning, analog I/O, storage and UI.
- ESP32-S3 exposes G815 as a second independent PC-facing USB device.
- P4/S3 transport uses 72-byte frames and CRC-16/CCITT-FALSE.
- ADS7128 0x10 and DAC7578 0x4C use P4 I3C in 1 MHz legacy-I2C mode.
- Eight analog HID lines map DAC 0..7 to ADC 7..0.
- Motion model uses 360 angular bins x 4 magnitude bins = 1440 cells.
- JSON schema v1 is authoritative; binary cache is derived using SHA-256 comparison.
- Explicit Ctrl+Alt+F12 export performs an atomic JSON replacement.
- Three LEDs plus SSD1306 SPI display provide feedback.

Overall engineering progress estimate: 63%. Hardware and Logitech/G HUB verification remain the dominant gap.
