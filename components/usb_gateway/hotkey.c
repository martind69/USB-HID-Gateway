#include "hotkey.h"

static volatile usb_gateway_mode_t s_mode = USB_GATEWAY_MODE_PASSTHROUGH;

void hotkey_init(void)
{
    s_mode = USB_GATEWAY_MODE_PASSTHROUGH;
}

void hotkey_process(void)
{
}

void hotkey_set_mode(usb_gateway_mode_t mode)
{
#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
    if (mode > USB_GATEWAY_MODE_SIMULATE) {
        mode = USB_GATEWAY_MODE_PASSTHROUGH;
    }
#else
    if (mode > USB_GATEWAY_MODE_PROXY) {
        mode = USB_GATEWAY_MODE_PASSTHROUGH;
    }
#endif
    s_mode = mode;
}

usb_gateway_mode_t hotkey_get_mode(void)
{
    return s_mode;
}

const char *hotkey_mode_name(usb_gateway_mode_t mode)
{
    switch (mode) {
#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
        case USB_GATEWAY_MODE_SIMULATE:
            return "SIMULATE";
#endif
        case USB_GATEWAY_MODE_PROXY:
            return "PROXY";
        case USB_GATEWAY_MODE_PASSTHROUGH:
        default:
            return "PASS";
    }
}
