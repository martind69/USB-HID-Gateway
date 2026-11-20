#include "usb_gateway.h"

#include "analog_control_gpio.h"
#include "analog_vector_io.h"
#include "diagnostics.h"
#include "gateway_self_test.h"
#include "gpio_output.h"
#include "hid_proxy.h"
#include "hotkey.h"
#include "mapping.h"
#include "motion_profile_cache.h"
#include "p4_s3_link.h"
#include "profile_export.h"
#include "profile_feedback.h"
#include "sd_storage.h"
#include "ssd1306_display.h"
#include "usb_device.h"
#include "usb_host.h"
#include "usb_proxy_control.h"
#include "usb_proxy_transport.h"
#include "verbose_capture.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdbool.h>
#include <stdio.h>

static const char *TAG = "usb_gateway";
static analog_vector_io_t s_analog;
static motion_profile_t s_profile;
static bool s_profile_valid;

static void update_display(const char *detail, bool analog_ok)
{
    ssd1306_status_view_t view = { 0 };
    const char *mode = hotkey_get_mode() == USB_GATEWAY_MODE_PROXY
        ? "PROXY"
        : "PASS";
    const char *profile_name = s_profile_valid
        ? s_profile.name
        : "NO PROFILE";

    (void)snprintf(view.mode, sizeof(view.mode), "%.11s", mode);
    (void)snprintf(view.profile, sizeof(view.profile), "%.16s", profile_name);
    (void)snprintf(
        view.detail,
        sizeof(view.detail),
        "%.21s",
        detail != NULL ? detail : "");

    view.usb_host_ok = true;
    view.usb_device_ok = true;
    view.analog_ok = analog_ok;
    ssd1306_display_set_view(&view);
}

static void initialize_storage(void)
{
    const esp_err_t mount_error = sd_storage_mount();
    if (mount_error != ESP_OK) {
        ESP_LOGW(TAG, "SD unavailable: %s", esp_err_to_name(mount_error));
    }

    const esp_err_t capture_error = verbose_capture_init();
    if (capture_error != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Capture initialization failed: %s",
            esp_err_to_name(capture_error));
    }
}

static bool initialize_analog(void)
{
    esp_err_t error = analog_control_gpio_init();
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Analog power GPIO initialization failed: %s",
            esp_err_to_name(error));
    }

    error = gpio_output_init();
    if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGE(
            TAG,
            "Digital output initialization failed: %s",
            esp_err_to_name(error));
    }

    error = analog_vector_io_init(&s_analog);
    const bool analog_ok =
        error == ESP_OK || error == ESP_ERR_NOT_SUPPORTED;
    if (!analog_ok) {
        ESP_LOGE(
            TAG,
            "Analog initialization failed: %s",
            esp_err_to_name(error));
        profile_feedback_set(PROFILE_FEEDBACK_ANALOG_ERROR);
    }

    return analog_ok;
}

static void initialize_profile(void)
{
    s_profile_valid = false;
    if (!sd_storage_is_mounted()) {
        return;
    }

    bool cache_rebuilt = false;
    const esp_err_t error = motion_profile_boot_load(
        "/sdcard/motion/profile-01.json",
        "/sdcard/motion/profile-01.bin",
        &s_profile,
        &cache_rebuilt);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Profile unavailable: %s", esp_err_to_name(error));
        return;
    }

    s_profile_valid = true;
    ESP_LOGI(
        TAG,
        "Profile loaded: %.64s, cache_rebuilt=%s",
        s_profile.name,
        cache_rebuilt ? "yes" : "no");

    const esp_err_t export_error = profile_export_init(
        &s_profile,
        "/sdcard/motion/profile-01.json");
    if (export_error != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Profile export initialization failed: %s",
            esp_err_to_name(export_error));
    }
}

static bool initialize_mapping(void)
{
    const mapping_config_t config = { 0 };
    if (mapping_set_config(&config) != MAPPING_OK) {
        diagnostics_set_proxy_state(
            DIAG_STATE_ERROR,
            ESP_ERR_INVALID_ARG);
        return false;
    }

    mapping_set_event_callback(NULL, NULL);
    hid_proxy_init();
    return true;
}

static void initialize_link_and_run_test(void)
{
    const esp_err_t error = p4_s3_link_init();
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "P4/S3 link initialization failed: %s",
            esp_err_to_name(error));
        return;
    }

    p4_s3_link_start();
    vTaskDelay(pdMS_TO_TICKS(100U));

    const esp_err_t test_error = gateway_self_test_start(1000U, false);
    if (test_error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "P4/S3 self-test start failed: %s",
            esp_err_to_name(test_error));
    }
}

static void initialize_display(void)
{
    const esp_err_t error = ssd1306_display_init();
    if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "OLED unavailable: %s", esp_err_to_name(error));
    }
}

void usb_gateway_init(void)
{
    diagnostics_init();
    (void)profile_feedback_init();
    hotkey_init();
    (void)gateway_self_test_init();
    usb_proxy_control_init();
    (void)usb_proxy_transport_init();

    initialize_storage();
    const bool analog_ok = initialize_analog();
    initialize_profile();

    if (!initialize_mapping()) {
        return;
    }

    initialize_link_and_run_test();
    initialize_display();
    usb_device_init();

#if CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST
    ESP_LOGW(TAG, "USB Host disabled for isolated mouse test");
#else
    usb_host_init();
#endif

    diagnostics_set_proxy_state(DIAG_STATE_READY, 0);
    update_display("READY", analog_ok);
}

void usb_gateway_process(void)
{
#if !CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST
    usb_host_process();
#endif

    usb_device_process();
    hotkey_process();
    hid_proxy_process();
    diagnostics_process(hotkey_get_mode());
}

void usb_gateway_set_mode(usb_gateway_mode_t mode)
{
    hotkey_set_mode(mode);
}

usb_gateway_mode_t usb_gateway_get_mode(void)
{
    return hotkey_get_mode();
}
