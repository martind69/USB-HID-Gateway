#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Device-related gateway configuration, /sdcard/config/gateway.json.
 * Motion profiles are separate files in the same directory.
 *
 * Schema version 1:
 *
 * {
 *     "schemaVersion": 1,
 *     "usb": {
 *         "ports": [
 *             { "match": { "vid": "046D", "pid": "C33F" }, "target": "s3" },
 *             { "match": { "vid": "046D" },                "target": "s3" },
 *             { "match": { "class": "keyboard" },          "target": "s3" },
 *             { "match": { "class": "mouse" },             "target": "p4_fs" },
 *             { "match": { "any": true },                  "target": "none" }
 *         ],
 *         "intercept": []
 *     }
 * }
 *
 * - "ports": first matching rule wins.
 * - "match": exactly one of {vid[,pid]}, {class}, {any:true}.
 *   vid/pid are 1..4 hexadecimal digits as string.
 * - "target": "p4_fs" | "s3" | "tap" | "none".
 *   p4_fs and s3 accept one device each (exclusive mirror targets).
 * - "intercept": reserved for milestone M5, must be an empty array.
 * - Unknown keys are rejected.
 *
 * If the file is missing or invalid, the default class rules apply:
 * keyboard -> s3, mouse -> p4_fs, anything else -> none.
 */

#define GATEWAY_CONFIG_SCHEMA_VERSION   1U
#define GATEWAY_CONFIG_MAX_PORT_RULES   16U
#define GATEWAY_CONFIG_MAX_FILE_BYTES   (16U * 1024U)

typedef enum {
    GATEWAY_PORT_TARGET_NONE = 0,
    GATEWAY_PORT_TARGET_P4_FS,
    GATEWAY_PORT_TARGET_S3,
    GATEWAY_PORT_TARGET_TAP,
} gateway_port_target_t;

typedef enum {
    GATEWAY_MATCH_ANY = 0,
    GATEWAY_MATCH_VENDOR,
    GATEWAY_MATCH_VENDOR_PRODUCT,
    GATEWAY_MATCH_CLASS_KEYBOARD,
    GATEWAY_MATCH_CLASS_MOUSE,
} gateway_match_kind_t;

typedef struct {
    gateway_match_kind_t kind;
    uint16_t vendor_id;
    uint16_t product_id;
    gateway_port_target_t target;
} gateway_port_rule_t;

typedef struct {
    uint32_t schema_version;
    size_t port_rule_count;
    gateway_port_rule_t port_rules[GATEWAY_CONFIG_MAX_PORT_RULES];
    bool from_file;
} gateway_config_t;

/* Identity of an enumerated device, filled by the USB host client. */
typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    bool has_keyboard;  /* boot protocol 1 or Generic Desktop / Keyboard */
    bool has_mouse;     /* boot protocol 2 or Generic Desktop / Mouse */
} gateway_device_identity_t;

/* Creates GATEWAY_CONFIG_DIR and moves legacy JSON files into it. */
esp_err_t gateway_config_prepare_storage(void);

/*
 * Loads the configuration file into the active configuration.
 * On any error the default rules become active and the error is returned.
 */
esp_err_t gateway_config_load(const char *path);

/* Parses JSON text into config. config is unchanged on error. */
esp_err_t gateway_config_parse(
    const char *text,
    size_t length,
    gateway_config_t *config);

void gateway_config_set_defaults(gateway_config_t *config);

const gateway_config_t *gateway_config_get(void);

/* Rule evaluation without occupancy check. */
gateway_port_target_t gateway_config_match(
    const gateway_config_t *config,
    const gateway_device_identity_t *identity);

/*
 * Resolves and reserves a target for a device address using the active
 * configuration. Returns NONE if the exclusive mirror target is occupied.
 */
gateway_port_target_t gateway_port_claim(
    uint8_t device_address,
    const gateway_device_identity_t *identity);

void gateway_port_release(uint8_t device_address);

const char *gateway_port_target_name(gateway_port_target_t target);

#ifdef __cplusplus
}
#endif
