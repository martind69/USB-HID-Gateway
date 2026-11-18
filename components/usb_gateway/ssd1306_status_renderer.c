#include "ssd1306_status_renderer.h"
#include "ssd1306_font.h"
#include <stdio.h>
void ssd1306_render_status(uint8_t*f,const ssd1306_status_view_t*v){char line[24];ssd1306_fb_clear(f,SSD1306_BUFFER_SIZE);ssd1306_fb_text(f,0,0,"USB HID GATEWAY");snprintf(line,sizeof line,"MODE %s",v->mode);ssd1306_fb_text(f,0,1,line);snprintf(line,sizeof line,"PROFILE %s",v->profile);ssd1306_fb_text(f,0,2,line);ssd1306_fb_text(f,0,3,v->detail);ssd1306_fb_progress(f,0,4,100,v->progress_percent);snprintf(line,sizeof line,"H %s D %s A %s",v->usb_host_ok?"OK":"ERR",v->usb_device_ok?"OK":"ERR",v->analog_ok?"OK":"ERR");ssd1306_fb_text(f,0,6,line);}
