M7d SIMULATE measurement update
================================

Source: current .fulltree.txt supplied on 2026-11-27.

Hardware defaults documented in Kconfig:
    DAC-to-ADC series resistor: 68 kOhm
    Cext at ADS7128 AIN: 0 pF (change if physically fitted)
    ADC/DAC reference used for mV conversion: 3300 mV

Reference definition:
    The commanded 12-bit DAC code is the reference value. Therefore the
    result measures end-to-end DAC -> resistor/RC -> ADS7128 agreement. It is
    not a traceable absolute-voltage calibration unless VREF and the actual
    DAC output are independently measured.

Metrics, accumulated from entry into SIMULATE:
    Accuracy: MSE and RMSE of (ADC code - commanded DAC code).
    Trueness: mean error (bias).
    Precision: pooled within-waveform-phase standard deviation. Samples from
               equal phase bins of repeated 2 Hz periods are compared, so the
               deterministic waveform and RC response are not counted as
               random repeatability noise.

Channels:
    LX / VOUTA: A-D X axis, rising sawtooth
    LY / VOUTB: W-S Y axis, falling sawtooth
    RX / VOUTC: mouse X, sine
    RY / VOUTD: mouse Y, cosine

The statistics reset whenever SIMULATE is entered. Precision valid=1 only
once repeated samples exist in the same phase bins (normally after the second
500 ms waveform period). Results are printed with the normal diagnostics
period as MEASURE lines.

Replace all files at the included relative paths and run a full reconfigure.
