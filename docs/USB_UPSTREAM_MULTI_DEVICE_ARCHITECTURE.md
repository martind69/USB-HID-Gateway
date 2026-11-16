# Two-device upstream architecture

## Binding requirement

The PC must enumerate the Logitech G815 and Logitech G502 as two separate USB devices. Each must retain its own downstream Device Descriptor identity (VID/PID), configuration, interfaces, endpoints and device-recipient control-request context. A single composite USB device is not acceptable.

This is a confirmed project requirement, not an open architecture choice.

## Why the current single-device proxy cannot meet it

A USB Device controller represents one USB device address on one upstream USB bus. A composite configuration can contain multiple interfaces, but it is still one device address and one Device Descriptor. It cannot create two independent child device addresses or two independent VID/PID identities.

The current ESP32-P4 design uses:
- One OTG controller in Host mode for the downstream Logitech topology.
- One OTG controller in Device mode toward the PC.

The ESP32-P4 has one High-Speed and one Full-Speed OTG controller. Both support Host or Device operation in hardware. The current firmware uses one in each role. The USB Serial/JTAG controller is dedicated to serial/JTAG/download functionality and is not a third general-purpose TinyUSB Device controller.

Espressif's current USB Device Stack documentation describes one High-Speed and one Full-Speed Device peripheral, but does not document two independent simultaneous TinyUSB Device instances. A dual-device implementation on one P4 must therefore not be assumed to work merely because the silicon contains two OTG controllers. See the [ESP32-P4 USB Device Stack documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/usb_device.html) and [ESP32-P4 datasheet](https://documentation.espressif.com/esp32-p4_datasheet_en.html).

## Candidate architecture: preserve current P4 roles and add one upstream Device node

Preferred direction for investigation:

~~~text
                         PC (one USB cable)
                                |
                       [PC-facing USB hub]
                         /             \
                        /               \
              [P4 USB Device]     [Secondary USB Device MCU]
                     |                       |
                 SPI / IPC              SPI / IPC
                     \                       /
                      \                     /
                       [ESP32-P4 Gateway]
                                |
                         USB Host controller
                                |
                    [Existing downstream hub path]
                                |
                       Logitech G815 + G502
~~~

The diagram is logical: the P4 remains the main gateway and owns the downstream Host stack. Its existing Device controller proxies one Logitech device. A second MCU or dedicated Device controller proxies the other. Both Device-side controllers connect to separate downstream ports of a new PC-facing USB hub. The hub's upstream port connects to the PC.

The PC then sees:
- One physical hub introduced by the gateway.
- Two separate child USB devices, each with its own address and original VID/PID.

This preserves the requested two-device identity while retaining the existing P4 Host + Device controller allocation. It requires additional hardware: a PC-facing hub and a second independently programmable USB Device controller. The current U14 CH334F is in the downstream path toward the P4 Host; it must not be assumed reusable as the PC-facing hub without a schematic/routing review.

## Data and control ownership

The P4 remains the sole owner of the downstream Host bus and the source of truth for both real Logitech devices.

For each proxied device, the upstream Device endpoint must have a stable device-slot association:
- Device slot / downstream Host device handle.
- Original Device Descriptor and active Configuration descriptor.
- Per-device interface and endpoint map.
- Per-device EP0 control-transfer state.
- Per-endpoint transfer queues and in-flight state.
- Disconnect, reset and re-enumeration lifecycle.

The secondary Device MCU must not independently enumerate the physical Logitech devices. It receives proxy transactions and report payloads from the P4 over a framed inter-processor link (SPI is the first candidate). The link must support asynchronous request/response correlation, bounded queues, transfer cancellation, backpressure and disconnect notification. Raw USB payloads must not be semantically rewritten in this transport.

For an upstream control request received by either Device-side controller:
1. Preserve the complete SETUP packet and data stage.
2. Identify the associated downstream device slot.
3. Submit the corresponding asynchronous Host control transfer on the P4.
4. Return the downstream result to the originating upstream Device controller.
5. Keep EP0 transaction state independent for G815 and G502.

For interrupt endpoints, the P4 Host owns downstream transfers and forwards raw payloads to the correct upstream Device controller. The P4 can continue to observe copied HID reports for mapping without modifying the forwarded bytes.

## USB topology and G HUB caveat

Separate VID/PID identities are necessary but may not be sufficient for complete G HUB compatibility.

The current USB topology analysis identifies the G815 and G502 as sibling devices behind the Genesys Logic hub (VID/PID 05E3:0610) integrated into the ASUS monitor. The G502 connection through the G815 pass-through is a cable-only path; it is not an additional USB hub. Do not model the G502 as a child of a G815-internal hub.

A PC-facing gateway hub with the two proxy Device controllers on separate downstream ports would also expose the G815 and G502 as sibling devices. It would, however, add a new hub node and change the exact upstream parent/port path compared with the current monitor-hub topology.

Before claiming full G HUB compatibility, capture and compare:
- The G815 Device Descriptor and its interface/configuration tree.
- The monitor's Genesys Logic hub identity and the observed port relationship for both devices.
- The G502 Device Descriptor and its original parent/port relationship.
- G HUB's vendor control transfers, recipient type, wIndex, interface/endpoint usage and device selection behavior.

The available topology evidence does not justify emulating a G815-internal hub. The remaining question is whether G HUB depends on the monitor hub's identity or physical port path, or only on the two independent Logitech device identities and their control/report behavior.

## Available descriptor summary

The prior capture analysis contains the following summarized endpoint information, but the original USBPcap files and complete raw descriptor dumps are not available in the repository:

| Device | VID:PID | Speed | Interface summary | Reported Interrupt IN endpoints |
|---|---|---|---|---|
| Logitech G815 DE | 046D:C33F | Full-Speed | 2 HID interfaces | IF0: 0x81, 8 bytes; IF1: 0x82, 64 bytes |
| Logitech G502 HERO | 046D:C08B | Full-Speed | 2 HID interfaces | IF0: 0x81, 8 bytes; IF1: 0x82, 20 bytes |

Both reported endpoint intervals are 1 ms. This summary is useful for preliminary sizing only. It does not prove that the configurations contain no additional endpoints, alternate settings or class/vendor-specific descriptor requirements. The complete Device, Configuration, Interface, HID and Endpoint descriptor byte streams are still required before controller allocation can be finalized.

## Controller allocation and endpoint audit

Do not select the secondary MCU or assign G815/G502 to the P4 versus the secondary controller until the captured descriptors have been inventoried.

Required audit per device:
- USB speed and bMaxPacketSize0.
- Number of interfaces and alternate settings.
- Number of IN and OUT endpoints by transfer type.
- Maximum packet size and interval per endpoint.
- HID report descriptor lengths and report IDs.
- Standard, HID class and vendor-specific control request sequences.
- Whether any endpoint or request requires High-Speed operation.

The P4's currently used PC-facing Device controller is Full-Speed. The upstream proxy therefore cannot advertise High-Speed operation through that port. The selected secondary controller must have enough endpoint resources for its assigned device. Its firmware stack must support a genuinely independent Device instance.

## Alternatives and trade-offs

### A. Add a second Device MCU plus a PC-facing hub

- One PC cable.
- Two separate device addresses and original VID/PID values.
- Retains the current P4 Host + Device allocation.
- Requires inter-MCU transport and additional PCB area/power.
- Hub topology may differ from the original G815 internal hub arrangement.

This is the preferred candidate for feasibility work.

### B. Two separate PC cables

Use two independent Device controllers, each connected directly to a separate PC USB port. This avoids the PC-facing hub, but the P4 still needs a downstream Host controller for the physical Logitech devices. It therefore also requires additional USB hardware and is not the preferred one-cable product topology.

### C. One composite Device

Rejected. It cannot satisfy the confirmed requirement for two separate USB device identities.

## Implementation gate

Do not continue implementing the single-composite upstream descriptor merger or interface-only vendor request routing.

Next engineering steps:
1. Extract and inventory the actual G815, G815 hub and G502 descriptors from the existing USBPcap captures.
2. Determine whether G HUB requires the original hub hierarchy, not just separate VID/PID identities.
3. Audit the P4's current USB Device endpoint budget against each device descriptor.
4. Evaluate candidate secondary USB Device MCUs/stacks and a PC-facing hub controller.
5. Define the inter-MCU transport protocol and ownership boundaries.
6. Only then resume descriptor generation, endpoint forwarding and EP0 bridge implementation.

No firmware build, hardware modification or G HUB compatibility test is claimed by this architecture note.
