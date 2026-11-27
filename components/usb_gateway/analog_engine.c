
#include "analog_engine.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "analog_bus.h"
#include "analog_control_gpio.h"
#include "analog_mapping.h"
#include "gpio_output.h"
#include "hotkey.h"
#include "human_motion_model.h"
#include "input_state.h"
#include "kinematic_estimator.h"
#include "learned_motion_model.h"
#include "profile_feedback.h"
#include "radial_envelope.h"
#include "report_filter.h"
#include "report_tap.h"
#include "trajectory_generator.h"
#include "usb_gateway.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define ENGINE_TASK_STACK           6144U
#define ENGINE_TASK_PRIORITY        12U
#define SAVE_TASK_STACK             8192U
#define SAVE_TASK_PRIORITY          3U

#define ESTIMATOR_ALPHA             0.3f
#define LEARN_MIN_SAMPLES           500U
#define DEFAULT_SAFE_CODE           2048U
#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
#define SIMULATE_FREQUENCY_HZ        2.0f
#define SIMULATE_TWO_PI              6.28318530718f
#endif

#if CONFIG_USB_GATEWAY_DAC7578_ENABLE
#define DAC_FITTED true
#else
#define DAC_FITTED false
#endif

#ifndef CONFIG_USB_GATEWAY_INTERCEPT_ENABLE
#define CONFIG_USB_GATEWAY_INTERCEPT_ENABLE 0
#endif

static const uint8_t STICK_CHANNEL_X[OUTPUT_STICK_COUNT] = { ANALOG_HID_LX, ANALOG_HID_RX };
static const uint8_t STICK_CHANNEL_Y[OUTPUT_STICK_COUNT] = { ANALOG_HID_LY, ANALOG_HID_RY };
static const uint8_t TRIGGER_CHANNEL[OUTPUT_TRIGGER_COUNT] = {
    ANALOG_HID_LT, ANALOG_HID_RT, ANALOG_HID_L2, ANALOG_HID_R2,
};

static const char *TAG = "analog_engine";

static analog_vector_io_t *s_io;
static motion_profile_t *s_profile;
static const char *s_profile_path;
static output_config_t s_config;

static volatile bool s_learn_requested;
static volatile bool s_saving;
static esp_err_t s_last_save_result = ESP_OK;

static analog_engine_mode_t s_mode = ANALOG_ENGINE_PASSTHROUGH;
static uint32_t s_ticks;
static uint32_t s_overruns;
static uint32_t s_adc_errors;
static uint32_t s_dac_errors;
static uint16_t s_codes[ANALOG_HID_COUNT];

/* DAC output state */
static bool s_dac_ready;            /* fitted and acknowledged */
static bool s_dac_driving;
static volatile analog_output_fault_t s_fault = ANALOG_OUTPUT_FAULT_NONE;
static uint16_t s_dac_power_raw;

/* LEARN state */
static radial_envelope_t s_envelope[OUTPUT_STICK_COUNT];
static kinematic_estimator_t s_estimator[OUTPUT_STICK_COUNT];
static motion_profile_t *s_backup;

/* GENERATE state */
static human_motion_model_t s_model;
static trajectory_generator_t s_trajectory[OUTPUT_STICK_COUNT];
static motion_vec2_t s_target[OUTPUT_STICK_COUNT];

static const motion_stick_calibration_t *stick_calibration(size_t stick)
{
    return stick == OUTPUT_STICK_LEFT ? &s_profile->left_stick : &s_profile->right_stick;
}

static uint16_t safe_code(uint8_t channel)
{
    return s_profile != NULL ? s_profile->channel[channel].safe_code : DEFAULT_SAFE_CODE;
}

static bool adc_ready(void)
{
    return s_io != NULL && s_io->adc.enabled;
}

static bool generate_possible(void)
{
    return s_dac_ready && s_profile != NULL && s_fault == ANALOG_OUTPUT_FAULT_NONE;
}

/* ---------------------------------------------------------------------- */
/* Reset paths                                                            */
/* ---------------------------------------------------------------------- */

/*
 * esp_restart() runs registered shutdown handlers (not on panic, watchdog
 * or brownout). Best effort, no read-back.
 */
static void shutdown_handler(void)
{
    report_filter_set_active(false);
    gpio_output_all_off();
    analog_generate_led_set(false);
    if (s_io != NULL && s_io->dac.enabled) {
        (void)dac7578_set_power(&s_io->dac, DAC7578_ALL_CHANNELS, DAC7578_POWER_DOWN_HIGH_Z);
    }
}

esp_err_t analog_engine_early_init(analog_vector_io_t *io)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_io = io;

    esp_err_t error = analog_control_gpio_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "GENERATE LED initialization failed: %s", esp_err_to_name(error));
    }
    error = gpio_output_init();
    if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGE(TAG, "Digital output initialization failed: %s", esp_err_to_name(error));
    }
    gpio_output_all_off();

    /* Bus recovery, ADS7128 reset (inputs), DAC7578 high-Z. */
    error = analog_vector_io_init(io);

    const esp_err_t handler_error = esp_register_shutdown_handler(shutdown_handler);
    if (handler_error != ESP_OK && handler_error != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Shutdown handler not registered: %s", esp_err_to_name(handler_error));
    }

#if CONFIG_USB_GATEWAY_CAPTURE_ENABLE
    analog_bus_scan();
#endif
    return error;
}

/* ---------------------------------------------------------------------- */
/* DAC output control                                                     */
/* ---------------------------------------------------------------------- */

static void enter_fault(analog_output_fault_t fault, esp_err_t error)
{
    if (s_fault == ANALOG_OUTPUT_FAULT_NONE) {
        s_fault = fault;
        ESP_LOGE(TAG, "FAULT %s (%s, DAC power=0x%04X): GENERATE blocked until reset",
            analog_output_fault_name(fault), esp_err_to_name(error),
            (unsigned)s_dac_power_raw);
    }
    profile_feedback_set(PROFILE_FEEDBACK_ANALOG_ERROR);
}

/*
 * Commands all channels to high-Z. The write result is decisive. Read-back is
 * diagnostic only: it can confirm register plausibility, but not the physical
 * high-impedance state of the analog output driver.
 */
static bool dac_release(void)
{
    bool released = true;

    if (s_dac_ready) {
        const esp_err_t command_error = dac7578_set_power(
            &s_io->dac, DAC7578_ALL_CHANNELS, DAC7578_POWER_DOWN_HIGH_Z);
        if (command_error != ESP_OK) {
            enter_fault(ANALOG_OUTPUT_FAULT_DAC_HIGH_Z, command_error);
            released = false;
        } else {
            const esp_err_t plausibility = dac7578_verify_power(
                &s_io->dac,
                DAC7578_ALL_CHANNELS,
                DAC7578_POWER_DOWN_HIGH_Z,
                &s_dac_power_raw);
            if (plausibility == ESP_OK) {
                ESP_LOGI(TAG,
                    "HIGH-Z-CMD accepted, register read-back plausible: 0x%04X",
                    (unsigned)s_dac_power_raw);
            } else if (plausibility == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG,
                    "HIGH-Z-CMD accepted, register read-back differs: 0x%04X "
                    "(diagnostic only)",
                    (unsigned)s_dac_power_raw);
            } else {
                ESP_LOGW(TAG,
                    "HIGH-Z-CMD accepted, register read-back unavailable: %s "
                    "(diagnostic only)",
                    esp_err_to_name(plausibility));
            }
        }
    }

    if (released) {
        s_dac_driving = false;
        analog_generate_led_set(false);
    }
    /* Machine no longer follows mouse/keyboard: PC gets the full reports again. */
    report_filter_set_active(false);
    return released;
}

/* Filtering starts first, then safe codes are preloaded and the DAC powered up. */
static bool dac_drive(bool intercept_reports)
{
    report_filter_set_active(
        intercept_reports && CONFIG_USB_GATEWAY_INTERCEPT_ENABLE != 0);

    for (uint8_t channel = 0U; channel < ANALOG_HID_COUNT; ++channel) {
        const esp_err_t error = analog_vector_io_write_channel(
            s_io, (analog_hid_channel_t)channel, safe_code(channel));
        if (error != ESP_OK) {
            enter_fault(ANALOG_OUTPUT_FAULT_DAC_WRITE, error);
            report_filter_set_active(false);
            return false;
        }
    }

    const esp_err_t error = dac7578_set_power(
        &s_io->dac, DAC7578_ALL_CHANNELS, DAC7578_POWER_UP);
    if (error != ESP_OK) {
        enter_fault(ANALOG_OUTPUT_FAULT_DAC_WRITE, error);
        report_filter_set_active(false);
        return false;
    }

    s_dac_driving = true;
    analog_generate_led_set(true);
    return true;
}

static void write_dac(uint8_t channel, uint16_t code)
{
    if (!s_dac_driving) {
        return;
    }
    if (analog_vector_io_write_channel(s_io, (analog_hid_channel_t)channel, code) != ESP_OK) {
        ++s_dac_errors;
    }
}

/* ---------------------------------------------------------------------- */
/* Report filter rules (M7)                                               */
/* ---------------------------------------------------------------------- */

static void add_rule(input_usage_t usage, unsigned *overflow)
{
    if (!report_filter_add_usage(usage)) {
        ++*overflow;
    }
}

/* Every usage that output.json maps to a DAC channel or a GPIO. Hotkeys stay visible. */
static void build_filter_rules(void)
{
    static const input_usage_t MOUSE_X = { HID_PAGE_GENERIC_DESKTOP, HID_USAGE_GD_X };
    static const input_usage_t MOUSE_Y = { HID_PAGE_GENERIC_DESKTOP, HID_USAGE_GD_Y };
    unsigned overflow = 0U;

    report_filter_reset();
    for (size_t stick = 0U; stick < OUTPUT_STICK_COUNT; ++stick) {
        const output_stick_config_t *config = &s_config.stick[stick];
        if (config->source == OUTPUT_STICK_SOURCE_MOUSE) {
            add_rule(MOUSE_X, &overflow);
            add_rule(MOUSE_Y, &overflow);
        } else if (config->source == OUTPUT_STICK_SOURCE_KEYS) {
            add_rule(config->up, &overflow);
            add_rule(config->down, &overflow);
            add_rule(config->left, &overflow);
            add_rule(config->right, &overflow);
        }
    }
    for (size_t trigger = 0U; trigger < OUTPUT_TRIGGER_COUNT; ++trigger) {
        add_rule(s_config.trigger[trigger], &overflow);
    }
    for (uint8_t index = 0U; index < s_config.gpio_count; ++index) {
        add_rule(s_config.gpio[index].usage, &overflow);
    }

    if (overflow > 0U) {
        ESP_LOGE(TAG, "Report filter: %u usage(s) above limit %u not neutralized",
            overflow, (unsigned)REPORT_FILTER_MAX_USAGES);
    }
    ESP_LOGI(TAG, "Report filter: %u mapped usage(s), %s",
        (unsigned)report_filter_rule_count(),
        CONFIG_USB_GATEWAY_INTERCEPT_ENABLE ? "neutralized in GENERATE" : "disabled (Kconfig)");
}

/* ---------------------------------------------------------------------- */
/* PASSTHROUGH / LEARN                                                    */
/* ---------------------------------------------------------------------- */

/* Reads the node voltages (Hall sensors, or the DAC in GENERATE). */
static void sample_adc(uint16_t codes[ANALOG_HID_COUNT], bool valid[ANALOG_HID_COUNT])
{
    const bool ready = adc_ready();
    for (uint8_t channel = 0U; channel < ANALOG_HID_COUNT; ++channel) {
        uint16_t code = 0U;
        valid[channel] = ready &&
            analog_vector_io_read_channel(s_io, (analog_hid_channel_t)channel, &code) == ESP_OK;
        if (valid[channel]) {
            s_codes[channel] = code;
        } else if (ready) {
            ++s_adc_errors;
        }
        codes[channel] = code;
    }
}

static void learn_start(void)
{
    s_backup = malloc(sizeof(*s_backup));
    if (s_backup == NULL) {
        ESP_LOGE(TAG, "Learning refused: no memory for profile backup");
        s_learn_requested = false;
        return;
    }
    *s_backup = *s_profile;

    learned_motion_model_reset(&s_profile->learned);
    for (size_t stick = 0U; stick < OUTPUT_STICK_COUNT; ++stick) {
        radial_envelope_reset(&s_envelope[stick], stick_calibration(stick)->safety_factor);
        kinematic_estimator_init(&s_estimator[stick], ESTIMATOR_ALPHA);
    }

    s_mode = ANALOG_ENGINE_LEARN;
    profile_feedback_set(PROFILE_FEEDBACK_LEARNING);
    ESP_LOGI(TAG, "Learning started: move both sticks over the full range");
}

static void learn_sample(
    const uint16_t codes[ANALOG_HID_COUNT],
    const bool valid[ANALOG_HID_COUNT],
    uint64_t now_us)
{
    for (size_t stick = 0U; stick < OUTPUT_STICK_COUNT; ++stick) {
        const uint8_t channel_x = STICK_CHANNEL_X[stick];
        const uint8_t channel_y = STICK_CHANNEL_Y[stick];
        if (!valid[channel_x] || !valid[channel_y]) {
            continue;
        }
        const motion_stick_calibration_t *calibration = stick_calibration(stick);
        const motion_vec2_t position = {
            .x = analog_mapping_code_to_axis(&calibration->x, codes[channel_x],
                s_profile->channel[channel_x].invert),
            .y = analog_mapping_code_to_axis(&calibration->y, codes[channel_y],
                s_profile->channel[channel_y].invert),
        };
        radial_envelope_observe(&s_envelope[stick], position);
        const motion_state2_t state = kinematic_estimator_update(
            &s_estimator[stick], position, now_us);
        learned_motion_model_observe(&s_profile->learned, &state);
    }
}

static void save_task(void *argument)
{
    (void)argument;
    profile_feedback_set(PROFILE_FEEDBACK_EXPORT_WRITING);
    s_last_save_result = motion_profile_save_json_atomic(s_profile_path, s_profile);
    if (s_last_save_result == ESP_OK) {
        ESP_LOGI(TAG, "Learned profile saved: %s", s_profile_path);
        profile_feedback_set(PROFILE_FEEDBACK_EXPORT_SUCCESS);
    } else {
        ESP_LOGE(TAG, "Saving learned profile failed: %s", esp_err_to_name(s_last_save_result));
        profile_feedback_set(PROFILE_FEEDBACK_EXPORT_ERROR);
    }
    s_saving = false;
    vTaskDelete(NULL);
}

static void learn_stop(void)
{
    const uint64_t samples = s_profile->learned.total_samples;
    if (samples < LEARN_MIN_SAMPLES) {
        *s_profile = *s_backup;
        ESP_LOGW(TAG, "Learning discarded: %llu samples < %u, previous profile restored",
            (unsigned long long)samples, (unsigned)LEARN_MIN_SAMPLES);
        profile_feedback_set(PROFILE_FEEDBACK_READY);
    } else {
        for (size_t stick = 0U; stick < OUTPUT_STICK_COUNT; ++stick) {
            radial_envelope_finalize(&s_envelope[stick]);
            motion_stick_calibration_t *calibration = stick == OUTPUT_STICK_LEFT
                ? &s_profile->left_stick
                : &s_profile->right_stick;
            memcpy(calibration->radial_envelope, s_envelope[stick].radius,
                sizeof(calibration->radial_envelope));
        }
        ESP_LOGI(TAG, "Learning finished: %llu samples", (unsigned long long)samples);
        s_saving = true;
        if (s_profile_path == NULL ||
            xTaskCreate(save_task, "profile_save", SAVE_TASK_STACK, NULL,
                SAVE_TASK_PRIORITY, NULL) != pdPASS) {
            s_saving = false;
            s_last_save_result = ESP_ERR_NO_MEM;
            ESP_LOGE(TAG, "Profile not saved (learned data stays in RAM)");
            profile_feedback_set(PROFILE_FEEDBACK_EXPORT_ERROR);
        }
    }
    free(s_backup);
    s_backup = NULL;
}

/* DAC fault during learning: samples may stem from a driven node, never keep them. */
static void learn_abort(void)
{
    if (s_backup != NULL) {
        *s_profile = *s_backup;
        free(s_backup);
        s_backup = NULL;
    }
    ESP_LOGW(TAG, "Learning aborted by DAC fault, previous profile restored");
}

/* ---------------------------------------------------------------------- */
/* GENERATE                                                               */
/* ---------------------------------------------------------------------- */

static float key_axis(const input_state_t *input, input_usage_t positive, input_usage_t negative)
{
    return (input_state_is_pressed(input, positive) ? 1.0f : 0.0f) -
        (input_state_is_pressed(input, negative) ? 1.0f : 0.0f);
}

static void generate_enter(uint64_t now_us)
{
    input_state_t discard;
    report_tap_take_snapshot(&discard);

    for (size_t stick = 0U; stick < OUTPUT_STICK_COUNT; ++stick) {
        s_target[stick] = (motion_vec2_t) { 0 };
        trajectory_generator_init(&s_trajectory[stick], (motion_vec2_t) { 0 }, now_us);
    }
    human_motion_model_init(&s_model, &s_profile->learned);
    s_model.tuning = s_profile->tuning;
}

static void generate(uint64_t now_us, float dt)
{
    input_state_t input;
    report_tap_take_snapshot(&input);

    for (size_t stick = 0U; stick < OUTPUT_STICK_COUNT; ++stick) {
        const output_stick_config_t *config = &s_config.stick[stick];
        motion_vec2_t target = s_target[stick];

        switch (config->source) {
            case OUTPUT_STICK_SOURCE_MOUSE: {
                /* Mouse Y grows downwards, stick Y grows forwards. */
                target.x += input.mouse_dx * config->gain;
                target.y -= input.mouse_dy * config->gain;
                float keep = 1.0f - config->return_rate * dt;
                keep = keep < 0.0f ? 0.0f : keep;
                target.x *= keep;
                target.y *= keep;
                break;
            }
            case OUTPUT_STICK_SOURCE_KEYS:
                target.x = key_axis(&input, config->right, config->left);
                target.y = key_axis(&input, config->up, config->down);
                break;
            case OUTPUT_STICK_SOURCE_NONE:
            default:
                target = (motion_vec2_t) { 0 };
                break;
        }

        s_target[stick] = analog_mapping_unit_circle(target);
        trajectory_generator_set_target(&s_trajectory[stick], s_target[stick]);
        const motion_state2_t state = trajectory_generator_step(
            &s_trajectory[stick], &s_model, now_us);

        const motion_stick_calibration_t *calibration = stick_calibration(stick);
        const motion_vec2_t physical = analog_mapping_envelope_scale(
            calibration->radial_envelope,
            calibration->safety_factor,
            state.position);

        const uint8_t channel_x = STICK_CHANNEL_X[stick];
        const uint8_t channel_y = STICK_CHANNEL_Y[stick];
        write_dac(channel_x, analog_mapping_axis_to_code(
            &calibration->x, physical.x, s_profile->channel[channel_x].invert));
        write_dac(channel_y, analog_mapping_axis_to_code(
            &calibration->y, physical.y, s_profile->channel[channel_y].invert));
    }

    for (size_t trigger = 0U; trigger < OUTPUT_TRIGGER_COUNT; ++trigger) {
        const uint8_t channel = TRIGGER_CHANNEL[trigger];
        if (s_config.trigger[trigger].usage == 0U) {
            write_dac(channel, s_profile->channel[channel].neutral_code);
            continue;
        }
        const float value = input_state_is_pressed(&input, s_config.trigger[trigger]) ? 1.0f : 0.0f;
        write_dac(channel, analog_mapping_trigger_to_code(
            &s_profile->trigger[trigger], value, s_profile->channel[channel].invert));
    }

    for (uint8_t index = 0U; index < s_config.gpio_count; ++index) {
        const output_gpio_binding_t *binding = &s_config.gpio[index];
        (void)gpio_output_set(binding->output, input_state_is_pressed(&input, binding->usage));
    }
}

#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
/*
 * Direct DAC test independent of HID input and learned motion data.
 * The configured key stick uses one DAC output per axis, not one per key:
 *     left/right (A/D) -> stick X
 *     up/down (W/S)    -> stick Y
 * The configured mouse stick uses its X/Y DAC outputs for sine/cosine.
 */
static float simulate_phase(uint64_t now_us)
{
    const uint64_t period_us =
        (uint64_t)(1000000.0f / SIMULATE_FREQUENCY_HZ);
    return (float)(now_us % period_us) / (float)period_us;
}

static void simulate(uint64_t now_us)
{
    const float phase = simulate_phase(now_us);
    const float angle = SIMULATE_TWO_PI * phase;
    const float sine = sinf(angle);
    const float cosine = cosf(angle);
    const float rising_saw = 2.0f * phase - 1.0f;
    const float falling_saw = 1.0f - 2.0f * phase;

    for (size_t stick = 0U; stick < OUTPUT_STICK_COUNT; ++stick) {
        const output_stick_config_t *config = &s_config.stick[stick];
        const motion_stick_calibration_t *calibration = stick_calibration(stick);
        const uint8_t channel_x = STICK_CHANNEL_X[stick];
        const uint8_t channel_y = STICK_CHANNEL_Y[stick];

        float x = 0.0f;
        float y = 0.0f;
        if (config->source == OUTPUT_STICK_SOURCE_MOUSE) {
            x = sine;
            y = cosine;
        } else if (config->source == OUTPUT_STICK_SOURCE_KEYS) {
            x = rising_saw;
            y = falling_saw;
        } else {
            continue;
        }

        write_dac(channel_x, analog_mapping_axis_to_code(
            &calibration->x, x, s_profile->channel[channel_x].invert));
        write_dac(channel_y, analog_mapping_axis_to_code(
            &calibration->y, y, s_profile->channel[channel_y].invert));
    }
}
#endif

/* ---------------------------------------------------------------------- */
/* Mode                                                                   */
/* ---------------------------------------------------------------------- */

static analog_engine_mode_t wanted_mode(void)
{
    if (s_profile == NULL || s_fault != ANALOG_OUTPUT_FAULT_NONE) {
        return ANALOG_ENGINE_PASSTHROUGH;
    }
    if (s_learn_requested) {
        return ANALOG_ENGINE_LEARN;
    }
    const usb_gateway_mode_t gateway_mode = hotkey_get_mode();
#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
    if (gateway_mode == USB_GATEWAY_MODE_SIMULATE) {
        return generate_possible()
            ? ANALOG_ENGINE_SIMULATE
            : ANALOG_ENGINE_PASSTHROUGH;
    }
#endif
    return gateway_mode == USB_GATEWAY_MODE_PROXY && generate_possible()
        ? ANALOG_ENGINE_GENERATE
        : ANALOG_ENGINE_PASSTHROUGH;
}

static void switch_mode(analog_engine_mode_t next, uint64_t now_us)
{
    if (next == s_mode) {
        return;
    }

    if (s_mode == ANALOG_ENGINE_LEARN) {
        learn_stop();
    }
    if (s_mode == ANALOG_ENGINE_GENERATE
#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
        || s_mode == ANALOG_ENGINE_SIMULATE
#endif
    ) {
        gpio_output_all_off();
        if (!dac_release()) {
            /* DAC state unknown: keep the safe codes on the outputs. */
            for (uint8_t channel = 0U; channel < ANALOG_HID_COUNT; ++channel) {
                write_dac(channel, safe_code(channel));
            }
        }
    }
    s_mode = ANALOG_ENGINE_PASSTHROUGH;

    if (next == ANALOG_ENGINE_LEARN) {
        if (s_saving) {
            ESP_LOGW(TAG, "Learning refused while the profile is being saved");
            s_learn_requested = false;
        } else {
            learn_start();      /* sets LEARN on success */
        }
    } else if (next == ANALOG_ENGINE_GENERATE) {
        generate_enter(now_us);
        if (dac_drive(true)) {
            s_mode = ANALOG_ENGINE_GENERATE;
        } else {
            (void)dac_release();
        }
#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
    } else if (next == ANALOG_ENGINE_SIMULATE) {
        if (dac_drive(false)) {
            s_mode = ANALOG_ENGINE_SIMULATE;
        } else {
            (void)dac_release();
        }
#endif
    }

    ESP_LOGI(TAG, "Mode %s", analog_engine_mode_name(s_mode));
}

/* Executed after a DAC fault while learning. */
static void handle_fault(void)
{
    if (s_mode == ANALOG_ENGINE_LEARN) {
        learn_abort();
        s_mode = ANALOG_ENGINE_PASSTHROUGH;
    }
    s_learn_requested = false;
}

/* ---------------------------------------------------------------------- */
/* Task                                                                   */
/* ---------------------------------------------------------------------- */

static void engine_task(void *argument)
{
    (void)argument;
    const TickType_t period = pdMS_TO_TICKS(s_config.tick_ms) > 0U
        ? pdMS_TO_TICKS(s_config.tick_ms)
        : 1U;
    const float dt = (float)s_config.tick_ms * 1e-3f;
    TickType_t wake = xTaskGetTickCount();
    uint16_t codes[ANALOG_HID_COUNT];
    bool valid[ANALOG_HID_COUNT];

    while (true) {
        if (xTaskDelayUntil(&wake, period) == pdFALSE) {
            ++s_overruns;
        }
        ++s_ticks;

        if (s_fault != ANALOG_OUTPUT_FAULT_NONE) {
            handle_fault();
        }

        const uint64_t now_us = (uint64_t)esp_timer_get_time();
        switch_mode(wanted_mode(), now_us);

        switch (s_mode) {
            case ANALOG_ENGINE_LEARN:
                sample_adc(codes, valid);
                learn_sample(codes, valid, now_us);
                break;
            case ANALOG_ENGINE_GENERATE:
                generate(now_us, dt);
                /* ADC reads back the driven nodes (diagnostics only). */
                sample_adc(codes, valid);
                break;
#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
            case ANALOG_ENGINE_SIMULATE:
                simulate(now_us);
                /* ADC reads back the direct DAC test waveforms. */
                sample_adc(codes, valid);
                break;
#endif
            case ANALOG_ENGINE_PASSTHROUGH:
            default:
                sample_adc(codes, valid);
                break;
        }
    }
}

static bool hotkey_matches(const output_hotkey_t *hotkey, input_usage_t usage, uint8_t modifiers)
{
    return hotkey->used &&
        usage.usage_page == HID_PAGE_KEYBOARD &&
        usage.usage == hotkey->usage &&
        (modifiers & hotkey->modifiers) == hotkey->modifiers;
}

/* report_tap edge handler (tap task context). */
static void on_key(input_usage_t usage, bool pressed, uint8_t modifiers)
{
    if (!pressed) {
        return;
    }
    if (hotkey_matches(&s_config.mode_hotkey, usage, modifiers)) {
        const usb_gateway_mode_t current = hotkey_get_mode();
        usb_gateway_mode_t next = USB_GATEWAY_MODE_PASSTHROUGH;
        if (current == USB_GATEWAY_MODE_PASSTHROUGH) {
            next = USB_GATEWAY_MODE_PROXY;
#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
        } else if (current == USB_GATEWAY_MODE_PROXY) {
            next = USB_GATEWAY_MODE_SIMULATE;
#endif
        }
        hotkey_set_mode(next);
        ESP_LOGI(TAG, "Hotkey: gateway mode %s", hotkey_mode_name(next));
    } else if (hotkey_matches(&s_config.learn_hotkey, usage, modifiers)) {
        analog_engine_request_learning(!s_learn_requested);
    }
}

esp_err_t analog_engine_start(
    analog_vector_io_t *io,
    motion_profile_t *profile,
    const char *profile_path,
    const output_config_t *config)
{
    if (io == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_io = io;
    s_profile = profile;
    s_profile_path = profile_path;
    s_config = *config;
    s_dac_ready = DAC_FITTED && io->dac.enabled;
    s_dac_driving = s_dac_ready;    /* unknown until high-Z is confirmed */

    gpio_output_all_off();
    build_filter_rules();

    /* Start in PASSTHROUGH: DAC released and confirmed, whatever init did before. */
    if (!dac_release()) {
        ESP_LOGE(TAG, "HIGH-Z-CMD failed at start (power read-back=0x%04X)",
            (unsigned)s_dac_power_raw);
    }

    report_tap_set_edge_handler(on_key);

    if (xTaskCreate(engine_task, "analog_engine", ENGINE_TASK_STACK, NULL,
            ENGINE_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG,
        "Analog engine started: tick=%lu ms, ADC=%s, DAC=%s (power=0x%04X), "
        "profile=%s, output.json=%s",
        (unsigned long)s_config.tick_ms,
        adc_ready() ? "yes" : "no",
        !s_dac_ready ? "no (PASSTHROUGH only)"
            : s_fault != ANALOG_OUTPUT_FAULT_NONE ? "FAULT" : "high-Z",
        (unsigned)s_dac_power_raw,
        s_profile != NULL ? "yes" : "no (PASSTHROUGH only)",
        s_config.from_file ? "yes" : "no");
    return ESP_OK;
}

void analog_engine_request_learning(bool start)
{
    if (start && s_profile == NULL) {
        ESP_LOGW(TAG, "Learning requires a valid profile");
        return;
    }
    if (start && s_fault != ANALOG_OUTPUT_FAULT_NONE) {
        ESP_LOGW(TAG, "Learning refused: DAC fault");
        return;
    }
    s_learn_requested = start;
    ESP_LOGI(TAG, "Learning %s requested", start ? "start" : "stop");
}

void analog_engine_get_status(analog_engine_status_t *status)
{
    if (status == NULL) {
        return;
    }

    *status = (analog_engine_status_t) {
        .mode = s_mode,
        .dac_driving = s_dac_driving,
        .fault = s_fault,
        .dac_power_raw = s_dac_power_raw,
        .saving = s_saving,
        .ticks = s_ticks,
        .overruns = s_overruns,
        .adc_errors = s_adc_errors,
        .dac_errors = s_dac_errors,
        .learned_samples = s_profile != NULL ? s_profile->learned.total_samples : 0U,
        .last_save_result = s_last_save_result,
    };
    memcpy(status->adc_code, s_codes, sizeof(status->adc_code));
}

const char *analog_engine_mode_name(analog_engine_mode_t mode)
{
    switch (mode) {
#if CONFIG_USB_GATEWAY_SIMULATE_ENABLE
        case ANALOG_ENGINE_SIMULATE:
            return "SIMULATE";
#endif
        case ANALOG_ENGINE_GENERATE:
            return "GENERATE";
        case ANALOG_ENGINE_LEARN:
            return "LEARN";
        case ANALOG_ENGINE_PASSTHROUGH:
        default:
            return "PASSTHROUGH";
    }
}

const char *analog_output_fault_name(analog_output_fault_t fault)
{
    switch (fault) {
        case ANALOG_OUTPUT_FAULT_NONE:
            return "NONE";
        case ANALOG_OUTPUT_FAULT_DAC_HIGH_Z:
            return "HIGH-Z-CMD";
        case ANALOG_OUTPUT_FAULT_DAC_WRITE:
            return "DAC-WR";
        default:
            return "UNKNOWN";
    }
}
