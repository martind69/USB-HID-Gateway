#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "analog_interlock.h"
#include "analog_vector_io.h"
#include "esp_err.h"
#include "motion_profile_storage.h"
#include "output_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Task 2 and task 3: the only user of the ADC/DAC (analog_vector_io) and the
 * digital outputs after initialization. One periodic task (tickMs from
 * output.json) runs in one of three modes:
 *
 *   PASSTHROUGH  gateway mode PASS: the physical joystick operates the
 *                machine directly (interlock JOYSTICK_ACTIVE, DAC high-Z).
 *                The ADC only monitors the nodes.
 *   LEARN        PASSTHROUGH plus learning: both sticks are normalized with
 *                the profile calibration and feed radial envelope, kinematic
 *                estimator and learned motion model. Stop saves the profile.
 *   GENERATE     gateway mode PROXY: joystick supply off, DAC drives
 *                (interlock DAC_ACTIVE). Mouse/keyboard (report_tap) ->
 *                target -> trajectory generator with the learned human
 *                motion model -> envelope -> DAC; buttons/keys -> triggers
 *                and GPIO.
 *
 * Without a valid profile or without DAC only PASSTHROUGH is available.
 * GPIOs are inactive outside GENERATE. An interlock FAULT stops all outputs
 * and discards a running learning session.
 */
typedef enum {
    ANALOG_ENGINE_PASSTHROUGH = 0,
    ANALOG_ENGINE_LEARN = 1,
    ANALOG_ENGINE_GENERATE = 2,
} analog_engine_mode_t;

typedef struct {
    analog_engine_mode_t mode;
    analog_interlock_state_t interlock_state;
    analog_interlock_fault_t interlock_fault;
    bool saving;
    uint32_t ticks;
    uint32_t overruns;
    uint32_t adc_errors;
    uint32_t dac_errors;
    uint64_t learned_samples;
    esp_err_t last_save_result;
    uint16_t adc_code[ANALOG_HID_COUNT];    /* last sample per logical channel */
} analog_engine_status_t;

/*
 * io: initialized by analog_vector_io_init(), interlock initialized by
 * analog_interlock_init(). profile may be NULL (no SD / invalid profile); it
 * must stay valid and is modified by learning. profile_path is used to save
 * after learning.
 */
esp_err_t analog_engine_start(
    analog_vector_io_t *io,
    motion_profile_t *profile,
    const char *profile_path,
    const output_config_t *config);

/* Starts or stops learning (also bound to hotkey "learn"). */
void analog_engine_request_learning(bool start);

void analog_engine_get_status(analog_engine_status_t *status);

const char *analog_engine_mode_name(analog_engine_mode_t mode);

#ifdef __cplusplus
}
#endif
