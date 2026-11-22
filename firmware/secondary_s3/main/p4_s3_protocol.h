#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define P4_S3_FRAME_SIZE 72U
#define P4_S3_PAYLOAD_SIZE 64U
#define P4_S3_OTA_DATA_SIZE 56U
#define P4_S3_OTA_VERSION_LENGTH 16U

typedef enum {
    P4_S3_MSG_NOP = 0x00,
    P4_S3_MSG_G815_IF0_INPUT = 0x10,
    P4_S3_MSG_G815_IF1_INPUT = 0x11,
    P4_S3_MSG_G815_ATTACH = 0x20,
    P4_S3_MSG_G815_DETACH = 0x21,
    P4_S3_MSG_OUTPUT_FRAGMENT = 0x30,
    P4_S3_MSG_FEATURE_GET = 0x31,
    P4_S3_MSG_FEATURE_SET_FRAGMENT = 0x32,
    P4_S3_MSG_EP0_FRAGMENT = 0x40,
    P4_S3_MSG_CONTROL_RESPONSE_FRAGMENT = 0x41,
    P4_S3_MSG_OTA_BEGIN = 0x50,
    P4_S3_MSG_OTA_DATA = 0x51,
    P4_S3_MSG_OTA_END = 0x52,
    P4_S3_MSG_OTA_ABORT = 0x53,
    P4_S3_MSG_OTA_STATUS = 0x54,
    P4_S3_MSG_OTA_CONFIRM = 0x55,
    P4_S3_MSG_STATUS = 0x80,
} p4_s3_message_type_t;

typedef enum {
    P4_S3_OTA_PHASE_IDLE = 0,
    P4_S3_OTA_PHASE_ERASING = 1,
    P4_S3_OTA_PHASE_WRITING = 2,
    P4_S3_OTA_PHASE_VERIFYING = 3,
    P4_S3_OTA_PHASE_REBOOT_PENDING = 4,
    P4_S3_OTA_PHASE_PENDING_CONFIRM = 5,
    P4_S3_OTA_PHASE_CONFIRMED = 6,
    P4_S3_OTA_PHASE_ABORTED = 7,
    P4_S3_OTA_PHASE_FAILED = 8,
} p4_s3_ota_phase_t;

typedef struct __attribute__((packed)) {
    uint32_t image_size;
    uint8_t sha256[32];
    char version[P4_S3_OTA_VERSION_LENGTH];
} p4_s3_ota_begin_t;

typedef struct __attribute__((packed)) {
    uint32_t offset;
    uint16_t chunk_crc16;
    uint8_t data_length;
    uint8_t reserved;
    uint8_t data[P4_S3_OTA_DATA_SIZE];
} p4_s3_ota_data_t;

typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t phase;
    int32_t error_code;
    uint32_t bytes_written;
    uint32_t image_size;
    uint32_t next_offset;
    char running_version[P4_S3_OTA_VERSION_LENGTH];
} p4_s3_ota_status_t;

typedef struct {
    uint8_t raw[P4_S3_FRAME_SIZE];
} p4_s3_frame_t;

uint16_t p4_s3_crc16(const uint8_t *data, size_t length);
void p4_s3_frame_build(
    p4_s3_frame_t *frame,
    uint8_t type,
    uint8_t sequence,
    const uint8_t *payload,
    uint8_t payload_length);
bool p4_s3_frame_validate(const p4_s3_frame_t *frame);
