# Host-side unit tests

These tests do not require ESP-IDF or target hardware.

```sh
gcc -std=c11 -Wall -Wextra -Werror -Icomponents/usb_gateway \
	components/usb_gateway/hid_parser.c tests/host/test_hid_parser.c \
	-o test_hid_parser
./test_hid_parser

gcc -std=c11 -Wall -Wextra -Werror -Icomponents/usb_gateway \
	components/usb_gateway/hid_parser.c \
	components/usb_gateway/hid_semantic.c \
	tests/host/test_hid_semantic.c -o test_hid_semantic
./test_hid_semantic
```

The tests cover independent keyboard/mouse parser contexts, report layout,
bit extraction, standard keyboard/mouse translation and disconnect release.
They are run by `.github/workflows/host-hid-tests.yml` on pushes and pull requests to `architecture/proxy-motion-gateway`. They are not part of the ESP-IDF target build.

The USB proxy descriptor-model tests validate complete Configuration descriptor
streams, interface alternate settings, HID report-descriptor metadata,
endpoint mappings, duplicate endpoints, and malformed lengths. They are also
run by the same GitHub Actions workflow.
