#include "gpio_output.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
static const int s_gpio[GPIO_OUTPUT_COUNT]={
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_0_GPIO,CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_1_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_2_GPIO,CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_3_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_4_GPIO,CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_5_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_6_GPIO,CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_7_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_8_GPIO,CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_9_GPIO,
    CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_10_GPIO,CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_11_GPIO};
static uint16_t s_configured;
static int output_level(bool state){return state!=CONFIG_USB_GATEWAY_DIGITAL_OUTPUT_ACTIVE_LOW?1:0;}
esp_err_t gpio_output_init(void){s_configured=0;for(uint8_t i=0;i<GPIO_OUTPUT_COUNT;i++){if(s_gpio[i]<0)continue;gpio_config_t c={.pin_bit_mask=1ULL<<s_gpio[i],.mode=GPIO_MODE_OUTPUT,.intr_type=GPIO_INTR_DISABLE};esp_err_t e=gpio_config(&c);if(e!=ESP_OK){gpio_output_all_off();return e;}s_configured|=(uint16_t)(1U<<i);gpio_set_level(s_gpio[i],output_level(false));}return ESP_OK;}
esp_err_t gpio_output_set(uint8_t output,bool state){if(output>=GPIO_OUTPUT_COUNT)return ESP_ERR_INVALID_ARG;if((s_configured&(1U<<output))==0U)return ESP_ERR_NOT_SUPPORTED;return gpio_set_level(s_gpio[output],output_level(state));}
void gpio_output_all_off(void){for(uint8_t i=0;i<GPIO_OUTPUT_COUNT;i++)if(s_configured&(1U<<i))gpio_set_level(s_gpio[i],output_level(false));}
