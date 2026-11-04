#include "hid_parser.h"

bool hid_parser_init(void)
{
	/* TODO: Initialize descriptor and report metadata storage. */
	return true;
}

bool hid_parser_parse_report(const uint8_t *descriptor, size_t length)
{
	/* TODO: Parse HID short/long items, collections, report IDs and bit offsets. */
	(void)descriptor;
	(void)length;
	return false;
}
