#include "usb_host.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "diagnostics.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#define USB_HOST_CLIENT_EVENT_QUEUE_SIZE	8
#define USB_HOST_MAX_DEVICE_ADDRESS		128
#define USB_HOST_DAEMON_STACK_SIZE		4096
#define USB_HOST_CLIENT_STACK_SIZE		6144

typedef struct {
	bool open_pending;
	bool close_pending;
	bool opened;
	uint8_t address;
	usb_device_handle_t handle;
} usb_host_device_slot_t;

static const char *TAG = "usb_host";
static usb_host_client_handle_t s_client_handle;
static usb_host_device_slot_t s_devices[USB_HOST_MAX_DEVICE_ADDRESS];

static void usb_host_library_task(void *arg)
{
	(void)arg;

	while (true) {
		uint32_t event_flags = 0;
		esp_err_t result = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

		if (result != ESP_OK) {
			ESP_LOGE(TAG, "USB Host library event handling failed: %s",
				esp_err_to_name(result));
			continue;
		}

		if ((event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) != 0) {
			ESP_LOGI(TAG, "USB Host has no registered clients");
		}

		if ((event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) != 0) {
			ESP_LOGI(TAG, "USB Host has released all device resources");
		}
	}
}

static void usb_host_client_event_cb(
	const usb_host_client_event_msg_t *event,
	void *arg)
{
	(void)arg;

	if (event == NULL) {
		return;
	}

	switch (event->event) {
	case USB_HOST_CLIENT_EVENT_NEW_DEV:
		if (event->new_dev.address < USB_HOST_MAX_DEVICE_ADDRESS) {
			usb_host_device_slot_t *slot =
				&s_devices[event->new_dev.address];

			slot->address = event->new_dev.address;
			slot->open_pending = true;
			ESP_LOGI(TAG, "Device enumerated, address=%u",
				(unsigned int)event->new_dev.address);
		}
		break;

	case USB_HOST_CLIENT_EVENT_DEV_GONE:
		for (size_t i = 1; i < USB_HOST_MAX_DEVICE_ADDRESS; i++) {
			if (s_devices[i].opened &&
				s_devices[i].handle == event->dev_gone.dev_hdl) {
				s_devices[i].close_pending = true;
				break;
			}
		}
		break;

	case USB_HOST_CLIENT_EVENT_DEV_REMOVED:
		ESP_LOGI(TAG, "Device removed, address=%u",
			(unsigned int)event->dev_removed.address);
		break;

	case USB_HOST_CLIENT_EVENT_DEV_SUSPENDED:
		ESP_LOGI(TAG, "USB device suspended");
		break;

	case USB_HOST_CLIENT_EVENT_DEV_RESUMED:
		ESP_LOGI(TAG, "USB device resumed");
		break;

	default:
		ESP_LOGW(TAG, "Unhandled USB Host client event: %d",
			(int)event->event);
		break;
	}
}

static void usb_host_open_device(usb_host_device_slot_t *slot)
{
	usb_device_info_t info = { 0 };
	const usb_device_desc_t *device_desc = NULL;
	const usb_config_desc_t *config_desc = NULL;
	diagnostics_host_device_t diagnostic_device = { 0 };
	esp_err_t result;

	result = usb_host_device_open(s_client_handle, slot->address,
		&slot->handle);
	if (result != ESP_OK) {
		ESP_LOGE(TAG, "Cannot open USB device at address %u: %s",
			slot->address, esp_err_to_name(result));
		return;
	}

	slot->opened = true;

	result = usb_host_device_info(slot->handle, &info);
	if (result != ESP_OK) {
		ESP_LOGE(TAG, "Cannot read USB device info at address %u: %s",
			slot->address, esp_err_to_name(result));
		goto error_close;
	}

	result = usb_host_get_device_descriptor(slot->handle, &device_desc);
	if (result != ESP_OK || device_desc == NULL) {
		ESP_LOGE(TAG, "Cannot read device descriptor at address %u: %s",
			slot->address, esp_err_to_name(result));
		goto error_close;
	}

	ESP_LOGI(TAG,
		"USB device addr=%u VID=%04X PID=%04X speed=%s configuration=%u",
		(unsigned int)slot->address,
		(unsigned int)device_desc->idVendor,
		(unsigned int)device_desc->idProduct,
		(info.speed == USB_SPEED_HIGH) ? "HS" :
		(info.speed == USB_SPEED_FULL) ? "FS" : "LS",
		(unsigned int)info.bConfigurationValue);

	result = usb_host_get_active_config_descriptor(slot->handle,
		&config_desc);
	if (result == ESP_OK && config_desc != NULL) {
		ESP_LOGI(TAG,
			"USB configuration addr=%u interfaces=%u attributes=0x%02X",
			(unsigned int)slot->address,
			(unsigned int)config_desc->bNumInterfaces,
			config_desc->bmAttributes);

		diagnostic_device.interface_count = config_desc->bNumInterfaces;
	} else {
		ESP_LOGW(TAG, "Configuration descriptor unavailable for addr=%u: %s",
			slot->address, esp_err_to_name(result));
	}

	diagnostic_device.present = true;
	diagnostic_device.address = slot->address;
	diagnostic_device.vid = device_desc->idVendor;
	diagnostic_device.pid = device_desc->idProduct;
	diagnostics_host_device_added(&diagnostic_device);
	return;

error_close:
	usb_host_device_close(s_client_handle, slot->handle);
	slot->handle = NULL;
	slot->opened = false;
}

static void usb_host_close_device(usb_host_device_slot_t *slot)
{
	if (slot->opened && slot->handle != NULL) {
		diagnostics_host_device_removed(slot->address);

		esp_err_t result = usb_host_device_close(
			s_client_handle, slot->handle);

		if (result != ESP_OK) {
			ESP_LOGW(TAG, "Closing USB device addr=%u failed: %s",
				(unsigned int)slot->address, esp_err_to_name(result));
		}
	}

	memset(slot, 0, sizeof(*slot));
}

static void usb_host_process_pending_devices(void)
{
	for (size_t i = 1; i < USB_HOST_MAX_DEVICE_ADDRESS; i++) {
		usb_host_device_slot_t *slot = &s_devices[i];

		if (slot->close_pending) {
			usb_host_close_device(slot);
			continue;
		}

		if (slot->open_pending) {
			slot->open_pending = false;
			usb_host_open_device(slot);
		}
	}
}

static void usb_host_client_task(void *arg)
{
	(void)arg;

	const usb_host_client_config_t client_config = {
		.is_synchronous = false,
		.max_num_event_msg = USB_HOST_CLIENT_EVENT_QUEUE_SIZE,
		.async = {
			.client_event_callback = usb_host_client_event_cb,
			.callback_arg = NULL
		}
	};

	esp_err_t result = usb_host_client_register(
		&client_config, &s_client_handle);
	if (result != ESP_OK) {
		ESP_LOGE(TAG, "USB Host client registration failed: %s",
			esp_err_to_name(result));
		diagnostics_set_host_state(DIAG_STATE_ERROR, result);
		vTaskDelete(NULL);
		return;
	}

	ESP_LOGI(TAG, "USB Host client registered; waiting for devices");

	while (true) {
		result = usb_host_client_handle_events(
			s_client_handle, pdMS_TO_TICKS(100));

		if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
			ESP_LOGE(TAG, "USB Host client event handling failed: %s",
				esp_err_to_name(result));
		}

		usb_host_process_pending_devices();
	}
}

void usb_host_init(void)
{
	const usb_host_config_t host_config = {
		/*
		 * The Logitech G815/G502 are Full-Speed devices.
		 * Running the HS OTG controller in FS/LS-only mode also avoids
		 * the missing Transaction Translator support for FS/LS devices
		 * behind a High-Speed hub in the current ESP-IDF hub driver.
		 */
		.fsls_only = true
	};

	esp_err_t result = usb_host_install(&host_config);
	if (result != ESP_OK) {
		ESP_LOGE(TAG, "USB Host installation failed: %s",
			esp_err_to_name(result));
		diagnostics_set_host_state(DIAG_STATE_ERROR, result);
		return;
	}

	BaseType_t task_result = xTaskCreate(
		usb_host_library_task,
		"usb_host_lib",
		USB_HOST_DAEMON_STACK_SIZE,
		NULL,
		configMAX_PRIORITIES - 3,
		NULL);

	if (task_result != pdPASS) {
		ESP_LOGE(TAG, "Cannot create USB Host library task");
		diagnostics_set_host_state(DIAG_STATE_ERROR, ESP_ERR_NO_MEM);
		return;
	}

	task_result = xTaskCreate(
		usb_host_client_task,
		"usb_host_client",
		USB_HOST_CLIENT_STACK_SIZE,
		NULL,
		configMAX_PRIORITIES - 4,
		NULL);

	if (task_result != pdPASS) {
		ESP_LOGE(TAG, "Cannot create USB Host client task");
		diagnostics_set_host_state(DIAG_STATE_ERROR, ESP_ERR_NO_MEM);
		return;
	}

	diagnostics_set_host_state(DIAG_STATE_READY, ESP_OK);
	ESP_LOGI(TAG,
		"USB Host stack installed on HS OTG in FS/LS-only mode; waiting for USB devices");
}

void usb_host_process(void)
{
	/* USB Host library and client events are handled in dedicated tasks. */
}
