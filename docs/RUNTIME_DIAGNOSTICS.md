# Runtime Diagnostics

## Current scope

The diagnostics component provides a central, bounded status registry and a
five-second serial summary. It is deliberately honest about incomplete
subsystems: a component remains `not-initialized` until its real driver reports
a state. No USB enumeration or I2C probe is fabricated.

## Serial output

Use the ESP-IDF monitor:

```powershell
idf.py -p COMx monitor
```

Log tag: `gateway_diag`.

The summary includes uptime, free/minimum heap, host/device controller state,
connected host-device count, device descriptors, proxy mode and counters,
I2C device states, and HID/I2C error totals.

## Feeding real hardware data

- USB host class-driver callbacks must call
  `diagnostics_host_device_added()` and
  `diagnostics_host_device_removed()`.
- Populate manufacturer/product from USB string descriptors, not VID/PID
  lookup guesses. Populate interface and HID counts after configuration parsing.
- HID enumeration code calls `diagnostics_report_hid_enum()` after obtaining
  the report descriptor. Descriptor length is not the same as report length.
- Proxy routing calls the forwarded/processed/error counters at the actual
  routing decision point.
- I2C driver initialization and every failed transaction report state and
  error code with `diagnostics_set_i2c_state()` or
  `diagnostics_report_i2c_error()`. Do not mark a device ready based only on
  configured address; require a successful transaction/identity check where
  supported.

## Current limitations

The USB host/device, HID parser, DAC and GPIO modules are still stubs. Thus
host enumeration, real USB strings, proxy traffic counters, I2C ACK/NACK,
transaction timing, and peripheral health will remain unavailable until those
drivers are implemented and connected to this API. The registry is currently
designed for calls from the gateway task; add synchronization before invoking
it concurrently from USB callbacks or multiple FreeRTOS tasks.
