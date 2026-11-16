# CH552 Boot, UART ISP and SPI Runtime Sequence

Status: architecture decision / implementation specification  
Target: Adafruit QT Py CH552, controlled by ESP32-P4  
Runtime role: independent Logitech USB Device emulator  
Programming role: factory ROM bootloader via UART1

## 1. Fixed pin allocation

| CH552 pin | UART ISP | Application runtime |
|---|---|---|
| P1.4 | Inactive / pulled high | SPI0 SCS, active low |
| P1.5 | GPIO / unused | SPI0 MOSI |
| P1.6 | UART1 RXD1 | SPI0 MISO |
| P1.7 | UART1 TXD1 | SPI0 SCK |
| P3.4 | Not used for factory UART ISP | Available GPIO |
| P3.2 | Not used for factory UART ISP | Available GPIO |

UART wiring is crossed:

- ESP32-P4 UART TX -> CH552 P1.6 / RXD1
- ESP32-P4 UART RX <- CH552 P1.7 / TXD1
- Common GND
- Logic level: 3.3 V

The UART1 alternate mapping to P3.4/P3.2 is not used. The factory ROM bootloader pin mapping is treated as fixed to P1.6/P1.7.

## 2. Hardware requirements

### 2.1 Power control

Use a P4-controlled load switch for the CH552 supply.

- VCC and V33 are connected to the same switched 3.3 V rail.
- Both pins retain their own local decoupling capacitors.
- The load-switch enable has a hardware pull-down so the CH552 remains off while the P4 is in reset or its GPIOs are not configured.
- The load switch must support the CH552 peak current and the selected board's additional loads.
- The P4 must not drive any CH552-connected signal while CH552 power is off. This prevents unintended back-powering through GPIO protection structures.

A controlled power cycle is the deterministic way to generate a power-on reset. Do not rely on the CH552 RST pin for the primary bootloader entry mechanism: the external reset input is controlled by the configuration option En_RST_RESET and is disabled by default in the datasheet reset configuration.

### 2.2 Shared UART/SPI signal lines

P1.6 and P1.7 are shared between UART1 and SPI0. The P4 must switch its own peripheral mux and GPIO direction explicitly.

During UART ISP:
- P4 UART TX drives P1.6.
- P4 UART RX samples P1.7.
- P4 SPI SCK, MOSI and CS are disabled / high impedance.
- P4 MISO input is not enabled as an active output (it remains an input).
- No SPI clock edges are permitted.

During SPI runtime:
- P4 UART peripheral is disabled.
- P4 UART TX/RX pins are changed to high-impedance GPIO inputs before SPI is enabled.
- P4 configures SPI master and drives SCK, MOSI and CS.
- CH552 configures SPI0 slave.
- CS is held high until both devices have completed initialization.

P1.4/SCS must remain inactive high during UART programming and power-up. A board-level pull-up may be used to guarantee this state before either MCU configures the pin.

### 2.3 MISO electrical behavior

The CH552 application configures P1.6 as the SPI MISO pin with the GPIO output disabled/high impedance while unselected. Enable the SPI MISO output function and let the SPI hardware gate MISO with SCS. No software CS interrupt is required for normal tri-state control.

This behavior is specified in the CH552 SPI slave documentation; verify it with a scope or logic analyzer on the QT Py before relying on a shared SPI bus.

### 2.4 USB connection to the PC-side hub

The CH552 USB D+/D- pair remains connected to its dedicated downstream port on the PC-facing hub. The UART programming path is separate from the USB data path.

The manual QT Py BOOT control that pulls D+ high is a USB-bootloader entry mechanism. It is not part of the P4 UART1 programming sequence and must not be treated as a substitute for UART ISP entry.

## 3. P4-controlled state machine

### State A — SAFE_OFF

Entry conditions: P4 power-up, watchdog recovery, or a requested CH552 power cycle.

1. Disable P4 SPI and UART peripherals for the shared pins.
2. Configure all P4 pins connected to CH552 as inputs/high impedance.
3. Keep SPI CS inactive.
4. Disable the CH552 load switch.
5. Wait for the switched rail to discharge below the CH552 POR threshold. The required discharge time must be measured on the assembled board; do not assume that disabling the switch alone guarantees a sufficiently fast power-on reset.

Transition: after the rail is discharged and P4 has selected the next operating mode.

### State B — UART_ISP_ENTRY

1. Keep P4 SPI disabled and its shared SPI pins high impedance.
2. Configure the P4 UART pins:
   - TX output idle-high
   - RX input
3. Enable the CH552 3.3 V load switch.
4. Allow the CH552 power-on reset interval to complete.
5. Start the CH552 UART1 ISP detection/handshake on P1.6/P1.7 using the selected CH55x programming tool's protocol and timing.
6. If the bootloader is detected, proceed to programming or send the bootloader exit command.
7. If no bootloader response is detected, do not switch to SPI blindly. Retry with a controlled power cycle, then report a programming/boot fault.

The ROM bootloader runs first after power-on reset on devices containing the ISP bootloader. The exact UART detection timing and the QT Py board's 3.3 V ISP behavior must be validated on the actual module.

### State C — UART_ISP_PROGRAM

1. Keep SPI disabled.
2. Transfer the CH552 application image through UART1.
3. Check the programming tool's acknowledgements and per-block / final checksum result.
4. Treat any timeout, negative response or checksum error as a failed update.
5. Do not record the new firmware version as installed until programming has completed and the application has subsequently passed its runtime handshake.

On successful completion, leave the ROM bootloader using the documented CH55x bootloader exit mechanism. The ch55xduino documentation lists the exit sequence 57 AB A2 01 00 01 A4 at 57600 baud. Confirm that the selected ISP tool and bootloader revision use this sequence before hard-coding it into the P4 firmware.

### State D — APPLICATION_START

1. Keep the P4 shared SPI pins high impedance.
2. Wait for the CH552 application to initialize its clock, USB device stack, SPI slave and internal buffers.
3. The CH552 application must not assert SPI MISO until selected by SCS.
4. The CH552 reports readiness through the first SPI status exchange or a separate READY GPIO, if one is allocated.
5. P4 switches its shared pins from UART to SPI only after UART is fully disabled and the CH552 application is confirmed running.

### State E — SPI_RUNTIME

P4 is SPI master; CH552 is SPI slave.

1. Configure P4 SPI mode to match the CH552 application.
2. Keep CS high while configuring SCK and MOSI.
3. Enable SPI master on the P4.
4. Confirm CH552 READY.
5. Assert CS only for a complete transaction.
6. Deassert CS after each transaction. CH552 hardware releases MISO when SCS is inactive.
7. UART1 remains disabled during runtime.

Start with a conservative SPI clock (1 MHz is the initial bench target). Increase only after signal-integrity and protocol tests.

### State F — UPDATE_REQUIRED

Firmware version comparison is performed after the CH552 application responds over SPI.

1. P4 reads the CH552 firmware identity/version.
2. If the version matches the required image, continue in SPI_RUNTIME.
3. If the version differs, stop all SPI transactions and deassert CS.
4. Disable P4 SPI and return all shared P4 pins to high impedance.
5. Enter SAFE_OFF and power-cycle the CH552.
6. Enter UART_ISP_ENTRY and program the required image.
7. Verify the programming result, exit the bootloader and repeat APPLICATION_START.
8. Accept the update only after the CH552 reports the expected version over SPI.

The P4 may cache the last successfully installed image version in non-volatile storage, but the CH552 runtime version remains authoritative. Never skip the SPI version check solely because the P4's cached version matches.

## 4. Reset and recovery policy

- Primary deterministic reset: controlled CH552 power cycle using the load switch.
- CH552 RST pin: optional only after confirming En_RST_RESET configuration on the actual module. Do not assume it works on an unmodified QT Py.
- P4 reset/watchdog: shared P4 pins return to high impedance; load-switch pull-down keeps CH552 off until P4 firmware deliberately starts it.
- CH552 does not answer SPI: deassert CS, disable SPI, power-cycle once, then retry startup.
- CH552 does not answer UART ISP: keep SPI disabled, retry one power-on sequence, then enter a fault state. Do not switch the pins to SPI based only on a timeout.
- Failed programming: retain the previous P4-side known-good image and retry only through an explicit recovery policy. The P4 must not mark the target firmware as updated until runtime verification succeeds.

## 5. Firmware version and update protocol

The CH552 application must expose a compact SPI identification/status response containing at least:

- protocol magic
- protocol revision
- firmware version (major/minor/patch or monotonic build ID)
- application state / ready flag
- response checksum or CRC

The P4 stores the expected image version and validates the returned identity before enabling normal gateway traffic.

Keep this application-level SPI protocol independent of Logitech HID report formats. It is only the management and transport link between P4 and CH552.

## 6. Electrical and firmware validation gates

The following items are not considered proven until measured on the actual QT Py:

1. UART1 factory ISP programming at VCC=V33=3.3 V. The user's practical experience supports this operating point, but WCH datasheet version 1G explicitly states that UART/USB ISP downloading requires temporary 5 V. This is a documented discrepancy and must be resolved by bench test before PCB freeze. Do not apply 5 V to VCC while V33 is tied directly to 3.3 V.
2. Exact UART ISP baud rate, detection timing, timeout and programming protocol for the QT Py's factory bootloader revision.
3. Whether a full power removal of the QT Py module is required for every UART ISP entry, and the minimum off-time needed for POR.
4. MISO high impedance when SCS is inactive.
5. No back-powering of the unpowered CH552 through P4 GPIO or USB lines.
6. Correct transition from UART1 pin mux to SPI0 pin mux without glitches on P1.6/P1.7.
7. USB enumeration of the CH552 application after programming and after a normal cold start.

## 7. Source references

- WCH CH552 Datasheet, version 1G:
  - §6.6: ISP download support and programming connection pins.
  - §7.1: VCC/V33 supply modes.
  - §7.3.2–§7.3.3: external/software reset and ROM bootloader startup after power-on reset.
  - §10.4: UART1 alternate pin mapping.
  - §14: SPI master/slave behavior and slave MISO handling.
- ch55xduino documentation:
  - UART upload uses UART1 on CH55x devices other than CH551.
  - UART bootloader handoff/entry support and bootloader exit sequence.
