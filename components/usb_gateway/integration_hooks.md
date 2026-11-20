# Required integration hooks

## CMakeLists.txt

Add:

```cmake
"g815_capture.c"
```

## usb_gateway.c

During software initialization:

```c
#include "g815_capture.h"
...
(void)g815_capture_init();
```

## usb_host.c: after device descriptor and configuration descriptor are available

Start capture only for the target keyboard. Initially use the observed G815 VID/PID.
If VID/PID is not yet known, temporarily start for every newly enumerated device,
but disconnect all unrelated USB devices from the host port.

```c
g815_capture_device_info_t info = {
    .vendor_id = device_descriptor.idVendor,
    .product_id = device_descriptor.idProduct,
    .bcd_device = device_descriptor.bcdDevice,
    .address = device_address,
    .configuration_value = active_configuration,
    .interface_count = configuration_descriptor->bNumInterfaces,
};

ESP_ERROR_CHECK_WITHOUT_ABORT(g815_capture_start(&info));
ESP_ERROR_CHECK_WITHOUT_ABORT(g815_capture_write_device_descriptor(
    (const uint8_t *)&device_descriptor,
    sizeof(device_descriptor)));
ESP_ERROR_CHECK_WITHOUT_ABORT(g815_capture_write_configuration_descriptor(
    raw_configuration_descriptor,
    raw_configuration_length));
```

On detach:

```c
(void)g815_capture_event(
    G815_CAPTURE_EVENT_DETACH,
    0xFFU,
    0U,
    0U,
    NULL,
    0U,
    ESP_OK);
(void)g815_capture_stop();
```

## usb_host_hid.c: after fetching each HID report descriptor

```c
(void)g815_capture_write_report_descriptor(
    interface_number,
    report_descriptor,
    report_descriptor_length);
```

## usb_host_hid.c: input callback

```c
const uint8_t report_id = report_length > 0U ? report[0] : 0U;
(void)g815_capture_event(
    G815_CAPTURE_EVENT_INPUT_REPORT,
    interface_number,
    report_id,
    endpoint_address,
    report,
    report_length,
    ESP_OK);
```

## Physical-host output and feature helpers

Before/after each transfer log the actual payload and transfer result:

```c
(void)g815_capture_event(
    G815_CAPTURE_EVENT_OUTPUT_REPORT,
    interface_number,
    report_id,
    endpoint_address,
    report,
    report_length,
    transfer_result);
```

Use `G815_CAPTURE_EVENT_FEATURE_GET` and
`G815_CAPTURE_EVENT_FEATURE_SET` equivalently.

## EP0 control transfers

```c
g815_capture_setup_packet_t setup_log = {
    .bm_request_type = setup.bmRequestType,
    .b_request = setup.bRequest,
    .w_value = setup.wValue,
    .w_index = setup.wIndex,
    .w_length = setup.wLength,
};

(void)g815_capture_control(
    direction_in
        ? G815_CAPTURE_EVENT_CONTROL_DATA_IN
        : G815_CAPTURE_EVENT_CONTROL_DATA_OUT,
    &setup_log,
    transfer_buffer,
    actual_length,
    transfer_result);
```
