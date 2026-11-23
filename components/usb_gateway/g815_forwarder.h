#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void g815_forwarder_init(void);

/*
 * Forwards one raw G815 input report to the S3.
 * Interface 0: 8-byte boot keyboard report.
 * Interface 1: numbered report including the Report ID byte.
 */
bool g815_forwarder_submit(
    uint8_t interface_number,
    const uint8_t *report,
    size_t length);

#ifdef __cplusplus
}
#endif
