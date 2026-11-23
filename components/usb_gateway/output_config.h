#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "input_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Task 3 output configuration, /sdcard/config/output.json (schemaVersion 1).
 * Maps HID usages of the mirrored devices to the analog channels (DAC) and
 * digital outputs (GPIO). No device identity, only HID usages.
 *
 * {
 *     "schemaVersion": 1,
 *     "tickMs": 2,
 *     "sticks": {
 *         "left":  { "source": "keys", "up": "07:1A", "down": "07:16",
 *                    "left": "07:04", "right": "07:07" },
 *         "right": { "source": "mouse", "gain": 0.004, "returnRate": 4.0 }
 *     },
 *     "triggers": { "LT": "09:02", "RT": "09:01" },
 *     "gpio": [ { "usage": "07:2C", "output": 0 } ],
 *     "hotkeys": {
 *         "mode":  { "modifiers": "05", "usage": "43" },
 *         "learn": { "modifiers": "05", "usage": "44" }
 *     }
 * }
 *
 * Usage notation "PPPP:UUUU": HID usage page and usage, 1..4 hex digits each.
 * stick source: "none" | "mouse" | "keys". Optional keys may be omitted.
 * Unknown keys are rejected. Missing or invalid file: everything disabled.
 */

#define OUTPUT_CONFIG_SCHEMA_VERSION    1U
#define OUTPUT_CONFIG_MAX_GPIO          12U
#define OUTPUT_CONFIG_MAX_FILE_BYTES    (8U * 1024U)

typedef enum {
    OUTPUT_STICK_LEFT = 0,
    OUTPUT_STICK_RIGHT = 1,
    OUTPUT_STICK_COUNT = 2,
} output_stick_index_t;

typedef enum {
    OUTPUT_TRIGGER_LT = 0,
    OUTPUT_TRIGGER_RT = 1,
    OUTPUT_TRIGGER_L2 = 2,
    OUTPUT_TRIGGER_R2 = 3,
    OUTPUT_TRIGGER_COUNT = 4,
} output_trigger_index_t;

typedef enum {
    OUTPUT_STICK_SOURCE_NONE = 0,
    OUTPUT_STICK_SOURCE_MOUSE,
    OUTPUT_STICK_SOURCE_KEYS,
} output_stick_source_t;

typedef struct {
    output_stick_source_t source;
    float gain;                     /* mouse: unit deflection per count */
    float return_rate;              /* mouse: 1/s exponential return to center */
    input_usage_t up;
    input_usage_t down;
    input_usage_t left;
    input_usage_t right;
} output_stick_config_t;

typedef struct {
    input_usage_t usage;
    uint8_t output;                 /* gpio_output index */
} output_gpio_binding_t;

typedef struct {
    bool used;
    uint8_t modifiers;              /* boot-protocol modifier mask */
    uint8_t usage;                  /* keyboard page usage */
} output_hotkey_t;

typedef struct {
    bool from_file;
    uint32_t tick_ms;
    output_stick_config_t stick[OUTPUT_STICK_COUNT];
    input_usage_t trigger[OUTPUT_TRIGGER_COUNT];    /* usage 0 = unused */
    uint8_t gpio_count;
    output_gpio_binding_t gpio[OUTPUT_CONFIG_MAX_GPIO];
    output_hotkey_t mode_hotkey;
    output_hotkey_t learn_hotkey;
} output_config_t;

void output_config_set_defaults(output_config_t *config);

/* config is unchanged on error. */
esp_err_t output_config_parse(const char *text, size_t length, output_config_t *config);

/* Defaults on any error; the error is returned and logged. */
esp_err_t output_config_load(const char *path, output_config_t *config);

#ifdef __cplusplus
}
#endif
