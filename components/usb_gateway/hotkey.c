#include "hotkey.h"
#include "profile_export.h"
#include "sdkconfig.h"
static usb_gateway_mode_t s_mode;static bool s_mode_down;
void hotkey_init(void){s_mode=USB_GATEWAY_MODE_PASSTHROUGH;s_mode_down=false;}
void hotkey_process(void){}
void hotkey_set_mode(usb_gateway_mode_t mode){s_mode=mode;}
usb_gateway_mode_t hotkey_get_mode(void){return s_mode;}
void hotkey_handle_mode_key(bool pressed){if(pressed&&!s_mode_down)s_mode=s_mode==USB_GATEWAY_MODE_PASSTHROUGH?USB_GATEWAY_MODE_PROXY:USB_GATEWAY_MODE_PASSTHROUGH;s_mode_down=pressed;}
bool hotkey_handle_keyboard_event(uint8_t modifiers,uint8_t usage,bool pressed){
#if CONFIG_USB_GATEWAY_PROFILE_EXPORT_HOTKEY_ENABLE
 bool match=usage==CONFIG_USB_GATEWAY_PROFILE_EXPORT_HID_USAGE&&(modifiers&CONFIG_USB_GATEWAY_PROFILE_EXPORT_MODIFIER_MASK)==CONFIG_USB_GATEWAY_PROFILE_EXPORT_MODIFIER_MASK;profile_export_on_key(modifiers,usage,pressed);return match;
#else
 (void)modifiers;(void)usage;(void)pressed;return false;
#endif
}
