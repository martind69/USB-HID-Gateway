# M6 - Report tap, analog output (task 3) and learning (task 2)

## Data flow

    HID input report (any mirrored or tap device)
        -> mirror path (unchanged, not delayed)
        \-> report_tap queue (copy) -> hid_report_decoder (runtime descriptor)
              -> input_state (pressed usages, mouse dx/dy/wheel)
              -> hotkeys, profile export key

    analog_engine task (tickMs, sole owner of ADC/DAC/GPIO)
        PASSTHROUGH  ADC -> DAC per channel
        LEARN        ADC -> DAC + learning
        GENERATE     input_state -> target -> trajectory (learned model) -> envelope -> DAC
                     buttons/keys -> triggers (DAC), GPIO

## Modes

| Mode | Entered by | DAC | GPIO |
|---|---|---|---|
| PASSTHROUGH | gateway mode PASS (default) | ADC code per channel; read error -> safe code | off |
| GENERATE | gateway mode PROXY (hotkey `mode`) | generated | per output.json |
| LEARN | hotkey `learn` (toggle) | ADC code (as PASSTHROUGH) | off |

Without a valid profile (no SD, invalid JSON) only PASSTHROUGH is available.
Without ADC/DAC hardware the engine runs but writes nothing.

## Configuration: /sdcard/config/output.json

See header of `output_config.h`. Example in `sdcard/config/output.json`:

| Binding | Example |
|---|---|
| left stick | W/A/S/D (keyboard page 0x07: 1A/04/16/07) |
| right stick | mouse, gain 0.004 per count, return rate 4 /s |
| LT / RT | right / left mouse button (page 0x09: 02 / 01) |
| GPIO 0 | space bar (07:2C) |
| hotkey mode | Ctrl+Alt+F10 (modifiers 05, usage 43) |
| hotkey learn | Ctrl+Alt+F11 (modifiers 05, usage 44) |
| profile export | Ctrl+Alt+F12 (Kconfig, unchanged) |

Usages are HID usages, so the same file works with any keyboard/mouse.

## Learning (task 2)

1. Gateway in PASS mode, physical joystick connected to the ADC.
2. Ctrl+Alt+F11: LEARN starts. Learned model and envelopes are reset; the
   previous profile is kept as backup. Status LED "busy".
3. Move both sticks over the full range with typical speeds.
4. Ctrl+Alt+F11 again: stop.
   - fewer than 500 samples (approx. 1 s at 2 ms): backup restored.
   - otherwise envelopes are written to the profile and
     /sdcard/config/profile-01.json is saved in a background task
     (LED: writing -> success/error). The binary cache is rebuilt on next boot.

## Diagnostics (CAPTURE_ENABLE)

    usb_gateway: TASK3 mode=.. ticks=.. overrun=.. adc_err=.. dac_err=.. learned=.. saving=. save=..
                 tap_in=.. tap_ok=.. tap_drop=.. tap_undecoded=..

## Safety notes (lab setup)

- From M6 on the DAC is actively driven from boot (PASSTHROUGH). Check the
  `safeCode` values in the profile before connecting a machine.
- Sensor power (`analog_sensor_power_set(true)`) is switched on when the
  engine starts.
- Hotkeys are not intercepted yet (M5): the PC also receives Ctrl+Alt+F10/F11.
- No timeout: in GENERATE a held key keeps the stick deflected until released
  or the device is removed (removal releases all usages).

## Acceptance test

| # | Step | Expected |
|---|---|---|
| A1 | Boot with output.json | `output.json loaded`, `Analog engine started ... profile=yes`, TASK3 mode=PASSTHROUGH |
| A2 | Move joystick in PASS | DAC follows ADC (scope / multimeter on DAC outputs) |
| A3 | Type on G815, move G502 | `tap_ok` increases, `tap_undecoded=0`, `tap_drop=0` |
| A4 | Ctrl+Alt+F10 | mode=GENERATE; W/A/S/D deflect left stick with human ramp; mouse moves right stick and returns |
| A5 | Mouse buttons / space | LT/RT codes switch min<->max; GPIO 0 follows space |
| A6 | Ctrl+Alt+F10 | PASSTHROUGH, GPIO off |
| A7 | Learning as described | `learned` grows during LEARN; after stop `save=ESP_OK`, profile file updated |
| A8 | Reboot, GENERATE again | ramps differ according to the learned model |
