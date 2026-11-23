#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared P4 <-> S3 SPI frame protocol.
 *
 * This file and p4_s3_protocol.c must be byte-identical in:
 *     components/usb_gateway/
 *     firmware/secondary_s3/main/
 *
 * Frame layout (72 bytes):
 *     [0]      magic 0xC5
 *     [1]      magic 0x5A
 *     [2]      protocol version
 *     [3]      message type
 *     [4]      sequence number
 *     [5]      payload length (0..64)
 *     [6..69]  payload
 *     [70..71] CRC-16/CCITT-FALSE over bytes 0..69, little endian
 */

#define P4_S3_FRAME_SIZE            72U
#define P4_S3_HEADER_SIZE           6U
#define P4_S3_PAYLOAD_SIZE          64U
#define P4_S3_CRC_OFFSET            70U

#define P4_S3_OFFSET_MAGIC_0        0U
#define P4_S3_OFFSET_MAGIC_1        1U
#define P4_S3_OFFSET_VERSION        2U
#define P4_S3_OFFSET_TYPE           3U
#define P4_S3_OFFSET_SEQUENCE       4U
#define P4_S3_OFFSET_LENGTH         5U
#define P4_S3_OFFSET_PAYLOAD        6U

#define P4_S3_MAGIC_0               0xC5U
#define P4_S3_MAGIC_1               0x5AU
#define P4_S3_PROTOCOL_VERSION      1U

#define P4_S3_OTA_DATA_SIZE         56U
#define P4_S3_OTA_VERSION_LENGTH    16U

#define P4_S3_FRAGMENT_HEADER_SIZE  8U
#define P4_S3_FRAGMENT_DATA_SIZE    56U

typedef enum {
    P4_S3_MSG_NOP = 0x00,

    /* P4 -> S3: G815 input reports. */
    P4_S3_MSG_G815_IF0_INPUT = 0x10,
    P4_S3_MSG_G815_IF1_INPUT = 0x11,

    /* P4 -> S3: G815 lifecycle. */
    P4_S3_MSG_G815_ATTACH = 0x20,
    P4_S3_MSG_G815_DETACH = 0x21,

    /* S3 -> P4: PC-to-G815 requests, fragmented (p4_s3_fragment_t). */
    P4_S3_MSG_OUTPUT_FRAGMENT = 0x30,
    P4_S3_MSG_FEATURE_GET = 0x31,
    P4_S3_MSG_FEATURE_SET_FRAGMENT = 0x32,
    P4_S3_MSG_EP0_FRAGMENT = 0x40,
    P4_S3_MSG_CONTROL_RESPONSE_FRAGMENT = 0x41,

    /* P4 -> S3: SPI OTA. */
    P4_S3_MSG_OTA_BEGIN = 0x50,
    P4_S3_MSG_OTA_DATA = 0x51,
    P4_S3_MSG_OTA_END = 0x52,
    P4_S3_MSG_OTA_ABORT = 0x53,
    P4_S3_MSG_OTA_STATUS = 0x54,
    P4_S3_MSG_OTA_CONFIRM = 0x55,

    /* S3 -> P4: response to the previous P4 frame. */
    P4_S3_MSG_STATUS = 0x80,
} p4_s3_message_type_t;

/* First payload byte of a simple STATUS frame. */
typedef enum {
    P4_S3_STATUS_OK = 0x00,
    P4_S3_STATUS_INVALID_FRAME = 0x01,
    P4_S3_STATUS_UNSUPPORTED = 0x02,
    P4_S3_STATUS_INVALID_LENGTH = 0x03,
    P4_S3_STATUS_USB_NOT_MOUNTED = 0x04,
    P4_S3_STATUS_QUEUE_FULL = 0x06,
    P4_S3_STATUS_INVALID_INTERFACE = 0x07,
    P4_S3_STATUS_UNSUPPORTED_REPORT_ID = 0x08,
} p4_s3_status_code_t;

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

/*
 * Fragment for S3 -> P4 control traffic (SET_REPORT, EP0).
 * The data stream of a SET_REPORT always contains the Report ID as first
 * byte when the report is numbered (USB HID 1.11, 7.2.2).
 */
typedef struct __attribute__((packed)) {
    uint8_t tag;
    uint8_t interface_number;
    uint8_t report_id;
    uint8_t report_type;
    uint8_t index;
    uint8_t count;
    uint8_t data_length;
    uint8_t reserved;
    uint8_t data[P4_S3_FRAGMENT_DATA_SIZE];
} p4_s3_fragment_t;

/* Word alignment is required for SPI DMA buffers on the ESP32-S3. */
typedef struct __attribute__((aligned(4))) {
    uint8_t raw[P4_S3_FRAME_SIZE];
} p4_s3_frame_t;

_Static_assert(sizeof(p4_s3_frame_t) == P4_S3_FRAME_SIZE,
    "p4_s3_frame_t size mismatch");
_Static_assert(sizeof(p4_s3_fragment_t) == P4_S3_PAYLOAD_SIZE,
    "p4_s3_fragment_t size mismatch");
_Static_assert(sizeof(p4_s3_ota_data_t) == P4_S3_PAYLOAD_SIZE,
    "p4_s3_ota_data_t size mismatch");
_Static_assert(sizeof(p4_s3_ota_begin_t) <= P4_S3_PAYLOAD_SIZE,
    "p4_s3_ota_begin_t too large");
_Static_assert(sizeof(p4_s3_ota_status_t) <= P4_S3_PAYLOAD_SIZE,
    "p4_s3_ota_status_t too large");

uint16_t p4_s3_crc16(const uint8_t *data, size_t length);

void p4_s3_frame_build(
    p4_s3_frame_t *frame,
    uint8_t type,
    uint8_t sequence,
    const uint8_t *payload,
    uint8_t payload_length);

bool p4_s3_frame_validate(const p4_s3_frame_t *frame);

static inline bool p4_s3_frame_is_empty(const p4_s3_frame_t *frame)
{
    return frame->raw[P4_S3_OFFSET_MAGIC_0] == 0U &&
        frame->raw[P4_S3_OFFSET_MAGIC_1] == 0U;
}

static inline bool p4_s3_is_ota_message(uint8_t type)
{
    return type >= (uint8_t)P4_S3_MSG_OTA_BEGIN &&
        type <= (uint8_t)P4_S3_MSG_OTA_CONFIRM;
}

#ifdef __cplusplus
}
#endif
