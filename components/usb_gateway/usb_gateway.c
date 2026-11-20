#include "usb_gateway.h"

#include "analog_control_gpio.h"
#include "analog_vector_io.h"
#include "diagnostics.h"
#include "g815_capture.h"
#include "gateway_self_test.h"
#include "gpio_output.h"
#include "hid_proxy.h"
#include "hotkey.h"
#include "keyboard_test_pattern.h"
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

#ifndef CONFIG_USB_GATEWAY_KEYBOARD_TEST_ENABLE
#define CONFIG_USB_GATEWAY_KEYBOARD_TEST_ENABLE 0
#endif

#ifndef CONFIG_USB_GATEWAY_KEYBOARD_TEST_BOOT_ENABLE
#define CONFIG_USB_GATEWAY_KEYBOARD_TEST_BOOT_ENABLE 0
#endif

#ifndef CONFIG_USB_GATEWAY_SELF_TEST_ENABLE
#define CONFIG_USB_GATEWAY_SELF_TEST_ENABLE 0
#endif

#ifndef CONFIG_USB_GATEWAY_SELF_TEST_SAFE_AT_BOOT
#define CONFIG_USB_GATEWAY_SELF_TEST_SAFE_AT_BOOT 0
#endif

#ifndef CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST
#define CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST 0
#endif

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
    (void)snprintf(
        view.profile,
        sizeof(view.profile),
        "%.16s",
        profile_name);
    (void)snprintf(
        view.detail,
        sizeof(view.detail),
        "%.21s",
        detail != NULL ? detail : "");

    /* TODO: Replace provisional USB states with live diagnostics. */
    view.usb_host_ok = true;
    view.usb_device_ok = true;
    view.analog_ok = analog_ok;
    ssd1306_display_set_view(&view);
}

static void initialize_software_state(void)
{
    diagnostics_init();
    (void)profile_feedback_init();
    hotkey_init();
    (void)gateway_self_test_init();
    (void)keyboard_test_pattern_init();
    (void)g815_capture_init();
    usb_proxy_control_init();
    (void)usb_proxy_transport_init();
}

static void initialize_storage(void)
{
    const esp_err_t mount_error = sd_storage_mount();
    if (mount_error != ESP_OK) {
        ESP_LOGW(
            TAG,
            "SD unavailable: %s",
            esp_err_to_name(mount_error));
    }

    const esp_err_t capture_error = verbose_capture_init();
    if (capture_error != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Verbose capture initialization failed: %s",
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
        ESP_LOGW(
            TAG,
            "Profile unavailable: %s",
            esp_err_to_name(error));
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

static bool initialize_link(void)
{
    const esp_err_t error = p4_s3_link_init();
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "P4/S3 link initialization failed: %s",
            esp_err_to_name(error));
        return false;
    }

    p4_s3_link_start();

    /* Allow startup synchronization before application traffic starts. */
    vTaskDelay(pdMS_TO_TICKS(100U));

    ESP_LOGI(TAG, "P4/S3 link initialized");
    return true;
}

static void initialize_display(void)
{
    const esp_err_t error = ssd1306_display_init();
    if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(
            TAG,
            "OLED unavailable: %s",
            esp_err_to_name(error));
    }
}

static void initialize_usb(void)
{
    usb_device_init();

#if CONFIG_USB_GATEWAY_USB_DEVICE_MOUSE_TEST
    ESP_LOGW(TAG, "USB Host disabled for isolated mouse test");
#else
    usb_host_init();
#endif
}

static void start_configured_test(bool link_ready)
{
    if (!link_ready) {
        ESP_LOGW(
            TAG,
            "Tests disabled because the P4/S3 link is unavailable");
        return;
    }

#if CONFIG_USB_GATEWAY_KEYBOARD_TEST_ENABLE && \
    CONFIG_USB_GATEWAY_KEYBOARD_TEST_BOOT_ENABLE
    const esp_err_t error = keyboard_test_pattern_start();
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Keyboard test-pattern start failed: %s",
            esp_err_to_name(error));
    } else {
        ESP_LOGW(TAG, "Keyboard test-pattern scheduled");
    }
#elif CONFIG_USB_GATEWAY_SELF_TEST_ENABLE && \
    CONFIG_USB_GATEWAY_SELF_TEST_SAFE_AT_BOOT
    const esp_err_t error = gateway_self_test_start(1000U, false);
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "P4/S3 self-test start failed: %s",
            esp_err_to_name(error));
    }
#endif
}

void usb_gateway_init(void)
{
    initialize_software_state();
    initialize_storage();

    const bool analog_ok = initialize_analog();
    initialize_profile();

    if (!initialize_mapping()) {
        ESP_LOGE(TAG, "Mapping initialization failed");
        return;
    }

    const bool link_ready = initialize_link();
    initialize_display();
    initialize_usb();

    diagnostics_set_proxy_state(DIAG_STATE_READY, ESP_OK);
    update_display("READY", analog_ok);

    /*
     * Capture startup is intentionally performed by usb_host.c once the real
     * device and configuration descriptors are available. This file only
     * initializes the capture backend.
     */
    start_configured_test(link_ready);
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
