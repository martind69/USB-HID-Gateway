#include "motion_profile_storage.h"

#include "cJSON.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROFILE_PATH_BUFFER_SIZE 320U

static cJSON *create_axis_json(const motion_axis_calibration_t *axis)
{
    cJSON *object = cJSON_CreateObject();
    if (object == NULL) {
        return NULL;
    }

    if ((cJSON_AddNumberToObject(object, "minCode", axis->min_code) == NULL) ||
        (cJSON_AddNumberToObject(object, "centerCode", axis->center_code) == NULL) ||
        (cJSON_AddNumberToObject(object, "maxCode", axis->max_code) == NULL) ||
        (cJSON_AddNumberToObject(object, "deadzone", axis->deadzone) == NULL)) {
        cJSON_Delete(object);
        return NULL;
    }

    return object;
}

static cJSON *create_gpio_json(const motion_gpio_behavior_t *gpio)
{
    cJSON *object = cJSON_CreateObject();
    if (object == NULL) {
        return NULL;
    }

    const char *mode = gpio->mode == MOTION_GPIO_OPEN_DRAIN
        ? "openDrain"
        : "pushPull";

    if ((cJSON_AddStringToObject(object, "mode", mode) == NULL) ||
        (cJSON_AddBoolToObject(object, "activeLow", gpio->active_low) == NULL)) {
        cJSON_Delete(object);
        return NULL;
    }

    return object;
}

static cJSON *create_stick_json(const motion_stick_calibration_t *stick)
{
    cJSON *object = cJSON_CreateObject();
    cJSON *x = create_axis_json(&stick->x);
    cJSON *y = create_axis_json(&stick->y);
    cJSON *envelope = cJSON_CreateArray();

    if ((object == NULL) || (x == NULL) || (y == NULL) || (envelope == NULL)) {
        cJSON_Delete(object);
        cJSON_Delete(x);
        cJSON_Delete(y);
        cJSON_Delete(envelope);
        return NULL;
    }

    cJSON_AddItemToObject(object, "x", x);
    cJSON_AddItemToObject(object, "y", y);

    for (size_t index = 0U; index < MOTION_PROFILE_ENVELOPE_BINS; ++index) {
        cJSON *value = cJSON_CreateNumber(stick->radial_envelope[index]);
        if (value == NULL) {
            cJSON_Delete(envelope);
            cJSON_Delete(object);
            return NULL;
        }
        cJSON_AddItemToArray(envelope, value);
    }

    cJSON_AddItemToObject(object, "radialEnvelope", envelope);
    if (cJSON_AddNumberToObject(
            object,
            "safetyFactor",
            stick->safety_factor) == NULL) {
        cJSON_Delete(object);
        return NULL;
    }

    return object;
}

static cJSON *create_motion_cell_json(const learned_motion_cell_t *cell)
{
    cJSON *object = cJSON_CreateObject();
    if (object == NULL) {
        return NULL;
    }

    const double sample_count = (double)cell->samples;
    const double velocity_mean = cell->samples > 0U
        ? (double)cell->v_sum / sample_count
        : 0.0;
    const double acceleration_mean = cell->samples > 0U
        ? (double)cell->a_sum / sample_count
        : 0.0;
    const double jerk_mean = cell->samples > 0U
        ? (double)cell->j_sum / sample_count
        : 0.0;

    if ((cJSON_AddNumberToObject(object, "samples", cell->samples) == NULL) ||
        (cJSON_AddNumberToObject(object, "velocityMean", velocity_mean) == NULL) ||
        (cJSON_AddNumberToObject(object, "accelerationMean", acceleration_mean) == NULL) ||
        (cJSON_AddNumberToObject(object, "jerkMean", jerk_mean) == NULL) ||
        (cJSON_AddNumberToObject(object, "velocityPeak", cell->v_peak) == NULL) ||
        (cJSON_AddNumberToObject(object, "accelerationPeak", cell->a_peak) == NULL) ||
        (cJSON_AddNumberToObject(object, "jerkPeak", cell->j_peak) == NULL)) {
        cJSON_Delete(object);
        return NULL;
    }

    return object;
}

static cJSON *create_profile_json(const motion_profile_t *profile)
{
    static const char *const logical_names[8] = {
        "LX", "LY", "RX", "RY", "LT", "RT", "L2", "R2"
    };
    static const char *const trigger_names[4] = {
        "LT", "RT", "L2", "R2"
    };

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    if (cJSON_AddNumberToObject(root, "schemaVersion", 1) == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *metadata = cJSON_AddObjectToObject(root, "profile");
    cJSON *analog = cJSON_AddObjectToObject(root, "analog");
    cJSON *calibration = cJSON_AddObjectToObject(root, "calibration");
    cJSON *motion_model = cJSON_AddObjectToObject(root, "motionModel");
    cJSON *tuning = cJSON_AddObjectToObject(root, "tuning");

    if ((metadata == NULL) || (analog == NULL) || (calibration == NULL) ||
        (motion_model == NULL) || (tuning == NULL)) {
        cJSON_Delete(root);
        return NULL;
    }

    if ((cJSON_AddStringToObject(metadata, "id", profile->id) == NULL) ||
        (cJSON_AddStringToObject(metadata, "name", profile->name) == NULL) ||
        (cJSON_AddStringToObject(
            metadata,
            "description",
            profile->description) == NULL)) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *voltage = cJSON_AddObjectToObject(analog, "voltage");
    cJSON *channels = cJSON_AddArrayToObject(analog, "channels");
    if ((voltage == NULL) || (channels == NULL)) {
        cJSON_Delete(root);
        return NULL;
    }

    if ((cJSON_AddNumberToObject(
            voltage,
            "adcFullScaleV",
            profile->adc_full_scale_v) == NULL) ||
        (cJSON_AddNumberToObject(
            voltage,
            "dacReferenceV",
            profile->dac_reference_v) == NULL) ||
        (cJSON_AddNumberToObject(
            voltage,
            "dacOutputMinV",
            profile->dac_output_min_v) == NULL) ||
        (cJSON_AddNumberToObject(
            voltage,
            "dacOutputMaxV",
            profile->dac_output_max_v) == NULL)) {
        cJSON_Delete(root);
        return NULL;
    }

    for (size_t index = 0U; index < 8U; ++index) {
        const motion_analog_channel_t *channel = &profile->channel[index];
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            cJSON_Delete(root);
            return NULL;
        }

        if ((cJSON_AddStringToObject(
                item,
                "logical",
                logical_names[index]) == NULL) ||
            (cJSON_AddNumberToObject(
                item,
                "dacChannel",
                channel->dac_channel) == NULL) ||
            (cJSON_AddNumberToObject(
                item,
                "adcChannel",
                channel->adc_channel) == NULL) ||
            (cJSON_AddNumberToObject(
                item,
                "neutralCode",
                channel->neutral_code) == NULL) ||
            (cJSON_AddNumberToObject(
                item,
                "safeCode",
                channel->safe_code) == NULL) ||
            (cJSON_AddBoolToObject(
                item,
                "invert",
                channel->invert) == NULL)) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            return NULL;
        }

        cJSON_AddItemToArray(channels, item);
    }

    cJSON *sensor_power = create_gpio_json(&profile->sensor_power);
    cJSON *fault_led = create_gpio_json(&profile->fault_led);
    if ((sensor_power == NULL) || (fault_led == NULL)) {
        cJSON_Delete(sensor_power);
        cJSON_Delete(fault_led);
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(analog, "sensorPower", sensor_power);
    cJSON_AddItemToObject(analog, "faultLed", fault_led);

    cJSON *left_stick = create_stick_json(&profile->left_stick);
    cJSON *right_stick = create_stick_json(&profile->right_stick);
    cJSON *triggers = cJSON_AddObjectToObject(calibration, "triggers");
    if ((left_stick == NULL) || (right_stick == NULL) ||
        (triggers == NULL)) {
        cJSON_Delete(left_stick);
        cJSON_Delete(right_stick);
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(calibration, "leftStick", left_stick);
    cJSON_AddItemToObject(calibration, "rightStick", right_stick);

    for (size_t index = 0U; index < 4U; ++index) {
        cJSON *trigger = create_axis_json(&profile->trigger[index]);
        if (trigger == NULL) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddItemToObject(triggers, trigger_names[index], trigger);
    }

    if ((cJSON_AddNumberToObject(
            motion_model,
            "angleBins",
            360) == NULL) ||
        (cJSON_AddNumberToObject(
            motion_model,
            "magnitudeBins",
            4) == NULL)) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *cells = cJSON_AddArrayToObject(motion_model, "cells");
    if (cells == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    for (size_t index = 0U; index < MOTION_MODEL_CELLS; ++index) {
        cJSON *cell = create_motion_cell_json(&profile->learned.cell[index]);
        if (cell == NULL) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddItemToArray(cells, cell);
    }

    if ((cJSON_AddNumberToObject(
            tuning,
            "velocityScale",
            profile->tuning.velocity_scale) == NULL) ||
        (cJSON_AddNumberToObject(
            tuning,
            "accelerationScale",
            profile->tuning.acceleration_scale) == NULL) ||
        (cJSON_AddNumberToObject(
            tuning,
            "jerkScale",
            profile->tuning.jerk_scale) == NULL) ||
        (cJSON_AddNumberToObject(
            tuning,
            "responseScale",
            profile->tuning.response_scale) == NULL)) {
        cJSON_Delete(root);
        return NULL;
    }

    if (profile->tuning.deadzone_override < 0.0f) {
        if (cJSON_AddNullToObject(tuning, "deadzoneOverride") == NULL) {
            cJSON_Delete(root);
            return NULL;
        }
    } else if (cJSON_AddNumberToObject(
            tuning,
            "deadzoneOverride",
            profile->tuning.deadzone_override) == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    if (profile->tuning.damping_override < 0.0f) {
        if (cJSON_AddNullToObject(tuning, "dampingOverride") == NULL) {
            cJSON_Delete(root);
            return NULL;
        }
    } else if (cJSON_AddNumberToObject(
            tuning,
            "dampingOverride",
            profile->tuning.damping_override) == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    return root;
}

static esp_err_t write_json_atomic(
    const char *path,
    const char *json,
    size_t json_length)
{
    char temporary[PROFILE_PATH_BUFFER_SIZE];
    char backup[PROFILE_PATH_BUFFER_SIZE];

    if ((snprintf(
            temporary,
            sizeof(temporary),
            "%s.tmp",
            path) >= (int)sizeof(temporary)) ||
        (snprintf(
            backup,
            sizeof(backup),
            "%s.bak",
            path) >= (int)sizeof(backup))) {
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = fopen(temporary, "wb");
    if (file == NULL) {
        return ESP_FAIL;
    }

    bool success = fwrite(json, 1U, json_length, file) == json_length;
    success = success && (fputc('\n', file) != EOF);
    success = success && (fflush(file) == 0);
    success = success && (fsync(fileno(file)) == 0);

    const int close_result = fclose(file);
    file = NULL;
    success = success && (close_result == 0);

    if (!success) {
        (void)remove(temporary);
        return ESP_FAIL;
    }

    const bool original_exists = access(path, F_OK) == 0;
    (void)remove(backup);

    if (original_exists && (rename(path, backup) != 0)) {
        (void)remove(temporary);
        return ESP_FAIL;
    }

    if (rename(temporary, path) != 0) {
        if (original_exists) {
            (void)rename(backup, path);
        }
        (void)remove(temporary);
        return ESP_FAIL;
    }

    (void)remove(backup);
    return ESP_OK;
}

esp_err_t motion_profile_validate(
    const motion_profile_t *profile,
    char *reason,
    size_t reason_size)
{
    const char *error = NULL;

    if (profile == NULL) {
        error = "null profile";
    } else if (profile->schema_version != 1U) {
        error = "invalid schema version";
    } else if (profile->dac_output_min_v >= profile->dac_output_max_v) {
        error = "invalid DAC voltage range";
    } else if (profile->learned.version != 1U) {
        error = "invalid motion-model version";
    }

    if ((reason != NULL) && (reason_size > 0U)) {
        (void)snprintf(
            reason,
            reason_size,
            "%s",
            error != NULL ? error : "ok");
    }

    return error != NULL ? ESP_ERR_INVALID_ARG : ESP_OK;
}

esp_err_t motion_profile_save_json_atomic(
    const char *path,
    const motion_profile_t *profile)
{
    if ((path == NULL) || (profile == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = motion_profile_validate(profile, NULL, 0U);
    if (error != ESP_OK) {
        return error;
    }

    cJSON *root = create_profile_json(profile);
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const size_t json_length = strlen(json);
    if (json_length > MOTION_PROFILE_JSON_MAX_BYTES) {
        cJSON_free(json);
        return ESP_ERR_INVALID_SIZE;
    }

    error = write_json_atomic(path, json, json_length);
    cJSON_free(json);
    return error;
}
