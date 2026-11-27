# M7e static and time-aligned dynamic measurement

Source: current `.fulltree 1.txt` supplied on 2026-11-27.

## Sequence after entering SIMULATE

1. Five static axis points: -0.90, -0.50, 0.00, +0.50, +0.90.
2. Each point settles for 10 ms.
3. 256 ADC readings per point and channel are collected.
4. Static Accuracy, Trueness, and Precision are finalized.
5. The 2 Hz waveforms start.
6. For 5 s the firmware searches the best ADC-to-DAC time alignment from
   0 to 12 ms in 250 us steps for every channel independently.
7. Dynamic RMSE, bias, and maximum absolute error are accumulated using the
   selected delay. Sawtooth reset transitions are excluded.

## Definitions

- Static Accuracy: RMSE across all settled samples and five reference points.
- Static Trueness: mean ADC minus commanded DAC code.
- Static Precision: pooled within-step standard deviation.
- Dynamic Accuracy: RMSE after timestamp alignment and linear interpolation of
  the actual DAC-command history.
- Dynamic Trueness: aligned mean error.
- Dynamic peak error: largest aligned absolute error.

## Default hardware documentation

- Series resistor: 68 kOhm.
- Cext: 0 pF.
- Reference used for code-to-mV display: 3300 mV.

## Files

Replace all included files at the same relative paths. Run `reconfigure.bat`
or a full clean/reconfigure because Kconfig and `sdkconfig.defaults` changed.
