#include "p4_s3_link.h"
#include "shared_spi_bus.h"
#include "gateway_test_metrics.h"
#include "usb_proxy_control.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>
typedef struct{p4_s3_frame_t frame;uint64_t queued_us;}queued_frame_t;static QueueHandle_t txq,ackq;static spi_device_handle_t dev;static uint8_t seq;static uint32_t dropped;static const char*TAG="p4_s3_link";
static esp_err_t transfer(const p4_s3_frame_t*tx,p4_s3_frame_t*rx){spi_transaction_t t={.length=P4_S3_FRAME_SIZE*8U,.tx_buffer=tx->raw,.rx_buffer=rx->raw};return spi_device_transmit(dev,&t);}
static void handle_inbound(const p4_s3_frame_t*rx,uint8_t expected,uint64_t started,bool*acked){if(!(rx->raw[0]||rx->raw[1]))return;if(!p4_s3_frame_validate(rx)){gateway_link_metrics_on_crc_error();return;}if(rx->raw[3]==P4_S3_MSG_STATUS){if(rx->raw[4]!=expected){gateway_link_metrics_on_sequence_error();return;}p4_s3_ack_t a={.sequence=rx->raw[4],.status=rx->raw[6],.round_trip_us=(uint32_t)(esp_timer_get_time()-started)};gateway_link_metrics_on_rx(a.round_trip_us);(void)xQueueSend(ackq,&a,0);*acked=true;return;}esp_err_t e=usb_proxy_control_from_s3(rx->raw[3],rx->raw+6,rx->raw[5]);if(e!=ESP_OK&&e!=ESP_ERR_NOT_SUPPORTED)ESP_LOGW(TAG,"Inbound type 0x%02x: %s",rx->raw[3],esp_err_to_name(e));}
esp_err_t p4_s3_link_init(void){txq=xQueueCreate(32,sizeof(queued_frame_t));ackq=xQueueCreate(8,sizeof(p4_s3_ack_t));if(!txq||!ackq)return ESP_ERR_NO_MEM;return shared_spi_bus_add_s3(&dev);}
bool p4_s3_link_enqueue(uint8_t type,const uint8_t*p,size_t n){if(!txq||n>64){dropped++;return false;}queued_frame_t q={.queued_us=esp_timer_get_time()};p4_s3_frame_build(&q.frame,type,seq++,p,(uint8_t)n);if(xQueueSend(txq,&q,0)!=pdPASS){dropped++;gateway_link_metrics_on_queue_overflow();return false;}return true;}
static void task(void*a){(void)a;queued_frame_t q;p4_s3_frame_t nop;for(;;){bool have=xQueueReceive(txq,&q,pdMS_TO_TICKS(2))==pdPASS;if(!have)p4_s3_frame_build(&q.frame,P4_S3_MSG_NOP,0,NULL,0);p4_s3_frame_t rx={0};uint64_t started=esp_timer_get_time();esp_err_t e=transfer(&q.frame,&rx);if(e!=ESP_OK){gateway_link_metrics_on_timeout();continue;}bool acked=!have||q.frame.raw[3]==P4_S3_MSG_NOP;handle_inbound(&rx,q.frame.raw[4],started,&acked);if(have&&q.frame.raw[3]!=P4_S3_MSG_NOP){gateway_link_metrics_on_tx();p4_s3_frame_build(&nop,P4_S3_MSG_NOP,0,NULL,0);for(unsigned poll=0;!acked&&poll<CONFIG_USB_GATEWAY_S3_STATUS_POLL_LIMIT;poll++){memset(&rx,0,sizeof rx);vTaskDelay(pdMS_TO_TICKS(CONFIG_USB_GATEWAY_S3_STATUS_POLL_MS));e=transfer(&nop,&rx);if(e!=ESP_OK){gateway_link_metrics_on_timeout();break;}handle_inbound(&rx,q.frame.raw[4],started,&acked);}if(!acked)gateway_link_metrics_on_timeout();}}}
void p4_s3_link_start(void){xTaskCreate(task,"p4_s3_link",4096,NULL,18,NULL);}bool p4_s3_link_wait_status(p4_s3_ack_t*a,uint32_t ms){return ackq&&a&&xQueueReceive(ackq,a,pdMS_TO_TICKS(ms))==pdPASS;}uint32_t p4_s3_link_dropped(void){return dropped;}
