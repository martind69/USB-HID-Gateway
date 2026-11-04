#pragma once

#include "usb_gateway.h"

void hotkey_init(void);
void hotkey_process(void);
void hotkey_set_mode(usb_gateway_mode_t mode);
usb_gateway_mode_t hotkey_get_mode(void);
