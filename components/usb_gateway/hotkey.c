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
    s_mode = mode;
}

usb_gateway_mode_t hotkey_get_mode(void)
{
    return s_mode;
}
