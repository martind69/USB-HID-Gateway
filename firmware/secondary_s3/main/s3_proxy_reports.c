#include "s3_proxy_reports.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#define FRAG_DATA 56U
#define MSG_OUTPUT_FRAGMENT 0x30U
#define MSG_FEATURE_GET 0x31U
#define MSG_FEATURE_SET_FRAGMENT 0x32U
#define MSG_EP0_FRAGMENT 0x40U
#define MSG_CONTROL_RESPONSE_FRAGMENT 0x41U
typedef struct{uint8_t tag,interface_number,report_id,report_type,index,count,data_length,reserved;uint8_t data[FRAG_DATA];}fragment_t;static QueueHandle_t frames;static SemaphoreHandle_t response_ready;static uint8_t next_tag,response_tag,response[512];static size_t response_length;
esp_err_t s3_proxy_reports_init(void){frames=xQueueCreate(16,sizeof(p4_s3_frame_t));response_ready=xSemaphoreCreateBinary();return frames&&response_ready?ESP_OK:ESP_ERR_NO_MEM;}
static esp_err_t queue_fragments(uint8_t type,uint8_t instance,uint8_t id,uint8_t rt,const uint8_t*d,size_t n,uint8_t*tag_out){if(!frames)return ESP_ERR_INVALID_STATE;uint8_t tag=++next_tag;if(tag_out)*tag_out=tag;uint8_t count=(uint8_t)((n+FRAG_DATA-1)/FRAG_DATA);if(!count)count=1;for(uint8_t i=0;i<count;i++){fragment_t f={.tag=tag,.interface_number=instance,.report_id=id,.report_type=rt,.index=i,.count=count,.data_length=(uint8_t)(n>FRAG_DATA?FRAG_DATA:n)};if(f.data_length)memcpy(f.data,d,f.data_length);p4_s3_frame_t frame;p4_s3_frame_build(&frame,type,tag,(const uint8_t*)&f,(uint8_t)(8+f.data_length));if(xQueueSend(frames,&frame,0)!=pdPASS)return ESP_ERR_NO_MEM;d+=f.data_length;n-=f.data_length;}return ESP_OK;}
esp_err_t s3_proxy_send_set_report(uint8_t i,uint8_t id,uint8_t rt,const uint8_t*d,size_t n){return queue_fragments(rt==3?MSG_FEATURE_SET_FRAGMENT:MSG_OUTPUT_FRAGMENT,i,id,rt,d,n,NULL);}
esp_err_t s3_proxy_send_get_report(uint8_t i,uint8_t id,uint8_t rt,uint16_t requested){fragment_t f={.tag=++next_tag,.interface_number=i,.report_id=id,.report_type=rt,.data_length=2};f.data[0]=(uint8_t)requested;f.data[1]=(uint8_t)(requested>>8);p4_s3_frame_t frame;p4_s3_frame_build(&frame,MSG_FEATURE_GET,f.tag,(const uint8_t*)&f,10);return xQueueSend(frames,&frame,0)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;}
bool s3_proxy_next_frame(p4_s3_frame_t*f){return frames&&f&&xQueueReceive(frames,f,0)==pdPASS;}
esp_err_t s3_proxy_accept_response(const p4_s3_frame_t*frame){if(!p4_s3_frame_validate(frame)||frame->raw[3]!=MSG_CONTROL_RESPONSE_FRAGMENT||frame->raw[5]<8)return ESP_ERR_INVALID_ARG;const fragment_t*f=(const fragment_t*)(frame->raw+6);if(f->index==0){response_length=0;response_tag=f->tag;}if(f->tag!=response_tag||response_length+f->data_length>sizeof response)return ESP_ERR_INVALID_STATE;memcpy(response+response_length,f->data,f->data_length);response_length+=f->data_length;if(f->index+1==f->count)xSemaphoreGive(response_ready);return ESP_OK;}
size_t s3_proxy_copy_get_response(uint8_t tag,uint8_t*b,size_t cap,uint32_t ms){if(!b||tag!=response_tag||xSemaphoreTake(response_ready,pdMS_TO_TICKS(ms))!=pdTRUE)return 0;size_t n=response_length<cap?response_length:cap;memcpy(b,response,n);return n;}
