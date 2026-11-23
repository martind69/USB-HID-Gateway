# Transparent mirror (M3, P4 USB1P1)

## Separation of code and configuration

| Layer | Content | Device-specific? |
|---|---|---|
| `/sdcard/config/gateway.json` | which device goes to which PC port | yes, only here |
| `usb_mirror_descriptor.c` | descriptor analysis, mirrorability check | no |
| `usb_device.c` | runtime descriptors, raw IN path, SET_REPORT forward | no |
| `usb_host.c` | identity from descriptors, assignment, session lifecycle | no |
| `usb_host_hid.c` | report routing by assigned target | no |
| `usb_host_control_bridge.c` | EP0 per target | no |
| `docs/devices/*.md` | reference data, acceptance criteria | yes, not compiled |

The P4 firmware contains no VID/PID constant any more.

## Data paths

    device EP 0x8n -> hid_host raw report -> usb_host_route(addr)
        p4_fs -> usb_device_mirror_input -> TinyUSB IN (report ID 0 = bytes unchanged)
        s3    -> g815_forwarder -> SPI -> S3
        tap   -> hid_proxy (task 3)
        none  -> dropped

    PC SET_REPORT (EP0 or interrupt OUT) -> tud_hid_set_report_cb
        -> Report ID restored -> control worker
        -> usb_host_control_bridge_set_report(p4_fs) -> device EP0

## Lifecycle

1. Device enumerates on the hub. `usb_host.c` reads device, configuration
   and string descriptors, evaluates `gateway.json`.
2. Target `p4_fs`: `usb_device_mirror_begin()` validates the configuration.
   Not mirrorable -> target `tap`, reason in the log.
3. `usb_host_hid.c` delivers each report descriptor. When all HID
   interfaces are complete, the PC port connects (first time: TinyUSB
   install, later: `tud_connect`).
4. Device removed -> `tud_disconnect`, the PC sees an unplug.

## Mirrored 1:1

- Device descriptor incl. VID/PID, bcdDevice, class fields
- Complete configuration descriptor incl. bmAttributes, bMaxPower,
  endpoint addresses, wMaxPacketSize, bInterval
- Report descriptors of all HID interfaces
- Manufacturer, product, serial number (ASCII)
- Input reports byte-identical (int16 axes are no longer truncated)
- SET_REPORT output/feature (HID++ commands)

## Deviations (logged when they occur)

| Item | Behaviour |
|---|---|
| bMaxPacketSize0 != 64 | replaced by 64 |
| bNumConfigurations | 1 (active configuration only) |
| String index >= 8 | removed (index 0) |
| Non-ASCII string characters | '?' ; strings longer than 31 characters are shortened |
| GET_REPORT | STALL (synchronous request, not forwarded) |
| SET_IDLE / SET_PROTOCOL | answered by TinyUSB, not forwarded |
| Device-level vendor requests | not supported |
| Interrupt OUT data | forwarded as SET_REPORT output on EP0 |
| Non-HID interfaces, alternate settings, bulk/iso endpoints, wMaxPacketSize > 64, > CONFIG_TINYUSB_HID_COUNT HID interfaces | not mirrorable -> tap |

## Required sdkconfig entry (P4)

    CONFIG_TINYUSB_HID_COUNT=4

The build stops with `#error` if the value is below 2.

## Removed

- Fixed keyboard/mouse composite device of the P4 incl.
  `CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST` sequence.
- G815 VID/PID constants in `usb_host.c`, `usb_host_hid.c`,
  `usb_host_control_bridge.c`. The S3 path is selected by target `s3`.
