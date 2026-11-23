#include "diagnostics.h"

#include <limits.h>
#include <string.h>

#include "link_metrics.h"
#include "p4_s3_link.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

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

#if CONFIG_USB_GATEWAY_CAPTURE_ENABLE

static int64_t s_last_log_us;

static const char *state_name(diagnostics_state_t state)
{
    switch (state) {
        case DIAG_STATE_NOT_INITIALIZED:
            return "not-initialized";
        case DIAG_STATE_READY:
            return "ready";
        case DIAG_STATE_CONNECTED:
            return "connected";
        case DIAG_STATE_ERROR:
            return "error";
        case DIAG_STATE_UNAVAILABLE:
            return "unavailable";
        default:
            return "unknown";
    }
}

static size_t host_device_count(void)
{
    size_t count = 0U;
    for (size_t index = 0U; index < DIAGNOSTICS_MAX_HOST_DEVICES; ++index) {
        if (s_devices[index].present) {
            ++count;
        }
    }
    return count;
}

static void log_link_metrics(void)
{
    link_metrics_t metrics;
    link_metrics_get(&metrics);
    const uint32_t average_us = metrics.rx_status > 0U
        ? (uint32_t)(metrics.round_trip_sum_us / metrics.rx_status)
        : 0U;
    ESP_LOGI(
        TAG,
        "LINK tx=%lu status=%lu crc=%lu seq=%lu timeout=%lu overflow=%lu "
        "dropped=%lu rtt_last=%luus rtt_avg=%luus rtt_max=%luus",
        (unsigned long)metrics.tx_frames,
        (unsigned long)metrics.rx_status,
        (unsigned long)metrics.crc_errors,
        (unsigned long)metrics.sequence_errors,
        (unsigned long)metrics.timeouts,
        (unsigned long)metrics.queue_overflows,
        (unsigned long)p4_s3_link_dropped(),
        (unsigned long)metrics.round_trip_last_us,
        (unsigned long)average_us,
        (unsigned long)metrics.round_trip_max_us);
}

#endif /* CONFIG_USB_GATEWAY_CAPTURE_ENABLE */

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
#if CONFIG_USB_GATEWAY_CAPTURE_ENABLE
    ESP_LOGI(TAG, "Diagnostics initialized, periodic report every %d s",
        CONFIG_USB_GATEWAY_CAPTURE_DIAG_PERIOD_S);
#else
    ESP_LOGI(TAG, "Diagnostics initialized, periodic report disabled");
#endif
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

void diagnostics_set_i2c_state(
    uint8_t bus,
    uint8_t address,
    diagnostics_state_t state,
    int error_code)
{
    for (size_t index = 0U; index < 4U; ++index) {
        if (s_i2c[index].bus == bus && s_i2c[index].address == address) {
            s_i2c[index].state = state;
            s_i2c[index].error_code = error_code;
            if (state == DIAG_STATE_ERROR) {
                ++s_i2c[index].errors;
            }
            return;
        }
    }
    for (size_t index = 0U; index < 4U; ++index) {
        if (s_i2c[index].state == DIAG_STATE_UNKNOWN) {
            s_i2c[index].bus = bus;
            s_i2c[index].address = address;
            s_i2c[index].state = state;
            s_i2c[index].error_code = error_code;
            if (state == DIAG_STATE_ERROR) {
                ++s_i2c[index].errors;
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
    for (size_t index = 0U; index < DIAGNOSTICS_MAX_HOST_DEVICES; ++index) {
        if (s_devices[index].present && s_devices[index].address == device->address) {
            s_devices[index] = *device;
            s_devices[index].present = true;
            return;
        }
    }
    for (size_t index = 0U; index < DIAGNOSTICS_MAX_HOST_DEVICES; ++index) {
        if (!s_devices[index].present) {
            s_devices[index] = *device;
            s_devices[index].present = true;
            ESP_LOGI(
                TAG,
                "Host device + addr=%u VID:PID=%04X:%04X "
                "manufacturer='%s' product='%s' interfaces=%u HID=%u",
                device->address,
                device->vid,
                device->pid,
                device->manufacturer,
                device->product,
                device->interface_count,
                device->hid_interface_count);
            return;
        }
    }
    ESP_LOGE(TAG, "Host device table full (max=%u)",
        (unsigned)DIAGNOSTICS_MAX_HOST_DEVICES);
}

void diagnostics_host_device_removed(uint8_t address)
{
    for (size_t index = 0U; index < DIAGNOSTICS_MAX_HOST_DEVICES; ++index) {
        if (s_devices[index].present && s_devices[index].address == address) {
            ESP_LOGI(TAG, "Host device - addr=%u VID:PID=%04X:%04X",
                address, s_devices[index].vid, s_devices[index].pid);
            memset(&s_devices[index], 0, sizeof(s_devices[index]));
            return;
        }
    }
}

void diagnostics_report_hid_enum(
    uint8_t address,
    uint8_t interface_number,
    uint8_t report_id,
    uint16_t report_length)
{
    for (size_t index = 0U; index < DIAGNOSTICS_MAX_HOST_DEVICES; ++index) {
        diagnostics_host_device_t *device = &s_devices[index];
        if (!device->present || device->address != address) {
            continue;
        }
        if (device->hid_interface_count < UINT8_MAX) {
            ++device->hid_interface_count;
        }
        const uint32_t total_length =
            (uint32_t)device->report_descriptor_length + report_length;
        device->report_descriptor_length = total_length > UINT16_MAX
            ? UINT16_MAX
            : (uint16_t)total_length;
        break;
    }
    ESP_LOGI(TAG, "HID enum: addr=%u interface=%u report_id=%u report_descriptor_length=%u",
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

void diagnostics_report_i2c_error(uint8_t bus, uint8_t address, int error_code)
{
    ++s_i2c_errors;
    diagnostics_set_i2c_state(bus, address, DIAG_STATE_ERROR, error_code);
    ESP_LOGE(TAG, "I2C error bus=%u address=0x%02X code=%d total=%lu",
        bus, address, error_code, (unsigned long)s_i2c_errors);
}

void diagnostics_process(usb_gateway_mode_t mode)
{
#if CONFIG_USB_GATEWAY_CAPTURE_ENABLE
    const int64_t now = esp_timer_get_time();
    const int64_t period_us =
        (int64_t)CONFIG_USB_GATEWAY_CAPTURE_DIAG_PERIOD_S * 1000000LL;
    if ((now - s_last_log_us) < period_us) {
        return;
    }
    s_last_log_us = now;

    ESP_LOGI(TAG, "SYSTEM uptime=%llds free_heap=%lu min_free_heap=%lu",
        (long long)(now / 1000000LL),
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "USB HOST=%s devices=%u errors=%lu; USB DEVICE=%s errors=%lu",
        state_name(s_host.state),
        (unsigned)host_device_count(),
        (unsigned long)s_host.errors,
        state_name(s_device.state),
        (unsigned long)s_device.errors);
    for (size_t index = 0U; index < DIAGNOSTICS_MAX_HOST_DEVICES; ++index) {
        const diagnostics_host_device_t *device = &s_devices[index];
        if (device->present) {
            ESP_LOGI(TAG,
                "DEVICE addr=%u VID:PID=%04X:%04X '%s' / '%s' "
                "interfaces=%u HID=%u report_desc=%u",
                device->address,
                device->vid,
                device->pid,
                device->manufacturer,
                device->product,
                device->interface_count,
                device->hid_interface_count,
                device->report_descriptor_length);
        }
    }
    ESP_LOGI(TAG,
        "PROXY mode=%s state=%s forwarded=%lu processed=%lu errors=%lu last_error=%d",
        mode == USB_GATEWAY_MODE_PASSTHROUGH ? "PASSTHROUGH" : "PROXY",
        state_name(s_proxy.state),
        (unsigned long)s_proxy_forwarded,
        (unsigned long)s_proxy_processed,
        (unsigned long)s_proxy.errors,
        s_proxy.error_code);
    log_link_metrics();
    for (size_t index = 0U; index < 4U; ++index) {
        if (s_i2c[index].state != DIAG_STATE_UNKNOWN) {
            ESP_LOGI(TAG, "I2C bus=%u address=0x%02X state=%s errors=%lu last_error=%d",
                s_i2c[index].bus,
                s_i2c[index].address,
                state_name(s_i2c[index].state),
                (unsigned long)s_i2c[index].errors,
                s_i2c[index].error_code);
        }
    }
    ESP_LOGI(TAG, "HID errors=%lu I2C errors=%lu",
        (unsigned long)s_hid_errors, (unsigned long)s_i2c_errors);
#else
    (void)mode;
#endif
}
