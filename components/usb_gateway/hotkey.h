#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "usb_gateway.h"
void hotkey_init(void);void hotkey_process(void);void hotkey_set_mode(usb_gateway_mode_t mode);usb_gateway_mode_t hotkey_get_mode(void);
bool hotkey_handle_keyboard_event(uint8_t modifiers,uint8_t usage,bool pressed);
void hotkey_handle_mode_key(bool pressed);
