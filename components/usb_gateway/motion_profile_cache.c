#include "motion_profile_cache.h"

#include "mbedtls/md.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CACHE_MAGIC 0x4D504331U
#define CACHE_VERSION 1U
#define SHA256_SIZE 32U
#define PROFILE_PATH_BUFFER_SIZE 320U

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t payload_size;
    uint8_t json_sha256[SHA256_SIZE];
} cache_header_t;

static esp_err_t hash_file(
    const char *path,
    uint8_t digest[SHA256_SIZE])
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(
        MBEDTLS_MD_SHA256);
    if (info == NULL) {
        fclose(file);
        return ESP_ERR_NOT_SUPPORTED;
    }

    mbedtls_md_context_t context;
    mbedtls_md_init(&context);

    int result = mbedtls_md_setup(&context, info, 0);
    if (result == 0) {
        result = mbedtls_md_starts(&context);
    }

    uint8_t buffer[1024];
    while (result == 0) {
        const size_t length = fread(buffer, 1U, sizeof(buffer), file);
        if (length > 0U) {
            result = mbedtls_md_update(&context, buffer, length);
        }
        if (length < sizeof(buffer)) {
            if (ferror(file)) {
                result = -1;
            }
            break;
        }
    }

    if (result == 0) {
        result = mbedtls_md_finish(&context, digest);
    }

    mbedtls_md_free(&context);
    fclose(file);
    return result == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t read_cache(
    const char *path,
    const uint8_t expected_hash[SHA256_SIZE],
    motion_profile_t *profile)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    cache_header_t header;
    bool valid =
        fread(&header, 1U, sizeof(header), file) == sizeof(header) &&
        header.magic == CACHE_MAGIC &&
        header.version == CACHE_VERSION &&
        header.payload_size == sizeof(*profile) &&
        memcmp(header.json_sha256, expected_hash, SHA256_SIZE) == 0 &&
        fread(profile, 1U, sizeof(*profile), file) == sizeof(*profile) &&
        fgetc(file) == EOF;

    fclose(file);
    if (!valid) {
        return ESP_ERR_INVALID_CRC;
    }

    return motion_profile_validate(profile, NULL, 0U);
}

static esp_err_t write_cache_atomic(
    const char *path,
    const uint8_t hash[SHA256_SIZE],
    const motion_profile_t *profile)
{
    char temporary[PROFILE_PATH_BUFFER_SIZE];
    char backup[PROFILE_PATH_BUFFER_SIZE];

    if ((snprintf(temporary, sizeof(temporary), "%s.tmp", path) >=
            (int)sizeof(temporary)) ||
        (snprintf(backup, sizeof(backup), "%s.bak", path) >=
            (int)sizeof(backup))) {
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = fopen(temporary, "wb");
    if (file == NULL) {
        return ESP_FAIL;
    }

    cache_header_t header = {
        .magic = CACHE_MAGIC,
        .version = CACHE_VERSION,
        .payload_size = sizeof(*profile),
    };
    memcpy(header.json_sha256, hash, SHA256_SIZE);

    bool success =
        fwrite(&header, 1U, sizeof(header), file) == sizeof(header) &&
        fwrite(profile, 1U, sizeof(*profile), file) == sizeof(*profile) &&
        fflush(file) == 0 &&
        fsync(fileno(file)) == 0;

    const int close_result = fclose(file);
    success = success && close_result == 0;
    if (!success) {
        (void)remove(temporary);
        return ESP_FAIL;
    }

    const bool original_exists = access(path, F_OK) == 0;
    (void)remove(backup);
    if (original_exists && rename(path, backup) != 0) {
        (void)remove(temporary);
        return ESP_FAIL;
    }

    if (rename(temporary, path) != 0) {
        if (original_exists) {
            (void)rename(backup, path);
        }
        (void)remove(temporary);
        return ESP_FAIL;
    }

    (void)remove(backup);
    return ESP_OK;
}

esp_err_t motion_profile_boot_load(
    const char *json_path,
    const char *binary_path,
    motion_profile_t *profile,
    bool *rebuilt)
{
    if (json_path == NULL || binary_path == NULL || profile == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t hash[SHA256_SIZE];
    esp_err_t error = hash_file(json_path, hash);
    if (error != ESP_OK) {
        return error;
    }

    error = read_cache(binary_path, hash, profile);
    if (error == ESP_OK) {
        if (rebuilt != NULL) {
            *rebuilt = false;
        }
        return ESP_OK;
    }

    error = motion_profile_load_json(json_path, profile);
    if (error != ESP_OK) {
        return error;
    }

    error = write_cache_atomic(binary_path, hash, profile);
    if (error == ESP_OK && rebuilt != NULL) {
        *rebuilt = true;
    }
    return error;
}
