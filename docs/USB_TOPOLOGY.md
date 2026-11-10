# USB HID Gateway – USB Topology

Status: Physical peripheral topology documented from the Windows USB tree captured on 2026-11-09. Exact ESP32-P4 board connector routing and controller assignment remain to be verified.

## 1. Physical peripheral topology

The observed peripheral setup uses the USB hub integrated into the ASUS ROG monitor. The Logitech G815 has two USB connections: its own keyboard connection and a pass-through connection for one downstream device. The G815 pass-through is not an additional USB hub.

The supplied dump and the physical clarification give this topology:

```text
                         ESP32-P4 USB Host
                                |
                       Monitor USB upstream
                                |
                  +--------------------------+
                  | Genesys Logic USB Hub    |
                  | VID:PID 05E3:0610        |
                  | 4 downstream ports       |
                  | High-Speed, Multi-TT     |
                  +--------------------------+
                     |       |       |       |
                   Port 1  Port 2  Port 3  Port 4
                     |       |       |       |
                  ASUS     empty   G502    G815
                  Aura             Mouse   Keyboard
                                   ^
                                   |
                         G815 USB pass-through
                         (cable path, no hub)
```

Observed USB port chains:

| Monitor hub port | Device | VID:PID | Port chain |
|---|---|---|---|
| 1 | ASUS ROG Gaming Display Aura Device | `0B05:18DA` | `2-7-1` |
| 2 | No device connected | – | `2-7-2` |
| 3 | Logitech G502 HERO | `046D:C08B` | `2-7-3` |
| 4 | Logitech G815 | `046D:C33F` | `2-7-4` |

The hub itself is at port chain `2-7`. Windows also reports a companion SuperSpeed hub at `2-23`; this is the paired SuperSpeed side of the monitor hub. The observed HID devices in this capture are operating at Full-Speed (12 Mbit/s).

The G502 and G815 appear as sibling devices because the G815 pass-through is a direct USB cable path, not a hub. USB enumeration therefore does not add a G815 hub tier between the monitor hub and the mouse. The physical pass-through path is known from the user's clarification; the Windows port tree alone cannot distinguish that cable route from a direct connection to the same monitor hub.

The Genesys Logic hub is self-powered, has four downstream ports and supports High-Speed operation with Multi-TT. The monitor's ASUS Aura HID function is another device on the same hub; it is not part of the G815 keyboard interface.

## 2. USB roles

### 2.1 PC-facing USB Device

Responsibilities:

- Enumerate to the PC as a USB device.
- Expose the gateway's PC-facing HID interface configuration.
- Provide report descriptors and endpoint behavior consistent with the reports being forwarded.
- Receive and handle the relevant class and control requests.
- Transmit forwarded or modified HID reports to the PC.

The exact descriptor strategy is an open design decision. A composite device with separate keyboard and mouse HID interfaces is a likely direction, but must be checked against the actual downstream descriptors and G HUB requirements before implementation.

### 2.2 Peripheral-facing USB Host

Responsibilities:

- Enumerate the monitor's Genesys Logic USB hub.
- Enumerate the ASUS Aura HID function, G502 and G815 on the hub's downstream ports.
- Discover configurations, interfaces, endpoints and HID report descriptors for each device.
- Keep per-device and per-interface state.
- Receive reports and route relevant keyboard/mouse reports to the proxy.
- Handle device removal, re-enumeration and errors without invalidating other active interfaces.

The Host implementation must not assume that the keyboard contains a hub or that the mouse is a child device of the keyboard. The G815 pass-through does not create a USB hub node.

The ASUS Aura function is a separate vendor-defined HID device. It should be enumerated and isolated from the Logitech keyboard/mouse proxy path. Its report semantics are unknown and must not be forwarded as keyboard or mouse input.

The implementation must not assume one HID interface or one report descriptor per physical USB device.

## 3. Controller separation

Host and Device roles must use independent USB controller instances. They are not two software modes of one controller.

The selected ESP32-P4 board must provide accessible routing for both controller instances at the same time. The following must be verified against the board schematic and the selected ESP-IDF release:

- Which USB controller is routed to each physical connector or pin set.
- Whether both controller instances can operate concurrently in the required roles.
- Required PHY configuration and any external PHY or power circuitry.
- VBUS sourcing, current limits and over-current handling on the Host port.
- USB data-line routing, connector type and signal integrity.

Do not infer simultaneous Host/Device operation from the MCU name alone; confirm it for the actual board and software release.

## 4. Data paths

### 4.1 Keyboard

```text
G815 keyboard HID interface
        |
     Monitor Hub
        |
     USB Host
        |
  HID descriptor parser
        |
     HID proxy
        |
    USB Device
        |
        PC
```

Keyboard reports should be forwarded without semantic modification except for the configured hotkey detection. Hotkey detection must not consume or suppress the key reports required by the PC or G HUB.

### 4.2 Mouse

```text
G502 mouse HID interface
        |
     G815 pass-through cable
        |
     Monitor Hub
        |
     USB Host
        |
  HID descriptor parser
        |
     HID proxy
        |
  Mode-dependent routing
        |
    USB Device
        |
        PC
```

In passthrough mode, supported mouse reports are forwarded unchanged.

In hotkey mode:

- Decode physical X/Y using the parsed report layout.
- Retain physical X/Y internally for the gateway's own logic.
- Replace only the X/Y fields sent to the PC with synthetic values.
- Preserve button, wheel and all unrelated fields.
- Handle report IDs and report lengths correctly.

### 4.3 Monitor Aura HID function

The monitor exposes a separate vendor-defined HID function on hub port 1. Its interface has interrupt IN and OUT endpoints, but the report descriptor bytes could not be retrieved in the supplied Windows capture.

This function is outside the initial keyboard/mouse forwarding scope. The Host must nevertheless handle its presence during hub enumeration. Do not assume its vendor-defined reports are Logitech reports or route them into the HID proxy.

## 5. Vendor-specific traffic and transparency limits

Generic HID report forwarding does not automatically forward all USB behavior.

G HUB compatibility may depend on vendor-specific HID reports, Feature reports, control transfers, interface associations, device identity or other protocol details. These must be captured and tested with the actual G815/G502 hardware.

The project must distinguish between:

- HID Input/Output/Feature report forwarding.
- USB standard requests.
- HID class requests.
- Vendor-specific control transfers.
- Hub management and downstream device enumeration.

Any unsupported transaction must be identified explicitly. Do not describe the gateway as a fully transparent USB proxy until the required traffic has been validated.

## 6. Topology verification checklist

- [ ] Confirm exact ESP32-P4 board and revision.
- [ ] Inspect board schematic and identify both USB controller routes.
- [ ] Confirm concurrent Host and Device support in the selected ESP-IDF release.
- [ ] Confirm Host-side VBUS power and protection.
- [x] Identify the monitor's Genesys Logic hub and its four downstream ports from the Windows USB tree.
- [x] Identify the ASUS Aura HID function, G502 and G815 as devices on that hub.
- [x] Confirm that the G815 pass-through is not a hub tier; the G502 and G815 appear as sibling hub devices.
- [ ] Retrieve raw HID report descriptor bytes for all required HID interfaces.
- [ ] Capture G HUB-related control and Feature report traffic.
- [ ] Verify PC-side enumeration and report delivery.
