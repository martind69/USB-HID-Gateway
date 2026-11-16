#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define USB_PROXY_DESCRIPTOR_MAX_CONFIG_SIZE 1024U
#define USB_PROXY_DESCRIPTOR_MAX_INTERFACES 16U
#define USB_PROXY_DESCRIPTOR_MAX_ENDPOINTS 32U
#define USB_PROXY_DESCRIPTOR_MAX_HID_DESCRIPTORS 16U

typedef enum {
	USB_PROXY_DESC_OK = 0,
	USB_PROXY_DESC_ERR_ARGUMENT,
	USB_PROXY_DESC_ERR_LENGTH,
	USB_PROXY_DESC_ERR_TYPE,
	USB_PROXY_DESC_ERR_CAPACITY,
	USB_PROXY_DESC_ERR_INTERFACE,
	USB_PROXY_DESC_ERR_ENDPOINT,
	USB_PROXY_DESC_ERR_DUPLICATE
} usb_proxy_descriptor_result_t;

typedef struct {
	uint8_t interface_number;
	uint8_t alternate_setting;
	uint8_t interface_class;
	uint8_t interface_subclass;
	uint8_t interface_protocol;
	uint8_t endpoint_count;
	uint8_t declared_endpoint_count;
} usb_proxy_interface_t;

typedef struct {
	uint8_t interface_number;
	uint8_t alternate_setting;
	uint8_t address;
	uint8_t attributes;
	uint16_t max_packet_size;
	uint8_t interval;
} usb_proxy_endpoint_t;

typedef struct {
	uint8_t interface_number;
	uint8_t alternate_setting;
	uint16_t report_descriptor_length;
} usb_proxy_hid_descriptor_t;

typedef struct {
	uint8_t configuration_value;
	uint8_t attributes;
	uint8_t max_power;
	uint8_t interface_count;
	uint8_t endpoint_count;
	uint8_t hid_descriptor_count;
	uint16_t total_length;
	uint8_t raw[USB_PROXY_DESCRIPTOR_MAX_CONFIG_SIZE];
	usb_proxy_interface_t interfaces[USB_PROXY_DESCRIPTOR_MAX_INTERFACES];
	usb_proxy_endpoint_t endpoints[USB_PROXY_DESCRIPTOR_MAX_ENDPOINTS];
	usb_proxy_hid_descriptor_t hid_descriptors[USB_PROXY_DESCRIPTOR_MAX_HID_DESCRIPTORS];
} usb_proxy_configuration_t;

/*
 * Parse and validate a complete USB Configuration descriptor stream.
 * The input buffer is copied into the model so its lifetime is independent
 * of the USB Host descriptor cache.
 */
usb_proxy_descriptor_result_t usb_proxy_configuration_parse(
	usb_proxy_configuration_t *configuration,
	const uint8_t *descriptor,
	size_t descriptor_length);

const usb_proxy_interface_t *usb_proxy_configuration_find_interface(
	const usb_proxy_configuration_t *configuration,
	uint8_t interface_number,
	uint8_t alternate_setting);

const usb_proxy_endpoint_t *usb_proxy_configuration_find_endpoint(
	const usb_proxy_configuration_t *configuration,
	uint8_t endpoint_address,
	uint8_t interface_number,
	uint8_t alternate_setting);
