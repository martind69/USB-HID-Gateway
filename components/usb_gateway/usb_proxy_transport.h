#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum{USB_PROXY_DIR_P4_TO_S3=0,USB_PROXY_DIR_S3_TO_P4}usb_proxy_direction_t;
esp_err_t usb_proxy_transport_init(void);
bool usb_proxy_send_input(uint8_t interface_number,const uint8_t*report,size_t length);
/* Work package 6 reservation: these APIs are stable while wire fragmentation remains TODO. */
esp_err_t usb_proxy_receive_output(uint8_t interface_number,uint8_t report_id,const uint8_t*data,size_t length);
esp_err_t usb_proxy_receive_feature(uint8_t interface_number,uint8_t report_id,uint8_t*data,size_t*length,bool set_report);
esp_err_t usb_proxy_control_transfer(const uint8_t setup[8],uint8_t*data,size_t*length,bool device_to_host);
