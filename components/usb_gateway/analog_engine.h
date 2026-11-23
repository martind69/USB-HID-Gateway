#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "analog_vector_io.h"
#include "esp_err.h"
#include "motion_profile_storage.h"
#include "output_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Task 2 and task 3: the only owner of the ADC/DAC (analog_vector_io) and the
 * digital outputs after initialization. One periodic task (tickMs from
 * output.json) runs in one of three modes:
 *
 *   PASSTHROUGH  gateway mode PASS: ADC code -> DAC code per channel
 *                (the physical joystick operates the machine).
 *   LEARN        PASSTHROUGH plus learning: both sticks are normalized with
 *                the profile calibration and feed radial envelope, kinematic
 *                estimator and learned motion model. Stop saves the profile.
 *   GENERATE     gateway mode PROXY: mouse/keyboard (report_tap) ->
 *                target -> trajectory generator with the learned human
 *                motion model -> envelope -> DAC; buttons/keys -> triggers
 *                and GPIO.
 *
 * Without a valid profile only PASSTHROUGH is available.
 * GPIOs are inactive outside GENERATE. ADC read errors write the channel's
 * safe code.
 */

typedef enum {
    ANALOG_ENGINE_PASSTHROUGH = 0,
    ANALOG_ENGINE_LEARN = 1,
    ANALOG_ENGINE_GENERATE = 2,
} analog_engine_mode_t;

typedef struct {
    analog_engine_mode_t mode;
    bool saving;
    uint32_t ticks;
    uint32_t overruns;
    uint32_t adc_errors;
    uint32_t dac_errors;
    uint64_t learned_samples;
    esp_err_t last_save_result;
} analog_engine_status_t;

/*
 * io_available: analog_vector_io_init() succeeded.
 * profile may be NULL (no SD / invalid profile); it must stay valid and is
 * modified by learning. profile_path is used to save after learning.
 */
esp_err_t analog_engine_start(
    analog_vector_io_t *io,
    bool io_available,
    motion_profile_t *profile,
    const char *profile_path,
    const output_config_t *config);

/* Starts or stops learning (also bound to hotkey "learn"). */
void analog_engine_request_learning(bool start);

void analog_engine_get_status(analog_engine_status_t *status);

#ifdef __cplusplus
}
#endif
