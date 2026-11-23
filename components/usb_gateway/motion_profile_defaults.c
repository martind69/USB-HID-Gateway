#include "motion_profile_defaults.h"

#include <stdio.h>
#include <string.h>

#include "analog_vector_io.h"
#include "esp_log.h"

#define CODE_MIN        0U
#define CODE_CENTER     2048U
#define CODE_MAX        4095U

static const char *TAG = "profile_default";

static motion_axis_calibration_t full_range_axis(void)
{
    return (motion_axis_calibration_t) {
        .min_code = CODE_MIN,
        .center_code = CODE_CENTER,
        .max_code = CODE_MAX,
        .deadzone = 0.05f,
    };
}

static void default_stick(motion_stick_calibration_t *stick)
{
    stick->x = full_range_axis();
    stick->y = full_range_axis();
    for (size_t index = 0U; index < MOTION_PROFILE_ENVELOPE_BINS; ++index) {
        stick->radial_envelope[index] = 1.0f;
    }
    stick->safety_factor = 0.95f;
}

void motion_profile_create_default(motion_profile_t *profile)
{
    if (profile == NULL) {
        return;
    }
    memset(profile, 0, sizeof(*profile));
    profile->schema_version = 1U;
    (void)snprintf(profile->id, sizeof(profile->id), "profile-01");
    (void)snprintf(profile->name, sizeof(profile->name), "Default");
    (void)snprintf(profile->description, sizeof(profile->description),
        "Generated default profile; verify voltages, safe codes and axis ranges");

    /* Placeholder: 3.3 V supply and reference, full DAC output range. */
    profile->adc_full_scale_v = 3.3f;
    profile->dac_reference_v = 3.3f;
    profile->dac_output_min_v = 0.0f;
    profile->dac_output_max_v = 3.3f;

    /* Same channel assignment as analog_vector_io_init(): DAC i, ADC 7 - i. */
    for (uint8_t channel = 0U; channel < ANALOG_HID_COUNT; ++channel) {
        const bool is_trigger = channel >= ANALOG_HID_LT;
        profile->channel[channel] = (motion_analog_channel_t) {
            .dac_channel = channel,
            .adc_channel = (uint8_t)(7U - channel),
            /* Placeholder: stick axes neutral at center, triggers at minimum. */
            .neutral_code = is_trigger ? CODE_MIN : CODE_CENTER,
            .safe_code = is_trigger ? CODE_MIN : CODE_CENTER,
            .invert = false,
        };
    }
    profile->sensor_power = (motion_gpio_behavior_t) { .mode = MOTION_GPIO_PUSH_PULL };
    profile->fault_led = (motion_gpio_behavior_t) { .mode = MOTION_GPIO_PUSH_PULL };

    default_stick(&profile->left_stick);
    default_stick(&profile->right_stick);
    for (size_t index = 0U; index < 4U; ++index) {
        profile->trigger[index] = full_range_axis();
        profile->trigger[index].center_code = CODE_MIN;
    }
    learned_motion_model_reset(&profile->learned);
    motion_model_tuning_defaults(&profile->tuning);
}

esp_err_t motion_profile_create_default_file(const char *path, motion_profile_t *profile)
{
    if (path == NULL || profile == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    motion_profile_create_default(profile);
    char reason[64];
    esp_err_t error = motion_profile_validate(profile, reason, sizeof(reason));
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Default profile invalid: %s", reason);
        return error;
    }
    error = motion_profile_save_json_atomic(path, profile);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Cannot write %s: %s", path, esp_err_to_name(error));
        return error;
    }
    /* Round trip: the parser must accept what the serializer wrote. */
    error = motion_profile_load_json(path, profile);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "%s written but rejected by the parser: %s", path, esp_err_to_name(error));
        return error;
    }
    ESP_LOGW(TAG, "Default profile created: %s (verify placeholder values)", path);
    return ESP_OK;
}
