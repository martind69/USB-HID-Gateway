

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "analog_vector_io.h"
#include "esp_err.h"
#include "motion_profile_storage.h"
#include "output_config.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Task 2 and task 3: the only user of the ADC/DAC (analog_vector_io) and the
 * digital outputs after initialization. One periodic task (tickMs from
 * output.json) runs in one of three modes:
 *
 *   PASSTHROUGH  gateway mode PASS: the physical joystick operates the
 *                machine directly. DAC HIGH-Z-CMD accepted; read-back is diagnostic, the
 *                ADC monitors the nodes.
 *   LEARN        PASSTHROUGH plus learning: both sticks are normalized with
 *                the profile calibration and feed radial envelope, kinematic
 *                estimator and learned motion model. Stop saves the profile.
 *   GENERATE     gateway mode PROXY: DAC drives and overrides the Hall
 *                outputs (about 16 kOhm). Mouse/keyboard (report_tap) ->
 *                target -> trajectory generator with the learned human
 *                motion model -> envelope -> DAC; buttons/keys -> triggers
 *                and GPIO. GENERATE LED on. With
 *                CONFIG_USB_GATEWAY_INTERCEPT_ENABLE the usages mapped in
 *                output.json are removed from the reports mirrored to the
 *                PC (report_filter, M7).
 *
 * The joystick supply is always on and not controlled by the firmware.
 * Without a valid profile or without DAC only PASSTHROUGH is available.
 *
 * DAC output fault (latched until reset): HIGH-Z-CMD failed or DAC write
 * failed. GENERATE is blocked, GPIOs off, learning discarded.
 *
 * esp_restart() (OTA, software reset): a shutdown handler releases the DAC,
 * the GPIOs and the report filter before the restart.
 */
typedef enum {
    ANALOG_ENGINE_PASSTHROUGH = 0,
    ANALOG_ENGINE_LEARN = 1,
    ANALOG_ENGINE_GENERATE = 2,
#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
    ANALOG_ENGINE_SIMULATE = 3,
#endif
} analog_engine_mode_t;

typedef enum {
    ANALOG_OUTPUT_FAULT_NONE = 0,
    ANALOG_OUTPUT_FAULT_DAC_HIGH_Z,     /* HIGH-Z-CMD write failed */
    ANALOG_OUTPUT_FAULT_DAC_WRITE,      /* preload / power-up failed */
} analog_output_fault_t;

typedef enum {
    ANALOG_MEASUREMENT_IDLE = 0,
    ANALOG_MEASUREMENT_STATIC_SETTLE,
    ANALOG_MEASUREMENT_STATIC_SAMPLE,
    ANALOG_MEASUREMENT_DYNAMIC_ALIGN,
    ANALOG_MEASUREMENT_DYNAMIC_RUN,
} analog_measurement_phase_t;

typedef struct {
    bool valid;
    uint8_t points_completed;
    uint64_t samples;
    float mse_code2;
    float rmse_code;
    float bias_code;
    float precision_sd_code;
    float rmse_mv;
    float bias_mv;
    float precision_sd_mv;
} analog_static_metrics_t;

typedef struct {
    bool valid;
    uint64_t samples;
    uint32_t effective_lag_us;
    float mse_code2;
    float rmse_code;
    float bias_code;
    float max_abs_error_code;
    float rmse_mv;
    float bias_mv;
    float max_abs_error_mv;
} analog_dynamic_metrics_t;

typedef struct {
    analog_engine_mode_t mode;
    bool dac_driving;
    analog_output_fault_t fault;
    uint16_t dac_power_raw;             /* diagnostic power register read-back */
    bool saving;
    uint32_t ticks;
    uint32_t overruns;
    uint32_t adc_errors;
    uint32_t dac_errors;
    uint64_t learned_samples;
    esp_err_t last_save_result;
    uint16_t adc_code[ANALOG_HID_COUNT];    /* last sample per logical channel */
#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
    analog_measurement_phase_t measurement_phase;
    uint8_t measurement_static_point;
    analog_static_metrics_t static_measurement[4];
    analog_dynamic_metrics_t dynamic_measurement[4];
#endif
} analog_engine_status_t;

/*
 * Early safe state, first call in usb_gateway_init(): GENERATE LED and
 * digital outputs off, I2C bus recovered, DAC7578 high-Z, ADS7128 inputs.
 * io is initialized by analog_vector_io_init(); the result is returned.
 */
esp_err_t analog_engine_early_init(analog_vector_io_t *io);

/*
 * io: initialized by analog_engine_early_init(). profile may be NULL
 * (no SD / invalid profile); it must stay valid and is modified by learning.
 * profile_path is used to save after learning.
 *
 * Forces the DAC to high-Z (PASSTHROUGH) before the task starts.
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
#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
const char *analog_measurement_phase_name(analog_measurement_phase_t phase);
#endif

/* Short names, max. 8 characters (OLED). */
const char *analog_output_fault_name(analog_output_fault_t fault);

#ifdef __cplusplus
}
#endif
