#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Aggregated input state of all mirrored/tapped HID devices.
 * Pure C, host-testable. Not thread-safe; the caller provides locking.
 *
 * Pressed usages are owned by (interface id, report id). Every decoded
 * report replaces the entries of its owner, so key releases are detected
 * without per-device knowledge. Relative axes (Generic Desktop X, Y, Wheel)
 * are accumulated until consumed.
 */

#define INPUT_STATE_MAX_PRESSED     48U

#define HID_PAGE_GENERIC_DESKTOP    0x01U
#define HID_PAGE_KEYBOARD           0x07U
#define HID_PAGE_BUTTON             0x09U
#define HID_PAGE_CONSUMER           0x0CU

#define HID_USAGE_GD_X              0x30U
#define HID_USAGE_GD_Y              0x31U
#define HID_USAGE_GD_WHEEL          0x38U
#define HID_USAGE_KEY_LEFT_CONTROL  0xE0U
#define HID_USAGE_KEY_RIGHT_GUI     0xE7U

typedef struct {
    uint16_t usage_page;
    uint16_t usage;
} input_usage_t;

typedef struct {
    input_usage_t usage;
    uint8_t owner_interface;
    uint8_t owner_report;
} input_pressed_t;

typedef struct {
    uint8_t pressed_count;
    bool overflow;
    input_pressed_t pressed[INPUT_STATE_MAX_PRESSED];
    float mouse_dx;
    float mouse_dy;
    float wheel;
} input_state_t;

/* Called for every key/button whose state changed. */
typedef void (*input_edge_cb_t)(
    input_usage_t usage,
    bool pressed,
    uint8_t modifiers,
    void *context);

void input_state_reset(input_state_t *state);

/*
 * Replaces the pressed usages of one report owner.
 * usages: currently active usages of this report (pages 0x07, 0x09, 0x0C).
 */
void input_state_apply_report(
    input_state_t *state,
    uint8_t owner_interface,
    uint8_t owner_report,
    const input_usage_t *usages,
    size_t usage_count,
    float dx,
    float dy,
    float wheel,
    input_edge_cb_t edge,
    void *context);

/* Releases all usages of an interface (device removed). */
void input_state_remove_interface(
    input_state_t *state,
    uint8_t owner_interface,
    input_edge_cb_t edge,
    void *context);

bool input_state_is_pressed(const input_state_t *state, input_usage_t usage);

/* Boot-protocol modifier byte built from keyboard usages 0xE0..0xE7. */
uint8_t input_state_modifiers(const input_state_t *state);

/* Copies the state and clears the accumulated relative motion. */
void input_state_take_snapshot(input_state_t *state, input_state_t *snapshot);

#ifdef __cplusplus
}
#endif
