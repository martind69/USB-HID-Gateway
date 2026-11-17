# ESP32-S3 secondary firmware framework

## Scope

This project is the independent USB Device firmware for the secondary ESP32-S3. It is a separate ESP-IDF application under firmware/secondary_s3, target esp32s3.

The initial framework provides:
- ESP-IDF project and managed esp_tinyusb dependency.
- Full-Speed USB Device initialization.
- Two HID interfaces matching the observed G815 endpoint topology.
- A provisional keyboard report descriptor and vendor-defined report descriptor.
- A fixed-size, CRC-protected SPI slave frame interface.
- Explicit separation between USB callbacks, SPI link processing and future Logitech protocol handling.

It is a framework scaffold, not a completed G815 emulator. Exact descriptor bytes, vendor control transfers, report semantics and P4 integration remain open.

## USB interface contract

| Interface | Function | Endpoint | Max packet | Interval |
|---|---|---|---:|---:|
| 0 | HID keyboard | IN 0x81 | 8 | 1 |
| 1 | Vendor-defined HID | IN 0x82 | 64 | 1 |

The device descriptor uses VID/PID 046D:C33F to preserve the observed G815 identity. Device release number, manufacturer/product strings and the raw 65-/188-byte report descriptors must be confirmed from capture evidence. The current report descriptors are explicitly provisional.

The vendor report scaffold includes Report IDs 0x11 and 0x12, with total report sizes of 20 and 64 bytes including the Report ID, based on the Windows collection evidence. It is not asserted to be byte-identical to the physical interface-level report descriptor.

## SPI frame

The initial link uses fixed 72-byte full-duplex transactions:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Magic 0xC5 |
| 1 | 1 | Magic 0x5A |
| 2 | 1 | Protocol version |
| 3 | 1 | Message type |
| 4 | 1 | Sequence |
| 5 | 1 | Payload length, 0–64 |
| 6 | 64 | Payload, zero padded |
| 70 | 2 | CRC-16/CCITT-FALSE, little-endian |

CRC covers bytes 0–69. The P4 is SPI master; the S3 is SPI slave. The S3 response is clocked out in the same transaction in which the next P4 request arrives, so responses are one transaction behind. Sequence numbers correlate requests and responses. The initial bus clock should remain conservative until measured on hardware.

Message-type allocation and the complete command set are intentionally reserved for the P4/S3 integration step. No legacy-controller-specific framing or bootloader protocol is used.

## Firmware update

The initial development workflow flashes the S3 directly using ESP-IDF over its native USB download interface or a supported UART programming adapter. P4-controlled remote update is explicitly out of scope until the runtime protocol and recovery path are proven.

## Build

From the repository root in an ESP-IDF 6.1 environment:

    cd firmware/secondary_s3
    idf.py set-target esp32s3
    idf.py build

USB pins GPIO19/GPIO20 and the proposed SPI pins are board-specific assumptions and must be confirmed before connecting hardware. The project has not yet been built or tested on the selected physical board.
