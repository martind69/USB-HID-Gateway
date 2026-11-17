#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define P4_S3_FRAME_SIZE 72U
#define P4_S3_PAYLOAD_SIZE 64U
typedef enum{P4_S3_MSG_NOP=0x00,P4_S3_MSG_G815_IF0_INPUT=0x10,P4_S3_MSG_G815_IF1_INPUT=0x11,P4_S3_MSG_G815_ATTACH=0x20,P4_S3_MSG_G815_DETACH=0x21,P4_S3_MSG_UPSTREAM_STATE=0x22,P4_S3_MSG_STATUS=0x80}p4_s3_message_type_t;
typedef struct{uint8_t raw[P4_S3_FRAME_SIZE];}p4_s3_frame_t;
uint16_t p4_s3_crc16(const uint8_t*d,size_t n);void p4_s3_frame_build(p4_s3_frame_t*f,uint8_t type,uint8_t seq,const uint8_t*p,uint8_t n);bool p4_s3_frame_validate(const p4_s3_frame_t*f);
