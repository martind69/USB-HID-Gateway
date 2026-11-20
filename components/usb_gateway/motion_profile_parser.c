#include "motion_profile_storage.h"

#include "cJSON.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const cJSON *object_item(const cJSON *object, const char *name)
{
    return object != NULL
        ? cJSON_GetObjectItemCaseSensitive(object, name)
        : NULL;
}

static bool object_has_only_keys(
    const cJSON *object,
    const char *const *allowed,
    size_t allowed_count)
{
    if (!cJSON_IsObject(object)) {
        return false;
    }

    for (const cJSON *child = object->child;
         child != NULL;
         child = child->next) {
        bool found = false;
        for (size_t index = 0U; index < allowed_count; ++index) {
            if (strcmp(child->string, allowed[index]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

static bool read_number(
    const cJSON *item,
    double minimum,
    double maximum,
    double *value)
{
    if (!cJSON_IsNumber(item) ||
        !isfinite(item->valuedouble) ||
        item->valuedouble < minimum ||
        item->valuedouble > maximum) {
        return false;
    }

    if (value != NULL) {
        *value = item->valuedouble;
    }
    return true;
}

static bool read_integer(
    const cJSON *item,
    unsigned minimum,
    unsigned maximum,
    unsigned *value)
{
    double number = 0.0;
    if (!read_number(item, minimum, maximum, &number) ||
        floor(number) != number) {
        return false;
    }

    if (value != NULL) {
        *value = (unsigned)number;
    }
    return true;
}

static bool read_string(
    const cJSON *item,
    char *destination,
    size_t destination_size,
    bool required)
{
    if (item == NULL) {
        return !required;
    }
    if (!cJSON_IsString(item) ||
        item->valuestring == NULL ||
        strlen(item->valuestring) >= destination_size) {
        return false;
    }

    memcpy(
        destination,
        item->valuestring,
        strlen(item->valuestring) + 1U);
    return true;
}

static bool parse_axis(
    const cJSON *object,
    motion_axis_calibration_t *axis)
{
    static const char *const keys[] = {
        "minCode", "centerCode", "maxCode", "deadzone"
    };
    unsigned integer = 0U;
    double number = 0.0;

    if (!object_has_only_keys(object, keys, 4U) ||
        !read_integer(object_item(object, keys[0]), 0U, 4095U, &integer)) {
        return false;
    }
    axis->min_code = (uint16_t)integer;

    if (!read_integer(object_item(object, keys[1]), 0U, 4095U, &integer)) {
        return false;
    }
    axis->center_code = (uint16_t)integer;

    if (!read_integer(object_item(object, keys[2]), 0U, 4095U, &integer)) {
        return false;
    }
    axis->max_code = (uint16_t)integer;

    if (!read_number(object_item(object, keys[3]), 0.0, 0.5, &number)) {
        return false;
    }
    axis->deadzone = (float)number;

    return axis->min_code <= axis->center_code &&
        axis->center_code <= axis->max_code;
}

static bool parse_gpio_behavior(
    const cJSON *object,
    motion_gpio_behavior_t *gpio)
{
    static const char *const keys[] = { "mode", "activeLow" };
    if (!object_has_only_keys(object, keys, 2U)) {
        return false;
    }

    const cJSON *mode = object_item(object, "mode");
    const cJSON *active_low = object_item(object, "activeLow");
    if (!cJSON_IsString(mode) || !cJSON_IsBool(active_low)) {
        return false;
    }

    if (strcmp(mode->valuestring, "pushPull") == 0) {
        gpio->mode = MOTION_GPIO_PUSH_PULL;
    } else if (strcmp(mode->valuestring, "openDrain") == 0) {
        gpio->mode = MOTION_GPIO_OPEN_DRAIN;
    } else {
        return false;
    }

    gpio->active_low = cJSON_IsTrue(active_low);
    return true;
}

static bool parse_stick(
    const cJSON *object,
    motion_stick_calibration_t *stick)
{
    static const char *const keys[] = {
        "x", "y", "radialEnvelope", "safetyFactor"
    };
    if (!object_has_only_keys(object, keys, 4U) ||
        !parse_axis(object_item(object, "x"), &stick->x) ||
        !parse_axis(object_item(object, "y"), &stick->y)) {
        return false;
    }

    const cJSON *envelope = object_item(object, "radialEnvelope");
    if (!cJSON_IsArray(envelope) ||
        cJSON_GetArraySize(envelope) != MOTION_PROFILE_ENVELOPE_BINS) {
        return false;
    }

    for (size_t index = 0U;
         index < MOTION_PROFILE_ENVELOPE_BINS;
         ++index) {
        double radius = 0.0;
        if (!read_number(
                cJSON_GetArrayItem(envelope, (int)index),
                0.000001,
                2.0,
                &radius)) {
            return false;
        }
        stick->radial_envelope[index] = (float)radius;
    }

    double safety_factor = 0.0;
    if (!read_number(
            object_item(object, "safetyFactor"),
            0.5,
            1.0,
            &safety_factor)) {
        return false;
    }
    stick->safety_factor = (float)safety_factor;
    return true;
}

static bool parse_motion_cell(
    const cJSON *object,
    learned_motion_cell_t *cell)
{
    static const char *const keys[] = {
        "samples",
        "velocityMean",
        "accelerationMean",
        "jerkMean",
        "velocityPeak",
        "accelerationPeak",
        "jerkPeak"
    };
    if (!object_has_only_keys(object, keys, 7U)) {
        return false;
    }

    unsigned samples = 0U;
    double values[6] = { 0.0 };
    if (!read_integer(
            object_item(object, "samples"),
            0U,
            UINT32_MAX,
            &samples)) {
        return false;
    }

    for (size_t index = 0U; index < 6U; ++index) {
        if (!read_number(
                object_item(object, keys[index + 1U]),
                0.0,
                1.0e9,
                &values[index])) {
            return false;
        }
    }

    if (values[3] < values[0] ||
        values[4] < values[1] ||
        values[5] < values[2]) {
        return false;
    }

    cell->samples = samples;
    cell->v_sum = (float)(values[0] * samples);
    cell->a_sum = (float)(values[1] * samples);
    cell->j_sum = (float)(values[2] * samples);
    cell->v_peak = (float)values[3];
    cell->a_peak = (float)values[4];
    cell->j_peak = (float)values[5];
    return true;
}

static bool parse_profile_metadata(
    const cJSON *object,
    motion_profile_t *profile)
{
    static const char *const keys[] = {
        "id", "name", "description", "createdUtc", "modifiedUtc"
    };
    return object_has_only_keys(object, keys, 5U) &&
        read_string(
            object_item(object, "id"),
            profile->id,
            sizeof(profile->id),
            true) &&
        read_string(
            object_item(object, "name"),
            profile->name,
            sizeof(profile->name),
            true) &&
        read_string(
            object_item(object, "description"),
            profile->description,
            sizeof(profile->description),
            false);
}

static bool parse_voltage(
    const cJSON *object,
    motion_profile_t *profile)
{
    static const char *const keys[] = {
        "adcFullScaleV", "dacReferenceV", "dacOutputMinV", "dacOutputMaxV"
    };
    double value = 0.0;
    if (!object_has_only_keys(object, keys, 4U)) {
        return false;
    }

    if (!read_number(object_item(object, keys[0]), 0.000001, 5.5, &value)) {
        return false;
    }
    profile->adc_full_scale_v = (float)value;
    if (!read_number(object_item(object, keys[1]), 0.000001, 5.5, &value)) {
        return false;
    }
    profile->dac_reference_v = (float)value;
    if (!read_number(object_item(object, keys[2]), 0.0, 5.5, &value)) {
        return false;
    }
    profile->dac_output_min_v = (float)value;
    if (!read_number(object_item(object, keys[3]), 0.000001, 5.5, &value)) {
        return false;
    }
    profile->dac_output_max_v = (float)value;
    return true;
}

static bool parse_channels(
    const cJSON *array,
    motion_profile_t *profile)
{
    static const char *const keys[] = {
        "logical", "dacChannel", "adcChannel", "neutralCode", "safeCode", "invert"
    };
    static const char *const logical_names[8] = {
        "LX", "LY", "RX", "RY", "LT", "RT", "L2", "R2"
    };
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) != 8) {
        return false;
    }

    uint8_t logical_seen = 0U;
    uint8_t adc_seen = 0U;
    uint8_t dac_seen = 0U;

    for (int index = 0; index < 8; ++index) {
        const cJSON *channel = cJSON_GetArrayItem(array, index);
        if (!object_has_only_keys(channel, keys, 6U)) {
            return false;
        }

        const cJSON *logical = object_item(channel, "logical");
        int logical_index = -1;
        if (cJSON_IsString(logical)) {
            for (int candidate = 0; candidate < 8; ++candidate) {
                if (strcmp(logical->valuestring, logical_names[candidate]) == 0) {
                    logical_index = candidate;
                    break;
                }
            }
        }
        if (logical_index < 0 ||
            (logical_seen & (1U << logical_index)) != 0U) {
            return false;
        }
        logical_seen |= (uint8_t)(1U << logical_index);

        motion_analog_channel_t *output = &profile->channel[logical_index];
        unsigned value = 0U;
        if (!read_integer(object_item(channel, "dacChannel"), 0U, 7U, &value) ||
            (dac_seen & (1U << value)) != 0U) {
            return false;
        }
        output->dac_channel = (uint8_t)value;
        dac_seen |= (uint8_t)(1U << value);

        if (!read_integer(object_item(channel, "adcChannel"), 0U, 7U, &value) ||
            (adc_seen & (1U << value)) != 0U) {
            return false;
        }
        output->adc_channel = (uint8_t)value;
        adc_seen |= (uint8_t)(1U << value);

        if (!read_integer(object_item(channel, "neutralCode"), 0U, 4095U, &value)) {
            return false;
        }
        output->neutral_code = (uint16_t)value;
        if (!read_integer(object_item(channel, "safeCode"), 0U, 4095U, &value)) {
            return false;
        }
        output->safe_code = (uint16_t)value;

        const cJSON *invert = object_item(channel, "invert");
        if (!cJSON_IsBool(invert)) {
            return false;
        }
        output->invert = cJSON_IsTrue(invert);
    }
    return true;
}

static bool parse_tuning(
    const cJSON *object,
    motion_model_tuning_t *tuning)
{
    static const char *const keys[] = {
        "velocityScale", "accelerationScale", "jerkScale", "responseScale",
        "deadzoneOverride", "dampingOverride"
    };
    if (!object_has_only_keys(object, keys, 6U)) {
        return false;
    }

    float *destination[4] = {
        &tuning->velocity_scale,
        &tuning->acceleration_scale,
        &tuning->jerk_scale,
        &tuning->response_scale
    };
    for (size_t index = 0U; index < 4U; ++index) {
        double value = 0.0;
        if (!read_number(object_item(object, keys[index]), 0.000001, 10.0, &value)) {
            return false;
        }
        *destination[index] = (float)value;
    }

    const cJSON *deadzone = object_item(object, "deadzoneOverride");
    if (cJSON_IsNull(deadzone)) {
        tuning->deadzone_override = -1.0f;
    } else {
        double value = 0.0;
        if (!read_number(deadzone, 0.0, 0.5, &value)) {
            return false;
        }
        tuning->deadzone_override = (float)value;
    }

    const cJSON *damping = object_item(object, "dampingOverride");
    if (cJSON_IsNull(damping)) {
        tuning->damping_override = -1.0f;
    } else {
        double value = 0.0;
        if (!read_number(damping, 0.0, 1.0, &value)) {
            return false;
        }
        tuning->damping_override = (float)value;
    }
    return true;
}

esp_err_t motion_profile_load_json(
    const char *path,
    motion_profile_t *profile)
{
    if (path == NULL || profile == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }

    const long file_size = ftell(file);
    rewind(file);
    if (file_size < 2L || file_size > (long)MOTION_PROFILE_JSON_MAX_BYTES) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }

    char *text = malloc((size_t)file_size + 1U);
    if (text == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    const bool read_ok =
        fread(text, 1U, (size_t)file_size, file) == (size_t)file_size &&
        !ferror(file);
    fclose(file);
    text[file_size] = '\0';
    if (!read_ok) {
        free(text);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_ParseWithLength(text, (size_t)file_size);
    free(text);
    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    motion_profile_t parsed = { 0 };
    static const char *const root_keys[] = {
        "schemaVersion", "profile", "analog", "calibration", "motionModel", "tuning"
    };
    unsigned integer_value = 0U;
    bool valid =
        object_has_only_keys(root, root_keys, 6U) &&
        read_integer(object_item(root, "schemaVersion"), 1U, 1U, &integer_value);
    if (valid) {
        parsed.schema_version = integer_value;
        valid = parse_profile_metadata(object_item(root, "profile"), &parsed);
    }

    const cJSON *analog = object_item(root, "analog");
    static const char *const analog_keys[] = {
        "voltage", "channels", "sensorPower", "faultLed"
    };
    if (valid) {
        valid = object_has_only_keys(analog, analog_keys, 4U) &&
            parse_voltage(object_item(analog, "voltage"), &parsed) &&
            parse_channels(object_item(analog, "channels"), &parsed) &&
            parse_gpio_behavior(object_item(analog, "sensorPower"), &parsed.sensor_power) &&
            parse_gpio_behavior(object_item(analog, "faultLed"), &parsed.fault_led);
    }

    const cJSON *calibration = object_item(root, "calibration");
    static const char *const calibration_keys[] = {
        "leftStick", "rightStick", "triggers"
    };
    static const char *const trigger_keys[] = { "LT", "RT", "L2", "R2" };
    if (valid) {
        valid = object_has_only_keys(calibration, calibration_keys, 3U) &&
            parse_stick(object_item(calibration, "leftStick"), &parsed.left_stick) &&
            parse_stick(object_item(calibration, "rightStick"), &parsed.right_stick);
    }
    const cJSON *triggers = object_item(calibration, "triggers");
    if (valid) {
        valid = object_has_only_keys(triggers, trigger_keys, 4U);
        for (size_t index = 0U; valid && index < 4U; ++index) {
            valid = parse_axis(
                object_item(triggers, trigger_keys[index]),
                &parsed.trigger[index]);
        }
    }

    const cJSON *motion_model = object_item(root, "motionModel");
    static const char *const motion_keys[] = {
        "angleBins", "magnitudeBins", "cells"
    };
    const cJSON *cells = object_item(motion_model, "cells");
    if (valid) {
        valid = object_has_only_keys(motion_model, motion_keys, 3U) &&
            read_integer(object_item(motion_model, "angleBins"), 360U, 360U, NULL) &&
            read_integer(object_item(motion_model, "magnitudeBins"), 4U, 4U, NULL) &&
            cJSON_IsArray(cells) &&
            cJSON_GetArraySize(cells) == MOTION_MODEL_CELLS;
    }
    parsed.learned.version = 1U;
    for (size_t index = 0U;
         valid && index < MOTION_MODEL_CELLS;
         ++index) {
        valid = parse_motion_cell(
            cJSON_GetArrayItem(cells, (int)index),
            &parsed.learned.cell[index]);
        if (valid) {
            parsed.learned.total_samples += parsed.learned.cell[index].samples;
        }
    }

    if (valid) {
        valid = parse_tuning(object_item(root, "tuning"), &parsed.tuning);
    }

    cJSON_Delete(root);
    if (!valid || motion_profile_validate(&parsed, NULL, 0U) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    *profile = parsed;
    return ESP_OK;
}
