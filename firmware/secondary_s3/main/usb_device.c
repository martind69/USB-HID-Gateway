#include "usb_device.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"

static const char *TAG = "secondary_usb";
static bool s_driver_installed;

#define USB_CONFIG_DESC_LEN (TUD_CONFIG_DESC_LEN + (2U * TUD_HID_DESC_LEN))

static const uint8_t s_keyboard_report_descriptor[] = {
	TUD_HID_REPORT_DESC_KEYBOARD()
};

/*
 * Provisional vendor collection based on the observed Windows collection
 * report IDs and lengths. Replace with the exact raw USB descriptor capture.
 */
static const uint8_t s_vendor_report_descriptor[] = {
	0x06, 0x43, 0xFF,	/* Usage Page (Vendor 0xFF43) */
	0x09, 0x01,			/* Usage */
	0xA1, 0x01,			/* Collection (Application) */
	0x15, 0x00,			/* Logical Minimum (0) */
	0x26, 0xFF, 0x00,	/* Logical Maximum (255) */
	0x75, 0x08,			/* Report Size (8) */
	0x85, 0x11,			/* Report ID 0x11 */
	0x95, 0x13,			/* Report Count (19) */
	0x09, 0x01,
	0x81, 0x02,			/* Input (Data, Variable, Absolute) */
	0x85, 0x12,			/* Report ID 0x12 */
	0x95, 0x3F,			/* Report Count (63) */
	0x09, 0x02,
	0x81, 0x02,
	0xC0
};

static const tusb_desc_device_t s_device_descriptor = {
	.bLength = sizeof(tusb_desc_device_t),
	.bDescriptorType = TUSB_DESC_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = 0x00,
	.bDeviceSubClass = 0x00,
	.bDeviceProtocol = 0x00,
	.bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
	.idVendor = 0x046D,
	.idProduct = 0xC33F,
	.bcdDevice = 0x0100,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1
};

static const char *s_string_descriptor[] = {
	(char[]){ 0x09, 0x04 },
	"USB HID Gateway",
	"G815 Proxy",
	"USBHIDGW-S3",
	"Keyboard",
	"Vendor HID"
};

static const uint8_t s_configuration_descriptor[] = {
	TUD_CONFIG_DESCRIPTOR(
		1, 2, 0, USB_CONFIG_DESC_LEN,
		TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100
	),
	TUD_HID_DESCRIPTOR(
		0, 4, HID_ITF_PROTOCOL_KEYBOARD,
		sizeof(s_keyboard_report_descriptor),
		0x81, 8, 1
	),
	TUD_HID_DESCRIPTOR(
		1, 5, HID_ITF_PROTOCOL_NONE,
		sizeof(s_vendor_report_descriptor),
		0x82, 64, 1
	)
};

static void usb_event_handler(tinyusb_event_t *event, void *context)
{
	(void)context;

	if (event == NULL) {
		return;
	}

	switch (event->id) {
	case TINYUSB_EVENT_ATTACHED:
		ESP_LOGI(TAG, "Upstream USB host attached");
		break;
	case TINYUSB_EVENT_DETACHED:
		ESP_LOGI(TAG, "Upstream USB host detached");
		break;
	case TINYUSB_EVENT_SUSPENDED:
		ESP_LOGI(TAG, "Upstream USB host suspended device");
		break;
	case TINYUSB_EVENT_RESUMED:
		ESP_LOGI(TAG, "Upstream USB host resumed device");
		break;
	default:
		break;
	}
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
	switch (instance) {
	case 0:
		return s_keyboard_report_descriptor;
	case 1:
		return s_vendor_report_descriptor;
	default:
		return NULL;
	}
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

esp_err_t usb_device_init(void)
{
	tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG(usb_event_handler);
	esp_err_t result;

	config.port = TINYUSB_PORT_FULL_SPEED_0;
	config.descriptor.device = &s_device_descriptor;
	config.descriptor.string = s_string_descriptor;
	config.descriptor.string_count =
		sizeof(s_string_descriptor) / sizeof(s_string_descriptor[0]);
	config.descriptor.full_speed_config = s_configuration_descriptor;

	result = tinyusb_driver_install(&config);
	if (result != ESP_OK) {
		ESP_LOGE(TAG, "TinyUSB installation failed: %s",
			esp_err_to_name(result));
		return result;
	}

	s_driver_installed = true;
	ESP_LOGW(TAG, "G815 identity and dual-HID layout enabled; report descriptors remain provisional");
	return ESP_OK;
}

bool usb_device_is_mounted(void)
{
	return s_driver_installed && tud_mounted();
}

bool usb_device_send_keyboard_report(const uint8_t report[8])
{
	if (!usb_device_is_mounted() || report == NULL ||
		!tud_hid_n_ready(0)) {
		return false;
	}

	return tud_hid_n_report(0, 0, report, 8);
}

bool usb_device_send_vendor_report(
	uint8_t report_id,
	const uint8_t *payload,
	size_t payload_length)
{
	if (!usb_device_is_mounted() || payload == NULL ||
		payload_length > 63U || !tud_hid_n_ready(1)) {
		return false;
	}

	return tud_hid_n_report(1, report_id, payload,
		(uint16_t)payload_length);
}
