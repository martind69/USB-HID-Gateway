# Reconstruction status

Reconstructed or repaired:

- complete CMake source inventory;
- cJSON component-manager dependency;
- Motion public headers;
- Gateway mode/hotkey API;
- digital-output implementation with disabled-by-default GPIOs;
- profile SHA-256 binary cache;
- asynchronous profile export;
- integrated gateway bootstrap retaining the functional USB/HID runtime;
- deprecated `dac_output` converted into a non-misleading compatibility stub;
- S3 GPIO9-13 Kconfig.

Retain the recovered functional implementations of diagnostics, HID parser, HID proxy, HID semantic, mapping, P4 USB Device, USB Host and USB Host HID. They are not duplicated in this overlay.

Still intentionally open:

- GPIO assignment for the twelve digital outputs;
- SSD1306 font renderer;
- P4/S3 STATUS correlation and reverse Output/Feature/EP0 routes;
- exact G815/G502 descriptors and Logitech captures;
- SD mount implementation and full verbose-log rotation.
