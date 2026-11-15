#include "usb_device.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "diagnostics.h"
#include "hid_proxy.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"
#include "hal/usb_serial_jtag_ll.h"

static const char *TAG = "usb_device";
static bool s_driver_installed;
static bool s_was_mounted;

static bool usb_device_forward_hid_report(
	uint8_t report_id,
	const uint8_t *payload,
	size_t payload_length,
	void *context)
{
	(void)context;

	if (!s_driver_installed || !tud_mounted() || !tud_hid_ready() ||
		payload == NULL || payload_length > UINT16_MAX) {
		return false;
	}
	return tud_hid_report(report_id, payload, (uint16_t)payload_length);
}

#if CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST
static size_t s_mouse_test_report_index;
static int64_t s_mouse_test_next_report_us;
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
	const int64_t now_us = esp_timer_get_time();

	/*
	 * Test sequence:
	 * 1. Move left by 24 units.
	 * 2. Press and hold the left mouse button.
	 * 3. Draw ten diagonal steps (+2 X, +2 Y) while holding the button.
	 * 4. Release the button.
	 *
	 * Each entry is a separate HID report. Relative movements are not summed
	 * in firmware; the host receives them in this exact sequence.
	 */
	static const int8_t reports[][5] = {
		{ 0, -24, 0, 0, 0 },
		{ 1, 0, 0, 0, 0 },
		{ 1, 2, 2, 0, 0 },
		{ 1, 2, 2, 0, 0 },
		{ 1, 2, 2, 0, 0 },
		{ 1, 2, 2, 0, 0 },
		{ 1, 2, 2, 0, 0 },
		{ 1, 2, 2, 0, 0 },
		{ 1, 2, 2, 0, 0 },
		{ 1, 2, 2, 0, 0 },
		{ 1, 2, 2, 0, 0 },
		{ 1, 2, 2, 0, 0 },
		{ 0, 0, 0, 0, 0 }
	};
	if (s_mouse_test_report_index >= (sizeof(reports) / sizeof(reports[0])) ||
		now_us < s_mouse_test_next_report_us || !tud_hid_ready()) {
		return;
	}

	if (tud_hid_report(1, reports[s_mouse_test_report_index],
		sizeof(reports[s_mouse_test_report_index]))) {
		ESP_LOGI(TAG, "Mouse test report %u/%u sent",
			(unsigned int)(s_mouse_test_report_index + 1),
			(unsigned int)(sizeof(reports) / sizeof(reports[0])));
		s_mouse_test_report_index++;
		s_mouse_test_next_report_us = now_us + 100000;
	}
}
#endif

void usb_device_init(void)
{
	tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG(usb_device_event_handler);
	esp_err_t result;

#if CONFIG_IDF_TARGET_ESP32P4
	/*
	 * Route USB Serial/JTAG to PHY1 (GPIO26/GPIO27) and USB OTG to PHY0
	 * (GPIO24/GPIO25), where the board's Type-C 6 / USB1P1 connector is wired.
	 * This is a runtime PHY selection; it does not program an eFuse.
	 */
	usb_serial_jtag_ll_phy_select(1);
	ESP_LOGI(TAG, "USB PHY mapping: USB OTG=GPIO24/25, USB Serial/JTAG=GPIO26/27");
#endif

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
	hid_proxy_set_output_callback(usb_device_forward_hid_report, NULL);
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
			s_mouse_test_next_report_us = 0;
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
