#pragma once

#include "esp_err.h"
#include "motion_profile_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Default profile, created at boot when /sdcard/config/profile-01.json does
 * not exist. It allows PASSTHROUGH, LEARN and GENERATE without a manually
 * written profile.
 *
 * All values marked "placeholder" in motion_profile_defaults.c must be
 * verified against the hardware (voltages, safe codes, axis ranges).
 * Learning fills radial envelopes and the motion model, not min/center/max.
 */
void motion_profile_create_default(motion_profile_t *profile);

/* Creates, saves and reloads the default profile (round-trip check). */
esp_err_t motion_profile_create_default_file(const char *path, motion_profile_t *profile);

#ifdef __cplusplus
}
#endif
