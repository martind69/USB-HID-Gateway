
#pragma once
#include <stdbool.h>
#include "sdkconfig.h"
typedef enum {
    USB_GATEWAY_MODE_PASSTHROUGH = 0,
    USB_GATEWAY_MODE_PROXY,
#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
    USB_GATEWAY_MODE_SIMULATE,
#endif
} usb_gateway_mode_t;
void usb_gateway_init(void);
void usb_gateway_process(void);
void usb_gateway_set_mode(usb_gateway_mode_t mode);
usb_gateway_mode_t usb_gateway_get_mode(void);
