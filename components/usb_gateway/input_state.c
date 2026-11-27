#include "input_state.h"

#include <string.h>

static bool same_usage(input_usage_t a, input_usage_t b)
{
    return a.usage_page == b.usage_page && a.usage == b.usage;
}

static bool is_modifier(input_usage_t usage)
{
    return usage.usage_page == HID_PAGE_KEYBOARD &&
        usage.usage >= HID_USAGE_KEY_LEFT_CONTROL &&
        usage.usage <= HID_USAGE_KEY_RIGHT_GUI;
}

void input_state_reset(input_state_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

bool input_state_is_pressed(const input_state_t *state, input_usage_t usage)
{
    if (state == NULL || usage.usage == 0U) {
        return false;
    }
    for (uint8_t index = 0U; index < state->pressed_count; ++index) {
        if (same_usage(state->pressed[index].usage, usage)) {
            return true;
        }
    }
    return false;
}

uint8_t input_state_modifiers(const input_state_t *state)
{
    uint8_t modifiers = 0U;
    if (state == NULL) {
        return 0U;
    }
    for (uint8_t index = 0U; index < state->pressed_count; ++index) {
        const input_usage_t usage = state->pressed[index].usage;
        if (is_modifier(usage)) {
            modifiers |= (uint8_t)(1U << (usage.usage - HID_USAGE_KEY_LEFT_CONTROL));
        }
    }
    return modifiers;
}

static bool owner_matches(
    const input_pressed_t *entry,
    uint8_t owner_interface,
    uint8_t owner_report,
    bool any_report)
{
    return entry->owner_interface == owner_interface &&
        (any_report || entry->owner_report == owner_report);
}

static void replace_owner(
    input_state_t *state,
    uint8_t owner_interface,
    uint8_t owner_report,
    bool any_report,
    const input_usage_t *usages,
    size_t usage_count,
    input_edge_cb_t edge,
    void *context)
{
    input_usage_t released[INPUT_STATE_MAX_PRESSED];
    size_t released_count = 0U;
    input_usage_t added[INPUT_STATE_MAX_PRESSED];
    size_t added_count = 0U;

    /* Remove entries of this owner that are no longer reported. */
    uint8_t write = 0U;
    for (uint8_t read = 0U; read < state->pressed_count; ++read) {
        const input_pressed_t entry = state->pressed[read];
        bool keep = true;
        if (owner_matches(&entry, owner_interface, owner_report, any_report)) {
            keep = false;
            for (size_t index = 0U; index < usage_count; ++index) {
                if (same_usage(usages[index], entry.usage)) {
                    keep = true;
                    break;
                }
            }
            if (!keep) {
                released[released_count++] = entry.usage;
            }
        }
        if (keep) {
            state->pressed[write++] = entry;
        }
    }
    state->pressed_count = write;

    /* Add new usages. */
    for (size_t index = 0U; index < usage_count; ++index) {
        bool present = false;
        for (uint8_t slot = 0U; slot < state->pressed_count; ++slot) {
            if (owner_matches(&state->pressed[slot], owner_interface, owner_report, false) &&
                same_usage(state->pressed[slot].usage, usages[index])) {
                present = true;
                break;
            }
        }
        if (present || usages[index].usage == 0U) {
            continue;
        }
        if (state->pressed_count >= INPUT_STATE_MAX_PRESSED) {
            state->overflow = true;
            continue;
        }
        state->pressed[state->pressed_count++] = (input_pressed_t) {
            .usage = usages[index],
            .owner_interface = owner_interface,
            .owner_report = owner_report,
        };
        added[added_count++] = usages[index];
    }

    if (edge == NULL) {
        return;
    }
    const uint8_t modifiers = input_state_modifiers(state);
    for (size_t index = 0U; index < released_count; ++index) {
        edge(released[index], false, modifiers, context);
    }
    for (size_t index = 0U; index < added_count; ++index) {
        edge(added[index], true, modifiers, context);
    }
}

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
    void *context)
{
    if (state == NULL || (usages == NULL && usage_count > 0U)) {
        return;
    }
    state->mouse_dx += dx;
    state->mouse_dy += dy;
    state->wheel += wheel;
    replace_owner(state, owner_interface, owner_report, false,
        usages, usage_count, edge, context);
}

void input_state_remove_interface(
    input_state_t *state,
    uint8_t owner_interface,
    input_edge_cb_t edge,
    void *context)
{
    if (state == NULL) {
        return;
    }
    replace_owner(state, owner_interface, 0U, true, NULL, 0U, edge, context);
}

void input_state_take_snapshot(input_state_t *state, input_state_t *snapshot)
{
    if (state == NULL || snapshot == NULL) {
        return;
    }
    *snapshot = *state;
    state->mouse_dx = 0.0f;
    state->mouse_dy = 0.0f;
    state->wheel = 0.0f;
}
