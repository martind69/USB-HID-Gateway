# USB HID Gateway

ESP32-P4-based USB HID gateway with a PC-facing USB Device connection and a peripheral-facing USB Host connection.

## Project objective

Provide a descriptor-driven HID gateway for the observed Logitech G815 DE keyboard (`046D:C33F`) and Logitech G502 HERO mouse (`046D:C08B`). The gateway supports two modes:

- **Passthrough** (default): forward supported input reports to the PC without semantic modification.
- **Proxy**: apply JSON-configured input mappings. Inputs without an active mapping continue to the PC normally.

The G815 **G1 key** is the global mode toggle. A rising edge switches between Passthrough and Proxy. The G1 release does not switch the mode again. G1 is reserved for mode control and is not part of the ordinary mapping engine.

The gateway is not a conventional USB hub or a guaranteed transaction-level USB repeater. HID report forwarding, USB control-transfer handling, device identity and Logitech G HUB compatibility are separate engineering concerns.

## Hardware topology

The observed setup contains an ASUS ROG monitor with a four-port Genesys Logic hub (`05E3:0610`). The G815 and G502 appear as sibling devices below this hub. The G502 is physically connected through the G815 USB pass-through cable; that pass-through is a cable path, not an additional hub.

## Architecture

- USB transport and generic HID descriptor/report parsing
- HID semantic layer and report router
- Gateway mode controller and G1 feedback abstraction
- JSON-configured input mapping
- Two-dimensional motion estimation, learning and trajectory generation
- ADS7128 ADC and DAC7578 DAC backends behind AnalogVectorIO
- Twelve logical digital outputs
- Versioned configuration, learned-model persistence and backup/restore

## Current implementation status

The repository is primarily an architecture and evidence baseline. USB Host/Device transport, the generic parser, mapping, motion algorithms and physical output drivers remain planned or skeletal unless explicitly marked otherwise in their module documentation. No successful ESP32-P4 firmware build or end-to-end hardware validation is claimed.

Runtime diagnostics now provide a five-second serial summary, a bounded USB-device registry, proxy counters, I2C health reporting hooks, heap and uptime information. Real USB enumeration, HID parsing, proxy traffic and I2C health remain unavailable until their hardware drivers are implemented and connected to the diagnostic API. See [Runtime diagnostics](docs/RUNTIME_DIAGNOSTICS.md).

## Documentation

### Architecture and requirements
- [Project requirements](docs/PROJECT_REQUIREMENTS.md)
- [Module architecture](docs/ARCHITECTURE.md)
- [Gateway mode and report routing](docs/GATEWAY_MODE_DESIGN.md)
- [Input mapping](docs/INPUT_MAPPING_DESIGN.md)
- [Motion model and trajectory generation](docs/MOTION_MODEL_DESIGN.md)
- [Analog I/O](docs/ANALOG_IO_DESIGN.md)
- [Configuration formats](docs/CONFIGURATION_FORMATS.md)
- [Test strategy](docs/TEST_STRATEGY.md)
- [Implementation plan](docs/IMPLEMENTATION_PLAN.md)
- [Runtime diagnostics](docs/RUNTIME_DIAGNOSTICS.md)
- [Review of external implementation proposal](docs/IMPLEMENTATION_PROPOSAL_REVIEW.md)
- [Implementation status](docs/IMPLEMENTATION_STATUS.md)
- [Input mapping and report-routing interface](docs/INPUT_MAPPING_ROUTING.md)

### USB/HID evidence and design
- [USB topology](docs/USB_TOPOLOGY.md)
- [USB device enumeration](docs/hardware/USB_DEVICE_ENUMERATION.md)
- [Universal HID parser design](docs/HID_PARSER_DESIGN.md)
- [G815 RGB and profile control](docs/hardware/LOGITECH_G815_RGB_PROTOCOL.md)
- [G502 DPI and lighting control](docs/hardware/LOGITECH_G502_PROTOCOL.md)

The device-specific protocol documents record observed traffic and confidence levels. They are not complete vendor protocol specifications and do not establish full G HUB compatibility.
