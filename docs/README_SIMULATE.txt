SIMULATE direct DAC test mode
================================

Source basis: current .fulltree.txt supplied on 2026-11-26.

Replace the included files at the same relative paths.
Then run a full reconfigure because Kconfig and sdkconfig.defaults changed.

Mode hotkey cycle with CONFIG_USB_GATEWAY_SIMULATE_ENABLE=y:
    PASS -> PROXY -> SIMULATE -> PASS

Current output.json mapping:
    left stick, source keys:
        DAC A / VOUTA / logical LX: A-D X axis, rising 2 Hz sawtooth
        DAC B / VOUTB / logical LY: W-S Y axis, falling 2 Hz sawtooth
    right stick, source mouse:
        DAC C / VOUTC / logical RX: mouse X, 2 Hz sine
        DAC D / VOUTD / logical RY: mouse Y, 2 Hz cosine

The remaining DAC outputs E-H stay at their safe codes in SIMULATE.
SIMULATE does not require learned motion data and does not intercept
mouse/keyboard reports. A valid base profile is still needed for calibrated
min/center/max output codes.
