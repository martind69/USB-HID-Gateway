# USB HID Gateway – USB Topology

Status: Logical topology based on the current target setup. Exact board connector routing and controller assignment remain to be verified.

## 1. Physical connection

The intended connection is:

```text
                         PC
                          |
                  USB connection
                          |
                 +----------------+
                 |   ESP32-P4     |
                 |                |
                 | USB Device     |  PC-facing upstream connection
                 | USB Host       |  Peripheral-facing downstream connection
                 +----------------+
                          |
                  Downstream USB
                          |
                 +----------------+
                 | Logitech G810  |
                 | Keyboard       |
                 | Integrated Hub |
                 +----------------+
                          |
                    Internal Hub
                          |
                 +----------------+
                 | Logitech G502  |
                 | Mouse          |
                 +----------------+
```

The G502 is connected to the USB hub integrated into the G810. Consequently, the Host side must discover and manage a hub with more than one downstream function, rather than assuming that a single directly attached HID device is present.

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

- Enumerate the G810 and its integrated hub.
- Enumerate the G502 behind the hub.
- Discover configurations, interfaces, endpoints and HID report descriptors.
- Keep per-device and per-interface state.
- Receive reports and route them to the proxy.
- Handle device removal, re-enumeration and errors without invalidating other active interfaces.

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
G810 keyboard interface
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
G502 mouse interface
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

## 5. Vendor-specific traffic and transparency limits

Generic HID report forwarding does not automatically forward all USB behavior.

G HUB compatibility may depend on vendor-specific HID reports, Feature reports, control transfers, interface associations, device identity or other protocol details. These must be captured and tested with the G810/G502 hardware.

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
- [ ] Enumerate G810 and integrated hub.
- [ ] Enumerate G502 through the G810 hub.
- [ ] Capture descriptors for every HID interface.
- [ ] Capture G HUB-related control and Feature report traffic.
- [ ] Verify PC-side enumeration and report delivery.
