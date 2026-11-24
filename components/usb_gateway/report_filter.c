#include "report_filter.h"

#include <string.h>

/* Stack budget: runs in the USB HID host task (4096 B stack). */
#define REPORT_FILTER_MAX_ARRAY_SLOTS   64U
#define REPORT_FILTER_MAX_REPORT_BYTES  64U

static input_usage_t s_rules[REPORT_FILTER_MAX_USAGES];
static size_t s_rule_count;
static volatile bool s_active;
static volatile report_filter_stats_t s_stats;

/* ---------------------------------------------------------------------- */
/* Rules                                                                  */
/* ---------------------------------------------------------------------- */

static bool is_mapped(uint16_t usage_page, uint16_t usage)
{
    if (usage == 0U) {
        return false;
    }
    for (size_t index = 0U; index < s_rule_count; ++index) {
        if (s_rules[index].usage_page == usage_page && s_rules[index].usage == usage) {
            return true;
        }
    }
    return false;
}

void report_filter_reset(void)
{
    s_active = false;
    s_rule_count = 0U;
    memset(s_rules, 0, sizeof(s_rules));
}

bool report_filter_add_usage(input_usage_t usage)
{
    if (usage.usage_page == 0U || usage.usage == 0U) {
        return true;
    }
    if (is_mapped(usage.usage_page, usage.usage)) {
        return true;
    }
    if (s_rule_count >= REPORT_FILTER_MAX_USAGES) {
        return false;
    }
    s_rules[s_rule_count++] = usage;
    return true;
}

size_t report_filter_rule_count(void)
{
    return s_rule_count;
}

void report_filter_set_active(bool active)
{
    s_active = active;
}

bool report_filter_is_active(void)
{
    return s_active;
}

void report_filter_get_stats(report_filter_stats_t *stats)
{
    if (stats != NULL) {
        stats->inspected = s_stats.inspected;
        stats->modified = s_stats.modified;
        stats->fail_open = s_stats.fail_open;
    }
}

/* ---------------------------------------------------------------------- */
/* Bit access (HID: little endian, LSB first)                             */
/* ---------------------------------------------------------------------- */

static uint32_t read_bits(const uint8_t *data, uint32_t offset, uint8_t size)
{
    uint32_t value = 0U;
    for (uint8_t bit = 0U; bit < size; ++bit) {
        const uint32_t absolute = offset + bit;
        if (((data[absolute / 8U] >> (absolute % 8U)) & 0x01U) != 0U) {
            value |= 1UL << bit;
        }
    }
    return value;
}

static void write_bits(uint8_t *data, uint32_t offset, uint8_t size, uint32_t value)
{
    for (uint8_t bit = 0U; bit < size; ++bit) {
        const uint32_t absolute = offset + bit;
        const uint8_t mask = (uint8_t)(1U << (absolute % 8U));
        if (((value >> bit) & 0x01U) != 0U) {
            data[absolute / 8U] |= mask;
        } else {
            data[absolute / 8U] &= (uint8_t)~mask;
        }
    }
}

static int32_t to_signed(uint32_t raw, uint8_t size, bool is_signed)
{
    if (!is_signed || size >= 32U) {
        return (int32_t)raw;
    }
    const uint32_t sign = 1UL << (size - 1U);
    return (raw & sign) != 0U ? (int32_t)(raw | ~((sign << 1U) - 1U)) : (int32_t)raw;
}

static uint32_t field_mask(uint8_t size)
{
    return size >= 32U ? UINT32_MAX : ((1UL << size) - 1U);
}

/* Same usage selection as hid_decoder_decode(). */
static uint16_t usage_at(const hid_decoder_field_t *field, uint32_t index)
{
    if (field->usage_count > 0U) {
        return field->usages[index < field->usage_count ? index : field->usage_count - 1U];
    }
    const uint32_t usage = (uint32_t)field->usage_minimum + index;
    return (uint16_t)(usage > field->usage_maximum ? field->usage_maximum : usage);
}

/* ---------------------------------------------------------------------- */
/* Field handling                                                         */
/* ---------------------------------------------------------------------- */

static bool field_has_mapped_usage(const hid_decoder_field_t *field)
{
    if (field->usage_count > 0U) {
        for (uint8_t slot = 0U; slot < field->usage_count; ++slot) {
            if (is_mapped(field->usage_page, field->usages[slot])) {
                return true;
            }
        }
        return false;
    }
    for (size_t index = 0U; index < s_rule_count; ++index) {
        if (s_rules[index].usage_page == field->usage_page &&
            s_rules[index].usage >= field->usage_minimum &&
            s_rules[index].usage <= field->usage_maximum) {
            return true;
        }
    }
    return false;
}

/* Neutral value of a variable field element. */
static uint32_t variable_neutral(const hid_decoder_field_t *field)
{
    const bool relative = (field->flags & HID_FIELD_FLAG_RELATIVE) != 0U;
    if (relative ||
        (field->logical_minimum <= 0 && field->logical_maximum >= 0)) {
        return 0U;
    }
    return (uint32_t)field->logical_minimum & field_mask(field->bit_size);
}

static bool neutralize_variable(const hid_decoder_field_t *field, uint8_t *data)
{
    bool changed = false;
    const uint32_t neutral = variable_neutral(field);
    for (uint32_t element = 0U; element < field->report_count; ++element) {
        if (!is_mapped(field->usage_page, usage_at(field, element))) {
            continue;
        }
        const uint32_t offset = field->bit_offset + element * field->bit_size;
        if (read_bits(data, offset, field->bit_size) != neutral) {
            write_bits(data, offset, field->bit_size, neutral);
            changed = true;
        }
    }
    return changed;
}

/*
 * Empty slot value of an array field: a value that selects no usage.
 * false if the field cannot represent an empty slot.
 */
static bool array_empty_value(const hid_decoder_field_t *field, uint32_t *empty)
{
    const uint32_t mask = field_mask(field->bit_size);
    const bool is_signed = field->logical_minimum < 0;
    /* 0 outside the logical range: ignored by every host. */
    if (field->logical_minimum > 0 || field->logical_maximum < 0) {
        *empty = 0U;
        return true;
    }
    /* 0 inside the range but selecting usage 0 (keyboard: "no event"). */
    if (usage_at(field, (uint32_t)(0 - field->logical_minimum)) == 0U) {
        *empty = 0U;
        return true;
    }
    /* Any other out-of-range value that fits into the field. */
    const int64_t above = (int64_t)field->logical_maximum + 1;
    const int64_t max_raw = is_signed ? (int64_t)(mask >> 1U) : (int64_t)mask;
    if (above <= max_raw) {
        *empty = (uint32_t)above & mask;
        return true;
    }
    return false;
}

/* Returns true if changed; *fail set if the field cannot be modified. */
static bool neutralize_array(const hid_decoder_field_t *field, uint8_t *data, bool *fail)
{
    const bool is_signed = field->logical_minimum < 0;
    uint32_t kept[REPORT_FILTER_MAX_ARRAY_SLOTS];
    uint32_t kept_count = 0U;
    bool removed = false;

    if (field->report_count > sizeof(kept) / sizeof(kept[0])) {
        *fail = true;
        return false;
    }
    for (uint32_t slot = 0U; slot < field->report_count; ++slot) {
        const uint32_t offset = field->bit_offset + slot * field->bit_size;
        const uint32_t raw = read_bits(data, offset, field->bit_size);
        const int32_t value = to_signed(raw, field->bit_size, is_signed);
        if (value >= field->logical_minimum && value <= field->logical_maximum &&
            is_mapped(field->usage_page,
                usage_at(field, (uint32_t)(value - field->logical_minimum)))) {
            removed = true;
            continue;
        }
        kept[kept_count++] = raw;
    }
    if (!removed) {
        return false;
    }

    uint32_t empty = 0U;
    if (!array_empty_value(field, &empty)) {
        *fail = true;
        return false;
    }
    for (uint32_t slot = 0U; slot < field->report_count; ++slot) {
        const uint32_t offset = field->bit_offset + slot * field->bit_size;
        write_bits(data, offset, field->bit_size, slot < kept_count ? kept[slot] : empty);
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Report                                                                 */
/* ---------------------------------------------------------------------- */

report_filter_result_t report_filter_apply(
    const hid_decoder_layout_t *layout,
    bool parsed,
    uint8_t *report,
    size_t length)
{
    if (!s_active || s_rule_count == 0U || report == NULL || length == 0U) {
        return REPORT_FILTER_UNCHANGED;
    }
    ++s_stats.inspected;

    if (layout == NULL || !parsed || layout->truncated) {
        ++s_stats.fail_open;
        return REPORT_FILTER_FAIL_OPEN;
    }

    uint8_t report_id = 0U;
    uint8_t *data = report;
    size_t data_length = length;
    if (layout->uses_report_ids) {
        report_id = report[0];
        ++data;
        --data_length;
    }

    /* Pass 1: does the report carry fields with mapped usages, and is it long enough? */
    bool any_field = false;
    bool any_mapped = false;
    uint32_t required_bits = 0U;
    for (uint8_t index = 0U; index < layout->field_count; ++index) {
        const hid_decoder_field_t *field = &layout->fields[index];
        if (field->report_id != report_id) {
            continue;
        }
        any_field = true;
        const uint32_t end = (uint32_t)field->bit_offset +
            (uint32_t)field->report_count * field->bit_size;
        if (end > required_bits) {
            required_bits = end;
        }
        if ((field->flags & HID_FIELD_FLAG_CONSTANT) == 0U &&
            field->bit_size > 0U && field->bit_size <= 32U &&
            field_has_mapped_usage(field)) {
            any_mapped = true;
        }
    }
    if (!any_field || !any_mapped) {
        return REPORT_FILTER_UNCHANGED;
    }
    if ((size_t)(required_bits + 7U) / 8U > data_length) {
        ++s_stats.fail_open;
        return REPORT_FILTER_FAIL_OPEN;
    }

    /* Pass 2: work on a copy; commit only if every field could be handled. */
    uint8_t work[REPORT_FILTER_MAX_REPORT_BYTES];
    if (data_length > sizeof(work)) {
        ++s_stats.fail_open;
        return REPORT_FILTER_FAIL_OPEN;
    }
    memcpy(work, data, data_length);

    bool changed = false;
    bool fail = false;
    for (uint8_t index = 0U; index < layout->field_count && !fail; ++index) {
        const hid_decoder_field_t *field = &layout->fields[index];
        if (field->report_id != report_id ||
            (field->flags & HID_FIELD_FLAG_CONSTANT) != 0U ||
            field->bit_size == 0U || field->bit_size > 32U ||
            !field_has_mapped_usage(field)) {
            continue;
        }
        if ((field->flags & HID_FIELD_FLAG_VARIABLE) != 0U) {
            changed = neutralize_variable(field, work) || changed;
        } else {
            changed = neutralize_array(field, work, &fail) || changed;
        }
    }
    if (fail) {
        ++s_stats.fail_open;
        return REPORT_FILTER_FAIL_OPEN;
    }
    if (!changed) {
        return REPORT_FILTER_UNCHANGED;
    }
    memcpy(data, work, data_length);
    ++s_stats.modified;
    return REPORT_FILTER_MODIFIED;
}
