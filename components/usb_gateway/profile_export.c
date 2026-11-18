#include "profile_export.h"
#include "profile_feedback.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>
typedef struct{uint8_t start;}request_t;static QueueHandle_t q;static motion_profile_t*p;static char path[192];static bool down,busy;static esp_err_t last;
static void task(void*a){(void)a;request_t r;for(;;){if(xQueueReceive(q,&r,portMAX_DELAY)!=pdPASS)continue;(void)r;busy=true;profile_feedback_set(PROFILE_FEEDBACK_EXPORT_WRITING);last=motion_profile_save_json_atomic(path,p);profile_feedback_set(last==ESP_OK?PROFILE_FEEDBACK_EXPORT_SUCCESS:PROFILE_FEEDBACK_EXPORT_ERROR);busy=false;}}
esp_err_t profile_export_init(motion_profile_t*x,const char*s){if(!x||!s||strlen(s)>=sizeof path)return ESP_ERR_INVALID_ARG;p=x;strcpy(path,s);q=xQueueCreate(1,sizeof(request_t));if(!q)return ESP_ERR_NO_MEM;return xTaskCreate(task,"profile_export",6144,NULL,8,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;}
void profile_export_on_key(uint8_t m,uint8_t u,bool pressed){bool match=u==CONFIG_USB_GATEWAY_PROFILE_EXPORT_HID_USAGE&&(m&CONFIG_USB_GATEWAY_PROFILE_EXPORT_MODIFIER_MASK)==CONFIG_USB_GATEWAY_PROFILE_EXPORT_MODIFIER_MASK;if(pressed&&!down&&match&&!busy&&q){request_t r={1};if(xQueueSend(q,&r,0)==pdPASS)profile_feedback_set(PROFILE_FEEDBACK_EXPORT_PENDING);}down=pressed&&match;}
bool profile_export_busy(void){return busy;}esp_err_t profile_export_last_error(void){return last;}
