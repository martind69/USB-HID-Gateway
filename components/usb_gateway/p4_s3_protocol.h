#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define P4_S3_FRAME_SIZE 72U
#define P4_S3_PAYLOAD_SIZE 64U
#define P4_S3_STATUS_TELEMETRY_VERSION 1U

#define P4_S3_S3_FLAG_USB_MOUNTED 0x01U
#define P4_S3_S3_FLAG_IF0_READY 0x02U
#define P4_S3_S3_FLAG_IF1_READY 0x04U

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
    P4_S3_MSG_STATUS = 0x80,
} p4_s3_message_type_t;

typedef struct {
    uint8_t raw[P4_S3_FRAME_SIZE];
} p4_s3_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t telemetry_version;
    uint8_t flags;
    uint8_t last_interface;
    uint8_t last_report_id;
    uint8_t reserved[3];
    uint32_t spi_received;
    uint32_t spi_invalid;
    uint32_t spi_failed;
    uint32_t reports_accepted;
    uint32_t reports_rejected;
    uint32_t usb_reports_sent;
    uint32_t usb_reports_failed;
    uint32_t queue_dropped;
    uint32_t queue_high_watermark;
    uint32_t output_reports_received;
} p4_s3_status_payload_t;

uint16_t p4_s3_crc16(const uint8_t *data, size_t length);
void p4_s3_frame_build(
    p4_s3_frame_t *frame,
    uint8_t type,
    uint8_t sequence,
    const uint8_t *payload,
    uint8_t payload_length);
bool p4_s3_frame_validate(const p4_s3_frame_t *frame);
