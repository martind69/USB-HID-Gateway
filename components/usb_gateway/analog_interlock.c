#include "analog_interlock.h"

#include "analog_control_gpio.h"
#include "profile_feedback.h"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#ifndef CONFIG_USB_GATEWAY_INTERLOCK_GUARD_US
#define CONFIG_USB_GATEWAY_INTERLOCK_GUARD_US 100
#endif

#ifndef CONFIG_USB_GATEWAY_INTERLOCK_SETTLE_US
#define CONFIG_USB_GATEWAY_INTERLOCK_SETTLE_US 1000
#endif

#ifndef CONFIG_USB_GATEWAY_INTERLOCK_OFF_US
#define CONFIG_USB_GATEWAY_INTERLOCK_OFF_US 1000
#endif

#ifndef CONFIG_USB_GATEWAY_INTERLOCK_DAC_VERIFY_TICKS
#define CONFIG_USB_GATEWAY_INTERLOCK_DAC_VERIFY_TICKS 1
#endif

#ifndef CONFIG_USB_GATEWAY_INTERLOCK_ADC_VERIFY_MS
#define CONFIG_USB_GATEWAY_INTERLOCK_ADC_VERIFY_MS 1000
#endif

#ifndef CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_ACTIVE_LOW
#define CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_ACTIVE_LOW 0
#endif

#if CONFIG_USB_GATEWAY_DAC7578_ENABLE
#define DAC_FITTED true
#else
#define DAC_FITTED false
#endif

#define ADC_VERIFY_PERIOD_US ((int64_t)CONFIG_USB_GATEWAY_INTERLOCK_ADC_VERIFY_MS * 1000LL)

static const char *TAG = "interlock";

static analog_vector_io_t *s_io;
static bool s_dac_ready;            /* DAC fitted and acknowledged */
static bool s_adc_present;          /* ADS7128 acknowledged */
static volatile analog_interlock_state_t s_state = ANALOG_INTERLOCK_SAFE;
static volatile analog_interlock_fault_t s_fault = ANALOG_INTERLOCK_FAULT_NONE;
static uint32_t s_dac_verify_ticks;
static int64_t s_next_adc_verify_us;
static uint16_t s_dac_power_raw;

/* ---------------------------------------------------------------------- */
/* Fault                                                                  */
/* ---------------------------------------------------------------------- */

static void enter_fault(analog_interlock_fault_t fault, esp_err_t error)
{
    /* Best effort; the GPIO is always switchable, the DAC may not answer. */
    (void)analog_joystick_enable_set(false);
    if (s_dac_ready) {
        (void)dac7578_set_power(&s_io->dac, DAC7578_ALL_CHANNELS, DAC7578_POWER_DOWN_HIGH_Z);
    }

    if (s_state != ANALOG_INTERLOCK_FAULT) {
        ESP_LOGE(TAG,
            "FAULT %s (%s, DAC power=0x%04X): joystick supply off, DAC high-Z "
            "requested, latched until reset",
            analog_interlock_fault_name(fault), esp_err_to_name(error),
            (unsigned)s_dac_power_raw);
        s_fault = fault;
    }
    s_state = ANALOG_INTERLOCK_FAULT;
    profile_feedback_set(PROFILE_FEEDBACK_ANALOG_ERROR);
}

/* ---------------------------------------------------------------------- */
/* Confirmations                                                          */
/* ---------------------------------------------------------------------- */

static esp_err_t verify_dac_high_z(void)
{
    if (!DAC_FITTED) {
        /* No DAC on the nodes: nothing can drive against the joystick. */
        return ESP_OK;
    }
    if (!s_dac_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return dac7578_verify_power(
        &s_io->dac, DAC7578_ALL_CHANNELS, DAC7578_POWER_DOWN_HIGH_Z, &s_dac_power_raw);
}

static esp_err_t verify_adc_inputs(void)
{
    if (!s_adc_present) {
        return ESP_OK;
    }

    ads7128_pin_config_t config = { 0 };
    const esp_err_t error = ads7128_verify_inputs(&s_io->adc, &config);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "ADS7128 PIN_CFG=0x%02X GPIO_CFG=0x%02X GPO_DRIVE_CFG=0x%02X: %s",
            config.pin_cfg, config.gpio_cfg, config.gpo_drive_cfg, esp_err_to_name(error));
    }
    return error;
}

/* Joystick supply off and DAC high-Z, both confirmed. false: FAULT entered. */
static bool make_safe(void)
{
    if (analog_joystick_enable_set(false) != ESP_OK || analog_joystick_enable_readback()) {
        enter_fault(ANALOG_INTERLOCK_FAULT_ENABLE_GPIO, ESP_ERR_INVALID_STATE);
        return false;
    }

    if (s_dac_ready) {
        esp_err_t error = dac7578_set_power(
            &s_io->dac, DAC7578_ALL_CHANNELS, DAC7578_POWER_DOWN_HIGH_Z);
        if (error == ESP_OK) {
            error = verify_dac_high_z();
        }
        if (error != ESP_OK) {
            enter_fault(ANALOG_INTERLOCK_FAULT_DAC_HIGH_Z, error);
            return false;
        }
    }

    s_state = ANALOG_INTERLOCK_SAFE;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                             */
/* ---------------------------------------------------------------------- */

esp_err_t analog_interlock_init(analog_vector_io_t *io)
{
    if (io == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_io = io;
    s_dac_ready = DAC_FITTED && io->dac.enabled;
    s_adc_present = io->adc.present;
    s_state = ANALOG_INTERLOCK_SAFE;
    s_fault = ANALOG_INTERLOCK_FAULT_NONE;

    if (DAC_FITTED && !s_dac_ready) {
        enter_fault(ANALOG_INTERLOCK_FAULT_DAC_MISSING, ESP_ERR_NOT_FOUND);
        return ESP_ERR_INVALID_STATE;
    }
    if (!make_safe()) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t error = verify_adc_inputs();
    if (error != ESP_OK) {
        enter_fault(ANALOG_INTERLOCK_FAULT_ADC_CONFIG, error);
        return ESP_ERR_INVALID_STATE;
    }
    s_next_adc_verify_us = esp_timer_get_time() + ADC_VERIFY_PERIOD_US;

    ESP_LOGI(TAG,
        "SAFE: joystick enable GPIO%d %s off, DAC %s (power=0x%04X), ADC %s; "
        "guard=%d us settle=%d us off=%d us",
        CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_GPIO,
        CONFIG_USB_GATEWAY_JOYSTICK_ENABLE_ACTIVE_LOW ? "active-low" : "active-high",
        s_dac_ready ? "high-Z confirmed" : "not fitted",
        (unsigned)s_dac_power_raw,
        s_adc_present ? "inputs confirmed" : "not present",
        CONFIG_USB_GATEWAY_INTERLOCK_GUARD_US,
        CONFIG_USB_GATEWAY_INTERLOCK_SETTLE_US,
        CONFIG_USB_GATEWAY_INTERLOCK_OFF_US);
    return ESP_OK;
}

esp_err_t analog_interlock_enter_safe(void)
{
    if (s_state == ANALOG_INTERLOCK_FAULT) {
        return ESP_ERR_INVALID_STATE;
    }
    return make_safe() ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t analog_interlock_enter_joystick(void)
{
    if (s_state == ANALOG_INTERLOCK_FAULT) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state == ANALOG_INTERLOCK_JOYSTICK_ACTIVE) {
        return ESP_OK;
    }
    if (!make_safe()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_rom_delay_us(CONFIG_USB_GATEWAY_INTERLOCK_GUARD_US);

    /* Confirm again directly before switching the supply on. */
    const esp_err_t error = verify_dac_high_z();
    if (error != ESP_OK) {
        enter_fault(ANALOG_INTERLOCK_FAULT_DAC_HIGH_Z, error);
        return ESP_ERR_INVALID_STATE;
    }

    if (analog_joystick_enable_set(true) != ESP_OK || !analog_joystick_enable_readback()) {
        enter_fault(ANALOG_INTERLOCK_FAULT_ENABLE_GPIO, ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    esp_rom_delay_us(CONFIG_USB_GATEWAY_INTERLOCK_SETTLE_US);

    s_dac_verify_ticks = 0U;
    s_state = ANALOG_INTERLOCK_JOYSTICK_ACTIVE;
    ESP_LOGI(TAG, "JOYSTICK_ACTIVE: DAC high-Z (0x%04X), joystick supply on",
        (unsigned)s_dac_power_raw);
    return ESP_OK;
}

esp_err_t analog_interlock_enter_dac(const uint16_t codes[ANALOG_HID_COUNT])
{
    if (codes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state == ANALOG_INTERLOCK_FAULT) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_dac_ready) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (s_state == ANALOG_INTERLOCK_DAC_ACTIVE) {
        return ESP_OK;
    }

    const bool joystick_was_on = s_state == ANALOG_INTERLOCK_JOYSTICK_ACTIVE;
    if (!make_safe()) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Hall outputs and supply capacitors discharge before the DAC drives. */
    if (joystick_was_on) {
        esp_rom_delay_us(CONFIG_USB_GATEWAY_INTERLOCK_OFF_US);
    }
    esp_rom_delay_us(CONFIG_USB_GATEWAY_INTERLOCK_GUARD_US);

    if (analog_joystick_enable_readback()) {
        enter_fault(ANALOG_INTERLOCK_FAULT_ENABLE_GPIO, ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    /* Preload while the outputs are still high-Z: power-up starts at these codes. */
    for (uint8_t channel = 0U; channel < ANALOG_HID_COUNT; ++channel) {
        const esp_err_t error = analog_vector_io_write_channel(
            s_io, (analog_hid_channel_t)channel, codes[channel]);
        if (error != ESP_OK) {
            enter_fault(ANALOG_INTERLOCK_FAULT_DAC_WRITE, error);
            return ESP_ERR_INVALID_STATE;
        }
    }

    const esp_err_t error = dac7578_set_power(
        &s_io->dac, DAC7578_ALL_CHANNELS, DAC7578_POWER_UP);
    if (error != ESP_OK) {
        enter_fault(ANALOG_INTERLOCK_FAULT_DAC_WRITE, error);
        return ESP_ERR_INVALID_STATE;
    }

    s_state = ANALOG_INTERLOCK_DAC_ACTIVE;
    ESP_LOGI(TAG, "DAC_ACTIVE: joystick supply off, DAC powered up");
    return ESP_OK;
}

void analog_interlock_check(void)
{
    switch (s_state) {
        case ANALOG_INTERLOCK_JOYSTICK_ACTIVE:
            if (!analog_joystick_enable_readback()) {
                enter_fault(ANALOG_INTERLOCK_FAULT_ENABLE_GPIO, ESP_ERR_INVALID_STATE);
                return;
            }
            if (++s_dac_verify_ticks >= (uint32_t)CONFIG_USB_GATEWAY_INTERLOCK_DAC_VERIFY_TICKS) {
                s_dac_verify_ticks = 0U;
                const esp_err_t error = verify_dac_high_z();
                if (error != ESP_OK) {
                    enter_fault(ANALOG_INTERLOCK_FAULT_DAC_HIGH_Z, error);
                    return;
                }
            }
            break;
        case ANALOG_INTERLOCK_DAC_ACTIVE:
            if (analog_joystick_enable_readback()) {
                enter_fault(ANALOG_INTERLOCK_FAULT_ENABLE_GPIO, ESP_ERR_INVALID_STATE);
                return;
            }
            break;
        case ANALOG_INTERLOCK_FAULT:
            return;
        case ANALOG_INTERLOCK_SAFE:
        default:
            break;
    }

    const int64_t now = esp_timer_get_time();
    if (now >= s_next_adc_verify_us) {
        s_next_adc_verify_us = now + ADC_VERIFY_PERIOD_US;
        const esp_err_t error = verify_adc_inputs();
        if (error != ESP_OK) {
            enter_fault(ANALOG_INTERLOCK_FAULT_ADC_CONFIG, error);
        }
    }
}

analog_interlock_state_t analog_interlock_get_state(void)
{
    return s_state;
}

analog_interlock_fault_t analog_interlock_get_fault(void)
{
    return s_fault;
}

bool analog_interlock_dac_available(void)
{
    return s_dac_ready && s_state != ANALOG_INTERLOCK_FAULT;
}

const char *analog_interlock_state_name(analog_interlock_state_t state)
{
    switch (state) {
        case ANALOG_INTERLOCK_SAFE:
            return "SAFE";
        case ANALOG_INTERLOCK_DAC_ACTIVE:
            return "DAC_ACTIVE";
        case ANALOG_INTERLOCK_JOYSTICK_ACTIVE:
            return "JOYSTICK_ACTIVE";
        case ANALOG_INTERLOCK_FAULT:
            return "FAULT";
        default:
            return "UNKNOWN";
    }
}

const char *analog_interlock_fault_name(analog_interlock_fault_t fault)
{
    switch (fault) {
        case ANALOG_INTERLOCK_FAULT_NONE:
            return "NONE";
        case ANALOG_INTERLOCK_FAULT_DAC_MISSING:
            return "DAC-MISS";
        case ANALOG_INTERLOCK_FAULT_DAC_HIGH_Z:
            return "DAC-HIZ";
        case ANALOG_INTERLOCK_FAULT_DAC_WRITE:
            return "DAC-WR";
        case ANALOG_INTERLOCK_FAULT_ADC_CONFIG:
            return "ADC-CFG";
        case ANALOG_INTERLOCK_FAULT_ENABLE_GPIO:
            return "EN-GPIO";
        default:
            return "UNKNOWN";
    }
}
