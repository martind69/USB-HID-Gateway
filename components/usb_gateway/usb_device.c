#include "usb_device.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diagnostics.h"
#include "esp_err.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"

static const char *TAG = "usb_device";
static bool s_driver_installed;
static bool s_was_mounted;
#if CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST
static size_t s_mouse_test_report_index;
#endif

#define USB_HID_CONFIG_DESC_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

#if CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST
static const uint8_t s_hid_report_descriptor[] = {
	TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(1))
};

static const char *s_hid_string_descriptor[] = {
	(char[]){ 0x09, 0x04 },
	"Waveshare Project",
	"USB HID Gateway Mouse Test",
	"USBHIDGW-P4",
	"HID Mouse Test"
};

#else
static const uint8_t s_hid_report_descriptor[] = {
	TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
	TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE))
};

static const char *s_hid_string_descriptor[] = {
	(char[]){ 0x09, 0x04 },
	"Waveshare Project",
	"USB HID Gateway",
	"USBHIDGW-P4",
	"HID Keyboard and Mouse"
};
#endif

static const uint8_t s_hid_config_fs[] = {
	TUD_CONFIG_DESCRIPTOR(
		1,
		1,
		0,
		USB_HID_CONFIG_DESC_LEN,
		TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
		100
	),
	TUD_HID_DESCRIPTOR(
		0,
		4,
		HID_ITF_PROTOCOL_NONE,
		sizeof(s_hid_report_descriptor),
		0x81,
		64,
		1
	)
};

static void usb_device_event_handler(tinyusb_event_t *event, void *context)
{
	(void)context;

	if (event == NULL) {
		return;
	}

	switch (event->id) {
	case TINYUSB_EVENT_ATTACHED:
		ESP_LOGI(TAG, "USB host attached");
		break;
	case TINYUSB_EVENT_DETACHED:
		ESP_LOGI(TAG, "USB host detached");
		break;
	case TINYUSB_EVENT_SUSPENDED:
		ESP_LOGI(TAG, "PC USB host suspended the HID device");
		break;
	case TINYUSB_EVENT_RESUMED:
		ESP_LOGI(TAG, "PC USB host resumed the HID device");
		break;
	default:
		break;
	}
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
	(void)instance;
	return s_hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(
	uint8_t instance,
	uint8_t report_id,
	hid_report_type_t report_type,
	uint8_t *buffer,
	uint16_t requested_length)
{
	(void)instance;
	(void)report_id;
	(void)report_type;
	(void)buffer;
	(void)requested_length;

	return 0;
}

void tud_hid_set_report_cb(
	uint8_t instance,
	uint8_t report_id,
	hid_report_type_t report_type,
	uint8_t const *buffer,
	uint16_t buffer_size)
{
	(void)instance;
	(void)report_id;
	(void)report_type;
	(void)buffer;
	(void)buffer_size;
}

#if CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST
static void usb_device_mouse_test_process(void)
{
	static const int8_t reports[][5] = {
		{ 0, 24, 0, 0, 0 },
		{ 0, -24, 0, 0, 0 },
		{ 1, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0 }
	};
	if (s_mouse_test_report_index >= (sizeof(reports) / sizeof(reports[0])) ||
		!tud_hid_ready()) {
		return;
	}

	if (tud_hid_report(0, 1, reports[s_mouse_test_report_index],
		sizeof(reports[s_mouse_test_report_index]))) {
		ESP_LOGI(TAG, "Mouse test report %u sent",
			(unsigned int)(s_mouse_test_report_index + 1));
		s_mouse_test_report_index++;
	}
}
#endif

void usb_device_init(void)
{
	tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG(usb_device_event_handler);
	esp_err_t result;

	/*
	 * Type-C 6 is wired to GPIO24/GPIO25. On ESP32-P4 this is FS PHY1.
	 * The USB PHY/eFuse routing must assign FS PHY1 to USB OTG before this
	 * connector can enumerate as the TinyUSB device.
	 */
	config.port = TINYUSB_PORT_FULL_SPEED_0;

	config.descriptor.device = NULL;
	config.descriptor.string = s_hid_string_descriptor;
	config.descriptor.string_count =
		sizeof(s_hid_string_descriptor) / sizeof(s_hid_string_descriptor[0]);
	config.descriptor.full_speed_config = s_hid_config_fs;

#if CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST
	ESP_LOGW(TAG, "Standalone HID mouse test mode enabled");
#else
	ESP_LOGI(TAG, "Composite HID keyboard/mouse mode selected");
#endif

	ESP_LOGI(TAG, "Installing TinyUSB HID device stack on USB Full-Speed OTG port");
	result = tinyusb_driver_install(&config);
	if (result != ESP_OK) {
		ESP_LOGE(TAG, "TinyUSB device installation failed: %s",
			esp_err_to_name(result));
		diagnostics_set_device_state(DIAG_STATE_ERROR, result);
		return;
	}

	s_driver_installed = true;
	s_was_mounted = false;
	diagnostics_set_device_state(DIAG_STATE_READY, ESP_OK);
	ESP_LOGI(TAG, "HID device stack ready; waiting for PC enumeration");
}

void usb_device_process(void)
{
	bool mounted;

	if (!s_driver_installed) {
		return;
	}

	mounted = tud_mounted();
	if (mounted != s_was_mounted) {
		s_was_mounted = mounted;
		if (mounted) {
			ESP_LOGI(TAG, "PC enumerated USB HID Gateway");
			diagnostics_set_device_state(DIAG_STATE_CONNECTED, ESP_OK);
		} else {
			ESP_LOGI(TAG, "PC USB HID connection detached");
#if CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST
			s_mouse_test_report_index = 0;
#endif
			diagnostics_set_device_state(DIAG_STATE_READY, ESP_OK);
		}
	}

#if CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST
	if (mounted) {
		usb_device_mouse_test_process();
	}
#endif
}
