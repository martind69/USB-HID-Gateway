#include "profile_export.h"
#include "profile_feedback.h"
#include "sdkconfig.h"
#include <string.h>
static motion_profile_t*p;static char path[192];static bool down;esp_err_t profile_export_init(motion_profile_t*x,const char*s){if(!x||!s||strlen(s)>=sizeof path)return ESP_ERR_INVALID_ARG;p=x;strcpy(path,s);return ESP_OK;}void profile_export_on_key(uint8_t m,uint8_t u,bool pressed){bool match=u==CONFIG_USB_GATEWAY_PROFILE_EXPORT_HID_USAGE&&(m&CONFIG_USB_GATEWAY_PROFILE_EXPORT_MODIFIER_MASK)==CONFIG_USB_GATEWAY_PROFILE_EXPORT_MODIFIER_MASK;if(pressed&&!down&&match){profile_feedback_set(PROFILE_FEEDBACK_EXPORT_WRITING);esp_err_t e=motion_profile_save_json_atomic(path,p);profile_feedback_set(e?PROFILE_FEEDBACK_EXPORT_ERROR:PROFILE_FEEDBACK_EXPORT_SUCCESS);}down=pressed&&match;}
