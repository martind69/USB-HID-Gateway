#pragma once

#include "esp_err.h"
#include "usb/usb_host.h"

esp_err_t usb_host_control_bridge_init(usb_host_client_handle_t client_handle);
void usb_host_control_bridge_attach(
    usb_device_handle_t device_handle,
    uint8_t device_address,
    uint16_t vendor_id,
    uint16_t product_id);
void usb_host_control_bridge_detach(uint8_t device_address);
