#include "profile_feedback.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
esp_err_t profile_feedback_init(void){gpio_config_t c={.pin_bit_mask=(1ULL<<CONFIG_USB_GATEWAY_STATUS_LED_BUSY_GPIO)|(1ULL<<CONFIG_USB_GATEWAY_STATUS_LED_OK_GPIO)|(1ULL<<CONFIG_USB_GATEWAY_STATUS_LED_ERROR_GPIO),.mode=GPIO_MODE_OUTPUT};return gpio_config(&c);}void profile_feedback_set(profile_feedback_state_t s){gpio_set_level(CONFIG_USB_GATEWAY_STATUS_LED_BUSY_GPIO,s==PROFILE_FEEDBACK_LEARNING||s==PROFILE_FEEDBACK_EXPORT_WRITING);gpio_set_level(CONFIG_USB_GATEWAY_STATUS_LED_OK_GPIO,s==PROFILE_FEEDBACK_EXPORT_SUCCESS);gpio_set_level(CONFIG_USB_GATEWAY_STATUS_LED_ERROR_GPIO,s==PROFILE_FEEDBACK_EXPORT_ERROR||s==PROFILE_FEEDBACK_ANALOG_ERROR);}
