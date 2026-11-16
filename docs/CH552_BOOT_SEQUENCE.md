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
| P3.2 | Not used for factory UART ISP | BOOT_REQ / INT0 |

UART wiring is crossed:

- ESP32-P4 UART TX -> CH552 P1.6 / RXD1
- ESP32-P4 UART RX <- CH552 P1.7 / TXD1
- Common GND
- Logic level: 3.3 V

The UART1 alternate mapping to P3.4/P3.2 is not used. The factory ROM bootloader pin mapping is treated as fixed to P1.6/P1.7.

## 2. Hardware requirements

### 2.1 Power domain and reset

The CH552 core is operated at 3.3 V:

- VCC = 3.3 V.
- V33 = the same 3.3 V rail, directly tied to VCC as required for the selected low-voltage operating mode.
- Use local decoupling at the CH552 supply pins.
- Select the internal 16 MHz clock for 3.3 V operation.

**Important QT Py board-level constraint:** the Adafruit QT Py USB-C connector also supplies the board's onboard 3.3 V regulator from USB VBUS. Switching only an external 3.3 V feed does not power-cycle the CH552 while USB VBUS remains connected. Before adding a P4-controlled power switch, verify the exact QT Py schematic and isolate every supply path, including USB VBUS and any external 3.3 V feed. Do not connect an external 3.3 V source against the onboard regulator output without confirming the board's power-path arrangement.

For normal firmware updates, use the application-assisted BOOT_REQ handoff; do not power-cycle the module as the routine update mechanism. A controlled cold start is a separate recovery method and is valid only when all supply paths are actually interrupted. If a load switch is retained for exceptional service isolation, it must default to the selected safe state during P4 reset and must not allow P4 signal pins to back-power an unpowered CH552. It is not part of normal reset or update operation.

The QT Py RESET button is connected to the CH552 RST input. The RST input is active HIGH and has an internal pull-down. The Adafruit QT Py is treated as delivered with En_RST_RESET=1, so the P4 may use the dedicated RESET GPIO without changing any CH552 configuration bit. Preserve the complete factory configuration unchanged during all firmware updates. The factory USB BOOT button is a separate recovery path: it pulls D+ high during a power-up sequence and must not be confused with UART ISP entry.

### 2.2 Shared UART/SPI signal lines

P1.6 and P1.7 are shared between UART1 and SPI0. The P4 must switch its own peripheral mux and GPIO direction explicitly.

### 2.3 Dedicated bootloader request

Reserve P3.2/INT0 as BOOT_REQ. It is separate from P1.4-P1.7 and is available on the selected QT Py board. P3.4 remains unused by the programming and runtime buses.

- P4 drives BOOT_REQ through an AC-coupling capacitor, following the DTR-to-interrupt approach documented by ch55xduino.
- The CH552 application enables the corresponding external interrupt and implements the bootloader handoff code in its interrupt handler.
- The input has a defined idle level using a board-level bias resistor.
- The P4 generates a short edge pulse only after stopping USB/HID activity and confirming that the CH552 application is responsive.
- The exact polarity, capacitor value, bias resistor and pulse timing must follow the tested ch55xduino implementation and be verified on the QT Py.

This signal is not UART data and does not change the factory UART1 pin mapping. The ROM bootloader is entered by application-assisted handoff; do not assume that a normal external reset or an arbitrary power cycle alone provides a reliable UART programming entry from every application state.

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

### 2.4 MISO electrical behavior

The CH552 application configures P1.6 as the SPI MISO pin with the GPIO output disabled/high impedance while unselected. Enable the SPI MISO output function and let the SPI hardware gate MISO with SCS. No software CS interrupt is required for normal tri-state control.

This behavior is specified in the CH552 SPI slave documentation; verify it with a scope or logic analyzer on the QT Py before relying on a shared SPI bus.

### 2.5 USB connection to the PC-side hub

The CH552 USB D+/D- pair remains connected to its dedicated downstream port on the PC-facing hub. The UART programming path is separate from the USB data path.

The manual QT Py BOOT control that pulls D+ high is a USB-bootloader entry mechanism. It is not part of the P4 UART1 programming sequence and must not be treated as a substitute for UART ISP entry.

## 3. P4-controlled state machine

### State A — SAFE_IDLE

Entry conditions: P4 power-up, P4 watchdog recovery, or normal gateway startup. The QT Py remains continuously powered from the board's 3.3 V supply.

1. Disable P4 SPI and UART peripherals for the shared pins.
2. Configure all P4 pins connected to CH552 as inputs/high impedance, except RESET which is driven LOW (inactive).
3. Keep SPI CS inactive HIGH and BOOT_REQ inactive.
4. Do not switch the QT Py supply during normal startup or updates.

Transition: proceed to RESET_RELEASE / APPLICATION_START.

### State B — RESET_RELEASE / APPLICATION_START

1. Keep P4 shared SPI pins high impedance; keep CS high and BOOT_REQ inactive.
2. Ensure CH552_RESET is LOW. If a deliberate restart is required, assert RST HIGH for at least 1 us (datasheet minimum Trst = 70 ns), then return it LOW. Use a conservative firmware pulse such as 1 ms; validate on the QT Py.
3. Wait for the CH552 application to initialize its clock, USB device stack, SPI slave and internal buffers.
4. Read the application identity/version over SPI.
5. If the application does not become responsive, use the BOOT_REQ recovery path only if the application is still capable of handling it; otherwise report a fault and use the separate manual USB BOOT service recovery.

The external RST input is enabled in the assumed Adafruit QT Py factory configuration (En_RST_RESET=1). Do not program or alter this bit. Preserve all factory configuration bytes unchanged. A hardware reset returns execution to address 0; it is not treated as a guaranteed UART ISP entry.

### State C — UART_ISP_ENTRY

1. Stop all CH552 USB/HID traffic and SPI transactions.
2. Deassert SPI CS.
3. Disable P4 SPI and return the shared P4 SPI pins to high impedance.
4. Generate the BOOT_REQ edge pulse through the coupling capacitor.
5. The CH552 application's interrupt handler executes the tested bootloader handoff code.
6. Configure P4 UART:
   - TX output idle-high -> CH552 P1.6/RXD1
   - RX input <- CH552 P1.7/TXD1
7. Start UART1 ISP detection/handshake using the selected CH55x programming tool's protocol and timing.
8. If the bootloader is detected, proceed to programming. If not, stop and report a bootloader-entry fault; do not switch to SPI blindly.

The BOOT_REQ interrupt handler and handoff code are mandatory for reliable serial re-entry from the running application. They must be implemented in the CH552 application and tested before the P4 update manager is considered complete.

### State D — UART_ISP_PROGRAM

1. Keep SPI disabled.
2. Transfer the CH552 application image through UART1.
3. Check the programming tool's acknowledgements and per-block / final checksum result.
4. Treat any timeout, negative response or checksum error as a failed update.
5. Do not record the new firmware version as installed until programming has completed and the application has subsequently passed its runtime handshake.

Programming is complete only after the selected ISP tool reports a successful final verification.

### State E — RETURN_TO_APPLICATION

1. After successful programming, send the bootloader exit command supported by the detected bootloader revision.
2. Disable P4 UART and set its TX/RX pins to high impedance.
3. Wait for the CH552 application to restart and initialize.
4. Confirm application readiness and read back the firmware version over SPI.

The ch55xduino documentation lists the exit sequence 57 AB A2 01 00 01 A4 at 57600 baud. Confirm that the selected ISP tool and bootloader revision use this sequence before hard-coding it into P4 firmware.

### State F — SPI_RUNTIME

P4 is SPI master; CH552 is SPI slave.

1. Configure P4 SPI mode to match the CH552 application.
2. Keep CS high while configuring SCK and MOSI.
3. Enable SPI master on the P4.
4. Confirm CH552 READY.
5. Assert CS only for a complete transaction.
6. Deassert CS after each transaction. CH552 hardware releases MISO when SCS is inactive.
7. UART1 remains disabled during runtime.

Start with a conservative SPI clock (1 MHz is the initial bench target). Increase only after signal-integrity and protocol tests.

### State G — UPDATE_REQUIRED

Firmware version comparison is performed after the CH552 application responds over SPI.

1. P4 reads the CH552 firmware identity/version.
2. If the version matches the required image, continue in SPI_RUNTIME.
3. If the version differs, stop all SPI transactions and deassert CS.
4. Enter UART_ISP_ENTRY and generate the BOOT_REQ pulse.
5. Program the required image.
6. Verify the programming result, exit the bootloader and repeat RETURN_TO_APPLICATION.
7. Accept the update only after the CH552 reports the expected version over SPI.

The P4 may cache the last successfully installed image version in non-volatile storage, but the CH552 runtime version remains authoritative. Never skip the SPI version check solely because the P4's cached version matches.

## 4. Reset and recovery policy

- Normal application restart: P4 asserts the dedicated CH552_RESET line HIGH, then returns it LOW. The Adafruit factory configuration is assumed to have En_RST_RESET=1; no configuration-bit change is part of this project.
- Normal update entry: application-assisted BOOT_REQ interrupt pulse, followed by UART1 ISP. RESET is not substituted for BOOT_REQ.
- P4 reset/watchdog: CH552 remains powered. P4 shared UART/SPI pins default to high impedance, CS is held HIGH and CH552_RESET is held LOW by the P4 output policy / defined board bias.
- CH552 does not answer SPI: deassert CS, disable SPI and issue one hardware RESET pulse; then retry the application READY handshake.
- CH552 does not answer UART ISP: keep SPI disabled and enter a fault state. RESET may restart the application but does not guarantee UART ISP entry. USB bootloader recovery through the board's D+ BOOT mechanism remains a separate service path.
- Cold power removal: reserved for exceptional bench/service recovery only. It requires interrupting every QT Py supply path, including USB VBUS; it is not the routine reset or update method.
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

## 6. Firmware update configuration protection

The CH552 datasheet (version 1G, Table 6.2 and Section 6.7) distinguishes the writable Configuration Information Word from the factory chip ID:

- The writable 16-bit configuration word is at `0x3FF8`/`0x3FF9`.
- The three following 16-bit words at `0x3FFA`–`0x3FFF` contain the factory chip ID; `0x3FFE`/`0x3FFF` are the final 16-bit chip-ID word and are not the writable configuration word.
- The complete Configuration Information area spans `0x3FF8`–`0x3FFF`.

The documented fields of the writable word at `0x3FF8`/`0x3FF9` are:

| Bit(s) | Datasheet field | Meaning / datasheet recommendation |
|---|---|---|
| 15 | `Code_Protect` | `0` = programmer read disabled; `1` = read enabled |
| 14 | `No_Boot_Load` | `0` = start application at `0x0000`; `1` = start BootLoader at `0x3800` |
| 13 | `En_Long_Reset` | `0` = standard short reset; `1` = add 44 ms reset delay |
| 12 | `En_RST_RESET` | `0` = external RST disabled; `1` = external RST enabled |
| 11–10 | Reserved | Datasheet says programmer sets `00b` |
| 9 | `Must_1` | Must be `1` |
| 8 | `Must_0` | Must be `0` |
| 7–0 | `All_1` | Must be `0xFF` |

The Adafruit QT Py is treated as delivered with `En_RST_RESET=1`, as established for this project from the module-specific reference/board information. This is distinct from the WCH raw-chip recommendation of `0`. Do not generalize the Adafruit value to unconfigured WCH raw chips.

**Project invariant: preserve the complete 16-bit word at `0x3FF8`/`0x3FF9` exactly as found on the Adafruit QT Py.** Also preserve the factory chip-ID words at `0x3FFA`–`0x3FFF`; they are identity data, not writable configuration fields. No bit is to be set, cleared, normalized or otherwise changed by the application build, firmware packaging, UART updater or recovery procedure.

Mandatory update-manager rules:

- Program application code only.
- Do not issue commands that erase, rewrite or otherwise alter the configuration word or chip-ID area.
- Do not generate a programming image that carries replacement configuration bytes.
- Do not silently apply a programmer's default configuration template.
- Before selecting an ISP implementation, inspect its source and document the exact flash address range and erase scope.
- A successful application write is not proof that configuration and chip-ID areas were preserved. Validate preservation independently during bring-up if the programmer's erase/write scope is not unambiguous.

### UART ISP tool investigation

The ch55xduino project documents both USB and UART upload for CH552-class devices and explicitly identifies UART1 as the serial ISP interface (CH551 excluded). It also documents that automatic entry into the ROM bootloader requires an application-side interrupt handoff. The documented bootloader-exit sequence is sent at 57600 baud. See [ch55xduino README, USB and Serial upload](https://github.com/DeqingSun/ch55xduino).

The UNIT-Electronics package exposes a separate `vnproch55x_serial` upload recipe:
`vnproch55x -s {serial.port} -t {build.mcu} {hex-file}`.
This establishes the wrapper's command-line contract, not by itself the binary's implementation.

The [DeqingSun/vnproch551 `main.cpp`](https://github.com/DeqingSun/vnproch551/blob/master/main.cpp) source contains both USB and UART1 transport paths. The serial path opens the port at 57600 baud, sends frames prefixed with `57 AB`, and validates replies prefixed with `55 AA`. The application-side BOOT_REQ handoff remains necessary; the host utility's DTR/RTS toggling is specific to its serial-adapter reset-capacitor arrangement and is not automatically applicable to the P4 design.

**Critical configuration finding:** the source defines an `A8` boot-options write command. Its source comment describes an 8-byte write relative to `ROM_CFG_ADDR-8` and says the CH552 default value `0x03` selects P3.6/D+ and clears P1.5. If the `-c` option is omitted, the program sends this command. The special argument `-c KEEP` skips that write. However, the UNIT-Electronics `vnproch55x_serial` recipe passes `-s` and `-t` but does **not** pass `-c KEEP`. Therefore the package's default serial upload invocation is **not compatible with this project's immutable-configuration requirement**. The programmer source's boot-option address description must also be reconciled with the WCH datasheet, which identifies `0x3FF8`/`0x3FF9` as the writable word and `0x3FFA`–`0x3FFF` as chip-ID data; do not assume the A8 command's complete physical write mapping from the comment alone.

The source also sends erase command `A4 01 00 08` for CH552. Reverse-engineered discussion of a later V2.5 bootloader describes the delete routine as deleting eight pages for V2.4 and later; the source comments also state that values below 8 fail and values of 8 or more are normalized to 8. This does not establish the exact page size or erase coverage for the QT Py's actual ROM version. `-c KEEP` only skips the explicit A8 boot-options write; it is not proof that the erase command preserves the configuration word or factory chip-ID area. The older [LoveMHz/vnproch55x `main.cpp`](https://github.com/LoveMHz/vnproch55x/blob/master/main.cpp) is USB-only and likewise sends the erase command.

**Result:** UART1 ISP and its 57600-baud framing are source-confirmed. The stock UNIT-Electronics serial recipe must not be used unchanged. Even with `-c KEEP`, erase-scope preservation of `0x3FF8`–`0x3FFF` remains unverified. No programmer is approved for automatic P4 updates yet.

## 7. Electrical and firmware validation gates

The following items are not considered proven until measured on the actual QT Py:

1. UART1 factory ISP programming at VCC=V33=3.3 V. The user's practical experience supports this operating point, but WCH datasheet version 1G explicitly states that UART/USB ISP downloading requires temporary 5 V. This is a documented discrepancy and must be resolved by bench test before PCB freeze. Do not apply 5 V to VCC while V33 is tied directly to 3.3 V.
2. BOOT_REQ pulse polarity, capacitor/bias network and tested application handoff code on P3.2/INT0.
3. Exact UART ISP baud rate, detection timing, timeout and programming protocol for the QT Py's factory bootloader revision.
4. Whether a full power removal of the QT Py module is required for recovery, and the minimum off-time needed for POR.
5. MISO high impedance when SCS is inactive.
6. No back-powering of the unpowered CH552 through P4 GPIO or USB lines.
7. Correct transition from UART1 pin mux to SPI0 pin mux without glitches on P1.6/P1.7.
8. USB enumeration of the CH552 application after programming and after a normal cold start.

## 8. Source references

- WCH CH552 Datasheet, version 1G:
  - §6.6: ISP download support and programming connection pins.
  - §7.1: VCC/V33 supply modes.
  - §7.3.2–§7.3.3: external/software reset and ROM bootloader startup after power-on reset.
  - §10.4: UART1 alternate pin mapping.
  - §14: SPI master/slave behavior and slave MISO handling.
- ch55xduino documentation:
  - UART upload uses UART1 on CH55x devices other than CH551.
  - UART bootloader handoff/entry support and bootloader exit sequence.

## 9. Concrete electrical interface and pin-state contract

This section is the wiring and firmware contract for the ESP32-P4-to-QT-Py connection. P4 GPIO numbers are intentionally not assigned here: they must be selected from the actual P4 board pinout and existing USB/boot/debug allocation before the PCB is frozen.

### 9.1 Net list

Use one shared P4 GPIO pair for the two shared CH552 pins. Do **not** wire separate UART and SPI P4 outputs together.

| Net | ESP32-P4 function in UART mode | ESP32-P4 function in SPI mode | QT Py connection |
|---|---|---|---|
| CH552_UART_SPI_1 | UART TX, output | SPI MISO, input | P1.6 / RXD1 / MISO |
| CH552_UART_SPI_2 | UART RX, input | SPI SCK, output | P1.7 / TXD1 / SCK |
| CH552_SPI_MOSI | High impedance / unused | SPI MOSI, output | P1.5 / MOSI |
| CH552_SPI_CS_N | High, output or pulled high | SPI CS, output, active low | P1.4 / SCS |
| CH552_BOOT_REQ | Idle; pulse output only for handoff | Idle; pulse output only for handoff | P3.2 / INT0 |
| CH552_RESET | GPIO low = normal run; high pulse = reset | Same | QT Py RESET pad / CH552 RST |
| GND | Common reference | Common reference | GND |

UART is crossed:
- P4 TX -> CH552 P1.6/RXD1.
- P4 RX <- CH552 P1.7/TXD1.

SPI is conventional:
- P4 MOSI -> CH552 P1.5/MOSI.
- P4 MISO <- CH552 P1.6/MISO.
- P4 SCK -> CH552 P1.7/SCK.
- P4 CS_N -> CH552 P1.4/SCS.

All signals are 3.3 V logic. No level shifter is required for this connection when both devices are actually operated from the same 3.3 V domain. Use a common ground. Optional small series resistors may be placed close to the P4 on driven lines for edge damping, but their values must be selected from measured signal integrity; they are not a substitute for correct pin direction.

### 9.2 Required passive bias and safe defaults

- P1.4/SCS: external pull-up to the CH552 3.3 V rail so the slave is deselected before either MCU configures its pins and throughout reset/boot.
- P3.2/INT0: external bias to the inactive level required by the selected ch55xduino interrupt handler. The coupling capacitor and pulse polarity must match that handler; do not select capacitor, resistor or polarity values by guesswork.
- CH552 RST: keep LOW in normal operation. The pin has an internal pull-down; add/retain a defined external pull-down if required by the P4 reset-domain behavior. The P4 GPIO asserts reset by driving HIGH and releases it by driving LOW. Do not leave the P4 GPIO push-pull HIGH at startup.
- Shared P1.6/P1.7 lines: no external pull-up is required for normal SPI operation if the P4 actively drives SCK/MOSI and the CH552 hardware gates MISO. Do not add a strong pull-up to MISO that could mask a failed tri-state condition.
- P4 pins: configure as inputs/high impedance before changing peripheral ownership, before CH552 power is removed, and whenever the CH552 is not known to be powered.
- Do not let P4 and CH552 drive the same shared net simultaneously. The mode transition must disable the old peripheral before enabling the new one.

### 9.3 Pin direction by phase

| Phase | P4 P1.6 net | P4 P1.7 net | P4 MOSI | P4 CS_N | CH552 role |
|---|---|---|---|---|---|
| P4 reset / CH552 continuously powered | High impedance | High impedance | High impedance | External pull-up holds high | RESET held inactive LOW; application may continue running |
| CH552 hardware reset | High impedance | High impedance | High impedance | High | RST driven HIGH, then returned LOW |
| Cold-start ROM ISP detection | UART TX output, idle high | UART RX input | High impedance | High | ROM UART1: RX on P1.6, TX on P1.7 |
| Application runtime | SPI MISO input | SPI SCK output | SPI MOSI output | Output, idle high | SPI0 slave |
| Runtime-to-ISP handoff | High impedance until handoff completes, then UART TX | High impedance until handoff completes, then UART RX | High impedance | High | Application receives BOOT_REQ, then ROM UART1 |
| UART programming | UART TX output | UART RX input | High impedance | High | ROM UART1 |
| ISP exit / return to runtime | High impedance during transition | High impedance during transition | High impedance | High | ROM exits; application restarts |

The P4 peripheral matrix may route UART and SPI functions to the same two physical P4 GPIOs. Firmware must change the GPIO matrix and direction explicitly; the table describes electrical pin roles, not simultaneous peripheral operation.

### 9.4 Deterministic transition procedures

**Normal startup / external reset**
1. Keep P4 shared pins high impedance; keep CS_N pulled high and BOOT_REQ inactive.
2. Ensure CH552_RESET is LOW before enabling P4 peripheral functions.
3. If the CH552 is already running, assert CH552_RESET HIGH for at least 1 us (datasheet minimum Trst is 70 ns), then drive it LOW. Use a conservative 1 ms pulse and validate on the QT Py.
4. After release, wait for application startup and SPI READY; read protocol revision and firmware version before enabling gateway traffic.
5. The Adafruit factory configuration is expected to enable the external RST input. If the actual QT Py does not respond to RESET, stop and investigate the board/configuration before considering any configuration change. Do not modify the configuration bits as a workaround.

**Cold start**
1. Keep P4 shared pins high impedance; keep CS_N pulled high, BOOT_REQ inactive and CH552_RESET LOW.
2. Start the P4 UART receiver and configure TX to idle high before the CH552 can drive its UART output.
3. The QT Py remains continuously powered from its verified 3.3 V supply. A true cold start occurs only when all supply paths, including USB VBUS, are interrupted; it is not part of routine update flow.
4. Observe the factory ROM UART ISP startup window using the measured bootloader protocol/timing. If no programming session is requested or detected, allow the ROM bootloader to launch the application.
5. Wait for the application READY indication over SPI; read its protocol revision and firmware version before enabling normal gateway traffic.

**Application recovery / restart**
1. Stop new gateway transactions and wait for the current SPI transaction to finish.
2. Drive CS_N high and stop the SPI master.
3. Disable the P4 SPI peripheral and set shared P1.6/P1.7 and MOSI to high impedance.
4. Assert CH552_RESET HIGH for the verified reset pulse width, then drive it LOW.
5. Wait for application READY and verify the firmware version. This restarts the CH552; it is not the UART ISP entry command.

**SPI runtime to UART ISP**
1. Stop new gateway transactions and wait for the current SPI transaction to finish.
2. Drive CS_N high and stop the SPI master.
3. Disable the P4 SPI peripheral and set the shared P1.6/P1.7 P4 pins to high impedance. Keep MOSI high impedance and CS_N high.
4. Generate the BOOT_REQ edge through the coupling network on P3.2/INT0. The CH552 application must have the tested interrupt handler and bootloader jump code installed.
5. Wait for the application-to-ROM handoff interval. Do not drive UART data during this interval.
6. Configure the same P4 shared pins as UART: TX output idle high on P1.6 and RX input on P1.7.
7. Start the ROM ISP handshake. Continue only after a valid bootloader response.

**UART ISP to SPI runtime**
1. Complete image transfer and final verification; do not treat a block ACK alone as update success.
2. Send the bootloader exit sequence supported by the actual bootloader revision.
3. Disable P4 UART and set both shared pins to high impedance.
4. Keep CS_N high. Wait for application startup and SPI READY.
5. Configure P4 SPI while CS_N remains high; only then begin SPI transactions.
6. Read back the application firmware version and protocol revision. Mark the update successful only when both match the expected image.

### 9.5 Recovery boundary

The BOOT_REQ handoff depends on a functioning CH552 application and its interrupt handler. The separate hardware RESET input provides a controlled application restart under the assumed Adafruit factory configuration (En_RST_RESET=1); it does not, by itself, guarantee entry into UART ISP. If the application is corrupted, hangs with interrupts disabled, or never reaches the handler, the P4 cannot assume that BOOT_REQ will enter UART ISP. Do not claim a guaranteed software recovery path in that state.

The QT Py's manual USB bootloader mechanism (BOOT button / D+ pull-up during power-up) is a separate service recovery path. A fully automatic P4 recovery path requires a separately validated way to control the board's complete power-up condition and USB D+ boot strap; this is not yet part of the approved interface.

### 9.6 Bring-up acceptance tests

Before enabling automatic updates, verify on the actual QT Py:
1. Cold-start ROM UART1 detection and upload at VCC=V33=3.3 V.
2. P3.2 BOOT_REQ pulse polarity, edge shape, bias level and application-to-ROM handoff.
3. UART1 operation on P1.6/P1.7 after handoff.
4. No contention or unintended clock edges on P1.6/P1.7 during either transition.
5. P1.6/MISO is high impedance whenever CS_N is high.
6. SPI slave transactions at 1 MHz, including repeated CS assertions and back-to-back transfers.
7. USB device enumeration after normal startup and after UART ISP programming.
8. No back-powering when any optional switched supply is off while USB D+/D- and P4 signal wiring remain connected.

## 10. Reference links

- [Adafruit CH552 QT Py pinout and power description](https://learn.adafruit.com/adafruit-ch552-qt-py/pinouts)
- [Adafruit CH552 QT Py guide: clock and USB bootloader settings](https://learn.adafruit.com/adafruit-ch552-qt-py)
- [ch55xduino: UART1 serial upload and interrupt-based bootloader handoff](https://github.com/adafruit/ch55xduino)
- [UNIT-Electronics CH55x package: separate `vnproch55x_serial` upload-tool mapping](https://github.com/UNIT-Electronics/Uelectronics-CH552-Arduino-Package/blob/main/ch55x/platform.txt)
- [WCH CH552 datasheet](https://wch-ic.com)



## 11. P4 management software framework (2026-11-16)

A first transport-independent P4 management module has been added:

- `components/usb_gateway/include/ch552_manager.h`
- `components/usb_gateway/ch552_manager.c`

The module is registered in the `usb_gateway` ESP-IDF component. It provides:
- a callback-based SPI transport boundary, so the board-specific SPI host and GPIO allocation remain separate;
- a runtime identity query and response validation;
- protocol version, firmware semantic version, 32-bit build ID, status and 16-bit capability reporting;
- CRC-16/CCITT-FALSE validation;
- a firmware update API that currently fails closed with `ESP_ERR_NOT_SUPPORTED`.

### 11.1 Application-level SPI identity protocol

This is a new gateway application protocol. It is **not** the WCH ROM UART ISP protocol and does not alter its framing.

The CH552 slave consumes a request in one CS transaction and returns the prepared response in the next CS transaction. The P4 callback must therefore perform one complete CS assertion per call.

Request frame (8 bytes, little-endian CRC):
| Offset | Field | Value |
|---:|---|---|
| 0 | Magic | `0xC5` |
| 1 | Magic | `0x5A` |
| 2 | Protocol | `0x01` |
| 3 | Command | `0x01` = GET_IDENTITY; `0x00` = NOP |
| 4 | Sequence | Incrementing 8-bit sequence |
| 5 | Reserved | `0x00` |
| 6–7 | CRC16 | CRC-16/CCITT-FALSE over bytes 0–5 |

Response frame (17 bytes, little-endian fields and CRC):
| Offset | Field |
|---:|---|
| 0–1 | Magic `C5 5A` |
| 2 | Protocol revision |
| 3 | Echoed command (`GET_IDENTITY`) |
| 4 | Echoed sequence |
| 5 | Status (`0` = READY) |
| 6–8 | Firmware major, minor, patch |
| 9–12 | 32-bit build ID |
| 13–14 | 16-bit capability flags |
| 15–16 | CRC16 over bytes 0–14 |

The P4 sends GET_IDENTITY (8 bytes), then clocks the response in a second transaction (17 bytes) while sending NOP. It accepts the response only if magic, protocol revision, command, sequence, READY status and CRC all match.

The CH552-side SPI slave implementation is still required. The protocol is deliberately transaction-oriented to accommodate the CH552's one-byte SPI FIFO; the slave must buffer the complete request and prepare the response before the next CS transaction. The initial bus rate remains 1 MHz pending bench validation.

### 11.2 Firmware update pipeline and safety interlock

The intended update sequence is:

1. Read the running application identity over SPI.
2. Compare it with the P4's required image manifest.
3. Stop HID/vendor traffic and complete the SPI-to-UART pin-mux transition.
4. Request application-assisted BOOT_REQ handoff.
5. Detect the ROM bootloader and identify its revision using read-only commands.
6. Validate image target, size, version and integrity.
7. Program application flash only, with per-block acknowledgement and final verification.
8. Exit ISP, return shared pins to high impedance, and wait for application startup.
9. Read identity over SPI again and accept the update only if the expected version/build ID is reported.

**Current implementation boundary:** the P4 manager exposes `ch552_manager_update()`, but it intentionally returns `ESP_ERR_NOT_SUPPORTED` without touching UART or flash. The exact A1/A7 read-only ISP packet formats still need to be derived from the reviewed source and checked against the actual QT Py ROM revision. More importantly, the A4 erase command's physical coverage is unknown. No erase, write, A8 boot-option command or automatic flash operation is implemented.

The update backend must not be enabled until all of these are proven:
- actual QT Py ROM bootloader revision and exact packet/response framing;
- erase coverage and application-only write range;
- no write/erase access to `0x3FF8–0x3FFF`;
- no implicit A8 configuration/boot-option write;
- successful application verification after programming.

### 11.3 Integration status

The manager source is registered with the ESP-IDF component, but it is not yet called from `usb_gateway_init()`. This is intentional: the P4 GPIO allocation, SPI host instance and CH552 board wiring have not yet been finalized. No UART/SPI pin routing, USB controller allocation, Host/Device PHY configuration or existing gateway behavior was changed.

No ESP-IDF build, unit test, flash operation or hardware test is claimed for this change.


## QT Py CH552 application memory baseline (2026-11-16)

A first standalone CH55xduino sketch has been added at `firmware/qt_py_memory_test/qt_py_memory_test.ino`. It includes the temporary standard HID keyboard USB stack, the agreed P4↔CH552 SPI identity protocol (8-byte request / 17-byte response, CRC-16/CCITT-FALSE), and the P3.2/INT0 BOOT_REQ handoff. The sketch is intended to measure baseline code ROM, xRAM and internal RAM/stack use before adding Logitech vendor-specific behavior.

Build settings: CH552 Board, internal 16 MHz / 3.3 V, P3.6 D+ bootloader pin, USB RAM profile `user148`. The ch55xduino README documents 148 bytes reserved for USB endpoint RAM and 876 bytes remaining xRAM under this profile. The CH552 provides 16 KiB program ROM, 256 bytes internal iRAM and 1 KiB xRAM. These are capacity figures, not a measured build result.

The keyboard profile is only a temporary standard-HID stand-in. It does not implement G815/G502 descriptors or vendor control/report traffic, so its memory report is a lower-bound infrastructure measurement, not proof that the final Logitech emulator fits. No compiler is available in the current execution environment; the sketch has not been compiled, flashed or tested. Capture the ch55xduino compiler summary and generated map/mem files before drawing a capacity conclusion. No flash/configuration update is implemented.


## 6. CH552 USB runtime target: Logitech G815

The CH552 QT Py is assigned to emulate the Logitech G815 (`046D:C33F`). The ESP32-P4 USB Device remains assigned to the G502 HERO (`046D:C08B`), keeping the G502's approximately 1 kHz regular mouse-report path off the CH552/SPI hop.

The supplied `DESCRIPTORs.pcapng` summary identifies the G815 as a 59-byte configuration descriptor with two HID interfaces:

| Interface | Role | HID report descriptor length | IN endpoint | Maximum packet | bInterval |
|---|---|---:|---|---:|---:|
| 0 | Boot keyboard | 65 bytes | 0x81 | 8 bytes | 1 |
| 1 | Vendor-defined HID | 188 bytes | 0x82 | 64 bytes | 1 |

Both endpoints must be supported concurrently. At Full Speed, `bInterval=1` requests a 1 ms polling interval. The 64-byte vendor endpoint must be included in endpoint-memory sizing. Exact HID report-descriptor bytes must be extracted from the capture; do not synthesize them from their lengths.

The existing `firmware/qt_py_memory_test/qt_py_memory_test.ino` still uses a temporary single-interface ch55xduino keyboard stack. Its G815 profile constants are inventory only and do not yet implement the two-interface configuration. The meaningful memory-capacity measurement is the compile report after the exact G815 composite descriptor and both endpoints are implemented.

See [CH552 G815 USB profile](CH552_G815_USB_PROFILE.md) for the descriptor-derived requirements and acceptance criteria.
