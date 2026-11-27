# Automated test start

Safe tests may run at boot when `CONFIG_USB_GATEWAY_SELF_TEST_SAFE_AT_BOOT=y`.
Hardware-active tests remain disabled at boot. Start them through the application API or reserved hotkey layer after physical confirmation.

Recommended hotkeys:
- Ctrl+Alt+F9: safe self-tests
- Ctrl+Alt+F10 held 3 s: hardware-active tests
- Ctrl+Alt+F11: verbose capture start/stop
- Ctrl+Alt+F12: profile export

See `HARDWARE_TEST_GUIDE.md`, `NEXT_VALIDATION_SEQUENCE.md` and `TEST_RESULT_TEMPLATE.md`.
