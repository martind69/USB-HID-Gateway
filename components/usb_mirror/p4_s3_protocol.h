#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared P4 <-> S3 SPI frame protocol (component usb_mirror, used by both
 * firmware projects; there is exactly one copy of this file).
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
#define P4_S3_PROTOCOL_VERSION      1U  /* unchanged: SPI OTA must keep working with older S3 images */

#define P4_S3_OTA_DATA_SIZE         56U
#define P4_S3_OTA_VERSION_LENGTH    16U

#define P4_S3_FRAGMENT_HEADER_SIZE  8U
#define P4_S3_FRAGMENT_DATA_SIZE    56U

#define P4_S3_DESCRIPTOR_HEADER_SIZE 8U
#define P4_S3_DESCRIPTOR_DATA_SIZE   56U

typedef enum {
    P4_S3_MSG_NOP = 0x00,

    /*
     * P4 -> S3: raw input report of the mirrored device, unchanged bytes.
     * Message type = P4_S3_MSG_INPUT_BASE + USB interface number (0..15).
     */
    P4_S3_MSG_INPUT_BASE = 0x10,
    P4_S3_MSG_INPUT_LAST = 0x1F,

    /* P4 -> S3: mirror session control. */
    P4_S3_MSG_MIRROR_BEGIN = 0x20,          /* p4_s3_mirror_begin_t */
    P4_S3_MSG_MIRROR_END = 0x21,            /* no payload */
    P4_S3_MSG_MIRROR_DESCRIPTOR = 0x22,     /* p4_s3_descriptor_chunk_t */
    P4_S3_MSG_MIRROR_COMMIT = 0x23,         /* no payload, S3 connects to PC */
    P4_S3_MSG_MIRROR_STATS = 0x24,          /* STATUS carries p4_s3_mirror_stats_t */

    /* S3 -> P4: PC-to-device requests, fragmented (p4_s3_fragment_t). */
    P4_S3_MSG_OUTPUT_FRAGMENT = 0x30,
    P4_S3_MSG_FEATURE_GET = 0x31,
    P4_S3_MSG_FEATURE_SET_FRAGMENT = 0x32,
    P4_S3_MSG_EP0_FRAGMENT = 0x40,
    P4_S3_MSG_CONTROL_RESPONSE_FRAGMENT = 0x41,

    /* S3 -> P4: S3 has no session (boot, OTA restart) and requests one. */
    P4_S3_MSG_MIRROR_REQUEST = 0x42,

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

/* First payload byte of a STATUS frame. */
typedef enum {
    P4_S3_STATUS_OK = 0x00,
    P4_S3_STATUS_INVALID_FRAME = 0x01,
    P4_S3_STATUS_UNSUPPORTED = 0x02,
    P4_S3_STATUS_INVALID_LENGTH = 0x03,
    P4_S3_STATUS_USB_NOT_MOUNTED = 0x04,
    P4_S3_STATUS_QUEUE_FULL = 0x06,
    P4_S3_STATUS_INVALID_INTERFACE = 0x07,
    P4_S3_STATUS_UNSUPPORTED_REPORT_ID = 0x08,
    P4_S3_STATUS_NO_SESSION = 0x09,
    P4_S3_STATUS_INVALID_STATE = 0x0A,
    P4_S3_STATUS_NOT_MIRRORABLE = 0x0B,
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

typedef enum {
    P4_S3_DESCRIPTOR_DEVICE = 1,
    P4_S3_DESCRIPTOR_CONFIGURATION = 2,
    P4_S3_DESCRIPTOR_REPORT = 3,        /* index = interface number */
    P4_S3_DESCRIPTOR_STRING = 4,        /* index = string index, data = ASCII */
} p4_s3_descriptor_kind_t;

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
 * For numbered reports data[0] of the first fragment is the Report ID.
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

typedef struct __attribute__((packed)) {
    uint8_t session;            /* USB host address of the device on the P4 */
} p4_s3_mirror_begin_t;

/* Descriptor transfer chunk; chunks of one descriptor are sent in order. */
typedef struct __attribute__((packed)) {
    uint8_t kind;               /* p4_s3_descriptor_kind_t */
    uint8_t index;
    uint16_t offset;
    uint16_t total_length;
    uint8_t data_length;
    uint8_t reserved;
    uint8_t data[P4_S3_DESCRIPTOR_DATA_SIZE];
} p4_s3_descriptor_chunk_t;

typedef enum {
    P4_S3_MIRROR_STATE_IDLE = 0,
    P4_S3_MIRROR_STATE_COLLECTING = 1,
    P4_S3_MIRROR_STATE_CONNECTED = 2,
} p4_s3_mirror_state_t;

/* STATUS payload of P4_S3_MSG_MIRROR_STATS. */
typedef struct __attribute__((packed)) {
    uint8_t status;             /* P4_S3_STATUS_OK */
    uint8_t session;            /* 0 = no session */
    uint8_t state;              /* p4_s3_mirror_state_t */
    uint8_t mounted;            /* PC has configured the mirror */
    uint16_t vendor_id;
    uint16_t product_id;
    uint32_t reports_sent;
    uint32_t reports_dropped;
    uint32_t set_report_forwarded;
    uint32_t set_report_dropped;
    uint32_t get_report_stalled;
    uint8_t last_get_interface;
    uint8_t last_get_report_id;
    uint8_t last_get_report_type;
    uint8_t reserved;
    uint32_t uptime_ms;
} p4_s3_mirror_stats_t;

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
_Static_assert(sizeof(p4_s3_descriptor_chunk_t) == P4_S3_PAYLOAD_SIZE,
    "p4_s3_descriptor_chunk_t size mismatch");
_Static_assert(sizeof(p4_s3_ota_begin_t) <= P4_S3_PAYLOAD_SIZE,
    "p4_s3_ota_begin_t too large");
_Static_assert(sizeof(p4_s3_ota_status_t) <= P4_S3_PAYLOAD_SIZE,
    "p4_s3_ota_status_t too large");
_Static_assert(sizeof(p4_s3_mirror_stats_t) <= P4_S3_PAYLOAD_SIZE,
    "p4_s3_mirror_stats_t too large");

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

static inline bool p4_s3_is_mirror_control(uint8_t type)
{
    return type >= (uint8_t)P4_S3_MSG_MIRROR_BEGIN &&
        type <= (uint8_t)P4_S3_MSG_MIRROR_STATS;
}

static inline bool p4_s3_is_input_message(uint8_t type)
{
    return type >= (uint8_t)P4_S3_MSG_INPUT_BASE &&
        type <= (uint8_t)P4_S3_MSG_INPUT_LAST;
}

#ifdef __cplusplus
}
#endif
