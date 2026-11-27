#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "p4_s3_protocol.h"

esp_err_t s3_ota_receiver_init(void);
esp_err_t s3_ota_receiver_handle(
    uint8_t message_type,
    const uint8_t *payload,
    size_t payload_length,
    p4_s3_ota_status_t *status);
void s3_ota_receiver_get_status(p4_s3_ota_status_t *status);
