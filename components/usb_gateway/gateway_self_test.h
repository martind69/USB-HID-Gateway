#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum { GATEWAY_SELF_TEST_IDLE=0,GATEWAY_SELF_TEST_RUNNING,GATEWAY_SELF_TEST_PASSED,GATEWAY_SELF_TEST_FAILED } gateway_self_test_state_t;
typedef struct { uint32_t requested_frames; uint32_t completed_frames; uint32_t failed_frames; gateway_self_test_state_t state; esp_err_t last_error; } gateway_self_test_status_t;
esp_err_t gateway_self_test_init(void);
esp_err_t gateway_self_test_start(uint32_t frame_count,bool inject_crc_errors);
gateway_self_test_status_t gateway_self_test_status(void);
