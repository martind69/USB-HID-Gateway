#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hid_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_HOST_HID_MAX_INTERFACES 8U
#define USB_HOST_HID_MAX_REPORT_DESCRIPTOR_SIZE 2048U

typedef struct {
	uint8_t interface_id;
	uint8_t device_address;
	uint8_t interface_number;
	uint8_t interface_subclass;
	uint8_t interface_protocol;
	uint16_t vendor_id;
	uint16_t product_id;
	bool descriptor_parsed;
	size_t report_descriptor_length;
} usb_host_hid_interface_info_t;

esp_err_t usb_host_hid_init(void);
size_t usb_host_hid_get_interface_count(void);
bool usb_host_hid_get_interface_info(
	uint8_t interface_id,
	usb_host_hid_interface_info_t *info);
bool usb_host_hid_copy_parser_context(
	uint8_t interface_id,
	hid_parser_context_t *context);
bool usb_host_hid_copy_report_descriptor(
	uint8_t interface_id,
	uint8_t *buffer,
	size_t buffer_capacity,
	size_t *descriptor_length);

#ifdef __cplusplus
}
#endif
