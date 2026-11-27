# Analog I/O Design

Status: Hardware design baseline. Exact schematic, voltage ranges, calibration topology and pin assignments remain open.

## 1. Selected design baseline

- MCU: ESP32-P4
- ADC: ADS7128, I²C
- DAC: DAC7578, I²C
- Logical analog outputs: four channels, forming left XY and right XY vectors
- Logical analog inputs: defined by the physical joystick/sensor feedback circuit; exact source assignment remains open

The ADS7128 and DAC7578 are current design selections, not a claim that the circuit has been assembled or electrically validated.

## 2. Driver separation

- `ADS7128Backend`: ADC register access, channel acquisition, device diagnostics.
- `DAC7578Backend`: DAC register access, output update and device diagnostics.
- `AnalogVectorIO`: calibration, normalization and conversion between physical values and logical XY vectors.

Do not put sensor calibration or joystick geometry into the low-level I²C drivers.

## 3. Input normalization

The logical pipeline is:

`ADC samples → channel calibration → normalized XY → center/deadzone → radial envelope`

Calibration must define per-channel offset, gain, polarity and valid physical range. Center calibration and deadzone must be distinct. Apply radial limiting to the combined vector rather than clipping X and Y independently.

The ADS7128 is multiplexed. Four channels are not sampled simultaneously; timestamp each sample or scan and account for channel skew in kinematic estimation. Required sample rate, I²C clock, averaging and filtering must be measured against the motion estimator's latency budget.

## 4. Output conversion

`normalized XY → calibrated output transform → DAC7578 channels`

Each axis requires explicit:
- output minimum and maximum voltage
- neutral voltage
- direction/polarity
- scale and offset
- slew/fault behavior if required by the analog load

Do not assume neutral equals DAC midscale. Do not assume ADC inputs are connected to DAC outputs; the schematic must define the actual signal source and feedback topology.

## 5. Fault and safe state

- Configure a known safe output state before enabling Proxy mode.
- On mode transition, set each DAC channel to its calibrated neutral value.
- On I²C failure, report the fault and follow a hardware-defined safe-state strategy.
- DAC power-up state, reference voltage, output load and external circuitry must be considered.
- Physical safe values are hardware configuration, not generic constants in the mapping layer.

## 6. Open hardware details

- Exact ADC channel-to-signal mapping.
- Exact analog input voltage range and input protection.
- DAC reference and output voltage range.
- Whether output buffering/amplification is required.
- Calibration procedure and storage format.
- Required scan rate, filtering and latency.
- I²C bus topology, address configuration, pull-ups and bus recovery.
- Board-level safe state during reset, brownout and MCU boot.
