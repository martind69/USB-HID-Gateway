#include "gateway_self_test.h"
#include "gateway_test_metrics.h"
#include "p4_s3_link.h"
#include "p4_s3_protocol.h"
#include "profile_feedback.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static gateway_self_test_status_t s_status;
static bool s_inject;
static void task(void *arg){(void)arg;uint8_t payload[64];s_status.state=GATEWAY_SELF_TEST_RUNNING;profile_feedback_set(PROFILE_FEEDBACK_LEARNING);for(uint32_t n=0;n<s_status.requested_frames;n++){for(uint32_t i=0;i<sizeof(payload);i++)payload[i]=(uint8_t)(n+i);/* TODO(TEST-CRC-INJECTION): add raw-frame enqueue entry point to corrupt CRC after encoding. */if(!p4_s3_link_enqueue(P4_S3_MSG_G815_IF1_INPUT,payload,sizeof(payload))){s_status.failed_frames++;s_status.last_error=ESP_ERR_TIMEOUT;gateway_link_metrics_on_queue_overflow();}else{s_status.completed_frames++;gateway_link_metrics_on_tx();}if((n&0x3fU)==0U)vTaskDelay(1);}s_status.state=s_status.failed_frames?GATEWAY_SELF_TEST_FAILED:GATEWAY_SELF_TEST_PASSED;profile_feedback_set(s_status.failed_frames?PROFILE_FEEDBACK_EXPORT_ERROR:PROFILE_FEEDBACK_EXPORT_SUCCESS);vTaskDelete(NULL);}
esp_err_t gateway_self_test_init(void){s_status=(gateway_self_test_status_t){0};gateway_link_metrics_reset();return ESP_OK;}
esp_err_t gateway_self_test_start(uint32_t count,bool inject){if(!count||s_status.state==GATEWAY_SELF_TEST_RUNNING)return ESP_ERR_INVALID_STATE;s_status=(gateway_self_test_status_t){.requested_frames=count,.state=GATEWAY_SELF_TEST_RUNNING};s_inject=inject;(void)s_inject;return xTaskCreate(task,"gateway_test",4096,NULL,6,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;}
gateway_self_test_status_t gateway_self_test_status(void){return s_status;}
