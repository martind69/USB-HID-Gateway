# Next validation sequence

## Gate 1: build and boot

- P4 and S3 clean build.
- Both boot for 10 min without reset/watchdog.
- Optional OLED absence must be nonfatal.
- GPIO0 stays low until analog power is explicitly enabled.

## Gate 2: feedback and shared SPI

- Verify LEDs active-high on GPIO1/4/5.
- Verify OLED GPIO23 reset pulse.
- Verify P4-S3 and OLED share GPIO6/7; only S3 drives GPIO8.
- Run OLED status changes while S3 transfers continue.

Acceptance: no SPI initialization conflict, no watchdog, no unexplained frame loss.

## Gate 3: profile storage

- Parser accepts reviewed schema-v1 fixture.
- Parser rejects each deliberately invalid fixture.
- Serializer round-trip across all 1440 cells.
- SHA-256 cache hit and invalidation.
- Power-loss recovery.

## Gate 4: analog

- Probe 0x10 and 0x4C at 1 MHz.
- Execute five-point test for each of eight channels.
- Record ADC error and measured voltage.
- Run 30 min continuous transfer.

## Gate 5: P4-S3 transport

- Pattern tests, CRC injection, wraparound, queue overflow and reset recovery.
- Then 1,000,000-frame soak test.

## Gate 6: USB

- Raw G815 Input byte comparison.
- Attach/detach/re-enumeration.
- Then Output, Feature and EP0 implementation/captures.
