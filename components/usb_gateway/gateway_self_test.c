#include "gateway_self_test.h"
#include "gateway_test_metrics.h"
#include "p4_s3_link.h"
#include "p4_s3_protocol.h"
#include "profile_feedback.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static gateway_self_test_status_t s_status;
static void test_task(void*a){(void)a;uint8_t payload[64];s_status.state=GATEWAY_SELF_TEST_RUNNING;profile_feedback_set(PROFILE_FEEDBACK_LEARNING);for(uint32_t n=0;n<s_status.requested_frames;n++){for(size_t i=0;i<sizeof payload;i++)payload[i]=(uint8_t)(n+i);if(!p4_s3_link_enqueue(P4_S3_MSG_G815_IF1_INPUT,payload,sizeof payload)){s_status.failed_frames++;s_status.last_error=ESP_ERR_NO_MEM;continue;}p4_s3_ack_t ack;if(!p4_s3_link_wait_status(&ack,30)){s_status.failed_frames++;s_status.last_error=ESP_ERR_TIMEOUT;}else{s_status.completed_frames++;}if((n&0x3fU)==0U)taskYIELD();}s_status.state=s_status.failed_frames?GATEWAY_SELF_TEST_FAILED:GATEWAY_SELF_TEST_PASSED;profile_feedback_set(s_status.failed_frames?PROFILE_FEEDBACK_EXPORT_ERROR:PROFILE_FEEDBACK_EXPORT_SUCCESS);vTaskDelete(NULL);}
esp_err_t gateway_self_test_init(void){s_status=(gateway_self_test_status_t){0};gateway_link_metrics_reset();return ESP_OK;}esp_err_t gateway_self_test_start(uint32_t count,bool inject){if(!count||s_status.state==GATEWAY_SELF_TEST_RUNNING)return ESP_ERR_INVALID_STATE;if(inject)return ESP_ERR_NOT_SUPPORTED;s_status=(gateway_self_test_status_t){.requested_frames=count,.state=GATEWAY_SELF_TEST_RUNNING};return xTaskCreate(test_task,"gateway_test",4096,NULL,6,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;}gateway_self_test_status_t gateway_self_test_status(void){return s_status;}
