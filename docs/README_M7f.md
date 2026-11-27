# M7f dynamic timing fix

Source: current `.fulltree 2.txt` supplied on 2026-11-27.

## Changes

- The static five-step measurement is unchanged.
- Every ADS7128 `RECENT_CHx` read receives its own timestamp immediately after
  the register transaction.
- Dynamic results report `effective_lag_us`. This is the complete effective
  path delay, not only analog delay.
- A bounded zero-order hold extends the newest DAC command by up to 3 ms. This
  prevents late-read channels, especially RY, from losing nearly all samples.
- Dynamic metrics become valid only after at least 1000 accepted samples.
- Alignment candidates also require at least 1000 accepted samples.
- Sawtooth discontinuity and adjacent intervals remain excluded.
- The heavy candidate search is used only during the 5 s ALIGN phase. The RUN
  phase evaluates one selected lag per channel.

## Expected log

`Measurement effective lag: LX=.../1 LY=.../1 RX=.../1 RY=.../1 us/valid`

`MEASURE_DYNAMIC RY/VOUTD effective_lag=... us n=>=1000 ... valid=1`

## Build

Replace all files at the same relative paths and run `reconfigure.bat` or a
full clean/reconfigure because Kconfig and `sdkconfig.defaults` changed.
