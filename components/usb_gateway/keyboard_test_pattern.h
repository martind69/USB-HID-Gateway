#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    KEYBOARD_TEST_PATTERN_IDLE = 0,
    KEYBOARD_TEST_PATTERN_COUNTDOWN,
    KEYBOARD_TEST_PATTERN_RUNNING,
    KEYBOARD_TEST_PATTERN_PASSED,
    KEYBOARD_TEST_PATTERN_FAILED,
    KEYBOARD_TEST_PATTERN_ABORTED,
} keyboard_test_pattern_state_t;

typedef struct {
    keyboard_test_pattern_state_t state;
    uint32_t reports_requested;
    uint32_t reports_completed;
    uint32_t reports_failed;
    uint32_t phases_completed;
    uint8_t last_status;
    esp_err_t last_error;
} keyboard_test_pattern_status_t;

esp_err_t keyboard_test_pattern_init(void);
esp_err_t keyboard_test_pattern_start(void);
esp_err_t keyboard_test_pattern_abort(void);
keyboard_test_pattern_status_t keyboard_test_pattern_status(void);
