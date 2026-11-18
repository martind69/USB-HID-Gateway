#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum{USB_PROXY_REPORT_OUTPUT=2,USB_PROXY_REPORT_FEATURE=3}usb_proxy_report_type_t;
typedef esp_err_t(*usb_proxy_host_set_report_fn)(uint8_t,uint8_t,usb_proxy_report_type_t,const uint8_t*,size_t);
typedef esp_err_t(*usb_proxy_host_get_report_fn)(uint8_t,uint8_t,usb_proxy_report_type_t,uint8_t*,size_t*);
typedef esp_err_t(*usb_proxy_host_control_fn)(const uint8_t setup[8],uint8_t*,size_t*,bool);
void usb_proxy_control_init(void);void usb_proxy_control_register_host(usb_proxy_host_set_report_fn set_fn,usb_proxy_host_get_report_fn get_fn,usb_proxy_host_control_fn control_fn);
esp_err_t usb_proxy_control_from_s3(uint8_t message_type,const uint8_t*payload,size_t length);
