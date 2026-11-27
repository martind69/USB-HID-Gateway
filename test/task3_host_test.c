/*
 * Host test for the task 3 pure-C modules:
 *     hid_report_decoder, input_state, analog_mapping, output_config
 *
 * Build (from test/):
 *   gcc -std=c17 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I. -I../components/usb_gateway \
 *       -Istubs task3_host_test.c cJSON_minimal.c \
 *       ../components/usb_gateway/hid_report_decoder.c ../components/usb_gateway/input_state.c \
 *       ../components/usb_gateway/analog_mapping.c ../components/usb_gateway/output_config.c -lm
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "analog_mapping.h"
#include "hid_report_decoder.h"
#include "input_state.h"
#include "output_config.h"

int log_stub(const char *format, ...) { (void)format; return 0; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "error"; }

/* ---- decoder ---------------------------------------------------------- */

static const uint8_t BOOT_KEYBOARD[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
    0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01,
    0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65,
    0x81, 0x00, 0xC0,
};

/* Mouse, 16 buttons, X/Y int16 relative, wheel, AC pan (G502 IF0 layout). */
static const uint8_t MOUSE_16BIT[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09,
    0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x95, 0x10, 0x75, 0x01,
    0x81, 0x02, 0x05, 0x01, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10,
    0x95, 0x02, 0x09, 0x30, 0x09, 0x31, 0x81, 0x06, 0x15, 0x81, 0x25, 0x7F,
    0x75, 0x08, 0x95, 0x01, 0x09, 0x38, 0x81, 0x06, 0x05, 0x0C, 0x0A, 0x38,
    0x02, 0x95, 0x01, 0x81, 0x06, 0xC0, 0xC0,
};

/* Consumer controls with Report ID 3 (G815 IF1 excerpt). */
static const uint8_t CONSUMER_ID3[] = {
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x07, 0x09, 0xB5, 0x09, 0xB6, 0x09, 0xB7, 0x09, 0xCD,
    0x09, 0xE9, 0x09, 0xEA, 0x09, 0xE2, 0x81, 0x02, 0x95, 0x01, 0x81, 0x01,
    0xC0,
};

typedef struct {
    input_usage_t usages[32];
    size_t count;
    float dx, dy, wheel;
} collect_t;

static void collect(uint16_t page, uint16_t usage, int32_t value, bool relative, void *context)
{
    collect_t *c = context;
    if (relative) {
        if (page == 0x01 && usage == 0x30) c->dx += (float)value;
        if (page == 0x01 && usage == 0x31) c->dy += (float)value;
        if (page == 0x01 && usage == 0x38) c->wheel += (float)value;
        return;
    }
    if (value != 0 && c->count < 32U) {
        c->usages[c->count++] = (input_usage_t) { page, usage };
    }
}

static bool contains(const collect_t *c, uint16_t page, uint16_t usage)
{
    for (size_t i = 0; i < c->count; ++i) {
        if (c->usages[i].usage_page == page && c->usages[i].usage == usage) return true;
    }
    return false;
}

static void test_decoder(void)
{
    static hid_decoder_layout_t layout;
    collect_t c;

    assert(hid_decoder_parse(BOOT_KEYBOARD, sizeof(BOOT_KEYBOARD), &layout) == HID_DECODER_OK);
    assert(!layout.uses_report_ids && layout.field_count == 2U);
    const uint8_t keys[8] = { 0x05, 0x00, 0x44, 0x04, 0x00, 0x00, 0x00, 0x00 };
    memset(&c, 0, sizeof(c));
    assert(hid_decoder_decode(&layout, keys, sizeof(keys), collect, &c) == HID_DECODER_OK);
    assert(c.count == 4U);
    assert(contains(&c, 0x07, 0xE0) && contains(&c, 0x07, 0xE2));
    assert(contains(&c, 0x07, 0x44) && contains(&c, 0x07, 0x04));

    assert(hid_decoder_parse(MOUSE_16BIT, sizeof(MOUSE_16BIT), &layout) == HID_DECODER_OK);
    assert(layout.field_count == 4U);
    /* buttons 1+3, X = -300, Y = +1000, wheel = -1 */
    const uint8_t mouse[8] = { 0x05, 0x00, 0xD4, 0xFE, 0xE8, 0x03, 0xFF, 0x00 };
    memset(&c, 0, sizeof(c));
    assert(hid_decoder_decode(&layout, mouse, sizeof(mouse), collect, &c) == HID_DECODER_OK);
    assert(c.dx == -300.0f && c.dy == 1000.0f && c.wheel == -1.0f);
    assert(contains(&c, 0x09, 1) && contains(&c, 0x09, 3) && !contains(&c, 0x09, 2));
    assert(hid_decoder_has_usage(&layout, 0x01, 0x30));

    assert(hid_decoder_parse(CONSUMER_ID3, sizeof(CONSUMER_ID3), &layout) == HID_DECODER_OK);
    assert(layout.uses_report_ids);
    const uint8_t consumer[2] = { 0x03, 0x08 };
    memset(&c, 0, sizeof(c));
    assert(hid_decoder_decode(&layout, consumer, sizeof(consumer), collect, &c) == HID_DECODER_OK);
    assert(c.count == 1U && contains(&c, 0x0C, 0xCD));
    const uint8_t other_id[2] = { 0x11, 0xFF };
    memset(&c, 0, sizeof(c));
    assert(hid_decoder_decode(&layout, other_id, sizeof(other_id), collect, &c) == HID_DECODER_OK);
    assert(c.count == 0U);

    const uint8_t truncated[] = { 0x05, 0x01, 0x26, 0xFF };
    assert(hid_decoder_parse(truncated, sizeof(truncated), &layout) == HID_DECODER_ERR_DESCRIPTOR);
    const uint8_t pop_underflow[] = { 0xB4 };
    assert(hid_decoder_parse(pop_underflow, sizeof(pop_underflow), &layout) == HID_DECODER_ERR_DESCRIPTOR);
}

/* ---- input_state ------------------------------------------------------ */

static int g_edges_pressed;
static int g_edges_released;
static uint8_t g_last_modifiers;

static void edge(input_usage_t usage, bool pressed, uint8_t modifiers, void *context)
{
    (void)usage;
    (void)context;
    if (pressed) ++g_edges_pressed; else ++g_edges_released;
    g_last_modifiers = modifiers;
}

static void test_input_state(void)
{
    input_state_t state, snapshot;
    input_state_reset(&state);
    const input_usage_t w = { 0x07, 0x1A }, ctrl = { 0x07, 0xE0 }, alt = { 0x07, 0xE2 }, f11 = { 0x07, 0x44 };
    const input_usage_t button1 = { 0x09, 0x01 };

    const input_usage_t report1[] = { ctrl, alt, f11 };
    input_state_apply_report(&state, 1, 0, report1, 3, 0, 0, 0, edge, NULL);
    assert(g_edges_pressed == 3 && g_last_modifiers == 0x05);
    assert(input_state_modifiers(&state) == 0x05 && input_state_is_pressed(&state, f11));

    const input_usage_t report2[] = { w };
    input_state_apply_report(&state, 1, 0, report2, 1, 0, 0, 0, edge, NULL);
    assert(g_edges_released == 3 && g_edges_pressed == 4);
    assert(!input_state_is_pressed(&state, f11) && input_state_is_pressed(&state, w));

    /* Second device keeps its own entries; motion accumulates. */
    const input_usage_t report3[] = { button1 };
    input_state_apply_report(&state, 2, 0, report3, 1, 10, -5, 1, edge, NULL);
    input_state_apply_report(&state, 2, 0, report3, 1, 3, 2, 0, edge, NULL);
    assert(input_state_is_pressed(&state, w) && input_state_is_pressed(&state, button1));
    input_state_take_snapshot(&state, &snapshot);
    assert(snapshot.mouse_dx == 13.0f && snapshot.mouse_dy == -3.0f && snapshot.wheel == 1.0f);
    assert(state.mouse_dx == 0.0f && state.mouse_dy == 0.0f);

    input_state_remove_interface(&state, 1, edge, NULL);
    assert(!input_state_is_pressed(&state, w) && input_state_is_pressed(&state, button1));
    input_state_remove_interface(&state, 2, NULL, NULL);
    assert(state.pressed_count == 0U);
}

/* ---- analog_mapping --------------------------------------------------- */

static void test_mapping(void)
{
    const motion_axis_calibration_t axis = { .min_code = 400, .center_code = 2000, .max_code = 3800 };
    assert(analog_mapping_axis_to_code(&axis, 0.0f, false) == 2000U);
    assert(analog_mapping_axis_to_code(&axis, 1.0f, false) == 3800U);
    assert(analog_mapping_axis_to_code(&axis, -1.0f, false) == 400U);
    assert(analog_mapping_axis_to_code(&axis, 0.5f, false) == 2900U);
    assert(analog_mapping_axis_to_code(&axis, 0.5f, true) == 1200U);
    assert(analog_mapping_axis_to_code(&axis, 7.0f, false) == 3800U);
    assert(fabsf(analog_mapping_code_to_axis(&axis, 2900U, false) - 0.5f) < 1e-6f);
    assert(fabsf(analog_mapping_code_to_axis(&axis, 1200U, false) + 0.5f) < 1e-6f);
    assert(fabsf(analog_mapping_code_to_axis(&axis, 1200U, true) - 0.5f) < 1e-6f);
    assert(analog_mapping_code_to_axis(&axis, 4095U, false) == 1.0f);
    for (int code = 400; code <= 3800; code += 17) {
        const float v = analog_mapping_code_to_axis(&axis, (uint16_t)code, false);
        const int back = analog_mapping_axis_to_code(&axis, v, false);
        assert(back >= code - 1 && back <= code + 1);
    }

    const motion_axis_calibration_t trigger = { .min_code = 100, .max_code = 4000 };
    assert(analog_mapping_trigger_to_code(&trigger, 0.0f, false) == 100U);
    assert(analog_mapping_trigger_to_code(&trigger, 1.0f, false) == 4000U);
    assert(analog_mapping_trigger_to_code(&trigger, 1.0f, true) == 100U);

    static float radius[MOTION_PROFILE_ENVELOPE_BINS];
    for (unsigned i = 0; i < MOTION_PROFILE_ENVELOPE_BINS; ++i) radius[i] = 0.8f;
    motion_vec2_t v = analog_mapping_envelope_scale(radius, 0.5f, (motion_vec2_t) { 1.0f, 0.0f });
    assert(fabsf(v.x - 0.4f) < 1e-6f && v.y == 0.0f);
    v = analog_mapping_envelope_scale(radius, 0.0f, (motion_vec2_t) { 0.0f, -2.0f });
    assert(fabsf(v.y + 0.8f) < 1e-6f);
    radius[45] = 0.0f;
    v = analog_mapping_envelope_scale(radius, 1.0f, (motion_vec2_t) { 0.7071f, 0.7072f });
    assert(fabsf(hypotf(v.x, v.y) - 1.0f) < 1e-3f);
    v = analog_mapping_unit_circle((motion_vec2_t) { 1.0f, 1.0f });
    assert(fabsf(hypotf(v.x, v.y) - 1.0f) < 1e-6f);
}

/* ---- output_config ---------------------------------------------------- */

static esp_err_t parse(const char *text, output_config_t *config)
{
    return output_config_parse(text, strlen(text), config);
}

static void test_output_config(void)
{
    output_config_t config;
    FILE *file = fopen("../sdcard/config/output.json", "rb");
    assert(file != NULL);
    static char text[8192];
    const size_t length = fread(text, 1, sizeof(text), file);
    fclose(file);
    assert(output_config_parse(text, length, &config) == ESP_OK);
    assert(config.from_file && config.tick_ms == 2U);
    assert(config.stick[OUTPUT_STICK_LEFT].source == OUTPUT_STICK_SOURCE_KEYS);
    assert(config.stick[OUTPUT_STICK_LEFT].up.usage_page == 0x07 && config.stick[OUTPUT_STICK_LEFT].up.usage == 0x1A);
    assert(config.stick[OUTPUT_STICK_RIGHT].source == OUTPUT_STICK_SOURCE_MOUSE);
    assert(fabsf(config.stick[OUTPUT_STICK_RIGHT].gain - 0.004f) < 1e-9f);
    assert(config.trigger[OUTPUT_TRIGGER_LT].usage_page == 0x09 && config.trigger[OUTPUT_TRIGGER_LT].usage == 0x02);
    assert(config.trigger[OUTPUT_TRIGGER_L2].usage == 0U);
    assert(config.gpio_count == 1U && config.gpio[0].usage.usage == 0x2C && config.gpio[0].output == 0U);
    assert(config.learn_hotkey.used && config.learn_hotkey.modifiers == 0x05 && config.learn_hotkey.usage == 0x44);
    assert(config.mode_hotkey.used && config.mode_hotkey.usage == 0x43);

    /* Minimal document: everything optional except schemaVersion. */
    assert(parse("{\"schemaVersion\":1}", &config) == ESP_OK);
    assert(config.tick_ms == 2U && config.stick[0].source == OUTPUT_STICK_SOURCE_NONE && config.gpio_count == 0U);

    static const char *const INVALID[] = {
        "{",
        "{\"schemaVersion\":2}",
        "{\"schemaVersion\":1,\"x\":1}",
        "{\"schemaVersion\":1,\"tickMs\":0}",
        "{\"schemaVersion\":1,\"tickMs\":2.5}",
        "{\"schemaVersion\":1,\"sticks\":{\"middle\":{\"source\":\"none\"}}}",
        "{\"schemaVersion\":1,\"sticks\":{\"left\":{\"source\":\"joystick\"}}}",
        "{\"schemaVersion\":1,\"sticks\":{\"left\":{\"source\":\"keys\"}}}",
        "{\"schemaVersion\":1,\"sticks\":{\"left\":{\"source\":\"mouse\",\"gain\":5}}}",
        "{\"schemaVersion\":1,\"triggers\":{\"LT\":\"9\"}}",
        "{\"schemaVersion\":1,\"triggers\":{\"LT\":\"09:0\"}}",
        "{\"schemaVersion\":1,\"triggers\":{\"XT\":\"09:01\"}}",
        "{\"schemaVersion\":1,\"gpio\":[{\"usage\":\"07:2C\",\"output\":12}]}",
        "{\"schemaVersion\":1,\"gpio\":[{\"output\":1}]}",
        "{\"schemaVersion\":1,\"hotkeys\":{\"learn\":{\"modifiers\":\"100\",\"usage\":\"44\"}}}",
        "{\"schemaVersion\":1,\"hotkeys\":{\"learn\":{\"modifiers\":\"05\",\"usage\":\"00\"}}}",
    };
    assert(parse("{\"schemaVersion\":1,\"tickMs\":7}", &config) == ESP_OK);
    for (size_t i = 0; i < sizeof(INVALID) / sizeof(INVALID[0]); ++i) {
        if (parse(INVALID[i], &config) == ESP_OK) {
            printf("accepted invalid #%zu\n", i);
            assert(false);
        }
        assert(config.tick_ms == 7U);
    }
    assert(output_config_load("missing.json", &config) == ESP_ERR_NOT_FOUND);
    assert(!config.from_file && config.tick_ms == 2U);
}

int main(void)
{
    test_decoder();
    test_input_state();
    test_mapping();
    test_output_config();
    puts("task3 host test passed");
    return 0;
}
