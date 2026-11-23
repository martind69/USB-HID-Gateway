#include "output_config.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

#define TICK_MS_MIN     1U
#define TICK_MS_MAX     50U

static const char *TAG = "output_config";

static const cJSON *object_item(const cJSON *object, const char *key)
{
    return cJSON_IsObject(object) ? cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
}

static bool has_only_keys(const cJSON *object, const char *const *keys, size_t count)
{
    if (!cJSON_IsObject(object)) {
        return false;
    }
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, object) {
        bool known = false;
        for (size_t index = 0U; index < count; ++index) {
            if (item->string != NULL && strcmp(item->string, keys[index]) == 0) {
                known = true;
                break;
            }
        }
        if (!known) {
            ESP_LOGE(TAG, "Unknown key \"%s\"", item->string != NULL ? item->string : "?");
            return false;
        }
    }
    return true;
}

static bool parse_hex(const char *text, size_t length, uint32_t maximum, uint32_t *value)
{
    if (length == 0U || length > 4U) {
        return false;
    }
    uint32_t result = 0U;
    for (size_t index = 0U; index < length; ++index) {
        const int character = (unsigned char)text[index];
        if (!isxdigit(character)) {
            return false;
        }
        result = (result << 4U) |
            (uint32_t)(isdigit(character) ? character - '0' : tolower(character) - 'a' + 10);
    }
    if (result > maximum) {
        return false;
    }
    *value = result;
    return true;
}

static bool parse_hex_item(const cJSON *item, uint32_t maximum, uint32_t *value)
{
    return cJSON_IsString(item) && item->valuestring != NULL &&
        parse_hex(item->valuestring, strlen(item->valuestring), maximum, value);
}

static bool parse_usage(const cJSON *item, input_usage_t *usage)
{
    if (item == NULL) {
        *usage = (input_usage_t) { 0 };
        return true;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    const char *text = item->valuestring;
    const char *colon = strchr(text, ':');
    uint32_t page = 0U;
    uint32_t value = 0U;
    if (colon == NULL ||
        !parse_hex(text, (size_t)(colon - text), 0xFFFFU, &page) ||
        !parse_hex(colon + 1, strlen(colon + 1), 0xFFFFU, &value) ||
        page == 0U || value == 0U) {
        return false;
    }
    *usage = (input_usage_t) { .usage_page = (uint16_t)page, .usage = (uint16_t)value };
    return true;
}

static bool parse_number(const cJSON *item, double minimum, double maximum, double fallback, double *value)
{
    if (item == NULL) {
        *value = fallback;
        return true;
    }
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < minimum || item->valuedouble > maximum) {
        return false;
    }
    *value = item->valuedouble;
    return true;
}

static bool parse_stick(const cJSON *object, output_stick_config_t *stick)
{
    static const char *const KEYS[] = {
        "source", "gain", "returnRate", "up", "down", "left", "right",
    };
    if (object == NULL) {
        *stick = (output_stick_config_t) { .source = OUTPUT_STICK_SOURCE_NONE };
        return true;
    }
    if (!has_only_keys(object, KEYS, 7U)) {
        return false;
    }
    const cJSON *source = object_item(object, "source");
    if (!cJSON_IsString(source) || source->valuestring == NULL) {
        return false;
    }
    output_stick_config_t parsed = { 0 };
    if (strcmp(source->valuestring, "none") == 0) {
        parsed.source = OUTPUT_STICK_SOURCE_NONE;
    } else if (strcmp(source->valuestring, "mouse") == 0) {
        parsed.source = OUTPUT_STICK_SOURCE_MOUSE;
    } else if (strcmp(source->valuestring, "keys") == 0) {
        parsed.source = OUTPUT_STICK_SOURCE_KEYS;
    } else {
        return false;
    }
    double gain = 0.0;
    double return_rate = 0.0;
    if (!parse_number(object_item(object, "gain"), 1e-6, 1.0, 0.004, &gain) ||
        !parse_number(object_item(object, "returnRate"), 0.0, 1000.0, 4.0, &return_rate) ||
        !parse_usage(object_item(object, "up"), &parsed.up) ||
        !parse_usage(object_item(object, "down"), &parsed.down) ||
        !parse_usage(object_item(object, "left"), &parsed.left) ||
        !parse_usage(object_item(object, "right"), &parsed.right)) {
        return false;
    }
    parsed.gain = (float)gain;
    parsed.return_rate = (float)return_rate;
    if (parsed.source == OUTPUT_STICK_SOURCE_KEYS &&
        parsed.up.usage == 0U && parsed.down.usage == 0U &&
        parsed.left.usage == 0U && parsed.right.usage == 0U) {
        ESP_LOGE(TAG, "Stick source \"keys\" without key bindings");
        return false;
    }
    *stick = parsed;
    return true;
}

static bool parse_hotkey(const cJSON *object, output_hotkey_t *hotkey)
{
    static const char *const KEYS[] = { "modifiers", "usage" };
    if (object == NULL) {
        *hotkey = (output_hotkey_t) { 0 };
        return true;
    }
    uint32_t modifiers = 0U;
    uint32_t usage = 0U;
    if (!has_only_keys(object, KEYS, 2U) ||
        !parse_hex_item(object_item(object, "modifiers"), 0xFFU, &modifiers) ||
        !parse_hex_item(object_item(object, "usage"), 0xFFU, &usage) ||
        usage == 0U) {
        return false;
    }
    *hotkey = (output_hotkey_t) {
        .used = true,
        .modifiers = (uint8_t)modifiers,
        .usage = (uint8_t)usage,
    };
    return true;
}

static bool parse_document(const cJSON *root, output_config_t *config)
{
    static const char *const ROOT_KEYS[] = {
        "schemaVersion", "tickMs", "sticks", "triggers", "gpio", "hotkeys",
    };
    static const char *const STICK_KEYS[] = { "left", "right" };
    static const char *const TRIGGER_KEYS[] = { "LT", "RT", "L2", "R2" };
    static const char *const GPIO_KEYS[] = { "usage", "output" };
    static const char *const HOTKEY_KEYS[] = { "mode", "learn" };

    if (!has_only_keys(root, ROOT_KEYS, 6U)) {
        return false;
    }
    double number = 0.0;
    if (!parse_number(object_item(root, "schemaVersion"), 1.0, 1.0, 0.0, &number) ||
        number != (double)OUTPUT_CONFIG_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "schemaVersion must be %u", (unsigned)OUTPUT_CONFIG_SCHEMA_VERSION);
        return false;
    }
    if (!parse_number(object_item(root, "tickMs"), TICK_MS_MIN, TICK_MS_MAX, 2.0, &number) ||
        floor(number) != number) {
        ESP_LOGE(TAG, "tickMs must be an integer %u..%u", TICK_MS_MIN, TICK_MS_MAX);
        return false;
    }
    config->tick_ms = (uint32_t)number;

    const cJSON *sticks = object_item(root, "sticks");
    if (sticks != NULL && !has_only_keys(sticks, STICK_KEYS, 2U)) {
        return false;
    }
    for (size_t index = 0U; index < OUTPUT_STICK_COUNT; ++index) {
        if (!parse_stick(object_item(sticks, STICK_KEYS[index]), &config->stick[index])) {
            ESP_LOGE(TAG, "sticks.%s invalid", STICK_KEYS[index]);
            return false;
        }
    }

    const cJSON *triggers = object_item(root, "triggers");
    if (triggers != NULL && !has_only_keys(triggers, TRIGGER_KEYS, 4U)) {
        return false;
    }
    for (size_t index = 0U; index < OUTPUT_TRIGGER_COUNT; ++index) {
        if (!parse_usage(object_item(triggers, TRIGGER_KEYS[index]), &config->trigger[index])) {
            ESP_LOGE(TAG, "triggers.%s invalid", TRIGGER_KEYS[index]);
            return false;
        }
    }

    const cJSON *gpio = object_item(root, "gpio");
    if (gpio != NULL) {
        if (!cJSON_IsArray(gpio) || cJSON_GetArraySize(gpio) > (int)OUTPUT_CONFIG_MAX_GPIO) {
            ESP_LOGE(TAG, "gpio: array with 0..%u entries", (unsigned)OUTPUT_CONFIG_MAX_GPIO);
            return false;
        }
        const cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, gpio) {
            output_gpio_binding_t binding = { 0 };
            if (!has_only_keys(entry, GPIO_KEYS, 2U) ||
                object_item(entry, "usage") == NULL ||
                !parse_usage(object_item(entry, "usage"), &binding.usage) ||
                !parse_number(object_item(entry, "output"), 0.0,
                    (double)(OUTPUT_CONFIG_MAX_GPIO - 1U), -1.0, &number) ||
                number < 0.0 || floor(number) != number) {
                ESP_LOGE(TAG, "gpio[%u] invalid", (unsigned)config->gpio_count);
                return false;
            }
            binding.output = (uint8_t)number;
            config->gpio[config->gpio_count++] = binding;
        }
    }

    const cJSON *hotkeys = object_item(root, "hotkeys");
    if (hotkeys != NULL && !has_only_keys(hotkeys, HOTKEY_KEYS, 2U)) {
        return false;
    }
    if (!parse_hotkey(object_item(hotkeys, "mode"), &config->mode_hotkey) ||
        !parse_hotkey(object_item(hotkeys, "learn"), &config->learn_hotkey)) {
        ESP_LOGE(TAG, "hotkeys invalid");
        return false;
    }
    return true;
}

void output_config_set_defaults(output_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->tick_ms = 2U;
}

esp_err_t output_config_parse(const char *text, size_t length, output_config_t *config)
{
    if (text == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_ParseWithLength(text, length);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON syntax error");
        return ESP_ERR_INVALID_ARG;
    }
    output_config_t parsed;
    output_config_set_defaults(&parsed);
    const bool valid = parse_document(root, &parsed);
    cJSON_Delete(root);
    if (!valid) {
        return ESP_ERR_INVALID_ARG;
    }
    parsed.from_file = true;
    *config = parsed;
    return ESP_OK;
}

esp_err_t output_config_load(const char *path, output_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    output_config_set_defaults(config);
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "%s not found, task 3 outputs disabled", path);
        return ESP_ERR_NOT_FOUND;
    }
    char *text = malloc(OUTPUT_CONFIG_MAX_FILE_BYTES + 1U);
    if (text == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    const size_t length = fread(text, 1U, OUTPUT_CONFIG_MAX_FILE_BYTES + 1U, file);
    const bool read_error = ferror(file) != 0;
    fclose(file);
    esp_err_t error = ESP_OK;
    if (read_error) {
        error = ESP_FAIL;
    } else if (length > OUTPUT_CONFIG_MAX_FILE_BYTES) {
        error = ESP_ERR_INVALID_SIZE;
    } else {
        error = output_config_parse(text, length, config);
    }
    free(text);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "%s rejected (%s), task 3 outputs disabled", path, esp_err_to_name(error));
        output_config_set_defaults(config);
        return error;
    }
    ESP_LOGI(TAG, "%s loaded: tick=%lu ms, gpio bindings=%u",
        path, (unsigned long)config->tick_ms, config->gpio_count);
    return ESP_OK;
}
