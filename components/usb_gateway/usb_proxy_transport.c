#include "usb_proxy_transport.h"
#include "g815_forwarder.h"
esp_err_t usb_proxy_transport_init(void){g815_forwarder_init();return ESP_OK;}
bool usb_proxy_send_input(uint8_t i,const uint8_t*r,size_t n){return g815_forwarder_submit(i,r,n);}
esp_err_t usb_proxy_receive_output(uint8_t i,uint8_t id,const uint8_t*d,size_t n){(void)i;(void)id;(void)d;(void)n;return ESP_ERR_NOT_SUPPORTED;/* TODO(OUTPUT-ROUTE) */}
esp_err_t usb_proxy_receive_feature(uint8_t i,uint8_t id,uint8_t*d,size_t*n,bool set){(void)i;(void)id;(void)d;(void)n;(void)set;return ESP_ERR_NOT_SUPPORTED;/* TODO(FEATURE-ROUTE) */}
esp_err_t usb_proxy_control_transfer(const uint8_t s[8],uint8_t*d,size_t*n,bool in){(void)s;(void)d;(void)n;(void)in;return ESP_ERR_NOT_SUPPORTED;/* TODO(EP0-FRAGMENTATION) */}
