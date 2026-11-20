#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DE_DE_MAX_STROKES 2U

#define DE_DE_MOD_LEFT_SHIFT 0x02U
#define DE_DE_MOD_RIGHT_ALT 0x40U

typedef struct {
    uint8_t modifiers;
    uint8_t usage;
} de_de_key_stroke_t;

typedef struct {
    de_de_key_stroke_t stroke[DE_DE_MAX_STROKES];
    uint8_t count;
} de_de_key_sequence_t;

bool de_de_encode_codepoint(
    uint32_t codepoint,
    de_de_key_sequence_t *sequence);

bool de_de_utf8_next(
    const char **cursor,
    uint32_t *codepoint);
