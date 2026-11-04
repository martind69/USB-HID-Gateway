#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	USB_GATEWAY_MODE_PASSTHROUGH = 0,
	USB_GATEWAY_MODE_HOTKEY
} usb_gateway_mode_t;

void usb_gateway_init(void);
void usb_gateway_process(void);
void usb_gateway_set_mode(usb_gateway_mode_t mode);
usb_gateway_mode_t usb_gateway_get_mode(void);

#ifdef __cplusplus
}
#endif
