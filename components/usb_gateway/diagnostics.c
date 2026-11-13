#include "diagnostics.h"

#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

static const char *TAG = "gateway_diag";

typedef struct {
	diagnostics_state_t state;
	int error_code;
	uint32_t errors;
} diagnostics_component_t;

typedef struct {
	uint8_t bus;
	uint8_t address;
	diagnostics_state_t state;
	int error_code;
	uint32_t errors;
} diagnostics_i2c_t;

static diagnostics_component_t s_host;
static diagnostics_component_t s_device;
static diagnostics_component_t s_proxy;
static diagnostics_i2c_t s_i2c[4];
static diagnostics_host_device_t s_devices[DIAGNOSTICS_MAX_HOST_DEVICES];
static uint32_t s_proxy_forwarded;
static uint32_t s_proxy_processed;
static uint32_t s_hid_errors;
static uint32_t s_i2c_errors;
static int64_t s_last_log_us;

static const char *state_name(diagnostics_state_t state)
{
	switch (state) {
	case DIAG_STATE_NOT_INITIALIZED: return "not-initialized";
	case DIAG_STATE_READY: return "ready";
	case DIAG_STATE_CONNECTED: return "connected";
	case DIAG_STATE_ERROR: return "error";
	case DIAG_STATE_UNAVAILABLE: return "unavailable";
	default: return "unknown";
	}
}

static size_t host_device_count(void)
{
	size_t count = 0;

	for (size_t i = 0; i < DIAGNOSTICS_MAX_HOST_DEVICES; ++i) {
		if (s_devices[i].present) {
			++count;
		}
	}

	return count;
}

void diagnostics_init(void)
{
	memset(&s_host, 0, sizeof(s_host));
	memset(&s_device, 0, sizeof(s_device));
	memset(&s_proxy, 0, sizeof(s_proxy));
	memset(s_i2c, 0, sizeof(s_i2c));
	memset(s_devices, 0, sizeof(s_devices));

	s_host.state = DIAG_STATE_NOT_INITIALIZED;
	s_device.state = DIAG_STATE_NOT_INITIALIZED;
	s_proxy.state = DIAG_STATE_NOT_INITIALIZED;

	ESP_LOGI(TAG, "Diagnostics initialized");
}

void diagnostics_set_host_state(diagnostics_state_t state, int error_code)
{
	s_host.state = state;
	s_host.error_code = error_code;
	if (state == DIAG_STATE_ERROR) {
		++s_host.errors;
	}
}

void diagnostics_set_device_state(diagnostics_state_t state, int error_code)
{
	s_device.state = state;
	s_device.error_code = error_code;
	if (state == DIAG_STATE_ERROR) {
		++s_device.errors;
	}
}

void diagnostics_set_proxy_state(diagnostics_state_t state, int error_code)
{
	s_proxy.state = state;
	s_proxy.error_code = error_code;
	if (state == DIAG_STATE_ERROR) {
		++s_proxy.errors;
	}
}

void diagnostics_set_i2c_state(uint8_t bus, uint8_t address,
	diagnostics_state_t state, int error_code)
{
	for (size_t i = 0; i < 4; ++i) {
		if (s_i2c[i].bus == bus && s_i2c[i].address == address) {
			s_i2c[i].state = state;
			s_i2c[i].error_code = error_code;
			if (state == DIAG_STATE_ERROR) {
				++s_i2c[i].errors;
			}
			return;
		}
	}

	for (size_t i = 0; i < 4; ++i) {
		if (s_i2c[i].state == DIAG_STATE_UNKNOWN) {
			s_i2c[i].bus = bus;
			s_i2c[i].address = address;
			s_i2c[i].state = state;
			s_i2c[i].error_code = error_code;
			if (state == DIAG_STATE_ERROR) {
				++s_i2c[i].errors;
			}
			return;
		}
	}
}

void diagnostics_host_device_added(const diagnostics_host_device_t *device)
{
	if (device == NULL) {
		return;
	}

	for (size_t i = 0; i < DIAGNOSTICS_MAX_HOST_DEVICES; ++i) {
		if (s_devices[i].present &&
			s_devices[i].address == device->address) {
			s_devices[i] = *device;
			s_devices[i].present = true;
			return;
		}
	}

	for (size_t i = 0; i < DIAGNOSTICS_MAX_HOST_DEVICES; ++i) {
		if (!s_devices[i].present) {
			s_devices[i] = *device;
			s_devices[i].present = true;
			ESP_LOGI(TAG, "Host device + addr=%u VID:PID=%04X:%04X "
				"manufacturer='%s' product='%s' interfaces=%u HID=%u",
				device->address, device->vid, device->pid,
				device->manufacturer, device->product,
				device->interface_count, device->hid_interface_count);
			return;
		}
	}

	ESP_LOGE(TAG, "Host device table full (max=%u)",
		(unsigned)DIAGNOSTICS_MAX_HOST_DEVICES);
}

void diagnostics_host_device_removed(uint8_t address)
{
	for (size_t i = 0; i < DIAGNOSTICS_MAX_HOST_DEVICES; ++i) {
		if (s_devices[i].present && s_devices[i].address == address) {
			ESP_LOGI(TAG, "Host device - addr=%u VID:PID=%04X:%04X",
				address, s_devices[i].vid, s_devices[i].pid);
			memset(&s_devices[i], 0, sizeof(s_devices[i]));
			return;
		}
	}
}

void diagnostics_report_hid_enum(uint8_t address, uint8_t interface_number,
	uint8_t report_id, uint16_t report_length)
{
	ESP_LOGI(TAG, "HID enum: addr=%u interface=%u report_id=%u "
		"report_descriptor_length=%u",
		address, interface_number, report_id, report_length);
}

void diagnostics_report_proxy_forwarded(void)
{
	++s_proxy_forwarded;
}

void diagnostics_report_proxy_processed(void)
{
	++s_proxy_processed;
}

void diagnostics_report_proxy_error(int error_code)
{
	++s_proxy.errors;
	s_proxy.state = DIAG_STATE_ERROR;
	s_proxy.error_code = error_code;
}

void diagnostics_report_hid_error(int error_code)
{
	++s_hid_errors;
	ESP_LOGE(TAG, "HID parser/routing error=%d total=%lu",
		error_code, (unsigned long)s_hid_errors);
}

void diagnostics_report_i2c_error(uint8_t bus, uint8_t address,
	int error_code)
{
	++s_i2c_errors;
	diagnostics_set_i2c_state(bus, address, DIAG_STATE_ERROR, error_code);
	ESP_LOGE(TAG, "I2C error bus=%u address=0x%02X code=%d total=%lu",
		bus, address, error_code, (unsigned long)s_i2c_errors);
}

void diagnostics_process(usb_gateway_mode_t mode)
{
	const int64_t now = esp_timer_get_time();

	if ((now - s_last_log_us) < 5000000) {
		return;
	}
	s_last_log_us = now;

	ESP_LOGI(TAG, "SYSTEM uptime=%llds free_heap=%lu min_free_heap=%lu",
		(long long)(now / 1000000),
		(unsigned long)esp_get_free_heap_size(),
		(unsigned long)esp_get_minimum_free_heap_size());

	ESP_LOGI(TAG, "USB HOST=%s devices=%u errors=%lu; USB DEVICE=%s errors=%lu",
		state_name(s_host.state), (unsigned)host_device_count(),
		(unsigned long)s_host.errors, state_name(s_device.state),
		(unsigned long)s_device.errors);

	for (size_t i = 0; i < DIAGNOSTICS_MAX_HOST_DEVICES; ++i) {
		const diagnostics_host_device_t *d = &s_devices[i];
		if (d->present) {
			ESP_LOGI(TAG, "DEVICE addr=%u VID:PID=%04X:%04X "
				"'%s' / '%s' interfaces=%u HID=%u report_desc=%u",
				d->address, d->vid, d->pid, d->manufacturer,
				d->product, d->interface_count, d->hid_interface_count,
				d->report_descriptor_length);
		}
	}

	ESP_LOGI(TAG, "PROXY mode=%s state=%s forwarded=%lu processed=%lu "
		"errors=%lu last_error=%d",
		mode == USB_GATEWAY_MODE_PASSTHROUGH ? "PASSTHROUGH" : "HOTKEY/UNDEFINED",
		state_name(s_proxy.state), (unsigned long)s_proxy_forwarded,
		(unsigned long)s_proxy_processed, (unsigned long)s_proxy.errors,
		s_proxy.error_code);

	for (size_t i = 0; i < 4; ++i) {
		if (s_i2c[i].state != DIAG_STATE_UNKNOWN) {
			ESP_LOGI(TAG, "I2C bus=%u address=0x%02X state=%s "
				"errors=%lu last_error=%d",
				s_i2c[i].bus, s_i2c[i].address,
				state_name(s_i2c[i].state),
				(unsigned long)s_i2c[i].errors,
				s_i2c[i].error_code);
		}
	}

	ESP_LOGI(TAG, "HID errors=%lu I2C errors=%lu",
		(unsigned long)s_hid_errors, (unsigned long)s_i2c_errors);
}
