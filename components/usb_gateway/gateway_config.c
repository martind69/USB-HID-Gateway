#include "gateway_config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_log.h"
#include "gateway_paths.h"

static const char *TAG = "gateway_config";

static gateway_config_t s_active;
static bool s_active_initialized;

/* Device address occupying each exclusive mirror target, 0 = free. */
static uint8_t s_owner_p4_fs;
static uint8_t s_owner_s3;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                */
/* ---------------------------------------------------------------------- */

static const cJSON *object_item(const cJSON *object, const char *key)
{
    return cJSON_IsObject(object)
        ? cJSON_GetObjectItemCaseSensitive(object, key)
        : NULL;
}

static bool object_has_only_keys(
    const cJSON *object,
    const char *const *keys,
    size_t key_count)
{
    if (!cJSON_IsObject(object)) {
        return false;
    }
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, object) {
        bool known = false;
        for (size_t index = 0U; index < key_count; ++index) {
            if (item->string != NULL && strcmp(item->string, keys[index]) == 0) {
                known = true;
                break;
            }
        }
        if (!known) {
            ESP_LOGE(TAG, "Unknown key \"%s\"",
                item->string != NULL ? item->string : "?");
            return false;
        }
    }
    return true;
}

static bool parse_hex16(const cJSON *item, uint16_t *value)
{
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    const char *text = item->valuestring;
    const size_t length = strlen(text);
    if (length == 0U || length > 4U) {
        return false;
    }
    uint32_t result = 0U;
    for (size_t index = 0U; index < length; ++index) {
        const int character = (unsigned char)text[index];
        if (!isxdigit(character)) {
            return false;
        }
        const uint32_t digit = isdigit(character)
            ? (uint32_t)(character - '0')
            : (uint32_t)(tolower(character) - 'a' + 10);
        result = (result << 4U) | digit;
    }
    *value = (uint16_t)result;
    return true;
}

static bool parse_target(const cJSON *item, gateway_port_target_t *target)
{
    static const struct {
        const char *name;
        gateway_port_target_t target;
    } TARGETS[] = {
        { "p4_fs", GATEWAY_PORT_TARGET_P4_FS },
        { "s3", GATEWAY_PORT_TARGET_S3 },
        { "tap", GATEWAY_PORT_TARGET_TAP },
        { "none", GATEWAY_PORT_TARGET_NONE },
    };
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return false;
    }
    for (size_t index = 0U; index < sizeof(TARGETS) / sizeof(TARGETS[0]); ++index) {
        if (strcmp(item->valuestring, TARGETS[index].name) == 0) {
            *target = TARGETS[index].target;
            return true;
        }
    }
    return false;
}

static bool parse_match(const cJSON *match, gateway_port_rule_t *rule)
{
    static const char *const KEYS[] = { "vid", "pid", "class", "any" };
    if (!object_has_only_keys(match, KEYS, 4U)) {
        return false;
    }
    const cJSON *vid = object_item(match, "vid");
    const cJSON *pid = object_item(match, "pid");
    const cJSON *device_class = object_item(match, "class");
    const cJSON *any = object_item(match, "any");

    const unsigned selectors =
        (vid != NULL ? 1U : 0U) +
        (device_class != NULL ? 1U : 0U) +
        (any != NULL ? 1U : 0U);
    if (selectors != 1U) {
        return false;
    }
    if (vid != NULL) {
        if (!parse_hex16(vid, &rule->vendor_id)) {
            return false;
        }
        if (pid == NULL) {
            rule->kind = GATEWAY_MATCH_VENDOR;
            return true;
        }
        if (!parse_hex16(pid, &rule->product_id)) {
            return false;
        }
        rule->kind = GATEWAY_MATCH_VENDOR_PRODUCT;
        return true;
    }
    if (pid != NULL) {
        return false;
    }
    if (device_class != NULL) {
        if (!cJSON_IsString(device_class) || device_class->valuestring == NULL) {
            return false;
        }
        if (strcmp(device_class->valuestring, "keyboard") == 0) {
            rule->kind = GATEWAY_MATCH_CLASS_KEYBOARD;
            return true;
        }
        if (strcmp(device_class->valuestring, "mouse") == 0) {
            rule->kind = GATEWAY_MATCH_CLASS_MOUSE;
            return true;
        }
        return false;
    }
    if (!cJSON_IsTrue(any)) {
        return false;
    }
    rule->kind = GATEWAY_MATCH_ANY;
    return true;
}

static bool parse_ports(const cJSON *ports, gateway_config_t *config)
{
    static const char *const KEYS[] = { "match", "target" };
    if (!cJSON_IsArray(ports)) {
        return false;
    }
    const int count = cJSON_GetArraySize(ports);
    if (count < 0 || (size_t)count > GATEWAY_CONFIG_MAX_PORT_RULES) {
        ESP_LOGE(TAG, "usb.ports: 0..%u rules allowed",
            (unsigned)GATEWAY_CONFIG_MAX_PORT_RULES);
        return false;
    }
    size_t index = 0U;
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, ports) {
        gateway_port_rule_t rule = { 0 };
        if (!object_has_only_keys(entry, KEYS, 2U) ||
            !parse_match(object_item(entry, "match"), &rule) ||
            !parse_target(object_item(entry, "target"), &rule.target)) {
            ESP_LOGE(TAG, "usb.ports[%u] invalid", (unsigned)index);
            return false;
        }
        config->port_rules[index++] = rule;
    }
    config->port_rule_count = index;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                             */
/* ---------------------------------------------------------------------- */

const char *gateway_port_target_name(gateway_port_target_t target)
{
    switch (target) {
        case GATEWAY_PORT_TARGET_P4_FS:
            return "p4_fs";
        case GATEWAY_PORT_TARGET_S3:
            return "s3";
        case GATEWAY_PORT_TARGET_TAP:
            return "tap";
        case GATEWAY_PORT_TARGET_NONE:
        default:
            return "none";
    }
}

void gateway_config_set_defaults(gateway_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->schema_version = GATEWAY_CONFIG_SCHEMA_VERSION;
    config->port_rules[0] = (gateway_port_rule_t) {
        .kind = GATEWAY_MATCH_CLASS_KEYBOARD,
        .target = GATEWAY_PORT_TARGET_S3,
    };
    config->port_rules[1] = (gateway_port_rule_t) {
        .kind = GATEWAY_MATCH_CLASS_MOUSE,
        .target = GATEWAY_PORT_TARGET_P4_FS,
    };
    config->port_rules[2] = (gateway_port_rule_t) {
        .kind = GATEWAY_MATCH_ANY,
        .target = GATEWAY_PORT_TARGET_NONE,
    };
    config->port_rule_count = 3U;
    config->from_file = false;
}

esp_err_t gateway_config_parse(
    const char *text,
    size_t length,
    gateway_config_t *config)
{
    if (text == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_ParseWithLength(text, length);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON syntax error");
        return ESP_ERR_INVALID_ARG;
    }

    static const char *const ROOT_KEYS[] = { "schemaVersion", "usb" };
    static const char *const USB_KEYS[] = { "ports", "intercept" };

    gateway_config_t parsed = { 0 };
    bool valid = object_has_only_keys(root, ROOT_KEYS, 2U);

    const cJSON *version = object_item(root, "schemaVersion");
    if (valid) {
        valid = cJSON_IsNumber(version) &&
            version->valuedouble == (double)GATEWAY_CONFIG_SCHEMA_VERSION;
        if (!valid) {
            ESP_LOGE(TAG, "schemaVersion must be %u",
                (unsigned)GATEWAY_CONFIG_SCHEMA_VERSION);
        }
    }
    const cJSON *usb = object_item(root, "usb");
    if (valid) {
        valid = object_has_only_keys(usb, USB_KEYS, 2U) &&
            parse_ports(object_item(usb, "ports"), &parsed);
    }
    if (valid) {
        const cJSON *intercept = object_item(usb, "intercept");
        valid = cJSON_IsArray(intercept) && cJSON_GetArraySize(intercept) == 0;
        if (!valid) {
            ESP_LOGE(TAG, "usb.intercept must be an empty array (milestone M5)");
        }
    }
    cJSON_Delete(root);

    if (!valid) {
        return ESP_ERR_INVALID_ARG;
    }
    parsed.schema_version = GATEWAY_CONFIG_SCHEMA_VERSION;
    parsed.from_file = true;
    *config = parsed;
    return ESP_OK;
}

esp_err_t gateway_config_load(const char *path)
{
    gateway_config_set_defaults(&s_active);
    s_active_initialized = true;
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "%s not found, default class rules active", path);
        return ESP_ERR_NOT_FOUND;
    }
    char *text = malloc(GATEWAY_CONFIG_MAX_FILE_BYTES + 1U);
    if (text == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    const size_t length = fread(text, 1U, GATEWAY_CONFIG_MAX_FILE_BYTES + 1U, file);
    const bool read_error = ferror(file) != 0;
    fclose(file);

    esp_err_t error = ESP_OK;
    if (read_error) {
        error = ESP_FAIL;
    } else if (length > GATEWAY_CONFIG_MAX_FILE_BYTES) {
        error = ESP_ERR_INVALID_SIZE;
    } else {
        gateway_config_t parsed;
        error = gateway_config_parse(text, length, &parsed);
        if (error == ESP_OK) {
            s_active = parsed;
        }
    }
    free(text);

    if (error != ESP_OK) {
        ESP_LOGE(TAG, "%s rejected (%s), default class rules active",
            path, esp_err_to_name(error));
        return error;
    }
    ESP_LOGI(TAG, "%s loaded: %u port rules",
        path, (unsigned)s_active.port_rule_count);
    return ESP_OK;
}

const gateway_config_t *gateway_config_get(void)
{
    if (!s_active_initialized) {
        gateway_config_set_defaults(&s_active);
        s_active_initialized = true;
    }
    return &s_active;
}

static bool rule_matches(
    const gateway_port_rule_t *rule,
    const gateway_device_identity_t *identity)
{
    switch (rule->kind) {
        case GATEWAY_MATCH_ANY:
            return true;
        case GATEWAY_MATCH_VENDOR:
            return identity->vendor_id == rule->vendor_id;
        case GATEWAY_MATCH_VENDOR_PRODUCT:
            return identity->vendor_id == rule->vendor_id &&
                identity->product_id == rule->product_id;
        case GATEWAY_MATCH_CLASS_KEYBOARD:
            return identity->has_keyboard;
        case GATEWAY_MATCH_CLASS_MOUSE:
            return identity->has_mouse;
        default:
            return false;
    }
}

gateway_port_target_t gateway_config_match(
    const gateway_config_t *config,
    const gateway_device_identity_t *identity)
{
    if (config == NULL || identity == NULL) {
        return GATEWAY_PORT_TARGET_NONE;
    }
    for (size_t index = 0U; index < config->port_rule_count; ++index) {
        if (rule_matches(&config->port_rules[index], identity)) {
            return config->port_rules[index].target;
        }
    }
    return GATEWAY_PORT_TARGET_NONE;
}

gateway_port_target_t gateway_port_claim(
    uint8_t device_address,
    const gateway_device_identity_t *identity)
{
    if (device_address == 0U || identity == NULL) {
        return GATEWAY_PORT_TARGET_NONE;
    }
    const gateway_port_target_t target =
        gateway_config_match(gateway_config_get(), identity);
    uint8_t *owner = NULL;
    if (target == GATEWAY_PORT_TARGET_P4_FS) {
        owner = &s_owner_p4_fs;
    } else if (target == GATEWAY_PORT_TARGET_S3) {
        owner = &s_owner_s3;
    } else {
        return target;
    }
    if (*owner != 0U && *owner != device_address) {
        ESP_LOGW(TAG, "Target %s occupied by address %u, %04X:%04X ignored",
            gateway_port_target_name(target), *owner,
            identity->vendor_id, identity->product_id);
        return GATEWAY_PORT_TARGET_NONE;
    }
    *owner = device_address;
    return target;
}

void gateway_port_release(uint8_t device_address)
{
    if (device_address == 0U) {
        return;
    }
    if (s_owner_p4_fs == device_address) {
        s_owner_p4_fs = 0U;
    }
    if (s_owner_s3 == device_address) {
        s_owner_s3 = 0U;
    }
}

esp_err_t gateway_config_prepare_storage(void)
{
    if (mkdir(GATEWAY_CONFIG_DIR, 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "Cannot create %s: errno %d", GATEWAY_CONFIG_DIR, errno);
        return ESP_FAIL;
    }
    struct stat status;
    const bool legacy_exists = stat(GATEWAY_LEGACY_PROFILE_JSON, &status) == 0;
    const bool target_exists = stat(GATEWAY_PROFILE_JSON_FILE, &status) == 0;
    if (legacy_exists && !target_exists) {
        if (rename(GATEWAY_LEGACY_PROFILE_JSON, GATEWAY_PROFILE_JSON_FILE) != 0) {
            ESP_LOGE(TAG, "Cannot move %s to %s: errno %d",
                GATEWAY_LEGACY_PROFILE_JSON, GATEWAY_PROFILE_JSON_FILE, errno);
            return ESP_FAIL;
        }
        ESP_LOGW(TAG, "Profile moved: %s -> %s",
            GATEWAY_LEGACY_PROFILE_JSON, GATEWAY_PROFILE_JSON_FILE);
    } else if (legacy_exists && target_exists) {
        ESP_LOGW(TAG, "%s ignored, %s takes precedence",
            GATEWAY_LEGACY_PROFILE_JSON, GATEWAY_PROFILE_JSON_FILE);
    }
    return ESP_OK;
}
