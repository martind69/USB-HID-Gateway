# Two-device upstream architecture

## Binding requirement

The PC must enumerate the Logitech G815 and Logitech G502 as two separate USB devices. Each must retain its own Device Descriptor identity (VID/PID), configuration, interfaces, endpoints and device-recipient control-request context. A single composite USB device is not acceptable.

## Selected architecture

The ESP32-P4 remains the main gateway and sole owner of the downstream USB Host bus. It emulates the G502 directly. A separate ESP32-S3 Mini emulates the G815. Both USB Device controllers connect to separate downstream ports of a PC-facing USB hub.

    PC
     |
    PC-facing USB hub
     |                         |
    ESP32-P4 USB Device       ESP32-S3 USB Device
    G502 (046D:C08B)          G815 (046D:C33F)
     |                         |
     +----------- SPI ---------+
                 |
             ESP32-P4
                 |
          USB Host controller
                 |
       Existing downstream hub
                 |
          Physical G815 + G502

The PC sees one gateway hub and two independent child devices. The S3 does not enumerate the physical Logitech devices; it receives the G815 proxy reports and control transactions from the P4 over SPI. The P4 remains the source of truth for the physical devices.

## Why two Device controllers are required

A USB composite configuration still represents one USB device address and one Device Descriptor. It cannot expose two independent VID/PID identities. The P4's current design uses one OTG controller as Host and one as Device; the S3 adds the second independent upstream Device instance without changing the P4's existing Host/Device allocation.

The USB Serial/JTAG controller on either MCU is fixed-function and is not treated as a general-purpose HID Device controller.

## ESP32-S3 Mini selection

The secondary controller is an ESP32-S3 Mini Development Board. Its programmable Full-Speed USB OTG controller uses GPIO19 (D−) and GPIO20 (D+). The board is suitable only if its USB-C connector is routed directly to those native USB pins. Exact product revision and continuity must be checked before hardware integration.

The current proposed S3 SPI pin assignment is SCLK GPIO6, MOSI GPIO7, MISO GPIO8, CS_N GPIO9, LINK_IRQ GPIO10 and RESET_N GPIO21. This allocation is provisional pending exact-board schematic and pinout verification. The P4-side SPI pins are not yet fixed.

## G815 USB interface target

The observed G815 configuration contains two HID interfaces:

| Interface | Function | Interrupt IN | Max packet | bInterval |
|---|---|---|---:|---:|
| 0 | Boot keyboard | 0x81 | 8 bytes | 1 |
| 1 | Vendor-defined HID | 0x82 | 64 bytes | 1 |

The S3 framework advertises these two interfaces and the observed VID/PID 046D:C33F. The current vendor report descriptor is provisional and based on Windows HID collection evidence (including Report IDs 0x11 and 0x12). The raw interface-level 65-byte and 188-byte report descriptor streams must be captured and used before claiming byte-exact descriptor fidelity.

## Data and control ownership

The P4 owns downstream USB Host enumeration and physical G815/G502 lifecycle, downstream control transfers and interrupt endpoint polling, per-device descriptor/configuration snapshots and endpoint state, routing decisions, report scheduling and the G502 emulation path.

The S3 owns independent upstream USB Device enumeration for the G815, G815-facing EP0 state and keyboard/vendor HID endpoints, USB Device callbacks and endpoint readiness, and conversion of framed SPI messages into G815 upstream reports.

The SPI transport must preserve raw report payloads, request/response correlation, sequence numbers, bounded buffering, backpressure and disconnect/reset notifications. It is not a USB bus and does not carry electrical USB transactions.

## Initial SPI frame

The initial S3 framework uses fixed 72-byte full-duplex frames: 6-byte header, 64-byte payload area and 2-byte CRC-16/CCITT-FALSE. The header contains magic, protocol version, message type, sequence and payload length. CRC is calculated over the first 70 bytes and stored little-endian. The P4 is master and S3 is slave; the response is prepared for the following transaction.

The message-type registry, complete P4 master driver, control-transfer command set and report scheduling remain to be implemented. The first firmware increment returns a status acknowledgement only.

## G HUB compatibility

Separate VID/PID identities are necessary but not sufficient for complete G HUB compatibility. The gateway introduces a new PC-facing hub node and changes the parent/port path compared with the original monitor topology. G HUB vendor control transfers, Feature reports, Output reports, endpoint traffic and device selection behavior must be captured and reproduced as required.

Do not model the G502 as a child of a G815-internal hub. Existing topology evidence places both devices as siblings below the monitor's Genesys Logic hub (05E3:0610); the G815 pass-through is a cable-only path.

## Validation gates

1. Confirm exact ESP32-S3 Mini board revision and USB-C routing to GPIO19/GPIO20.
2. Build the S3 application for esp32s3.
3. Verify two independent upstream device identities through the PC-facing hub.
4. Verify the S3's two HID interfaces, endpoint addresses, packet sizes and polling interval.
5. Implement and verify the P4 SPI master against the S3 fixed-frame slave.
6. Forward raw keyboard and vendor reports without unintended modification.
7. Implement the necessary EP0, Feature, Output and vendor control paths.
8. Test Logitech G HUB profiles, RGB, media keys and other observed functions.

No S3 build, P4/S3 integration test or G HUB compatibility test is claimed by this architecture note.
