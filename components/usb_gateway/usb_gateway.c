#include "usb_gateway.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "analog_bus.h"
#include "analog_control_gpio.h"
#include "analog_engine.h"
#include "analog_interlock.h"
#include "analog_vector_io.h"
#include "capture_log.h"
#include "diagnostics.h"
#include "gateway_config.h"
#include "gateway_paths.h"
#include "gpio_output.h"
#include "hotkey.h"
#include "json_memory.h"
#include "motion_profile_cache.h"
#include "motion_profile_defaults.h"
#include "output_config.h"
#include "p4_s3_link.h"
#include "profile_export.h"
#include "profile_feedback.h"
#include "report_tap.h"
#include "s3_mirror.h"
#include "s3_ota_manager.h"
#include "sd_storage.h"
#include "ssd1306_display.h"
#include "usb_capture.h"
#include "usb_device.h"
#include "usb_host.h"
#include "usb_proxy_control.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

/*
 * Profile load/create runs in a temporary task: the JSON parser keeps a
 * complete motion_profile_t (about 4.4 x 10^4 bytes) on its stack.
 */
#define PROFILE_TASK_STACK      (64U * 1024U)
#define PROFILE_TASK_PRIORITY   5U

static const char *TAG = "usb_gateway";

static analog_vector_io_t s_analog;
static motion_profile_t s_profile;
static bool s_profile_valid;
static bool s_last_analog_ok;
static output_config_t s_output_config;
static TaskHandle_t s_ota_restore_task;
/* Set while the S3 OTA owns the display; cleared 5 s after a confirmed OTA. */
static volatile bool s_ota_display_hold;
static ssd1306_status_view_t s_last_view;
static bool s_last_view_valid;
static SemaphoreHandle_t s_profile_done;
#if CONFIG_USB_GATEWAY_CAPTURE_ENABLE
static int64_t s_last_status_us;
#endif

static void refresh_display(void);

static void restore_display_after_ota(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(5000U));
    s_ota_display_hold = false;
    s_last_view_valid = false;
    refresh_display();
    s_ota_restore_task = NULL;
    vTaskDelete(NULL);
}

static void update_s3_ota_display(const s3_ota_progress_t *progress)
{
    if (progress == NULL) {
        return;
    }

    s_ota_display_hold = true;

    ssd1306_status_view_t view;
    memset(&view, 0, sizeof(view));
    (void)snprintf(view.mode, sizeof(view.mode), "S3 OTA");
    (void)snprintf(view.info_label, sizeof(view.info_label), "VERSION");
    (void)snprintf(
        view.info,
        sizeof(view.info),
        "%.16s",
        progress->version[0] != '\0' ? progress->version : "UNKNOWN");

    switch (progress->phase) {
        case P4_S3_OTA_PHASE_IDLE:
            (void)snprintf(view.detail, sizeof(view.detail), "CHECK IMAGE");
            break;
        case P4_S3_OTA_PHASE_ABORTED:
            (void)snprintf(view.detail, sizeof(view.detail), "CLEANUP");
            break;
        case P4_S3_OTA_PHASE_ERASING:
            (void)snprintf(view.detail, sizeof(view.detail), "ERASING");
            break;
        case P4_S3_OTA_PHASE_WRITING:
            (void)snprintf(view.detail, sizeof(view.detail), "WRITING");
            break;
        case P4_S3_OTA_PHASE_VERIFYING:
            (void)snprintf(view.detail, sizeof(view.detail), "VERIFY SHA256");
            break;
        case P4_S3_OTA_PHASE_REBOOT_PENDING:
            (void)snprintf(view.detail, sizeof(view.detail), "WAIT REBOOT");
            break;
        case P4_S3_OTA_PHASE_PENDING_CONFIRM:
            (void)snprintf(view.detail, sizeof(view.detail), "CONFIRM");
            break;
        case P4_S3_OTA_PHASE_CONFIRMED:
            (void)snprintf(view.detail, sizeof(view.detail), "COMPLETE");
            break;
        case P4_S3_OTA_PHASE_FAILED:
        default:
            (void)snprintf(
                view.detail,
                sizeof(view.detail),
                "ERR 0x%lX",
                (unsigned long)progress->error);
            break;
    }

    view.progress_percent = progress->progress_percent;
    view.show_progress = true;
    view.usb_host_ok = true;
    view.usb_device_ok = true;
    view.analog_ok = s_last_analog_ok;
    ssd1306_display_set_view(&view);

    if (progress->phase == P4_S3_OTA_PHASE_CONFIRMED &&
        s_ota_restore_task == NULL) {
        (void)xTaskCreate(
            restore_display_after_ota,
            "ota_display_restore",
            2048U,
            NULL,
            4U,
            &s_ota_restore_task);
    }
}

static void update_display(const char *detail, bool analog_ok)
{
    ssd1306_status_view_t view;
    memset(&view, 0, sizeof(view));

    const char *mode = hotkey_get_mode() == USB_GATEWAY_MODE_PROXY
        ? "PROXY"
        : "PASS";
    const char *profile_name = "NONE";
    if (s_profile_valid) {
        profile_name = s_profile.name[0] != '\0' ? s_profile.name : "UNNAMED";
    }

    (void)snprintf(view.mode, sizeof(view.mode), "%.11s", mode);
    (void)snprintf(view.info_label, sizeof(view.info_label), "PROFILE");
    (void)snprintf(view.info, sizeof(view.info), "%.16s", profile_name);
    (void)snprintf(
        view.detail,
        sizeof(view.detail),
        "%.21s",
        detail != NULL ? detail : "");
    view.show_progress = false;
    /* TODO(DISPLAY-HEALTH): replace provisional USB states with live diagnostics. */
    view.usb_host_ok = true;
    view.usb_device_ok = true;
    view.analog_ok = analog_ok;

    /* Called every main loop pass: only hand over real changes. */
    if (s_last_view_valid && memcmp(&view, &s_last_view, sizeof(view)) == 0) {
        return;
    }
    s_last_view = view;
    s_last_view_valid = true;
    ssd1306_display_set_view(&view);
}

/* Mode, engine state and interlock state; the S3 OTA view has priority. */
static void refresh_display(void)
{
    if (s_ota_display_hold) {
        return;
    }

    analog_engine_status_t engine;
    analog_engine_get_status(&engine);

    char detail[22];
    if (engine.interlock_state == ANALOG_INTERLOCK_FAULT) {
        (void)snprintf(detail, sizeof(detail), "FAULT %s",
            analog_interlock_fault_name(engine.interlock_fault));
    } else if (engine.saving) {
        (void)snprintf(detail, sizeof(detail), "SAVING");
    } else if (engine.mode == ANALOG_ENGINE_LEARN) {
        (void)snprintf(detail, sizeof(detail), "LEARNING");
    } else if (engine.mode == ANALOG_ENGINE_GENERATE) {
        (void)snprintf(detail, sizeof(detail), "GENERATE");
    } else {
        (void)snprintf(detail, sizeof(detail), "READY");
    }

    update_display(
        detail,
        s_last_analog_ok && engine.interlock_state != ANALOG_INTERLOCK_FAULT);
}

static void initialize_software_state(void)
{
    /* First: every later JSON parser/serializer uses these hooks. */
    (void)json_memory_init();
    diagnostics_init();
    (void)profile_feedback_init();
    hotkey_init();
    (void)s3_ota_manager_init();
    s3_ota_manager_set_progress_callback(update_s3_ota_display);
    (void)usb_capture_init();

    esp_err_t error = usb_proxy_control_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "USB proxy control initialization failed: %s",
            esp_err_to_name(error));
    }
    error = report_tap_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Report tap initialization failed: %s", esp_err_to_name(error));
    }
}

static void initialize_storage(void)
{
    const esp_err_t mount_error = sd_storage_mount();
    if (mount_error != ESP_OK) {
        ESP_LOGW(TAG, "SD unavailable: %s", esp_err_to_name(mount_error));
    }

    const esp_err_t capture_error = capture_log_init();
    if (capture_error != ESP_OK && capture_error != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "SD event log initialization failed: %s",
            esp_err_to_name(capture_error));
    }
}

static void initialize_configuration(void)
{
    output_config_set_defaults(&s_output_config);

    if (!sd_storage_is_mounted()) {
        /* gateway_config_get() provides the default rules when nothing is loaded. */
        ESP_LOGW(TAG, "No SD card, default port rules active, task 3 outputs disabled");
        return;
    }

    const esp_err_t storage_error = gateway_config_prepare_storage();
    if (storage_error != ESP_OK) {
        ESP_LOGE(TAG, "Config directory unavailable: %s",
            esp_err_to_name(storage_error));
    }

    /* Errors are logged by the parsers; defaults stay active. */
    (void)gateway_config_load(GATEWAY_CONFIG_FILE);
    (void)output_config_load(GATEWAY_OUTPUT_CONFIG_FILE, &s_output_config);
}

static bool initialize_analog(void)
{
    esp_err_t error = analog_control_gpio_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Joystick enable GPIO initialization failed: %s",
            esp_err_to_name(error));
    }

    error = gpio_output_init();
    if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGE(TAG, "Digital output initialization failed: %s",
            esp_err_to_name(error));
    }

#if CONFIG_USB_GATEWAY_CAPTURE_ENABLE
    analog_bus_scan();
#endif

    error = analog_vector_io_init(&s_analog);
    bool analog_ok = error == ESP_OK || error == ESP_ERR_NOT_SUPPORTED;
    if (!analog_ok) {
        ESP_LOGE(TAG, "Analog initialization failed: %s", esp_err_to_name(error));
        profile_feedback_set(PROFILE_FEEDBACK_ANALOG_ERROR);
    }

    /* DAC high-Z, joystick supply off and ADS7128 inputs confirmed before anything else. */
    error = analog_interlock_init(&s_analog);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Analog interlock in FAULT: %s",
            analog_interlock_fault_name(analog_interlock_get_fault()));
        analog_ok = false;
    }
    return analog_ok;
}

static void load_profile(void)
{
    bool cache_rebuilt = false;
    esp_err_t error = motion_profile_boot_load(
        GATEWAY_PROFILE_JSON_FILE,
        GATEWAY_PROFILE_CACHE_FILE,
        &s_profile,
        &cache_rebuilt);

    if (error == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "No profile on SD card, creating default profile");
        error = motion_profile_create_default_file(GATEWAY_PROFILE_JSON_FILE, &s_profile);
        if (error == ESP_OK) {
            /* Builds the binary cache from the new JSON file. */
            error = motion_profile_boot_load(
                GATEWAY_PROFILE_JSON_FILE,
                GATEWAY_PROFILE_CACHE_FILE,
                &s_profile,
                &cache_rebuilt);
        }
    }

    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Profile unavailable: %s", esp_err_to_name(error));
        return;
    }

    s_profile_valid = true;
    ESP_LOGI(TAG, "Profile loaded: %.64s, cache_rebuilt=%s",
        s_profile.name, cache_rebuilt ? "yes" : "no");
}

static void profile_task(void *argument)
{
    (void)argument;
    load_profile();
    xSemaphoreGive(s_profile_done);
    vTaskDelete(NULL);
}

static void initialize_profile(void)
{
    s_profile_valid = false;
    if (!sd_storage_is_mounted()) {
        return;
    }

    s_profile_done = xSemaphoreCreateBinary();
    if (s_profile_done == NULL ||
        xTaskCreate(profile_task, "profile_load", PROFILE_TASK_STACK, NULL,
            PROFILE_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Cannot start profile load task");
        return;
    }
    (void)xSemaphoreTake(s_profile_done, portMAX_DELAY);
    vSemaphoreDelete(s_profile_done);
    s_profile_done = NULL;

    if (s_profile_valid) {
        const esp_err_t export_error = profile_export_init(
            &s_profile,
            GATEWAY_PROFILE_JSON_FILE);
        if (export_error != ESP_OK) {
            ESP_LOGW(TAG, "Profile export initialization failed: %s",
                esp_err_to_name(export_error));
        }
    }
}

static bool initialize_link(void)
{
    esp_err_t error = p4_s3_link_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "P4/S3 link initialization failed: %s",
            esp_err_to_name(error));
        return false;
    }

    p4_s3_link_start();

    /* Allow startup synchronization before application traffic starts. */
    vTaskDelay(pdMS_TO_TICKS(100U));

    error = s3_mirror_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "S3 mirror initialization failed: %s",
            esp_err_to_name(error));
        return false;
    }

    ESP_LOGI(TAG, "P4/S3 link initialized");
    return true;
}

static void initialize_display(void)
{
    const esp_err_t error = ssd1306_display_init();
    if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "OLED unavailable: %s", esp_err_to_name(error));
    }
}

static void initialize_analog_engine(void)
{
    const esp_err_t error = analog_engine_start(
        &s_analog,
        s_profile_valid ? &s_profile : NULL,
        GATEWAY_PROFILE_JSON_FILE,
        &s_output_config);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Analog engine start failed: %s", esp_err_to_name(error));
    }
}

#if CONFIG_USB_GATEWAY_CAPTURE_ENABLE
static void log_task3_status(void)
{
    const int64_t now = esp_timer_get_time();
    if ((now - s_last_status_us) <
        (int64_t)CONFIG_USB_GATEWAY_CAPTURE_DIAG_PERIOD_S * 1000000LL) {
        return;
    }
    s_last_status_us = now;

    analog_engine_status_t engine;
    analog_engine_get_status(&engine);
    report_tap_stats_t tap;
    report_tap_get_stats(&tap);

    ESP_LOGI(TAG,
        "TASK3 mode=%s interlock=%s fault=%s ticks=%lu overrun=%lu adc_err=%lu dac_err=%lu "
        "learned=%llu saving=%u save=%s tap_in=%lu tap_ok=%lu tap_drop=%lu tap_undecoded=%lu",
        analog_engine_mode_name(engine.mode),
        analog_interlock_state_name(engine.interlock_state),
        analog_interlock_fault_name(engine.interlock_fault),
        (unsigned long)engine.ticks,
        (unsigned long)engine.overruns,
        (unsigned long)engine.adc_errors,
        (unsigned long)engine.dac_errors,
        (unsigned long long)engine.learned_samples,
        engine.saving,
        esp_err_to_name(engine.last_save_result),
        (unsigned long)tap.submitted,
        (unsigned long)tap.processed,
        (unsigned long)tap.dropped,
        (unsigned long)tap.undecodable);
    ESP_LOGI(TAG,
        "ADC LX=%u LY=%u RX=%u RY=%u LT=%u RT=%u L2=%u R2=%u",
        engine.adc_code[ANALOG_HID_LX], engine.adc_code[ANALOG_HID_LY],
        engine.adc_code[ANALOG_HID_RX], engine.adc_code[ANALOG_HID_RY],
        engine.adc_code[ANALOG_HID_LT], engine.adc_code[ANALOG_HID_RT],
        engine.adc_code[ANALOG_HID_L2], engine.adc_code[ANALOG_HID_R2]);
}
#endif

void usb_gateway_init(void)
{
    initialize_software_state();
    initialize_storage();
    initialize_configuration();

    const bool analog_ok = initialize_analog();
    s_last_analog_ok = analog_ok;

    initialize_profile();

    const bool link_ready = initialize_link();
    initialize_display();

    usb_device_init();
    usb_host_init();

    initialize_analog_engine();

    diagnostics_set_proxy_state(DIAG_STATE_READY, ESP_OK);
    refresh_display();

    if (link_ready && sd_storage_is_mounted()) {
        const esp_err_t ota_error = s3_ota_manager_start_if_requested();
        if (ota_error != ESP_OK &&
            ota_error != ESP_ERR_NOT_FOUND &&
            ota_error != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGE(TAG, "S3 SPI OTA start failed: %s",
                esp_err_to_name(ota_error));
        }
    }
}

void usb_gateway_process(void)
{
    usb_host_process();
    usb_device_process();
    hotkey_process();
    diagnostics_process(hotkey_get_mode());
    refresh_display();
#if CONFIG_USB_GATEWAY_CAPTURE_ENABLE
    log_task3_status();
#endif
}

void usb_gateway_set_mode(usb_gateway_mode_t mode)
{
    hotkey_set_mode(mode);
}

usb_gateway_mode_t usb_gateway_get_mode(void)
{
    return hotkey_get_mode();
}
