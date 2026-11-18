#include "usb_proxy_control.h"
#include "p4_s3_link.h"
#include <string.h>
#define FRAG_DATA 56U
#define MSG_OUTPUT_FRAGMENT 0x30U
#define MSG_FEATURE_GET 0x31U
#define MSG_FEATURE_SET_FRAGMENT 0x32U
#define MSG_EP0_FRAGMENT 0x40U
#define MSG_CONTROL_RESPONSE_FRAGMENT 0x41U
typedef struct{uint8_t tag,interface_number,report_id,report_type,index,count,data_length,reserved;uint8_t data[FRAG_DATA];}fragment_t;static usb_proxy_host_set_report_fn sf;static usb_proxy_host_get_report_fn gf;static usb_proxy_host_control_fn cf;static uint8_t buffer[512],active_tag;static size_t used;static uint8_t expected;
void usb_proxy_control_init(void){used=0;expected=0;active_tag=0;}
void usb_proxy_control_register_host(usb_proxy_host_set_report_fn s,usb_proxy_host_get_report_fn g,usb_proxy_host_control_fn c){sf=s;gf=g;cf=c;}
static esp_err_t send_fragments(uint8_t type,uint8_t tag,uint8_t itf,uint8_t id,uint8_t rt,const uint8_t*d,size_t n){uint8_t count=(uint8_t)((n+FRAG_DATA-1)/FRAG_DATA);if(!count)count=1;for(uint8_t i=0;i<count;i++){fragment_t f={.tag=tag,.interface_number=itf,.report_id=id,.report_type=rt,.index=i,.count=count,.data_length=(uint8_t)((n>FRAG_DATA)?FRAG_DATA:n)};if(f.data_length)memcpy(f.data,d,f.data_length);if(!p4_s3_link_enqueue(type,(const uint8_t*)&f,(size_t)8+f.data_length))return ESP_ERR_TIMEOUT;d+=f.data_length;n-=f.data_length;}return ESP_OK;}
static esp_err_t collect(const fragment_t*f,size_t n,bool*complete){if(n<8||f->count==0||f->index>=f->count||f->data_length>FRAG_DATA||8U+f->data_length>n)return ESP_ERR_INVALID_ARG;if(f->index==0){active_tag=f->tag;used=0;expected=f->count;}if(f->tag!=active_tag||f->index+1>expected||used+f->data_length>sizeof buffer)return ESP_ERR_INVALID_STATE;memcpy(buffer+used,f->data,f->data_length);used+=f->data_length;*complete=f->index+1==f->count;return ESP_OK;}
esp_err_t usb_proxy_control_from_s3(uint8_t type,const uint8_t*p,size_t n){if(!p)return ESP_ERR_INVALID_ARG;const fragment_t*f=(const fragment_t*)p;bool complete=false;esp_err_t e;if(type==MSG_FEATURE_GET){if(n<8||!gf)return ESP_ERR_NOT_SUPPORTED;size_t l=sizeof buffer;e=gf(f->interface_number,f->report_id,USB_PROXY_REPORT_FEATURE,buffer,&l);return e==ESP_OK?send_fragments(MSG_CONTROL_RESPONSE_FRAGMENT,f->tag,f->interface_number,f->report_id,USB_PROXY_REPORT_FEATURE,buffer,l):e;}e=collect(f,n,&complete);if(e!=ESP_OK||!complete)return e;if(type==MSG_OUTPUT_FRAGMENT&&sf)return sf(f->interface_number,f->report_id,USB_PROXY_REPORT_OUTPUT,buffer,used);if(type==MSG_FEATURE_SET_FRAGMENT&&sf)return sf(f->interface_number,f->report_id,USB_PROXY_REPORT_FEATURE,buffer,used);if(type==MSG_EP0_FRAGMENT&&cf){if(used<8)return ESP_ERR_INVALID_ARG;size_t l=used-8;return cf(buffer,buffer+8,&l,(buffer[0]&0x80U)!=0);}return ESP_ERR_NOT_SUPPORTED;}
