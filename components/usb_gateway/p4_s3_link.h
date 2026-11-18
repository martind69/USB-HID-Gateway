#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "p4_s3_protocol.h"
typedef struct{uint8_t sequence,status;uint32_t round_trip_us;}p4_s3_ack_t;
esp_err_t p4_s3_link_init(void);void p4_s3_link_start(void);bool p4_s3_link_enqueue(uint8_t type,const uint8_t*payload,size_t length);bool p4_s3_link_wait_status(p4_s3_ack_t*ack,uint32_t timeout_ms);uint32_t p4_s3_link_dropped(void);
