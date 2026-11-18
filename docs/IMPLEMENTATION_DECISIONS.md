# Implemented decisions

- Shared P4 SPI2 bus: GPIO6 SCLK, GPIO7 MOSI, GPIO8 MISO.
- S3 device: CS GPIO36, 10 MHz, high-priority task.
- SSD1306: CS GPIO37, D/C GPIO38, RESET GPIO23, 10 MHz, low-priority task, dirty refresh <= 5 Hz.
- LEDs: GPIO1 Busy, GPIO4 Success, GPIO5 Error; active-high by default, individually invertible.
- Hall/button power: GPIO0, push-pull, low at POR/init, active-high by default; configurable.
- S3 Super Mini: GPIO12 SCLK, GPIO11 MOSI, GPIO13 MISO, GPIO10 CS, GPIO9 optional IRQ.
- Full schema-v1 parser supplied as a separate source module.
- Input proxy API implemented; Output, Feature and EP0 APIs reserved and return explicit NOT_SUPPORTED until the reverse transport is completed.
