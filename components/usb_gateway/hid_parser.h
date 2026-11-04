#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint8_t *data;
	size_t length;
} hid_report_t;

bool hid_parser_init(void);
bool hid_parser_parse_report(const uint8_t *descriptor, size_t length);
