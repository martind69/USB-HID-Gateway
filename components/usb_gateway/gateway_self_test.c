#include "gateway_self_test.h"

#include "gateway_test_metrics.h"
#include "p4_s3_link.h"
#include "p4_s3_protocol.h"
#include "profile_feedback.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SELF_TEST_ACK_TIMEOUT_MS 50U
#define SELF_TEST_ENQUEUE_TIMEOUT_MS 100U
#define SELF_TEST_RETRY_DELAY_MS 1U

static const char *TAG = "gateway_self_test";
static gateway_self_test_status_t s_status;

static void log_results(void)
{
    const gateway_link_metrics_t metrics =
        gateway_link_metrics_snapshot();

    ESP_LOGI(
        TAG,
        "TEST requested=%lu completed=%lu failed=%lu last_error=%s",
        (unsigned long)s_status.requested_frames,
        (unsigned long)s_status.completed_frames,
        (unsigned long)s_status.failed_frames,
        esp_err_to_name(s_status.last_error));

    ESP_LOGI(
        TAG,
        "LINK tx=%llu rx=%llu crc=%llu seq=%llu timeout=%llu "
        "overflow=%llu latency=%lu..%lu us",
        (unsigned long long)metrics.frames_sent,
        (unsigned long long)metrics.frames_received,
        (unsigned long long)metrics.crc_errors,
        (unsigned long long)metrics.sequence_errors,
        (unsigned long long)metrics.timeouts,
        (unsigned long long)metrics.queue_overflows,
        (unsigned long)metrics.latency_min_us,
        (unsigned long)metrics.latency_max_us);
}

static bool enqueue_with_backpressure(
    const uint8_t *payload,
    size_t payload_length)
{
    const int64_t deadline_us = esp_timer_get_time() +
        (int64_t)SELF_TEST_ENQUEUE_TIMEOUT_MS * 1000LL;

    do {
        if (p4_s3_link_enqueue(
                P4_S3_MSG_G815_IF1_INPUT,
                payload,
                payload_length)) {
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(SELF_TEST_RETRY_DELAY_MS));
    } while (esp_timer_get_time() < deadline_us);

    return false;
}

static void test_task(void *argument)
{
    (void)argument;
    uint8_t payload[P4_S3_PAYLOAD_SIZE];

    s_status.state = GATEWAY_SELF_TEST_RUNNING;
    profile_feedback_set(PROFILE_FEEDBACK_LEARNING);

    for (uint32_t frame_index = 0U;
         frame_index < s_status.requested_frames;
         ++frame_index) {
        for (size_t payload_index = 0U;
             payload_index < sizeof(payload);
             ++payload_index) {
            payload[payload_index] =
                (uint8_t)(frame_index + payload_index);
        }

        if (!enqueue_with_backpressure(payload, sizeof(payload))) {
            ++s_status.failed_frames;
            s_status.last_error = ESP_ERR_TIMEOUT;
            continue;
        }

        p4_s3_ack_t acknowledgement;
        if (!p4_s3_link_wait_status(
                &acknowledgement,
                SELF_TEST_ACK_TIMEOUT_MS)) {
            ++s_status.failed_frames;
            s_status.last_error = ESP_ERR_TIMEOUT;
        } else if (acknowledgement.status != 0U) {
            ++s_status.failed_frames;
            s_status.last_error = ESP_FAIL;
        } else {
            ++s_status.completed_frames;
        }

        if ((frame_index & 0x3FU) == 0U) {
            taskYIELD();
        }
    }

    s_status.state = s_status.failed_frames == 0U
        ? GATEWAY_SELF_TEST_PASSED
        : GATEWAY_SELF_TEST_FAILED;

    profile_feedback_set(
        s_status.failed_frames == 0U
            ? PROFILE_FEEDBACK_EXPORT_SUCCESS
            : PROFILE_FEEDBACK_EXPORT_ERROR);

    log_results();
    vTaskDelete(NULL);
}

esp_err_t gateway_self_test_init(void)
{
    s_status = (gateway_self_test_status_t) { 0 };
    gateway_link_metrics_reset();
    return ESP_OK;
}

esp_err_t gateway_self_test_start(
    uint32_t frame_count,
    bool inject_crc_errors)
{
    if (frame_count == 0U ||
        s_status.state == GATEWAY_SELF_TEST_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (inject_crc_errors) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    gateway_link_metrics_reset();
    s_status = (gateway_self_test_status_t) {
        .requested_frames = frame_count,
        .state = GATEWAY_SELF_TEST_RUNNING,
        .last_error = ESP_OK,
    };

    ESP_LOGI(
        TAG,
        "Starting P4/S3 test with %lu frames",
        (unsigned long)frame_count);

    return xTaskCreate(
        test_task,
        "gateway_test",
        4096U,
        NULL,
        6U,
        NULL) == pdPASS
        ? ESP_OK
        : ESP_ERR_NO_MEM;
}

gateway_self_test_status_t gateway_self_test_status(void)
{
    return s_status;
}
