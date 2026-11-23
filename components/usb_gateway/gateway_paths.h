#pragma once

/*
 * Central SD card layout. Every path used by the firmware is defined here.
 *
 *     /sdcard/config/      all JSON configuration files
 *     /sdcard/motion/      binary profile caches
 *     /sdcard/captures/    USB capture sessions
 */

#define GATEWAY_SD_ROOT                 "/sdcard"

#define GATEWAY_CONFIG_DIR              GATEWAY_SD_ROOT "/config"
#define GATEWAY_CONFIG_FILE             GATEWAY_CONFIG_DIR "/gateway.json"
#define GATEWAY_PROFILE_JSON_FILE       GATEWAY_CONFIG_DIR "/profile-01.json"

#define GATEWAY_MOTION_DIR              GATEWAY_SD_ROOT "/motion"
#define GATEWAY_PROFILE_CACHE_FILE      GATEWAY_MOTION_DIR "/profile-01.bin"

/* Location before 2026-11-23, migrated once at boot. */
#define GATEWAY_LEGACY_PROFILE_JSON     GATEWAY_MOTION_DIR "/profile-01.json"
