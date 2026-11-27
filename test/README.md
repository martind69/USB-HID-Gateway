# Host tests

`cJSON.h` / `cJSON_minimal.c` and `stubs/` are test-only replacements.
Never add them to a firmware component.

    cd test
    gcc -std=c17 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I. -I../components/usb_gateway -Istubs ^
        task3_host_test.c cJSON_minimal.c ^
        ../components/usb_gateway/hid_report_decoder.c ../components/usb_gateway/input_state.c ^
        ../components/usb_gateway/analog_mapping.c ../components/usb_gateway/output_config.c -lm -o task3_host_test
    task3_host_test
