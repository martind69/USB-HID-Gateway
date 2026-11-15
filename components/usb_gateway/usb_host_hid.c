#include "usb_host_hid.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "usb/hid_host.h"
#include "hid_proxy.h"

#define USB_HOST_HID_REPORT_BUFFER_SIZE 64U

typedef struct {
	bool active;
	bool opened;
	uint8_t interface_id;
	hid_host_device_handle_t handle;
	hid_host_dev_params_t params;
	hid_host_dev_info_t device_info;
	uint8_t report_descriptor[USB_HOST_HID_MAX_REPORT_DESCRIPTOR_SIZE];
	size_t report_descriptor_length;
	bool descriptor_parsed;
	hid_parser_context_t parser;
} usb_host_hid_interface_t;

static const char *TAG = "usb_host_hid";
static usb_host_hid_interface_t s_interfaces[USB_HOST_HID_MAX_INTERFACES];
static uint8_t s_next_interface_id = 1U;
static size_t s_interface_count;

static usb_host_hid_interface_t *find_interface(
	hid_host_device_handle_t handle)
{
	size_t i;

	for (i = 0U; i < USB_HOST_HID_MAX_INTERFACES; ++i) {
		if (s_interfaces[i].active && s_interfaces[i].handle == handle) {
			return &s_interfaces[i];
		}
	}
	return NULL;
}

static usb_host_hid_interface_t *allocate_interface(
	hid_host_device_handle_t handle)
{
	size_t i;

	for (i = 0U; i < USB_HOST_HID_MAX_INTERFACES; ++i) {
		if (!s_interfaces[i].active) {
			usb_host_hid_interface_t *interface = &s_interfaces[i];

			memset(interface, 0, sizeof(*interface));
			interface->active = true;
			interface->handle = handle;
			interface->interface_id = s_next_interface_id++;
			if (s_next_interface_id == 0U) {
				s_next_interface_id = 1U;
			}
			hid_parser_init(&interface->parser);
			s_interface_count++;
			return interface;
		}
	}
	return NULL;
}

static void release_interface(usb_host_hid_interface_t *interface)
{
	if (interface == NULL || !interface->active) {
		return;
	}

	if (!hid_proxy_enqueue_interface_removed(interface->interface_id)) {
		ESP_LOGE(TAG, "Cannot queue interface removal; proxy queue is full");
	}

	if (interface->opened) {
		esp_err_t result = hid_host_device_close(interface->handle);
		if (result != ESP_OK) {
			ESP_LOGW(TAG, "HID interface close failed: %s",
				esp_err_to_name(result));
		}
	}

	memset(interface, 0, sizeof(*interface));
	if (s_interface_count > 0U) {
		s_interface_count--;
	}
}

static void hid_host_interface_event_callback(
	hid_host_device_handle_t handle,
	const hid_host_interface_event_t event,
	void *arg)
{
	usb_host_hid_interface_t *interface = (usb_host_hid_interface_t *)arg;

	if (interface == NULL || !interface->active || interface->handle != handle) {
		return;
	}

	switch (event) {
	case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
		uint8_t report[USB_HOST_HID_REPORT_BUFFER_SIZE];
		size_t report_length = 0U;
		esp_err_t result = hid_host_device_get_raw_input_report_data(
			handle, report, sizeof(report), &report_length);

		if (result == ESP_OK && report_length > 0U) {
			if (!hid_proxy_enqueue_raw_report(
				interface->interface_id, report, report_length)) {
				ESP_LOGW(TAG, "Raw report queue full; interface=%u length=%u",
					(unsigned int)interface->interface_id,
					(unsigned int)report_length);
			}
		} else if (result != ESP_OK) {
			ESP_LOGW(TAG, "Cannot copy HID input report: %s",
				esp_err_to_name(result));
		}
		break;
	}

	case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
		ESP_LOGW(TAG, "HID input transfer error; interface=%u",
			(unsigned int)interface->interface_id);
		break;

	case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "HID interface disconnected; interface=%u",
			(unsigned int)interface->interface_id);
		release_interface(interface);
		break;

	default:
		break;
	}
}

static void hid_host_driver_event_callback(
	hid_host_device_handle_t handle,
	const hid_host_driver_event_t event,
	void *arg)
{
	usb_host_hid_interface_t *interface;
	hid_host_device_config_t config;
	uint8_t *descriptor;
	size_t descriptor_length = 0U;
	esp_err_t result;

	(void)arg;

	if (event != HID_HOST_DRIVER_EVENT_CONNECTED) {
		return;
	}

	interface = find_interface(handle);
	if (interface != NULL) {
		return;
	}

	interface = allocate_interface(handle);
	if (interface == NULL) {
		ESP_LOGE(TAG, "HID interface capacity exhausted");
		return;
	}

	result = hid_host_device_get_params(handle, &interface->params);
	if (result != ESP_OK) {
		ESP_LOGE(TAG, "Cannot read HID interface parameters: %s",
			esp_err_to_name(result));
		release_interface(interface);
		return;
	}

	config.callback = hid_host_interface_event_callback;
	config.callback_arg = interface;
	result = hid_host_device_open(handle, &config);
	if (result != ESP_OK) {
		ESP_LOGE(TAG, "Cannot open HID interface %u: %s",
			(unsigned int)interface->params.iface_num,
			esp_err_to_name(result));
		release_interface(interface);
		return;
	}
	interface->opened = true;

	result = hid_host_get_device_info(handle, &interface->device_info);
	if (result != ESP_OK) {
		ESP_LOGW(TAG, "Cannot read HID device information: %s",
			esp_err_to_name(result));
	}

	descriptor = hid_host_get_report_descriptor(handle, &descriptor_length);
	if (descriptor != NULL && descriptor_length > 0U) {
		if (descriptor_length <= sizeof(interface->report_descriptor)) {
			memcpy(interface->report_descriptor, descriptor, descriptor_length);
			interface->report_descriptor_length = descriptor_length;
			result = hid_parser_parse_report(
				&interface->parser, descriptor, descriptor_length);
			interface->descriptor_parsed = (result == HID_PARSER_OK);
			if (interface->parser.top_level_usage_page == 0U &&
				interface->params.sub_class == 1U) {
				if (interface->params.proto == 1U) {
					interface->parser.top_level_usage_page = 0x01U;
					interface->parser.top_level_usage = 0x06U;
				} else if (interface->params.proto == 2U) {
					interface->parser.top_level_usage_page = 0x01U;
					interface->parser.top_level_usage = 0x02U;
				}
			}
		} else {
			ESP_LOGW(TAG, "HID report descriptor too large: %u bytes",
				(unsigned int)descriptor_length);
		}
	} else {
		ESP_LOGW(TAG, "HID report descriptor unavailable");
	}

	ESP_LOGI(TAG,
		"HID interface open: id=%u addr=%u iface=%u VID=%04X PID=%04X subclass=%u protocol=%u descriptor=%u parsed=%u",
		(unsigned int)interface->interface_id,
		(unsigned int)interface->params.addr,
		(unsigned int)interface->params.iface_num,
		(unsigned int)interface->device_info.VID,
		(unsigned int)interface->device_info.PID,
		(unsigned int)interface->params.sub_class,
		(unsigned int)interface->params.proto,
		(unsigned int)interface->report_descriptor_length,
		(unsigned int)interface->descriptor_parsed);

	result = hid_host_device_start(handle);
	if (result != ESP_OK) {
		ESP_LOGE(TAG, "Cannot start HID input reports: %s",
			esp_err_to_name(result));
		release_interface(interface);
	}
}

esp_err_t usb_host_hid_init(void)
{
	const hid_host_driver_config_t config = {
		.create_background_task = true,
		.task_priority = 5U,
		.stack_size = 4096U,
		.core_id = tskNO_AFFINITY,
		.callback = hid_host_driver_event_callback,
		.callback_arg = NULL
	};

	memset(s_interfaces, 0, sizeof(s_interfaces));
	s_next_interface_id = 1U;
	s_interface_count = 0U;

	return hid_host_install(&config);
}

size_t usb_host_hid_get_interface_count(void)
{
	return s_interface_count;
}

bool usb_host_hid_get_interface_info(
	uint8_t interface_id,
	usb_host_hid_interface_info_t *info)
{
	size_t i;

	if (info == NULL) {
		return false;
	}

	for (i = 0U; i < USB_HOST_HID_MAX_INTERFACES; ++i) {
		const usb_host_hid_interface_t *interface = &s_interfaces[i];

		if (interface->active && interface->interface_id == interface_id) {
			info->interface_id = interface->interface_id;
			info->device_address = interface->params.addr;
			info->interface_number = interface->params.iface_num;
			info->interface_subclass = interface->params.sub_class;
			info->interface_protocol = interface->params.proto;
			info->vendor_id = interface->device_info.VID;
			info->product_id = interface->device_info.PID;
			info->descriptor_parsed = interface->descriptor_parsed;
			info->report_descriptor_length = interface->report_descriptor_length;
			info->report_descriptor = interface->report_descriptor;
			info->parser_context = &interface->parser;
			return true;
		}
	}
	return false;
}
