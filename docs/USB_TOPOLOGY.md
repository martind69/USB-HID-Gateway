# USB HID Gateway – USB Topology and Routing Context

Status: Peripheral topology documented from Windows USB tree capture and physical clarification. ESP32-P4 board routing remains open.

## 1. Physical peripheral topology

The ASUS ROG monitor contains a four-port Genesys Logic USB hub (`05E3:0610`). The G815 and G502 appear as sibling devices below this hub. The G502 is physically connected through the G815 USB pass-through cable; this is a direct cable path, not an additional hub.

| Monitor hub port | Device | VID:PID | Observed port chain |
|---|---|---|---|
| 1 | ASUS ROG Gaming Display Aura | `0B05:18DA` | `2-7-1` |
| 2 | Empty | – | `2-7-2` |
| 3 | Logitech G502 HERO | `046D:C08B` | `2-7-3` |
| 4 | Logitech G815 DE | `046D:C33F` | `2-7-4` |

The monitor hub is High-Speed, self-powered and Multi-TT. The observed Logitech devices operate at Full-Speed (12 Mbit/s) with 1 ms polling. The ASUS Aura function is a separate vendor-defined HID device and is outside the initial keyboard/mouse input path.

## 2. USB roles

### PC-facing USB Device

- Enumerate to the PC using a compatible keyboard/mouse HID interface strategy.
- Expose report descriptors and endpoint behavior consistent with the reports delivered.
- Handle required standard, HID class and supported vendor-specific control requests.
- Send unchanged or selectively modified reports according to gateway routing policy.

### Peripheral-facing USB Host

- Enumerate the Genesys Logic hub and its downstream devices.
- Discover all configurations, interfaces, endpoints and report descriptors.
- Maintain independent per-device/per-interface state and lifecycle.
- Receive G815/G502 reports and preserve original report context.
- Isolate the ASUS Aura function from the Logitech keyboard/mouse input route.

## 3. Controller separation

Host and Device roles require independent controller instances. Simultaneous operation, connector routing, PHY, VBUS sourcing/protection and board pin assignment must be verified against the exact ESP32-P4 board revision and ESP-IDF version. Do not infer board support from the MCU name alone.

## 4. Routing semantics

### Passthrough

Supported keyboard and mouse Input reports are forwarded unchanged.

### Proxy

Routing is dynamic and configuration-driven:

1. The G1 key is intercepted by GatewayModeController. Its rising edge toggles Passthrough/Proxy; release does not toggle.
2. G1 is reserved for mode control and excluded from normal mapping by default.
3. For any other semantic input event, an enabled JSON mapping match means the event is consumed by the gateway and does not reach the PC as its ordinary input.
4. An event with no active mapping continues to the PC normally and unchanged.
5. A report containing multiple fields/events must be routed without corrupting unrelated or unmapped content. Field-level modification/reconstruction requirements must be tested against actual descriptors and PC-facing reports.

The initial policy is **consume if mapped; otherwise forward**. Configuration is authoritative; there is no hard-coded list of all keys/buttons to suppress.

## 5. Mouse report handling

The G502 reports signed relative 16-bit X/Y according to Windows HID capability data. The implementation must identify X/Y through the parsed descriptor and must not hard-code byte offsets. Buttons, wheel, AC Pan, vendor fields and unknown bits remain intact unless individually mapped by configuration.

## 6. USB transparency boundary

Generic HID Input report forwarding does not provide complete USB transaction transparency. Device identity, interface topology, standard requests, HID class requests, Feature reports, vendor-specific transfers and hub lifecycle must be handled separately. G HUB compatibility remains unverified until tested end-to-end through the gateway.

## 7. Verification checklist

- [x] Monitor Genesys Logic hub and four-port topology identified.
- [x] G815 and G502 identified as sibling devices below the monitor hub.
- [x] G815 pass-through established as a cable path, not a hub.
- [x] ASUS Aura HID isolated from Logitech input path.
- [ ] Exact ESP32-P4 board/revision and concurrent Host/Device routing verified.
- [ ] Raw report descriptors captured for all required interfaces.
- [ ] G1 Input encoding and LED feedback command captured and decoded.
- [ ] PC-facing composite descriptor strategy validated.
- [ ] Mapped-event consumption and unmapped-event passthrough tested.
- [ ] G HUB vendor/control/Feature traffic validated end-to-end.
