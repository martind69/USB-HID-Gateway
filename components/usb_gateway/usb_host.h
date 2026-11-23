#pragma once

#include <stdint.h>

#include "gateway_config.h"

#ifdef __cplusplus
extern "C" {
#endif

void usb_host_init(void);
void usb_host_process(void);

/*
 * Mirror target assigned to a device address by gateway.json.
 * GATEWAY_PORT_TARGET_NONE until the device is fully opened.
 */
gateway_port_target_t usb_host_route(uint8_t device_address);

#ifdef __cplusplus
}
#endif
