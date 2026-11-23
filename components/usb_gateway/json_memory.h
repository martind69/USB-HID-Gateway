#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Routes all cJSON allocations (nodes, keys, print buffers) to PSRAM.
 * A motion profile has about 1.2 x 10^4 nodes plus a print buffer of several
 * 10^5 bytes, which does not fit into internal RAM.
 * Falls back to the default heap if PSRAM is unavailable or exhausted.
 * Call once at boot, before any cJSON use.
 */
esp_err_t json_memory_init(void);

#ifdef __cplusplus
}
#endif
