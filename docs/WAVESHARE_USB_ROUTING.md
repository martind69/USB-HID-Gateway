# Waveshare ESP32-P4-Module-DEV-KIT USB routing assessment

Date: 2026-11-13

## Purpose

This document records the USB connector routing relevant to the USB-HID-Gateway. It separates the ESP32-P4 USB controllers from the board's physical connectors and prevents the USB Serial/JTAG connection from being mistaken for the PC-facing TinyUSB HID connection.

## Board connectors

| Board reference | Physical interface | Verified function |
|---|---|---|
| 4 | Type-C | Onboard CH343P USB-to-UART bridge; Windows enumerates WCH `1A86:55D3` and a COM port. Used for flashing/logging through UART. |
| 6 | Type-C | ESP32-P4 USB Serial/JTAG connection; Windows enumerates Espressif `303A:1001`. This is a fixed-function controller, not a programmable TinyUSB HID port. |
| 12 | Four Type-A sockets | USB OTG path, with one socket connected directly to the P4 and three sockets behind the CH334 hub. |
| 13 | H3 jumper | Selects which Type-A path is enabled. Waveshare FAQ states HOST enables Type-A port 1; DEVICE enables Type-A ports 2–4. |

Sources:
- Waveshare board hardware description: https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT
- Waveshare FAQ, “How to use the Type-A ports”: https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT/FAQ
- Waveshare schematic: https://files.waveshare.com/wiki/ESP32-P4-Module-DEV-KIT/ESP32-P4-Module-DEV-KIT.pdf
- ESP-IDF 6.1 USB Serial/JTAG documentation: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/usb-serial-jtag-console.html

## Interpretation of the two Windows reports

The supplied USB Device Tree Viewer reports show:
- Type-C 4: WCH CH343P, VID:PID `1A86:55D3`, USB Single Serial (COM5 in the latest report).
- Type-C 6: Espressif USB JTAG/serial debug unit, VID:PID `303A:1001`.

These are expected fixed-function debug/programming interfaces. Neither report demonstrates that the programmable USB OTG controller has enumerated as a HID device.

The ESP32-P4 USB Serial/JTAG controller is hardware-fixed and cannot be reconfigured as HID. ESP32-P4 has separate USB OTG capability; the OTG path on this board is routed to the Type-A connector group.

## TinyUSB port selection

The ESP32-P4 TinyUSB device stack supports selecting the High-Speed OTG port. The current PoC uses `esp_tinyusb` and must explicitly select `TINYUSB_PORT_HIGH_SPEED_0` so that the code's intended controller is unambiguous.

The successful return from `tinyusb_driver_install()` means the stack and PHY were initialized. It does not prove that the board has connected that OTG PHY to the external PC cable or that the PC has enumerated the HID descriptors.

## Hardware limitation / unresolved topology

The Waveshare board documentation describes the Type-A connectors as OTG ports: one direct P4 connection and three downstream ports behind the CH334 hub. H3 selects between those Type-A paths. Type-C 6 is the fixed USB Serial/JTAG interface, not a documented programmable OTG-device connector.

Consequently, the current board documentation does not identify a standards-compliant, separately exposed PC-facing USB OTG Device connector while the Type-A group is used for peripheral-facing USB Host operation. Do not use a Type-A-to-Type-A cable or assume that Type-C 6 can carry TinyUSB HID.

This is a board-level integration blocker for the intended simultaneous topology:
- ESP32-P4 USB Host -> G815/G502 (including hub topology)
- ESP32-P4 USB Device -> PC as composite keyboard/mouse

Before implementing more proxy transport, resolve this physically. Options to investigate:
1. A board revision/schematic variant exposing the P4 OTG Device D+/D- on a separate connector.
2. A documented, electrically safe hardware modification that routes an independent P4 OTG controller/PHY to a USB device connector.
3. A different development board with two independently exposed USB data paths (one Host, one Device).

The ESP32-P4 chip's controller capabilities alone do not establish that the Waveshare board exposes both required paths simultaneously.

## Current test conclusion

- The CH343P and USB Serial/JTAG reports are not evidence of a TinyUSB HID failure.
- `USB DEVICE=ready` is only a stack-initialized state; it is not a PC-enumerated state.
- No Windows HID enumeration has yet been demonstrated.
- Do not mark the USB Device PoC accepted until a real PC-facing OTG Device connection is established and Windows retrieves the keyboard and mouse report descriptors.
