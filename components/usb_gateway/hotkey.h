
#pragma once

#include "usb_gateway.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Gateway mode (PASS / PROXY / optional SIMULATE). The mode hotkey is configured in output.json
 * and evaluated by analog_engine; the profile export key by profile_export.
 */

void hotkey_init(void);
void hotkey_process(void);
void hotkey_set_mode(usb_gateway_mode_t mode);
usb_gateway_mode_t hotkey_get_mode(void);
const char *hotkey_mode_name(usb_gateway_mode_t mode);

#ifdef __cplusplus
}
#endif
