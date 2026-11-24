# USB-HID-Gateway

Transparent USB HID gateway based on an ESP32-P4 (primary) and an ESP32-S3
(secondary). Keyboard and mouse are attached to the gateway and mirrored to
the PC. In parallel their reports are decoded to drive an analog joystick
interface (DAC7578) and digital outputs. The physical joystick (Hall sensors)
is read by an ADS7128 for learning a human motion model.

Status: milestone M7 (report intercept, reset paths). M6c..M6e are part of
this state. See [Open points](#9-open-points).

---

## 1. Architecture

```
                 +---------------------------- ESP32-P4 -----------------------------+
 G815 ---+       |                                                                    |
 G502 ---+-- USB-A hub (CH334F) -- HS OTG host (FS/LS only)                           |
 (free) -+       |        |                                                           |
                 |        +-- usb_host / usb_host_hid -- route per gateway.json       |
                 |        |        |-- original -> report_tap (task 3 decoding)       |
                 |        |        |-- copy -> report_filter (M7, GENERATE only)      |
                 |        |        |      |-- p4_fs : usb_mirror (USB1P1) ------------------> PC (mouse)
                 |        |        |      |-- s3    : s3_mirror -- SPI2 link --+      |
                 |        |        |-- tap   : no PC connection              |      |
                 |        |                                                   |      |
                 |        |   analog_engine (task 3)                          |      |
                 |        |     |-- I2C: ADS7128 (ADC), DAC7578               |      |
                 |        |     |-- GPIO outputs, GENERATE LED                |      |
                 |   SD card (config, profiles, logs)   SSD1306 (SPI3)        |      |
                 +-----------------------------------------------------------|------+
                                                                              |
                 +------------- ESP32-S3 -------------------------------------+------+
                 |  SPI slave -- s3_mirror_session -- usb_mirror (TinyUSB) ---------------> PC (keyboard)
                 |  s3_ota_receiver (application update over SPI)                     |
                 +--------------------------------------------------------------------+
```

### Tasks

| Task | Function |
|---|---|
| 1 | Transparent mirror: descriptors and input reports to the PC, PC requests (SET_REPORT, EP0) back to the real device |
| 2 | Learning: ADC samples of the physical joystick feed the radial envelope and the learned motion model |
| 3 | Generation: decoded mouse/keyboard input -> trajectory generator with learned model -> DAC; keys -> GPIO |

### Data paths per input report (usb_host_hid.c)

1. The original report is queued to `report_tap` (decoded with the runtime
   report descriptor, task 3).
2. A copy passes `report_filter` (M7). Outside GENERATE it is not modified.
3. The copy goes to the mirror target `p4_fs` or `s3`.

No device-specific code exists; device identity is evaluated only in
`gateway_config.c`.

### Report intercept (M7)

While the DAC drives (GENERATE), every usage that `output.json` maps to a
stick, trigger or GPIO is removed from the mirrored report:

| Field type | Action |
|---|---|
| variable, absolute (keys, buttons, modifiers) | value -> 0 (logical minimum if 0 is outside the range) |
| variable, relative (X, Y) | value -> 0 |
| array (keyboard key slots) | mapped usages removed, remaining slots shifted forward, freed slots -> empty value |
| constant (padding) | never touched |

Everything else is byte-identical (other keys, other interfaces, vendor
reports, unknown Report IDs). Hotkeys are never removed.

Fail-open (report forwarded unchanged, counter `fail_open`):
report descriptor not parsed or truncated (> 64 input fields), report
shorter than its fields, array without representable empty value or with
more than 64 slots, report longer than 64 bytes.

Order on mode change: entering GENERATE starts filtering before the DAC
powers up; leaving GENERATE releases the DAC (high-Z confirmed) before
filtering stops. Machine and PC never follow the same input at the same
time.

Switch: `CONFIG_USB_GATEWAY_INTERCEPT_ENABLE` (default y).

### Analog engine modes

| Gateway mode | Engine mode | DAC7578 | GENERATE LED | Report filter | ADC |
|---|---|---|---|---|---|
| PASS | PASSTHROUGH | high-Z (read-back confirmed) | off | off | monitors Hall sensors |
| PASS + learn hotkey | LEARN | high-Z | off | off | samples feed the model |
| PROXY | GENERATE | drives (overrides Hall outputs, ~16 kOhm) | on | on | reads back driven nodes |

The joystick supply is permanently on and not controlled by the firmware.
GENERATE requires a valid profile and a fitted DAC; otherwise only
PASSTHROUGH is available. A DAC fault (high-Z not confirmed, write failure)
blocks GENERATE until reset.

---

## 2. Hardware

### ESP32-P4 (Waveshare ESP32-P4-Module DEV KIT, SKU 30560)

16 MB flash, 32 MB PSRAM, chip revision >= v3.1.

| Function | Pins / value |
|---|---|
| Console UART | GPIO37 / GPIO38 |
| USB host | HS OTG -> onboard CH334F hub -> USB-A 1..3, operated FS/LS-only |
| USB device (mirror p4_fs) | USB1P1 FS OTG, GPIO24/25 (Serial/JTAG moved to GPIO26/27) |
| Analog I2C | SDA GPIO33, SCL GPIO32, 1 MHz (Fm+), external pull-ups |
| SPI2 -> S3 | SCLK 6, MOSI 7, MISO 8, CS 23, 10 MHz |
| SPI3 -> SSD1306 | SCLK 20, MOSI 21, CS 5, DC 4, RESET 22, 10 MHz |
| SDMMC 1-bit | CLK 43, CMD 44, D0 39, 10 MHz, supply LDO_VO4 + GPIO45 (active low) |
| Status LEDs | busy GPIO1, success GPIO2, error GPIO3 |
| GENERATE LED | GPIO0 (polarity to be verified) |
| Digital outputs 0..11 | `CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_n_GPIO`, default unused |

### Analog module

| Device | Address | Function |
|---|---|---|
| ADS7128 | 0x10 | 8 x 12-bit ADC, all pins analog inputs (output configuration rejected by the driver) |
| DAC7578 | 0x4C | 8 x 12-bit DAC, high-Z after init; 0x47 = broadcast address |

Channel mapping (logical -> converter):

| Logical | LX | LY | RX | RY | LT | RT | L2 | R2 |
|---|---|---|---|---|---|---|---|---|
| DAC channel | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
| ADC channel | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |

Measured bus (2026-11-23): t_r(30-70 %) ~ 49 ns, t_f ~ 35 ns, V_OL = 60 mV
-> Fm+ compliant.

### ESP32-S3 (ESP32-S3FH4R2, 4 MB flash, 2 MB PSRAM)

| Function | Pins |
|---|---|
| SPI2 slave | SCLK 12, MOSI 11, MISO 13, CS 10 |
| Link IRQ | GPIO9 (reserved, unused) |
| USB device | native USB, mirror target "s3" |

---

## 3. Reset behaviour (P4)

Boot order in `usb_gateway_init()`:

1. `analog_engine_early_init()`: GENERATE LED off, digital outputs off,
   I2C bus recovery (up to 9 SCL clocks + STOP if SDA is held low),
   ADS7128 software reset (inputs), DAC7578 high-Z
2. software state, SD, configuration, profile
3. P4/S3 link (3 NOP sync, then `MIRROR_END` and new session to the S3)
4. display, USB device, USB host, analog engine (DAC high-Z confirmed by read-back)

| Component | After P4 reset | Without replug |
|---|---|---|
| DAC7578 | keeps last output until step 1 (bootloader + PSRAM test, about 1.5 s) | yes |
| ADS7128 | software reset in step 1 | yes |
| I2C bus | recovery in step 1 | yes, unless SCL is held low |
| S3 | no hardware reset; session ended and re-sent over SPI | yes, if the S3 firmware runs |
| G815 / G502 | port reset and enumeration by the host stack | yes |
| PC mirrors | disconnected and re-enumerated | yes |

`esp_restart()` (software reset, OTA): a shutdown handler releases the DAC,
GPIO outputs and the report filter before the restart. Panic, watchdog and
brownout do not run shutdown handlers.

---

## 4. Repository layout

```
CMakeLists.txt, CMakePresets.json      P4 project (presets: default, verbose, production)
sdkconfig.defaults[.verbose|.production]
reconfigure.bat                        clean reconfigure (deletes build/ and managed_components/)
main/                                  app_main: usb_gateway_init() + 10 ms process loop
components/usb_gateway/                P4 application
components/usb_mirror/                 shared with the S3: SPI protocol, descriptor analysis, TinyUSB mirror
firmware/secondary_s3/                 S3 project (uses components/usb_mirror via EXTRA_COMPONENT_DIRS)
test/                                  host tests (gcc, no ESP-IDF)
tools/usb_capture_decode.py            events.bin -> CSV
```

### Main modules (components/usb_gateway)

| Area | Files |
|---|---|
| USB host and routing | `usb_host.c`, `usb_host_hid.c`, `gateway_config.c` |
| Mirrors | `usb_device.c` (p4_fs), `s3_mirror.c` (s3), `usb_host_control_bridge.c` (EP0 to the real device) |
| P4/S3 link | `p4_s3_link.c`, `shared_spi_bus.c`, `usb_proxy_control.c`, `link_metrics.c` |
| S3 update | `s3_ota_manager.c` |
| Tap, decoding, intercept | `report_tap.c`, `hid_report_decoder.c`, `input_state.c`, `report_filter.c` |
| Analog | `analog_bus.c`, `ads7128.c`, `dac7578.c`, `analog_vector_io.c`, `analog_engine.c`, `analog_control_gpio.c` |
| Motion | `trajectory_generator.c`, `human_motion_model.c`, `learned_motion_model.c`, `kinematic_estimator.c`, `radial_envelope.c`, `analog_mapping.c` |
| Profiles | `motion_profile_*.c`, `profile_export.c`, `json_memory.c` (cJSON in PSRAM) |
| Output config | `output_config.c`, `gpio_output.c`, `hotkey.c` |
| Storage, logs | `sd_storage.c`, `gateway_paths.h`, `capture_log.c`, `usb_capture.c` |
| Display, diagnostics | `ssd1306_*.c`, `diagnostics.c`, `profile_feedback.c` |

---

## 5. Build and flash

ESP-IDF v6.x, targets `esp32p4` and `esp32s3`.

```bat
REM P4 (repository root)
reconfigure.bat
idf.py --preset default build flash monitor

REM S3
cd firmware\secondary_s3
idf.py set-target esp32s3
idf.py build flash
```

After renaming Kconfig symbols run `reconfigure.bat` (or delete
`build/default/sdkconfig`), otherwise stale values remain active.

### S3 update over SPI (no USB connection to the S3 needed)

1. Copy `secondary_s3.bin` to `/sdcard/firmware/`.
2. Optional: version text in `/sdcard/firmware/secondary_s3.version`.
3. Create trigger file `/sdcard/firmware/secondary_s3.update`.
4. Restart the P4. After confirmation the trigger is renamed to
   `secondary_s3.done`.

Only the S3 application partition is updated (rollback enabled).

---

## 6. SD card

```
/sdcard/config/gateway.json       device -> mirror target rules
/sdcard/config/output.json        HID usages -> DAC / GPIO, hotkeys, tick
/sdcard/config/profile-01.json    motion profile (created with defaults if missing)
/sdcard/motion/profile-01.bin     binary profile cache (SHA-256 bound to the JSON)
/sdcard/captures/                 USB capture sessions (device mirrored by the S3)
/sdcard/logs/                     binary event log (CAPTURE_LEVEL_SD)
/sdcard/firmware/                 S3 update image, trigger, marker
```

Missing or invalid files never stop the gateway: defaults become active and
the error is logged.

### gateway.json (schemaVersion 1)

```json
{
    "schemaVersion": 1,
    "usb": {
        "ports": [
            { "match": { "vid": "046D", "pid": "C33F" }, "target": "s3" },
            { "match": { "vid": "046D", "pid": "C08B" }, "target": "p4_fs" },
            { "match": { "class": "keyboard" },          "target": "s3" },
            { "match": { "class": "mouse" },             "target": "p4_fs" },
            { "match": { "any": true },                  "target": "none" }
        ],
        "intercept": []
    }
}
```

- First matching rule wins; `match` is exactly one of `{vid[,pid]}`,
  `{class}`, `{any:true}`.
- Targets: `p4_fs`, `s3` (one device each), `tap` (decode only), `none`.
- `intercept` is reserved and must be empty. The intercept rules of M7 are
  derived from `output.json`.
- Defaults: keyboard -> s3, mouse -> p4_fs, rest -> none.

### output.json (schemaVersion 1)

```json
{
    "schemaVersion": 1,
    "tickMs": 2,
    "sticks": {
        "left":  { "source": "keys", "up": "07:1A", "down": "07:16", "left": "07:04", "right": "07:07" },
        "right": { "source": "mouse", "gain": 0.004, "returnRate": 4.0 }
    },
    "triggers": { "LT": "09:02", "RT": "09:01" },
    "gpio": [ { "usage": "07:2C", "output": 0 } ],
    "hotkeys": {
        "mode":  { "modifiers": "05", "usage": "43" },
        "learn": { "modifiers": "05", "usage": "44" }
    }
}
```

Usage notation `PPPP:UUUU` = HID usage page : usage (hex). Unknown keys are
rejected. In GENERATE every usage listed under `sticks`, `triggers` and
`gpio` is removed from the mirrored reports; `source: "mouse"` removes
Generic Desktop X and Y (wheel and buttons stay unless mapped).

### Hotkeys (default configuration)

| Function | Keys | Source |
|---|---|---|
| Gateway mode PASS / PROXY | Ctrl + Alt + F10 | output.json `hotkeys.mode` |
| Learning start / stop | Ctrl + Alt + F11 | output.json `hotkeys.learn` |
| Profile export | Ctrl + Alt + F12 | Kconfig `PROFILE_EXPORT_*` |

---

## 7. Operation

### OLED

```
USB HID GATEWAY
MODE: PASS | PROXY | S3 OTA
PROFILE: <name> | NONE           (VERSION: <x> during S3 OTA)
STATE: READY | GENERATE | LEARNING | SAVING | FAULT <reason> | <OTA phase>
[progress bar]  nn%              (S3 OTA only)
H:OK D:OK A:OK
```

### Diagnostics

`CONFIG_USB_GATEWAY_CAPTURE_ENABLE=y` enables the periodic report
(`CAPTURE_DIAG_PERIOD_S`, default 5 s): system, USB host/device, mirrors,
P4/S3 link, I2C, task 3 incl. raw ADC codes, report filter
(`FILTER active rules inspected modified fail_open`). Attach/detach and
errors are always logged. `CAPTURE_LEVEL_SD` additionally writes
`/sdcard/logs`.

Decode a capture session:

```bat
python tools\usb_capture_decode.py <session>\events.bin
```

---

## 8. Host tests

```bash
cd test
gcc -std=c17 -Wall -Wextra -Werror -I../components/usb_gateway \
    report_filter_host_test.c ../components/usb_gateway/report_filter.c -o rf && ./rf
```

Further tests: `task3_host_test.c`, `usb_mirror_descriptor_host_test.c`,
`s3_mirror_session_host_test.c` (build lines in the file headers).

---

## 9. Open points

| ID | Topic |
|---|---|
| M7 | A key held while entering GENERATE stays pressed on the PC until the device sends its next report |
| HW | DAC7578 power-down read-back format not verified on hardware (expected 0x7FE0 for all channels high-Z) |
| HW | GENERATE LED polarity, Hall sensor supply current |
| RESET | DAC keeps its last output until step 1 of the boot order (about 1.5 s); S3 has no hardware reset line |
| DIAG | Display health line: USB host/device states are provisional |
| MIRROR | GET_REPORT from the PC is answered with STALL (counted in the statistics) |
| LINK | S3 link IRQ GPIO reserved, polling only |
