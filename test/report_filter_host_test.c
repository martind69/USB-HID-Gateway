/*
 * Host test for report_filter (M7).
 * Build: gcc -std=c17 -Wall -Wextra -Werror -I../components/usb_gateway \
 *        report_filter_host_test.c ../components/usb_gateway/report_filter.c -o rf && ./rf
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "report_filter.h"

static hid_decoder_field_t variable_field(uint8_t id, uint16_t offset, uint8_t size,
    uint16_t count, uint16_t page, uint16_t umin, uint16_t umax,
    int32_t lmin, int32_t lmax, bool relative)
{
    return (hid_decoder_field_t) {
        .report_id = id,
        .flags = (uint8_t)(HID_FIELD_FLAG_VARIABLE | (relative ? HID_FIELD_FLAG_RELATIVE : 0U)),
        .bit_size = size,
        .bit_offset = offset,
        .report_count = count,
        .usage_page = page,
        .usage_minimum = umin,
        .usage_maximum = umax,
        .logical_minimum = lmin,
        .logical_maximum = lmax,
    };
}

/* Boot keyboard: modifiers, reserved byte, 6 key slots. */
static void keyboard_layout(hid_decoder_layout_t *layout)
{
    memset(layout, 0, sizeof(*layout));
    layout->fields[0] = variable_field(0, 0, 1, 8, 0x07, 0xE0, 0xE7, 0, 1, false);
    layout->fields[1] = (hid_decoder_field_t) {
        .flags = HID_FIELD_FLAG_CONSTANT, .bit_size = 8, .bit_offset = 8, .report_count = 1,
    };
    layout->fields[2] = (hid_decoder_field_t) {
        .bit_size = 8, .bit_offset = 16, .report_count = 6, .usage_page = 0x07,
        .usage_minimum = 0, .usage_maximum = 0xFF, .logical_minimum = 0, .logical_maximum = 0xFF,
    };
    layout->field_count = 3;
}

/* Mouse with Report ID 2: 16 buttons, X/Y 16 bit, wheel 8 bit. */
static void mouse_layout(hid_decoder_layout_t *layout)
{
    memset(layout, 0, sizeof(*layout));
    layout->uses_report_ids = true;
    layout->fields[0] = variable_field(2, 0, 1, 16, 0x09, 1, 16, 0, 1, false);
    layout->fields[1] = variable_field(2, 16, 16, 2, 0x01, 0x30, 0x31, -32767, 32767, true);
    layout->fields[1].usage_count = 2;
    layout->fields[1].usages[0] = 0x30;
    layout->fields[1].usages[1] = 0x31;
    layout->fields[2] = variable_field(2, 48, 8, 1, 0x01, 0x38, 0x38, -127, 127, true);
    layout->field_count = 3;
}

static void rules(void)
{
    report_filter_reset();
    /* WASD, space, left button, mouse X/Y */
    const input_usage_t usages[] = {
        { 0x07, 0x1A }, { 0x07, 0x04 }, { 0x07, 0x16 }, { 0x07, 0x07 },
        { 0x07, 0x2C }, { 0x09, 0x01 }, { 0x01, 0x30 }, { 0x01, 0x31 },
    };
    for (size_t index = 0U; index < sizeof(usages) / sizeof(usages[0]); ++index) {
        assert(report_filter_add_usage(usages[index]));
    }
    assert(report_filter_add_usage(usages[0]));     /* duplicate ignored */
    assert(report_filter_rule_count() == 8U);
}

int main(void)
{
    hid_decoder_layout_t keyboard;
    hid_decoder_layout_t mouse;
    keyboard_layout(&keyboard);
    mouse_layout(&mouse);
    rules();

    /* Inactive: never touched. */
    uint8_t k1[8] = { 0x02, 0x00, 0x1A, 0x15, 0x04, 0, 0, 0 };
    assert(report_filter_apply(&keyboard, true, k1, sizeof(k1)) == REPORT_FILTER_UNCHANGED);
    assert(k1[2] == 0x1A);

    report_filter_set_active(true);

    /* W (mapped), R (not), A (mapped), Shift kept: R moves to slot 0. */
    assert(report_filter_apply(&keyboard, true, k1, sizeof(k1)) == REPORT_FILTER_MODIFIED);
    const uint8_t k1_expected[8] = { 0x02, 0x00, 0x15, 0, 0, 0, 0, 0 };
    assert(memcmp(k1, k1_expected, sizeof(k1)) == 0);

    /* Only unmapped keys: byte-identical. */
    uint8_t k2[8] = { 0x01, 0x00, 0x06, 0x19, 0, 0, 0, 0 };
    uint8_t k2_copy[8];
    memcpy(k2_copy, k2, sizeof(k2));
    assert(report_filter_apply(&keyboard, true, k2, sizeof(k2)) == REPORT_FILTER_UNCHANGED);
    assert(memcmp(k2, k2_copy, sizeof(k2)) == 0);

    /* Short report: fail-open, unchanged. */
    uint8_t k3[4] = { 0x00, 0x00, 0x1A, 0x00 };
    assert(report_filter_apply(&keyboard, true, k3, sizeof(k3)) == REPORT_FILTER_FAIL_OPEN);
    assert(k3[2] == 0x1A);

    /* Not parsed / truncated: fail-open. */
    uint8_t k4[8] = { 0x00, 0x00, 0x1A, 0, 0, 0, 0, 0 };
    assert(report_filter_apply(&keyboard, false, k4, sizeof(k4)) == REPORT_FILTER_FAIL_OPEN);
    keyboard.truncated = true;
    assert(report_filter_apply(&keyboard, true, k4, sizeof(k4)) == REPORT_FILTER_FAIL_OPEN);
    keyboard.truncated = false;
    assert(k4[2] == 0x1A);

    /* Mouse: button 1 + X/Y removed, button 2 and wheel kept. */
    uint8_t m1[8] = { 0x02, 0x03, 0x00, 0x05, 0x00, 0xFB, 0xFF, 0x01 };
    assert(report_filter_apply(&mouse, true, m1, sizeof(m1)) == REPORT_FILTER_MODIFIED);
    const uint8_t m1_expected[8] = { 0x02, 0x02, 0x00, 0, 0, 0, 0, 0x01 };
    assert(memcmp(m1, m1_expected, sizeof(m1)) == 0);

    /* Unknown Report ID (vendor): unchanged, not counted as fail-open. */
    report_filter_stats_t before;
    report_filter_get_stats(&before);
    uint8_t v1[8] = { 0x11, 0xFF, 0x0A, 0x1A, 0, 0, 0, 0 };
    assert(report_filter_apply(&mouse, true, v1, sizeof(v1)) == REPORT_FILTER_UNCHANGED);
    assert(v1[3] == 0x1A);
    report_filter_stats_t after;
    report_filter_get_stats(&after);
    assert(after.fail_open == before.fail_open);

    /* Array field 1..3 with usages 1..3, logical 1..3: empty = 0 (out of range). */
    hid_decoder_layout_t consumer;
    memset(&consumer, 0, sizeof(consumer));
    consumer.fields[0] = (hid_decoder_field_t) {
        .bit_size = 8, .report_count = 2, .usage_page = 0x09,
        .usage_minimum = 1, .usage_maximum = 3, .logical_minimum = 1, .logical_maximum = 3,
    };
    consumer.field_count = 1;
    uint8_t c1[2] = { 0x01, 0x03 };
    assert(report_filter_apply(&consumer, true, c1, sizeof(c1)) == REPORT_FILTER_MODIFIED);
    assert(c1[0] == 0x03 && c1[1] == 0x00);

    /* Array field without empty value (full 8-bit range, usage 0 not "none"): fail-open. */
    consumer.fields[0].logical_minimum = 0;
    consumer.fields[0].logical_maximum = 255;
    consumer.fields[0].usage_minimum = 1;
    consumer.fields[0].usage_maximum = 256;
    uint8_t c2[2] = { 0x00, 0x05 };
    assert(report_filter_apply(&consumer, true, c2, sizeof(c2)) == REPORT_FILTER_FAIL_OPEN);
    assert(c2[0] == 0x00 && c2[1] == 0x05);

    report_filter_set_active(false);
    uint8_t m2[8] = { 0x02, 0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00 };
    assert(report_filter_apply(&mouse, true, m2, sizeof(m2)) == REPORT_FILTER_UNCHANGED);
    assert(m2[1] == 0x01 && m2[3] == 0x05);

    report_filter_get_stats(&after);
    printf("report_filter host test passed: inspected=%lu modified=%lu fail_open=%lu\n",
        (unsigned long)after.inspected, (unsigned long)after.modified,
        (unsigned long)after.fail_open);
    return 0;
}
