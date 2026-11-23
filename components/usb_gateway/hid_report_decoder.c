#include "hid_report_decoder.h"

#include <string.h>

#define ITEM_TYPE_MAIN      0U
#define ITEM_TYPE_GLOBAL    1U
#define ITEM_TYPE_LOCAL     2U

#define MAIN_INPUT          0x8U
#define MAIN_OUTPUT         0x9U
#define MAIN_FEATURE        0xBU
#define MAIN_COLLECTION     0xAU
#define MAIN_END_COLLECTION 0xCU

#define GLOBAL_USAGE_PAGE   0x0U
#define GLOBAL_LOGICAL_MIN  0x1U
#define GLOBAL_LOGICAL_MAX  0x2U
#define GLOBAL_REPORT_SIZE  0x7U
#define GLOBAL_REPORT_ID    0x8U
#define GLOBAL_REPORT_COUNT 0x9U
#define GLOBAL_PUSH         0xAU
#define GLOBAL_POP          0xBU

#define LOCAL_USAGE         0x0U
#define LOCAL_USAGE_MIN     0x1U
#define LOCAL_USAGE_MAX     0x2U

#define GLOBAL_STACK_DEPTH  4U
#define REPORT_ID_SLOTS     256U

typedef struct {
    uint16_t usage_page;
    int32_t logical_minimum;
    int32_t logical_maximum;
    uint8_t report_size;
    uint8_t report_id;
    uint16_t report_count;
} global_state_t;

typedef struct {
    uint8_t usage_count;
    uint16_t usages[HID_DECODER_MAX_USAGES];
    uint16_t usage_pages[HID_DECODER_MAX_USAGES];
    bool has_minimum;
    bool has_maximum;
    uint16_t usage_minimum;
    uint16_t usage_maximum;
    uint16_t minimum_page;
} local_state_t;

static uint32_t read_unsigned(const uint8_t *data, uint8_t size)
{
    uint32_t value = 0U;
    for (uint8_t index = 0U; index < size; ++index) {
        value |= (uint32_t)data[index] << (8U * index);
    }
    return value;
}

static int32_t read_signed(const uint8_t *data, uint8_t size)
{
    const uint32_t value = read_unsigned(data, size);
    switch (size) {
        case 1U:
            return (int32_t)(int8_t)value;
        case 2U:
            return (int32_t)(int16_t)value;
        case 4U:
            return (int32_t)value;
        default:
            return 0;
    }
}

static void add_input_field(
    hid_decoder_layout_t *layout,
    const global_state_t *global,
    const local_state_t *local,
    uint8_t main_flags,
    uint16_t *bit_offsets)
{
    const uint32_t field_bits = (uint32_t)global->report_size * global->report_count;
    const uint16_t offset = bit_offsets[global->report_id];
    bit_offsets[global->report_id] = (uint16_t)(offset + field_bits);

    if ((main_flags & HID_FIELD_FLAG_CONSTANT) != 0U ||
        global->report_size == 0U || global->report_size > 32U ||
        global->report_count == 0U) {
        return;
    }
    if (layout->field_count >= HID_DECODER_MAX_FIELDS) {
        layout->truncated = true;
        return;
    }
    hid_decoder_field_t *field = &layout->fields[layout->field_count++];
    memset(field, 0, sizeof(*field));
    field->report_id = global->report_id;
    field->flags = (uint8_t)(main_flags & (HID_FIELD_FLAG_VARIABLE | HID_FIELD_FLAG_RELATIVE));
    field->bit_size = global->report_size;
    field->bit_offset = offset;
    field->report_count = global->report_count;
    field->logical_minimum = global->logical_minimum;
    field->logical_maximum = global->logical_maximum;
    field->usage_page = global->usage_page;

    if (local->usage_count > 0U) {
        field->usage_count = local->usage_count;
        memcpy(field->usages, local->usages, sizeof(field->usages));
        /* Extended (32-bit) usages may carry their own page; first one wins. */
        if (local->usage_pages[0] != 0U) {
            field->usage_page = local->usage_pages[0];
        }
    } else if (local->has_minimum) {
        field->usage_minimum = local->usage_minimum;
        field->usage_maximum = local->has_maximum ? local->usage_maximum : local->usage_minimum;
        if (local->minimum_page != 0U) {
            field->usage_page = local->minimum_page;
        }
    }
}

hid_decoder_result_t hid_decoder_parse(
    const uint8_t *descriptor,
    size_t length,
    hid_decoder_layout_t *layout)
{
    if (descriptor == NULL || layout == NULL) {
        return HID_DECODER_ERR_ARGUMENT;
    }
    memset(layout, 0, sizeof(*layout));

    global_state_t global = { 0 };
    global_state_t stack[GLOBAL_STACK_DEPTH];
    uint8_t stack_depth = 0U;
    local_state_t local = { 0 };
    uint16_t bit_offsets[REPORT_ID_SLOTS];
    memset(bit_offsets, 0, sizeof(bit_offsets));

    size_t position = 0U;
    while (position < length) {
        const uint8_t prefix = descriptor[position];
        if (prefix == 0xFEU) {
            /* Long item: skip. */
            if (position + 2U >= length) {
                return HID_DECODER_ERR_DESCRIPTOR;
            }
            position += 3U + descriptor[position + 1U];
            continue;
        }
        static const uint8_t SIZES[4] = { 0U, 1U, 2U, 4U };
        const uint8_t size = SIZES[prefix & 0x03U];
        const uint8_t type = (uint8_t)((prefix >> 2U) & 0x03U);
        const uint8_t tag = (uint8_t)(prefix >> 4U);
        if (position + 1U + size > length) {
            return HID_DECODER_ERR_DESCRIPTOR;
        }
        const uint8_t *data = &descriptor[position + 1U];
        const uint32_t value = read_unsigned(data, size);
        position += 1U + size;

        if (type == ITEM_TYPE_MAIN) {
            if (tag == MAIN_INPUT) {
                add_input_field(layout, &global, &local, (uint8_t)value, bit_offsets);
            } else if (tag == MAIN_OUTPUT || tag == MAIN_FEATURE) {
                /* Output and feature reports are not decoded. */
            } else if (tag != MAIN_COLLECTION && tag != MAIN_END_COLLECTION) {
                return HID_DECODER_ERR_DESCRIPTOR;
            }
            memset(&local, 0, sizeof(local));
        } else if (type == ITEM_TYPE_GLOBAL) {
            switch (tag) {
                case GLOBAL_USAGE_PAGE:
                    global.usage_page = (uint16_t)value;
                    break;
                case GLOBAL_LOGICAL_MIN:
                    global.logical_minimum = read_signed(data, size);
                    break;
                case GLOBAL_LOGICAL_MAX:
                    global.logical_maximum = read_signed(data, size);
                    break;
                case GLOBAL_REPORT_SIZE:
                    global.report_size = (uint8_t)value;
                    break;
                case GLOBAL_REPORT_ID:
                    if (value == 0U || value > 0xFFU) {
                        return HID_DECODER_ERR_DESCRIPTOR;
                    }
                    global.report_id = (uint8_t)value;
                    layout->uses_report_ids = true;
                    break;
                case GLOBAL_REPORT_COUNT:
                    global.report_count = (uint16_t)value;
                    break;
                case GLOBAL_PUSH:
                    if (stack_depth >= GLOBAL_STACK_DEPTH) {
                        return HID_DECODER_ERR_DESCRIPTOR;
                    }
                    stack[stack_depth++] = global;
                    break;
                case GLOBAL_POP:
                    if (stack_depth == 0U) {
                        return HID_DECODER_ERR_DESCRIPTOR;
                    }
                    global = stack[--stack_depth];
                    break;
                default:
                    break;
            }
        } else if (type == ITEM_TYPE_LOCAL) {
            const uint16_t page = size == 4U ? (uint16_t)(value >> 16U) : 0U;
            switch (tag) {
                case LOCAL_USAGE:
                    if (local.usage_count < HID_DECODER_MAX_USAGES) {
                        local.usages[local.usage_count] = (uint16_t)value;
                        local.usage_pages[local.usage_count] = page;
                        ++local.usage_count;
                    }
                    break;
                case LOCAL_USAGE_MIN:
                    local.has_minimum = true;
                    local.usage_minimum = (uint16_t)value;
                    local.minimum_page = page;
                    break;
                case LOCAL_USAGE_MAX:
                    local.has_maximum = true;
                    local.usage_maximum = (uint16_t)value;
                    break;
                default:
                    break;
            }
        }
    }
    return HID_DECODER_OK;
}

static uint32_t extract_bits(const uint8_t *data, size_t length, uint32_t bit_offset, uint8_t bit_size)
{
    uint32_t value = 0U;
    for (uint8_t bit = 0U; bit < bit_size; ++bit) {
        const uint32_t absolute = bit_offset + bit;
        const size_t byte = absolute / 8U;
        if (byte >= length) {
            break;
        }
        if ((data[byte] >> (absolute % 8U)) & 0x01U) {
            value |= 1UL << bit;
        }
    }
    return value;
}

static int32_t to_signed(uint32_t raw, uint8_t bit_size, bool is_signed)
{
    if (!is_signed || bit_size >= 32U) {
        return (int32_t)raw;
    }
    const uint32_t sign = 1UL << (bit_size - 1U);
    return (raw & sign) != 0U ? (int32_t)(raw | ~((sign << 1U) - 1U)) : (int32_t)raw;
}

static uint16_t usage_at(const hid_decoder_field_t *field, uint32_t index)
{
    if (field->usage_count > 0U) {
        return field->usages[index < field->usage_count ? index : field->usage_count - 1U];
    }
    const uint32_t usage = (uint32_t)field->usage_minimum + index;
    return (uint16_t)(usage > field->usage_maximum ? field->usage_maximum : usage);
}

hid_decoder_result_t hid_decoder_decode(
    const hid_decoder_layout_t *layout,
    const uint8_t *report,
    size_t length,
    hid_decoder_usage_cb_t callback,
    void *context)
{
    if (layout == NULL || report == NULL || callback == NULL) {
        return HID_DECODER_ERR_ARGUMENT;
    }
    uint8_t report_id = 0U;
    if (layout->uses_report_ids) {
        if (length == 0U) {
            return HID_DECODER_ERR_ARGUMENT;
        }
        report_id = report[0];
        ++report;
        --length;
    }
    for (uint8_t index = 0U; index < layout->field_count; ++index) {
        const hid_decoder_field_t *field = &layout->fields[index];
        if (field->report_id != report_id) {
            continue;
        }
        const bool is_signed = field->logical_minimum < 0;
        const bool variable = (field->flags & HID_FIELD_FLAG_VARIABLE) != 0U;
        const bool relative = (field->flags & HID_FIELD_FLAG_RELATIVE) != 0U;
        for (uint32_t element = 0U; element < field->report_count; ++element) {
            const uint32_t offset = field->bit_offset + element * field->bit_size;
            if ((offset + field->bit_size + 7U) / 8U > length) {
                break;
            }
            const int32_t value = to_signed(
                extract_bits(report, length, offset, field->bit_size),
                field->bit_size,
                is_signed);
            if (variable) {
                callback(field->usage_page, usage_at(field, element), value, relative, context);
                continue;
            }
            /* Array: value selects a usage relative to the logical minimum. */
            if (value < field->logical_minimum || value > field->logical_maximum) {
                continue;
            }
            const uint32_t selector = (uint32_t)(value - field->logical_minimum);
            const uint16_t usage = usage_at(field, selector);
            if (usage != 0U) {
                callback(field->usage_page, usage, 1, false, context);
            }
        }
    }
    return HID_DECODER_OK;
}

bool hid_decoder_has_usage(
    const hid_decoder_layout_t *layout,
    uint16_t usage_page,
    uint16_t usage)
{
    if (layout == NULL) {
        return false;
    }
    for (uint8_t index = 0U; index < layout->field_count; ++index) {
        const hid_decoder_field_t *field = &layout->fields[index];
        if (field->usage_page != usage_page) {
            continue;
        }
        if (field->usage_count > 0U) {
            for (uint8_t slot = 0U; slot < field->usage_count; ++slot) {
                if (field->usages[slot] == usage) {
                    return true;
                }
            }
        } else if (usage >= field->usage_minimum && usage <= field->usage_maximum) {
            return true;
        }
    }
    return false;
}
