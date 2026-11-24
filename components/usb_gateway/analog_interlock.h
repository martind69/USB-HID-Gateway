#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "analog_vector_io.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Output interlock between DAC7578 and the physical joystick.
 *
 * Every analog node is shared by a Hall sensor output (supplied by the
 * joystick enable GPIO), a DAC7578 output and an ADS7128 input. Two drivers
 * must never be active on a node at the same time:
 *
 *     state             DAC7578     joystick enable    ADS7128
 *     SAFE              high-Z      off                analog input
 *     DAC_ACTIVE        driving     off                analog input
 *     JOYSTICK_ACTIVE   high-Z      on                 analog input
 *     FAULT             high-Z *)   off                analog input
 *     *) requested, not confirmed
 *
 * Every transition passes SAFE. Each step is confirmed by read-back:
 *     DAC high-Z:      power-down register read-back
 *     joystick enable: pad level read-back
 *     ADS7128:         PIN_CFG / GPIO_CFG / GPO_DRIVE_CFG == 0
 *
 * Any failed confirmation latches FAULT until reset. Not thread-safe:
 * all calls except the getters come from one task (analog_engine).
 */
typedef enum {
    ANALOG_INTERLOCK_SAFE = 0,
    ANALOG_INTERLOCK_DAC_ACTIVE,
    ANALOG_INTERLOCK_JOYSTICK_ACTIVE,
    ANALOG_INTERLOCK_FAULT,
} analog_interlock_state_t;

typedef enum {
    ANALOG_INTERLOCK_FAULT_NONE = 0,
    ANALOG_INTERLOCK_FAULT_DAC_MISSING,     /* DAC enabled in Kconfig, no acknowledge */
    ANALOG_INTERLOCK_FAULT_DAC_HIGH_Z,      /* high-Z not confirmed */
    ANALOG_INTERLOCK_FAULT_DAC_WRITE,       /* preload / power-up failed */
    ANALOG_INTERLOCK_FAULT_ADC_CONFIG,      /* ADS7128 pin configuration not all inputs */
    ANALOG_INTERLOCK_FAULT_ENABLE_GPIO,     /* joystick enable read-back mismatch */
} analog_interlock_fault_t;

/*
 * Call after analog_control_gpio_init() and analog_vector_io_init().
 * Ends in SAFE or FAULT. Returns ESP_ERR_INVALID_STATE in FAULT.
 */
esp_err_t analog_interlock_init(analog_vector_io_t *io);

/* SAFE -> JOYSTICK_ACTIVE: DAC high-Z confirmed, guard time, supply on, settle time. */
esp_err_t analog_interlock_enter_joystick(void);

/*
 * SAFE -> DAC_ACTIVE: supply off confirmed, off time, guard time, codes
 * preloaded while high-Z, power-up. codes are indexed by logical channel.
 * ESP_ERR_NOT_SUPPORTED if no DAC is fitted.
 */
esp_err_t analog_interlock_enter_dac(const uint16_t codes[ANALOG_HID_COUNT]);

/* Both outputs released and confirmed. */
esp_err_t analog_interlock_enter_safe(void);

/* Cyclic supervision, call once per engine tick. */
void analog_interlock_check(void);

analog_interlock_state_t analog_interlock_get_state(void);
analog_interlock_fault_t analog_interlock_get_fault(void);

/* DAC fitted and acknowledged: DAC_ACTIVE (GENERATE) is possible. */
bool analog_interlock_dac_available(void);

const char *analog_interlock_state_name(analog_interlock_state_t state);

/* Short names, max. 8 characters (OLED). */
const char *analog_interlock_fault_name(analog_interlock_fault_t fault);

#ifdef __cplusplus
}
#endif
