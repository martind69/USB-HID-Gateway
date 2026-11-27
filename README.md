# USB-HID-Gateway

Transparent USB HID gateway based on an ESP32-P4 primary controller and an
ESP32-S3 secondary controller. Keyboard and mouse are attached through the
P4 USB host hub and mirrored to separate PC-facing USB device ports. In
parallel, selected HID inputs are decoded and mapped to analog joystick
outputs through a DAC7578 and to configurable GPIO outputs. An ADS7128 reads
the physical analog channels for monitoring, learning, and end-to-end
measurement.

**Project status:** milestones M1 through M7 are implemented. The complete
transparent USB path, HID usage interception, analog output path, autonomous
ADC acquisition, learning infrastructure, diagnostics, S3 application update,
and direct DAC simulation are operational. The current hardware-validation
focus is the end-to-end DAC-to-ADC measurement path.

---

## 1. Architecture

```text
                 +---------------------------- ESP32-P4 -----------------------------+
 G815 ---+       |                                                                    |
 G502 ---+-- USB-A hub (CH334F) -- HS OTG host, operated FS/LS-only                  |
 (free) -+       |        |                                                           |
                 |        +-- usb_host / usb_host_hid -- route per gateway.json       |
                 |        |        |-- original -> report_tap -> task 3 decoding      |
                 |        |        |-- copy -> report_filter -> PC mirror             |
                 |        |        |      |-- p4_fs -> USB1P1 -> PC mouse             |
                 |        |        |      +-- s3 -> SPI2 -> ESP32-S3 -> PC keyboard   |
                 |        |                                                           |
                 |        +-- analog_engine                                            |
                 |              |-- ADS7128: monitoring and learning                   |
                 |              |-- DAC7578: joystick outputs                          |
                 |              |-- GPIO outputs and GENERATE LED                      |
                 |                                                                    |
                 |   SD card: configuration, profiles, logs, captures, S3 firmware    |
                 |   SSD1306: operating mode, profile, state, health, OTA progress     |
                 +--------------------------------------------------------------------+
```

### Main operating paths

1. **Transparent mirror**: HID descriptors, input reports, supported control
   requests, and device-specific reports remain separated from the task-3
   processing path.
2. **Tap and decode**: `report_tap` receives the original input report and
   decodes usages from the runtime HID report descriptor.
3. **Intercept**: in GENERATE, only usages mapped in `output.json` are
   neutralized in the PC-bound report. Unmapped fields remain byte-identical.
4. **Analog generation**: mapped mouse and keyboard axes feed trajectory and
   analog mapping before the corresponding DAC7578 channels are updated.
5. **Learning and monitoring**: ADS7128 samples the physical analog channels
   for learning, diagnostics, and DAC-to-ADC validation.

---

## 2. Hardware

### ESP32-P4 primary

Waveshare ESP32-P4-Module DEV KIT, SKU 30560:

- ESP32-P4 revision >= v3.1
- 16 MB flash
- 32 MB PSRAM
- ESP-IDF v6.x development branch

| Function | Pins / parameter |
|---|---|
| Console UART | GPIO37 / GPIO38 |
| USB host | HS OTG -> onboard CH334F hub -> USB-A ports 1..3 |
| P4 PC-facing USB mirror | USB1P1 FS OTG, GPIO24 / GPIO25 |
| USB Serial/JTAG alternate PHY | GPIO26 / GPIO27 |
| Analog bus | SDA GPIO33, SCL GPIO32, 1 MHz FM+ |
| SPI2 to S3 | SCLK 6, MOSI 7, MISO 8, CS 23, 10 MHz |
| SPI3 to SSD1306 | SCLK 20, MOSI 21, CS 5, DC 4, RESET 22 |
| SDMMC 1-bit | CLK 43, CMD 44, D0 39, 10 MHz |
| SD supply control | LDO_VO4 and GPIO45, active low |
| Status LEDs | GPIOs and polarity configurable through Kconfig |
| GENERATE LED | GPIO and polarity configurable through Kconfig |
| Digital outputs 0..11 | individually configurable through Kconfig |

### ESP32-S3 secondary

- Native USB device mirror for the keyboard
- SPI slave connection to the P4
- Application update over the P4/S3 SPI link
- No dedicated hardware-reset line required for normal operation
- Session synchronization and recovery occur over the SPI protocol

### Analog module

| Device | Address | Function |
|---|---:|---|
| ADS7128 | `0x10` | 8-channel, 12-bit ADC; autonomous scan and hardware averaging |
| DAC7578 | `0x4C` | 8-channel, 12-bit DAC |
| DAC7578 broadcast | `0x47` | write-only broadcast address |

Logical channel mapping:

| Logical channel | LX | LY | RX | RY | LT | RT | L2 | R2 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| DAC7578 | A / 0 | B / 1 | C / 2 | D / 3 | E / 4 | F / 5 | G / 6 | H / 7 |
| ADS7128 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |

Current DAC-to-ADC test wiring uses **68 kΩ series resistance**. The planned
reference input network is **100 kΩ + 4.7 nF directly at the ADC input**, with
`tau = 470 us` and `fc ≈ 339 Hz`.

---

## 3. Operating modes

| Gateway mode | Analog-engine mode | DAC7578 | Report intercept | Purpose |
|---|---|---|---|---|
| PASS | PASSTHROUGH | high impedance | off | Transparent USB operation and analog monitoring |
| PASS + learn hotkey | LEARN | high impedance | off | Acquire physical joystick motion data |
| PROXY | GENERATE | drives mapped outputs | on | Generate analog/GPIO outputs from mapped HID input |
| SIMULATE | SIMULATE | drives test waveforms | off | Direct DAC and ADC hardware validation |

### SIMULATE signals

| Input mapping represented | Logical axis | Physical DAC output | Waveform |
|---|---|---|---|
| A / D | LX | VOUTA / channel 0 | rising sawtooth, 2 Hz |
| W / S | LY | VOUTB / channel 1 | falling sawtooth, 2 Hz |
| Mouse X | RX | VOUTC / channel 2 | sine, 2 Hz |
| Mouse Y | RY | VOUTD / channel 3 | cosine, 2 Hz |

The mode hotkey cycles:

```text
PASS -> PROXY -> SIMULATE -> PASS
```

SIMULATE is build-time configurable:

```ini
CONFIG_USB_GATEWAY_SIMULATE_ENABLE=y
```

When disabled, SIMULATE is removed from the mode cycle.

---

## 4. HID routing and interception

### Device routing

`/sdcard/config/gateway.json` selects a mirror target by VID/PID, HID class,
or catch-all rule. First match wins.

Supported targets:

- `p4_fs`: P4 USB1P1 mirror
- `s3`: S3 native USB mirror
- `tap`: decode only
- `none`: do not route

### Usage mapping

`/sdcard/config/output.json` maps HID usages to logical analog axes, triggers,
GPIO outputs, and hotkeys.

For a key-controlled stick:

```json
"left": {
    "source": "keys",
    "up": "07:1A",
    "down": "07:16",
    "left": "07:04",
    "right": "07:07"
}
```

The mapping represents two axes:

- A / D form the X axis.
- W / S form the Y axis.
- Four keys therefore drive two physical DAC outputs, not four.

### Mapped-field interception

In GENERATE:

- variable absolute fields are neutralized;
- relative mouse axes are set to zero;
- mapped array usages are removed and remaining entries compacted;
- constant fields and unmapped usages remain unchanged;
- malformed, truncated, unsupported, or ambiguous reports are forwarded
  unchanged and counted as fail-open.

A key held while entering GENERATE was tested and caused no disturbance.

---

## 5. Analog acquisition and generation

### ADS7128 acquisition

The ADS7128 operates autonomously:

- all eight analog channels enabled;
- raw conversion rate: approximately 31.3 kSPS;
- hardware averaging: 4 conversions per channel;
- effective averaged update rate: approximately 978 S/s per channel;
- averaged values read through `RECENT_CHx_LSB/MSB`;
- external API remains 12 bit by using bits `[15:4]` of the 16-bit average;
- reading over the bus does not trigger a new conversion.

All ADS7128 pins are explicitly kept as analog inputs. Attempts to configure
the relevant pin-function registers as outputs are rejected by the driver.

### DAC7578 output control

- DAC code range: 0..4095
- Output channels follow the logical mapping in the profile.
- Entering GENERATE or SIMULATE preloads safe values before powering up the
  outputs.
- Leaving a driving mode sends `HIGH-Z-CMD` before report interception stops.
- The power-register readback is diagnostic only. It verifies register
  plausibility, not the electrical high-impedance state of the analog output.
- A failed HIGH-Z-CMD write or DAC data write latches a fault and blocks further
  generation until reset.

### Measurement metrics

During SIMULATE, the firmware can compare the ADC result against the commanded
DAC code for VOUTA..VOUTD:

- **Accuracy**: MSE and RMSE of `ADC - DAC reference`;
- **Trueness**: mean error (bias);
- **Precision**: pooled within-phase standard deviation across repeated 2-Hz
  periods.

The commanded DAC code is the internal reference. The resulting metrics
therefore characterize the complete DAC -> wiring/RC -> ADS7128 path, but do
not constitute traceable absolute-voltage calibration without an independent
calibrated voltage reference.

---

## 6. Reset and recovery

Initialization is deliberately ordered so that the analog path reaches a safe
state before storage, profiles, display, and USB are initialized:

1. GENERATE LED and digital outputs off.
2. Analog-bus recovery using up to nine SCL pulses followed by STOP.
3. ADS7128 reset and analog-input verification.
4. DAC7578 HIGH-Z-CMD.
5. Storage, configuration, profile, P4/S3 link, display, and USB startup.
6. Analog engine starts and confirms the operational state.

Additional behavior:

- `esp_restart()` invokes a shutdown handler that releases DAC and GPIO outputs
  and disables report interception.
- USB devices re-enumerate after P4 reset without requiring replugging.
- The S3 resumes through SPI session synchronization without a hardware-reset
  line.
- External I2C devices are deterministically reinitialized.

---

## 7. SD card and configuration

```text
/sdcard/config/gateway.json       USB routing rules
/sdcard/config/output.json        HID-to-axis/GPIO mapping and hotkeys
/sdcard/config/profile-01.json    human-motion and analog profile
/sdcard/motion/profile-01.bin     profile cache
/sdcard/captures/                 USB capture sessions
/sdcard/logs/                     binary event logs
/sdcard/firmware/                 S3 OTA image and markers
```

Missing or invalid configuration files do not stop the transparent gateway.
Defaults remain active and the condition is logged.

### Default hotkeys

| Function | Keys |
|---|---|
| Mode cycle | Ctrl + Alt + F10 |
| Learning start / stop | Ctrl + Alt + F11 |
| Profile export | Ctrl + Alt + F12 |

---

## 8. Build, flash, and update

### ESP32-P4

```bat
reconfigure.bat
idf.py --preset default build flash monitor
```

### ESP32-S3

```bat
cd firmware\secondary_s3
idf.py set-target esp32s3
idf.py build flash
```

### S3 application update over SPI

1. Copy `secondary_s3.bin` to `/sdcard/firmware/`.
2. Optionally provide `secondary_s3.version`.
3. Create `/sdcard/firmware/secondary_s3.update`.
4. Restart the P4.
5. After successful confirmation, the trigger becomes
   `secondary_s3.done`.

Only the S3 application partition is updated. Bootloader and partition table
remain unchanged.

After Kconfig changes, delete stale build configuration through
`reconfigure.bat` or perform a full clean/reconfigure.

---

## 9. Milestones and verified achievements

| Milestone | Result | Status |
|---|---|---|
| M1 | ESP32-P4 and ESP32-S3 project foundations, independent build targets | Complete |
| M2 | P4/S3 SPI transport, framing, status correlation, recovery, and diagnostics | Complete |
| M3 | Transparent keyboard mirror through the S3, including G815 operation | Complete |
| M4 | P4 USB mirror and composite HID handling for the G502 path | Complete |
| M5 | HID report tap, runtime descriptor parsing, routing, and fail-open behavior | Complete |
| M6 | ADC/DAC/GPIO task-3 infrastructure, profiles, learning pipeline, diagnostics, and SD persistence | Complete |
| M7 | Mapped-usage interception, early analog safe-state initialization, I2C recovery, and reset-path hardening | Complete |
| M7c | Direct 2-Hz SIMULATE waveforms on VOUTA..VOUTD | Hardware verified |
| M7d | End-to-end Accuracy, Trueness, and Precision measurement infrastructure | Implemented |

### Verified project achievements

- Both P4 and S3 projects build cleanly.
- Logitech G815 keyboard passthrough operates correctly.
- Logitech G HUB works through the mirrored keyboard path.
- Logitech G502 mirror path and HID interface handling are implemented.
- USB descriptors and input reports are handled independently of task-3
  decoding.
- Mode hotkey works and the SSD1306 display follows PASS, PROXY, SIMULATE,
  learning, save, fault, and OTA states.
- DAC7578 direct output was verified with an oscilloscope:
  - sine approximately 2.000 Hz;
  - sawtooth approximately 1.998 Hz;
  - approximately 3.3 V peak-to-peak.
- S3 continues operation and re-synchronizes without a dedicated hardware
  reset line.
- Status and GENERATE LEDs are configurable in Kconfig.
- Holding a mapped key while entering GENERATE was tested without disturbance.
- HIGH-Z readback has been correctly classified as an advisory register check,
  not proof of the physical output impedance.
- Fail-open report handling and mapped-only neutralization are implemented.

---

## 10. Completed design decisions

The former open-point list has been closed. The final decisions are:

| Former topic | Resolution |
|---|---|
| Held key on entry to GENERATE | Tested; no disturbance observed |
| DAC7578 power-down readback | Diagnostic plausibility check only; successful HIGH-Z-CMD write is decisive |
| LED polarity and assignment | GPIOs and active level are configurable through Kconfig |
| S3 hardware reset line | Not required; SPI session synchronization is sufficient in operation |
| DAC state during P4 startup | Analog safe-state initialization executes first after `app_main()`; software restart uses a shutdown handler |
| Display health status | Integrated into the operating display and diagnostics path |
| GET_REPORT behavior | Unsupported requests are intentionally stalled and counted; accepted as defined mirror behavior |
| P4/S3 link IRQ | Polling implementation retained and accepted; reserved IRQ line is not required |

There are currently **no unresolved architectural open points**. Further work
is validation and characterization: ADC/DAC end-to-end measurement, learning
runs with real joystick motion, and final profile tuning.

---

## 11. Repository layout

```text
CMakeLists.txt
CMakePresets.json
sdkconfig.defaults
sdkconfig.defaults.verbose
sdkconfig.defaults.production
reconfigure.bat
main/
components/usb_gateway/
components/usb_mirror/
firmware/secondary_s3/
sdcard/config/
test/
tools/
```

Important modules:

| Area | Modules |
|---|---|
| USB routing and host | `usb_host.c`, `usb_host_hid.c`, `gateway_config.c` |
| PC-facing mirrors | `usb_device.c`, `s3_mirror.c`, `usb_host_control_bridge.c` |
| P4/S3 link | `p4_s3_link.c`, `shared_spi_bus.c`, `usb_proxy_control.c` |
| HID decode and intercept | `hid_report_decoder.c`, `input_state.c`, `report_tap.c`, `report_filter.c` |
| Analog I/O | `analog_bus.c`, `ads7128.c`, `dac7578.c`, `analog_vector_io.c`, `analog_engine.c` |
| Motion model | `trajectory_generator.c`, `human_motion_model.c`, `learned_motion_model.c`, `kinematic_estimator.c`, `radial_envelope.c` |
| Configuration and profiles | `output_config.c`, `motion_profile_*.c`, `profile_export.c`, `json_memory.c` |
| Display and diagnostics | `ssd1306_*.c`, `diagnostics.c`, `profile_feedback.c` |
| Storage and capture | `sd_storage.c`, `capture_log.c`, `usb_capture.c` |

---

## 12. Diagnostics

With capture diagnostics enabled, the monitor periodically reports:

- system heap and uptime;
- USB host/device and mirror state;
- connected HID devices and report descriptors;
- P4/S3 transport metrics;
- I2C device status;
- analog-engine mode, DAC state, faults, ADC codes, and learning samples;
- report-filter rules, modified reports, and fail-open count;
- SIMULATE measurement metrics for LX, LY, RX, and RY.

Binary event logging to SD is optionally enabled through Kconfig. Capture files
can be decoded using:

```bat
python tools\usb_capture_decode.py <session>\events.bin
```
