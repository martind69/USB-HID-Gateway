#include "verbose_capture.h"
#include "sd_storage.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct{uint64_t timestamp_us;uint16_t source,event,length,flags;uint32_t sequence;uint8_t payload[64];}record_t;static QueueHandle_t q;static SemaphoreHandle_t lock;static FILE*f;static bool active;static uint64_t stop_us,file_bytes,session_bytes;static uint32_t file_index,dropped;static char session[192];
static esp_err_t open_next(void){if(f){fflush(f);fsync(fileno(f));fclose(f);}char p[256];snprintf(p,sizeof p,"%s/events-%03lu.bin",session,(unsigned long)file_index++);f=fopen(p,"wb");file_bytes=0;return f?ESP_OK:ESP_FAIL;}
static void close_locked(void){if(f){fflush(f);fsync(fileno(f));fclose(f);f=NULL;}active=false;}
static void writer(void*a){(void)a;record_t r;TickType_t last=xTaskGetTickCount();for(;;){if(xQueueReceive(q,&r,pdMS_TO_TICKS(100))==pdPASS&&xSemaphoreTake(lock,pdMS_TO_TICKS(100))==pdTRUE){if(active&&f){if(file_bytes+sizeof r>(uint64_t)CONFIG_USB_GATEWAY_VERBOSE_CAPTURE_FILE_MIB*1024*1024)open_next();if(f&&fwrite(&r,1,sizeof r,f)==sizeof r){file_bytes+=sizeof r;session_bytes+=sizeof r;}else dropped++;if(session_bytes>=(uint64_t)CONFIG_USB_GATEWAY_VERBOSE_CAPTURE_MAX_TOTAL_MIB*1024*1024)close_locked();}xSemaphoreGive(lock);}if(active&&stop_us&&(uint64_t)esp_timer_get_time()>=stop_us)verbose_capture_stop();if(active&&xTaskGetTickCount()-last>=pdMS_TO_TICKS(CONFIG_USB_GATEWAY_VERBOSE_CAPTURE_FLUSH_MS)){if(xSemaphoreTake(lock,pdMS_TO_TICKS(100))==pdTRUE){if(f)fflush(f);xSemaphoreGive(lock);}last=xTaskGetTickCount();}}}
esp_err_t verbose_capture_init(void){q=xQueueCreate(CONFIG_USB_GATEWAY_VERBOSE_CAPTURE_QUEUE_DEPTH,sizeof(record_t));lock=xSemaphoreCreateMutex();if(!q||!lock)return ESP_ERR_NO_MEM;return xTaskCreate(writer,"sd_log",4096,NULL,3,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;}
esp_err_t verbose_capture_start(uint32_t duration_s){if(!sd_storage_is_mounted())return ESP_ERR_INVALID_STATE;if(sd_storage_free_bytes()<64ULL*1024*1024)return ESP_ERR_NO_MEM;if(xSemaphoreTake(lock,pdMS_TO_TICKS(100))!=pdTRUE)return ESP_ERR_TIMEOUT;if(active){xSemaphoreGive(lock);return ESP_ERR_INVALID_STATE;}snprintf(session,sizeof session,"/sdcard/logs/current/session-%llu",(unsigned long long)esp_timer_get_time());if(mkdir(session,0775)!=0){xSemaphoreGive(lock);return ESP_FAIL;}file_index=0;file_bytes=0;session_bytes=0;dropped=0;stop_us=duration_s?(uint64_t)esp_timer_get_time()+(uint64_t)duration_s*1000000ULL:0;esp_err_t e=open_next();active=e==ESP_OK;xSemaphoreGive(lock);return e;}
esp_err_t verbose_capture_stop(void){if(!lock)return ESP_ERR_INVALID_STATE;if(xSemaphoreTake(lock,pdMS_TO_TICKS(100))!=pdTRUE)return ESP_ERR_TIMEOUT;close_locked();xSemaphoreGive(lock);return ESP_OK;}
bool verbose_capture_write(uint16_t s,uint16_t e,uint32_t seq,const void*p,size_t n){if(!active||!q)return false;record_t r={0};r.timestamp_us=esp_timer_get_time();r.source=s;r.event=e;r.sequence=seq;r.length=(uint16_t)(n>64?64:n);if(p&&r.length)memcpy(r.payload,p,r.length);if(xQueueSend(q,&r,0)!=pdPASS){dropped++;return false;}return true;}uint32_t verbose_capture_dropped(void){return dropped;}
