#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "p4_s3_protocol.h"
esp_err_t s3_proxy_reports_init(void);
esp_err_t s3_proxy_send_set_report(uint8_t instance,uint8_t report_id,uint8_t report_type,const uint8_t*data,size_t length);
esp_err_t s3_proxy_send_get_report(uint8_t instance,uint8_t report_id,uint8_t report_type,uint16_t requested_length);
bool s3_proxy_next_frame(p4_s3_frame_t *frame);
esp_err_t s3_proxy_accept_response(const p4_s3_frame_t *frame);
size_t s3_proxy_copy_get_response(uint8_t tag,uint8_t *buffer,size_t capacity,uint32_t timeout_ms);
