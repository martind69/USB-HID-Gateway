# USB HID Gateway

ESP32-P4-based USB HID gateway with a PC-facing USB Device connection and a downstream USB Host connection.

## Project goal

Provide transparent HID passthrough in normal operation and selective report modification in hotkey mode. The initial target setup is a Logitech G810 keyboard with a Logitech G502 mouse connected through the keyboard's integrated USB hub.

The firmware is currently an architectural skeleton. USB transport, HID parsing, report forwarding and output drivers are not yet implemented.

## Documentation

- [Project requirements](docs/PROJECT_REQUIREMENTS.md) – functional requirements, target hardware and open decisions.
- [USB topology](docs/USB_TOPOLOGY.md) – physical connection and controller responsibilities.
- [Implementation plan](docs/IMPLEMENTATION_PLAN.md) – staged development and verification gates.
- [Module architecture](docs/ARCHITECTURE.md) – software modules and data flow.
- [Universal HID parser design](docs/HID_PARSER_DESIGN.md) – descriptor parser requirements and proposed design.

## Current status

- ESP-IDF project skeleton and ESP32-P4 target configuration exist.
- Software modules are separated into Host, Device, parser, proxy, hotkey, mapping and output modules.
- HID parser architecture is documented, but the parser source is still a placeholder.
- USB Host and Device initialization and data transfer are not implemented.
- No successful firmware build or hardware validation is claimed.

See [Implementation plan](docs/IMPLEMENTATION_PLAN.md) for the next steps.
