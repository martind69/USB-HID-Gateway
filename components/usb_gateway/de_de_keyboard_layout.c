#include "de_de_keyboard_layout.h"

#include <string.h>

#define HID_A 0x04U
#define HID_1 0x1EU
#define HID_ENTER 0x28U
#define HID_BACKSPACE 0x2AU
#define HID_TAB 0x2BU
#define HID_SPACE 0x2CU
#define HID_MINUS_US 0x2DU
#define HID_EQUAL_US 0x2EU
#define HID_LEFT_BRACKET_US 0x2FU
#define HID_RIGHT_BRACKET_US 0x30U
#define HID_BACKSLASH_US 0x31U
#define HID_SEMICOLON_US 0x33U
#define HID_APOSTROPHE_US 0x34U
#define HID_GRAVE_US 0x35U
#define HID_COMMA 0x36U
#define HID_DOT 0x37U
#define HID_SLASH_US 0x38U
#define HID_NON_US_BACKSLASH 0x64U

#define MOD_NONE 0x00U
#define MOD_SHIFT DE_DE_MOD_LEFT_SHIFT
#define MOD_ALT_GR DE_DE_MOD_RIGHT_ALT

#define KEY(modifier, key_usage) \
    ((de_de_key_stroke_t) { \
        .modifiers = (modifier), \
        .usage = (key_usage), \
    })

#define ENTRY(character, modifier, key_usage) \
    { (character), KEY((modifier), (key_usage)) }

typedef struct {
    uint32_t codepoint;
    de_de_key_stroke_t key;
} direct_mapping_t;

typedef struct {
    uint32_t codepoint;
    de_de_key_stroke_t dead_key;
    de_de_key_stroke_t base_key;
} composed_mapping_t;

/*
 * Windows de-DE keyboard layout, ISO 105-key physical positions.
 * HID Usage IDs describe positions, while the host layout produces symbols.
 */
static const direct_mapping_t s_direct[] = {
    ENTRY('\b', MOD_NONE, HID_BACKSPACE),
    ENTRY('\t', MOD_NONE, HID_TAB),
    ENTRY('\n', MOD_NONE, HID_ENTER),
    ENTRY('\r', MOD_NONE, HID_ENTER),
    ENTRY(' ', MOD_NONE, HID_SPACE),

    ENTRY('!', MOD_SHIFT, 0x1EU),
    ENTRY('"', MOD_SHIFT, 0x1FU),
    ENTRY('#', MOD_NONE, HID_BACKSLASH_US),
    ENTRY('$', MOD_SHIFT, 0x21U),
    ENTRY('%', MOD_SHIFT, 0x22U),
    ENTRY('&', MOD_SHIFT, 0x23U),
    ENTRY('\'', MOD_SHIFT, HID_BACKSLASH_US),
    ENTRY('(', MOD_SHIFT, 0x25U),
    ENTRY(')', MOD_SHIFT, 0x26U),
    ENTRY('*', MOD_SHIFT, HID_RIGHT_BRACKET_US),
    ENTRY('+', MOD_NONE, HID_RIGHT_BRACKET_US),
    ENTRY(',', MOD_NONE, HID_COMMA),
    ENTRY('-', MOD_NONE, HID_SLASH_US),
    ENTRY('.', MOD_NONE, HID_DOT),
    ENTRY('/', MOD_SHIFT, 0x24U),

    ENTRY('0', MOD_NONE, 0x27U),
    ENTRY('1', MOD_NONE, 0x1EU),
    ENTRY('2', MOD_NONE, 0x1FU),
    ENTRY('3', MOD_NONE, 0x20U),
    ENTRY('4', MOD_NONE, 0x21U),
    ENTRY('5', MOD_NONE, 0x22U),
    ENTRY('6', MOD_NONE, 0x23U),
    ENTRY('7', MOD_NONE, 0x24U),
    ENTRY('8', MOD_NONE, 0x25U),
    ENTRY('9', MOD_NONE, 0x26U),

    ENTRY(':', MOD_SHIFT, HID_DOT),
    ENTRY(';', MOD_SHIFT, HID_COMMA),
    ENTRY('<', MOD_NONE, HID_NON_US_BACKSLASH),
    ENTRY('=', MOD_SHIFT, 0x27U),
    ENTRY('>', MOD_SHIFT, HID_NON_US_BACKSLASH),
    ENTRY('?', MOD_SHIFT, HID_MINUS_US),
    ENTRY('@', MOD_ALT_GR, 0x14U),

    ENTRY('[', MOD_ALT_GR, 0x25U),
    ENTRY('\\', MOD_ALT_GR, HID_MINUS_US),
    ENTRY(']', MOD_ALT_GR, 0x26U),
    ENTRY('_', MOD_SHIFT, HID_SLASH_US),
    ENTRY('{', MOD_ALT_GR, 0x24U),
    ENTRY('|', MOD_ALT_GR, HID_NON_US_BACKSLASH),
    ENTRY('}', MOD_ALT_GR, 0x27U),
    ENTRY('~', MOD_ALT_GR, HID_RIGHT_BRACKET_US),

    ENTRY(0x00A7U, MOD_SHIFT, 0x20U), /* section sign */
    ENTRY(0x00B0U, MOD_SHIFT, HID_GRAVE_US),
    ENTRY(0x00B2U, MOD_ALT_GR, 0x1FU),
    ENTRY(0x00B3U, MOD_ALT_GR, 0x20U),
    ENTRY(0x00B5U, MOD_ALT_GR, 0x10U),
    ENTRY(0x00DFU, MOD_NONE, HID_MINUS_US),
    ENTRY(0x00E4U, MOD_NONE, HID_APOSTROPHE_US),
    ENTRY(0x00F6U, MOD_NONE, HID_SEMICOLON_US),
    ENTRY(0x00FCU, MOD_NONE, HID_LEFT_BRACKET_US),
    ENTRY(0x00C4U, MOD_SHIFT, HID_APOSTROPHE_US),
    ENTRY(0x00D6U, MOD_SHIFT, HID_SEMICOLON_US),
    ENTRY(0x00DCU, MOD_SHIFT, HID_LEFT_BRACKET_US),
    ENTRY(0x20ACU, MOD_ALT_GR, 0x08U),
};

static const composed_mapping_t s_composed[] = {
    /* Acute accent. */
    { 0x00E1U, KEY(MOD_NONE, HID_EQUAL_US), KEY(MOD_NONE, 0x04U) },
    { 0x00E9U, KEY(MOD_NONE, HID_EQUAL_US), KEY(MOD_NONE, 0x08U) },
    { 0x00EDU, KEY(MOD_NONE, HID_EQUAL_US), KEY(MOD_NONE, 0x0CU) },
    { 0x00F3U, KEY(MOD_NONE, HID_EQUAL_US), KEY(MOD_NONE, 0x12U) },
    { 0x00FAU, KEY(MOD_NONE, HID_EQUAL_US), KEY(MOD_NONE, 0x18U) },
    { 0x00FDU, KEY(MOD_NONE, HID_EQUAL_US), KEY(MOD_NONE, 0x1DU) },
    { 0x00C1U, KEY(MOD_NONE, HID_EQUAL_US), KEY(MOD_SHIFT, 0x04U) },
    { 0x00C9U, KEY(MOD_NONE, HID_EQUAL_US), KEY(MOD_SHIFT, 0x08U) },
    { 0x00CDU, KEY(MOD_NONE, HID_EQUAL_US), KEY(MOD_SHIFT, 0x0CU) },
    { 0x00D3U, KEY(MOD_NONE, HID_EQUAL_US), KEY(MOD_SHIFT, 0x12U) },
    { 0x00DAU, KEY(MOD_NONE, HID_EQUAL_US), KEY(MOD_SHIFT, 0x18U) },
    { 0x00DDU, KEY(MOD_NONE, HID_EQUAL_US), KEY(MOD_SHIFT, 0x1DU) },

    /* Grave accent. */
    { 0x00E0U, KEY(MOD_SHIFT, HID_EQUAL_US), KEY(MOD_NONE, 0x04U) },
    { 0x00E8U, KEY(MOD_SHIFT, HID_EQUAL_US), KEY(MOD_NONE, 0x08U) },
    { 0x00ECU, KEY(MOD_SHIFT, HID_EQUAL_US), KEY(MOD_NONE, 0x0CU) },
    { 0x00F2U, KEY(MOD_SHIFT, HID_EQUAL_US), KEY(MOD_NONE, 0x12U) },
    { 0x00F9U, KEY(MOD_SHIFT, HID_EQUAL_US), KEY(MOD_NONE, 0x18U) },
    { 0x00C0U, KEY(MOD_SHIFT, HID_EQUAL_US), KEY(MOD_SHIFT, 0x04U) },
    { 0x00C8U, KEY(MOD_SHIFT, HID_EQUAL_US), KEY(MOD_SHIFT, 0x08U) },
    { 0x00CCU, KEY(MOD_SHIFT, HID_EQUAL_US), KEY(MOD_SHIFT, 0x0CU) },
    { 0x00D2U, KEY(MOD_SHIFT, HID_EQUAL_US), KEY(MOD_SHIFT, 0x12U) },
    { 0x00D9U, KEY(MOD_SHIFT, HID_EQUAL_US), KEY(MOD_SHIFT, 0x18U) },

    /* Circumflex accent. */
    { 0x00E2U, KEY(MOD_NONE, HID_GRAVE_US), KEY(MOD_NONE, 0x04U) },
    { 0x00EAU, KEY(MOD_NONE, HID_GRAVE_US), KEY(MOD_NONE, 0x08U) },
    { 0x00EEU, KEY(MOD_NONE, HID_GRAVE_US), KEY(MOD_NONE, 0x0CU) },
    { 0x00F4U, KEY(MOD_NONE, HID_GRAVE_US), KEY(MOD_NONE, 0x12U) },
    { 0x00FBU, KEY(MOD_NONE, HID_GRAVE_US), KEY(MOD_NONE, 0x18U) },
    { 0x00C2U, KEY(MOD_NONE, HID_GRAVE_US), KEY(MOD_SHIFT, 0x04U) },
    { 0x00CAU, KEY(MOD_NONE, HID_GRAVE_US), KEY(MOD_SHIFT, 0x08U) },
    { 0x00CEU, KEY(MOD_NONE, HID_GRAVE_US), KEY(MOD_SHIFT, 0x0CU) },
    { 0x00D4U, KEY(MOD_NONE, HID_GRAVE_US), KEY(MOD_SHIFT, 0x12U) },
    { 0x00DBU, KEY(MOD_NONE, HID_GRAVE_US), KEY(MOD_SHIFT, 0x18U) },
};

static bool encode_letter(
    uint32_t codepoint,
    de_de_key_sequence_t *sequence)
{
    bool uppercase = codepoint >= 'A' && codepoint <= 'Z';
    uint32_t lower = uppercase ? codepoint + ('a' - 'A') : codepoint;
    if (lower < 'a' || lower > 'z') {
        return false;
    }

    uint8_t usage = (uint8_t)(HID_A + (lower - 'a'));
    if (lower == 'y') {
        usage = 0x1DU;
    } else if (lower == 'z') {
        usage = 0x1CU;
    }

    sequence->stroke[0] = KEY(uppercase ? MOD_SHIFT : MOD_NONE, usage);
    sequence->count = 1U;
    return true;
}

bool de_de_encode_codepoint(
    uint32_t codepoint,
    de_de_key_sequence_t *sequence)
{
    if (sequence == NULL) {
        return false;
    }

    memset(sequence, 0, sizeof(*sequence));
    if (encode_letter(codepoint, sequence)) {
        return true;
    }

    /* Dead keys must be followed by Space to emit the literal accent. */
    if (codepoint == '^') {
        sequence->stroke[0] = KEY(MOD_NONE, HID_GRAVE_US);
        sequence->stroke[1] = KEY(MOD_NONE, HID_SPACE);
        sequence->count = 2U;
        return true;
    }
    if (codepoint == '`') {
        sequence->stroke[0] = KEY(MOD_SHIFT, HID_EQUAL_US);
        sequence->stroke[1] = KEY(MOD_NONE, HID_SPACE);
        sequence->count = 2U;
        return true;
    }
    if (codepoint == 0x00B4U) {
        sequence->stroke[0] = KEY(MOD_NONE, HID_EQUAL_US);
        sequence->stroke[1] = KEY(MOD_NONE, HID_SPACE);
        sequence->count = 2U;
        return true;
    }

    for (size_t index = 0U;
         index < sizeof(s_direct) / sizeof(s_direct[0]);
         ++index) {
        if (s_direct[index].codepoint == codepoint) {
            sequence->stroke[0] = s_direct[index].key;
            sequence->count = 1U;
            return true;
        }
    }

    for (size_t index = 0U;
         index < sizeof(s_composed) / sizeof(s_composed[0]);
         ++index) {
        if (s_composed[index].codepoint == codepoint) {
            sequence->stroke[0] = s_composed[index].dead_key;
            sequence->stroke[1] = s_composed[index].base_key;
            sequence->count = 2U;
            return true;
        }
    }

    return false;
}

bool de_de_utf8_next(const char **cursor, uint32_t *codepoint)
{
    if (cursor == NULL || *cursor == NULL || codepoint == NULL || **cursor == '\0') {
        return false;
    }

    const uint8_t *text = (const uint8_t *)*cursor;
    if (text[0] < 0x80U) {
        *codepoint = text[0];
        *cursor += 1;
        return true;
    }

    if ((text[0] & 0xE0U) == 0xC0U &&
        (text[1] & 0xC0U) == 0x80U) {
        *codepoint = ((uint32_t)(text[0] & 0x1FU) << 6U) |
            (uint32_t)(text[1] & 0x3FU);
        *cursor += 2;
        return true;
    }

    if ((text[0] & 0xF0U) == 0xE0U &&
        (text[1] & 0xC0U) == 0x80U &&
        (text[2] & 0xC0U) == 0x80U) {
        *codepoint = ((uint32_t)(text[0] & 0x0FU) << 12U) |
            ((uint32_t)(text[1] & 0x3FU) << 6U) |
            (uint32_t)(text[2] & 0x3FU);
        *cursor += 3;
        return true;
    }

    return false;
}
