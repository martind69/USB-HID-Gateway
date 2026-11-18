#include "dac7578.h"
#include "analog_i3c_bus.h"
#include "sdkconfig.h"
#define CMD_WRITE_INPUT 0x00U
#define CMD_GLOBAL_LDAC 0x20U
#define CMD_WRITE_UPDATE 0x30U
#define TIMEOUT_MS 20
static esp_err_t send(dac7578_t*d,uint8_t c,uint16_t v){const uint8_t b[3]={c,(uint8_t)(v>>4),(uint8_t)(v<<4)};return i3c_master_i2c_device_transmit(d->device,b,sizeof b,TIMEOUT_MS);}
esp_err_t dac7578_init(dac7578_t*d,uint8_t a){if(!d)return ESP_ERR_INVALID_ARG;*d=(dac7578_t){.address=a};
#if !CONFIG_USB_GATEWAY_DAC7578_ENABLE
return ESP_ERR_NOT_SUPPORTED;
#else
esp_err_t e=analog_i3c_add_legacy_i2c(a,&d->device);if(e==ESP_OK)d->enabled=true;return e;
#endif
}
esp_err_t dac7578_write_channel(dac7578_t*d,uint8_t c,uint16_t v,bool u){if(!d||c>7||v>0xfff)return ESP_ERR_INVALID_ARG;
#if !CONFIG_USB_GATEWAY_DAC7578_ENABLE
return ESP_ERR_NOT_SUPPORTED;
#else
if(!d->enabled||!d->device)return ESP_ERR_INVALID_STATE;return send(d,(uint8_t)((u?CMD_WRITE_UPDATE:CMD_WRITE_INPUT)|c),v);
#endif
}
esp_err_t dac7578_write_pair(dac7578_t*d,uint8_t xc,uint8_t yc,uint16_t x,uint16_t y){if(xc==yc)return ESP_ERR_INVALID_ARG;esp_err_t e=dac7578_write_channel(d,xc,x,false);if(e==ESP_OK)e=dac7578_write_channel(d,yc,y,false);return e==ESP_OK?send(d,CMD_GLOBAL_LDAC,0):e;}
