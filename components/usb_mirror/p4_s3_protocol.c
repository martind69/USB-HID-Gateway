#include "p4_s3_protocol.h"

#include <string.h>

uint16_t p4_s3_crc16(const uint8_t *data, size_t length)
{
    /* CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF. */
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= (uint16_t)((uint16_t)data[index] << 8U);
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1U);
            }
        }
    }
    return crc;
}

void p4_s3_frame_build(
    p4_s3_frame_t *frame,
    uint8_t type,
    uint8_t sequence,
    const uint8_t *payload,
    uint8_t payload_length)
{
    if (frame == NULL) {
        return;
    }
    if (payload_length > P4_S3_PAYLOAD_SIZE) {
        payload_length = (uint8_t)P4_S3_PAYLOAD_SIZE;
    }
    memset(frame, 0, sizeof(*frame));
    frame->raw[P4_S3_OFFSET_MAGIC_0] = P4_S3_MAGIC_0;
    frame->raw[P4_S3_OFFSET_MAGIC_1] = P4_S3_MAGIC_1;
    frame->raw[P4_S3_OFFSET_VERSION] = P4_S3_PROTOCOL_VERSION;
    frame->raw[P4_S3_OFFSET_TYPE] = type;
    frame->raw[P4_S3_OFFSET_SEQUENCE] = sequence;
    frame->raw[P4_S3_OFFSET_LENGTH] = payload_length;
    if (payload != NULL && payload_length > 0U) {
        memcpy(&frame->raw[P4_S3_OFFSET_PAYLOAD], payload, payload_length);
    }
    const uint16_t crc = p4_s3_crc16(frame->raw, P4_S3_CRC_OFFSET);
    frame->raw[P4_S3_CRC_OFFSET] = (uint8_t)(crc & 0xFFU);
    frame->raw[P4_S3_CRC_OFFSET + 1U] = (uint8_t)(crc >> 8U);
}

bool p4_s3_frame_validate(const p4_s3_frame_t *frame)
{
    if (frame == NULL ||
        frame->raw[P4_S3_OFFSET_MAGIC_0] != P4_S3_MAGIC_0 ||
        frame->raw[P4_S3_OFFSET_MAGIC_1] != P4_S3_MAGIC_1 ||
        frame->raw[P4_S3_OFFSET_VERSION] != P4_S3_PROTOCOL_VERSION ||
        frame->raw[P4_S3_OFFSET_LENGTH] > P4_S3_PAYLOAD_SIZE) {
        return false;
    }
    const uint16_t expected =
        (uint16_t)frame->raw[P4_S3_CRC_OFFSET] |
        (uint16_t)((uint16_t)frame->raw[P4_S3_CRC_OFFSET + 1U] << 8U);
    return p4_s3_crc16(frame->raw, P4_S3_CRC_OFFSET) == expected;
}
