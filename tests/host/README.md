# Host-side unit tests

The parser and mapping tests are designed to run without ESP-IDF or target hardware.

Example with GCC:

```sh
gcc -std=c11 -Wall -Wextra -Werror -Icomponents/usb_gateway \
	components/usb_gateway/hid_parser.c tests/host/test_hid_parser.c \
	-o test_hid_parser
./test_hid_parser

gcc -std=c11 -Wall -Wextra -Werror -Icomponents/usb_gateway \
	components/usb_gateway/mapping.c tests/host/test_mapping.c \
	-o test_mapping
./test_mapping
```

These tests are not yet wired into ESP-IDF CMake or CI.
