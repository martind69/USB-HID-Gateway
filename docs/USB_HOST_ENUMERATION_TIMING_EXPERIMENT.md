# USB Host enumeration timing experiment

Date: 2026-11-15

## Purpose

The CH334F downstream-port tests now show intermittent failure of the initial EP0 control transfer, followed by successful enumeration of the same Full-Speed device after a brief data-contact interruption/re-detection. The user also observed that VBUS is already present before D+/D- make contact during slow insertion; merely having VBUS lead the data contacts does not prevent the failure.

This supports testing the timing around the Host enumeration state machine and first EP0 request. It does not prove that the device needs more settling time or identify the electrical cause.

## Change

The experiment inserts a configurable delay before the USB Host enumeration state machine advances to its next stage:

`enum_process()` in the managed `espressif/usb` component, immediately before `set_next_stage()`.

- Initial value: **10 ms**
- Kconfig symbol: `CONFIG_USB_GATEWAY_ENUMERATION_STAGE_DELAY_MS`
- Range: 0–100 ms
- Set to 0 to disable the delay.
- The delay is applied between enumeration stages, not only before the first `GET_DESCRIPTOR(Device)` request. It therefore adds latency to enumeration generally.
- No changes are made to reset hold, reset recovery, debounce, PHY selection, `fsls_only`, or hub configuration.

The project-level Kconfig option is consumed by a small source patch because the upstream USB component does not expose this experimental delay as a Kconfig setting.

## Apply the patch

Run these commands from the repository root in the ESP-IDF PowerShell environment:

```powershell
idf.py -B build/default reconfigure
.\scripts\apply_usb_enum_delay.ps1
idf.py -B build/default build
```

The script patches:

`managed_components/espressif__usb/src/enum.c`

It is idempotent and checks for one unique insertion point before changing the file. If the managed component is reinstalled or updated, run the script again. The patch is intentionally kept out of the upstream component package and is tracked in this repository as a reproducible script.

For an A/B baseline, set `CONFIG_USB_GATEWAY_ENUMERATION_STAGE_DELAY_MS=0` in `build/default/sdkconfig`, reconfigure, apply the script, and build. For the first experiment, set it to 10 and rebuild. The default for a fresh configuration is 10 ms.

## Controlled test

Use the same device `8564:1000`, the same CH334F downstream port (Port3 if retaining the previous test setup), the same cable/fixture and the already-confirmed board selector setting.

1. Baseline: delay = 0 ms; perform at least 10 complete insertions and record success/failure for each.
2. Experiment: delay = 10 ms; repeat at least 10 insertions under the same conditions.
3. Do not change any other timing or hardware setting between the two runs.
4. Record full UART output, especially the first `CHECK_SHORT_DEV_DESC` result, port disable/removal, and successful enumeration lines.
5. If 10 ms does not materially improve the result, repeat with 15 ms. Do not jump directly to large reset-recovery values.

Compare the failure rate, not just whether one insertion succeeds. If the 10 ms delay improves reliability, repeat the test with delay = 0 once more to rule out a coincidental improvement.

## Interpretation and limitations

A similar ESP-IDF report (IDFGH-16845) found that a 15 ms delay between enumeration state-machine stages resolved an intermittent descriptor-transfer failure on another device. That report involved `GET_FULL_CONFIG_DESC`, not this project's initial short Device Descriptor transfer. It is supporting precedent for a timing experiment, not proof of the same root cause.

A positive result would show sensitivity to enumeration pacing. It would not, by itself, distinguish device startup time, hub port state timing, signal integrity, or Host/HUB scheduling. A negative result would not rule out timing at the more specific reset-completion-to-first-EP0 interval.

If this broad state-machine delay changes the failure rate, the next refinement should instrument timestamps for hub connect/change, reset start/completion, port enable, and the first `GET_DESCRIPTOR(Device)` SETUP. Then replace the broad delay with a narrowly placed delay at the proven transition.

## References

- ESP-IDF issue IDFGH-16845: https://github.com/espressif/esp-idf/issues/17918
- USB Host component v1.6.1 changelog: https://components.espressif.com/components/espressif/usb/versions/1.6.1/changelog
