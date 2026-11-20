#include "keyboard_test_pattern.h"

#include "de_de_keyboard_layout.h"
#include "p4_s3_link.h"
#include "p4_s3_protocol.h"
#include "profile_feedback.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stddef.h>
#include <string.h>

#define REPORT_SIZE 8U
#define ACK_TIMEOUT_MS 100U

typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} keyboard_report_t;

static const char *TAG = "keyboard_test_de";
static keyboard_test_pattern_status_t s_status;
static TaskHandle_t s_task;
static volatile bool s_abort;

static bool send_report(const keyboard_report_t *report, uint32_t delay_ms)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(100U);
    while (!p4_s3_link_enqueue(
               P4_S3_MSG_G815_IF0_INPUT,
               (const uint8_t *)report,
               sizeof(*report))) {
        if (xTaskGetTickCount() >= deadline || s_abort) {
            ++s_status.reports_failed;
            s_status.last_error = ESP_ERR_TIMEOUT;
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }

    ++s_status.reports_requested;
    p4_s3_ack_t acknowledgement;
    if (!p4_s3_link_wait_status(&acknowledgement, ACK_TIMEOUT_MS)) {
        ++s_status.reports_failed;
        s_status.last_error = ESP_ERR_TIMEOUT;
        return false;
    }

    s_status.last_status = acknowledgement.status;
    if (acknowledgement.status != 0U) {
        ++s_status.reports_failed;
        s_status.last_error = ESP_FAIL;
        return false;
    }

    ++s_status.reports_completed;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    return true;
}

static bool send_stroke(const de_de_key_stroke_t *stroke)
{
    const keyboard_report_t down = {
        .modifiers = stroke->modifiers,
        .keys = { stroke->usage, 0U, 0U, 0U, 0U, 0U },
    };
    const keyboard_report_t release = { 0 };

    return send_report(&down, CONFIG_USB_GATEWAY_KEYBOARD_TEST_DOWN_MS) &&
        send_report(&release, CONFIG_USB_GATEWAY_KEYBOARD_TEST_UP_MS);
}

static bool send_codepoint(uint32_t codepoint)
{
    de_de_key_sequence_t sequence;
    if (!de_de_encode_codepoint(codepoint, &sequence)) {
        ESP_LOGE(TAG, "Unsupported codepoint U+%04lX", (unsigned long)codepoint);
        s_status.last_error = ESP_ERR_NOT_SUPPORTED;
        ++s_status.reports_failed;
        return false;
    }

    for (uint8_t index = 0U; index < sequence.count; ++index) {
        if (!send_stroke(&sequence.stroke[index])) {
            return false;
        }
    }
    return true;
}

static bool send_utf8(const char *text)
{
    const char *cursor = text;
    uint32_t codepoint;
    while (!s_abort && de_de_utf8_next(&cursor, &codepoint)) {
        if (!send_codepoint(codepoint)) {
            return false;
        }
    }
    return !s_abort;
}

static bool phase(const char *name, const char *text)
{
    ESP_LOGI(TAG, "Phase: %s", name);
    if (!send_utf8(text) || !send_codepoint('\n')) {
        return false;
    }
    ++s_status.phases_completed;
    return true;
}

static bool rollover_phase(void)
{
    static const uint8_t keys[] = { 0x04U, 0x16U, 0x07U, 0x09U, 0x0AU, 0x0BU };
    ESP_LOGI(TAG, "Phase: 2-to-6-key rollover");

    for (size_t count = 2U; count <= 6U; ++count) {
        keyboard_report_t down = { 0 };
        const keyboard_report_t release = { 0 };
        memcpy(down.keys, keys, count);
        if (!send_report(&down, 100U) || !send_report(&release, 100U)) {
            return false;
        }
    }
    ++s_status.phases_completed;
    return true;
}

static void runner_task(void *argument)
{
    (void)argument;
    s_status.state = KEYBOARD_TEST_PATTERN_COUNTDOWN;

    ESP_LOGW(TAG, "de-DE keyboard test starts in %u seconds",
        CONFIG_USB_GATEWAY_KEYBOARD_TEST_COUNTDOWN_S);
    for (uint32_t remaining = CONFIG_USB_GATEWAY_KEYBOARD_TEST_COUNTDOWN_S;
         remaining > 0U && !s_abort;
         --remaining) {
        ESP_LOGI(TAG, "Start in %lu", (unsigned long)remaining);
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }

    s_status.state = KEYBOARD_TEST_PATTERN_RUNNING;
    profile_feedback_set(PROFILE_FEEDBACK_LEARNING);

    bool success = !s_abort &&
        phase("letters-lower", "abcdefghijklmnopqrstuvwxyz") &&
        phase("letters-upper", "ABCDEFGHIJKLMNOPQRSTUVWXYZ") &&
        phase("digits", "0123456789") &&
        phase("umlauts", "\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x84\xC3\x96\xC3\x9C\xC3\x9F") &&
        phase("number-row-shift", "!\x22\xC2\xA7$%&/()=?") &&
        phase("punctuation", ",.;:-_#+*'<>|\\") &&
        phase("altgr", "@\xE2\x82\xAC\xC2\xB2\xC2\xB3\xC2\xB5{}[]\\|~") &&
        phase("dead-literal", "^`\xC2\xB4") &&
        phase("acute", "\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA\xC3\xBD \xC3\x81\xC3\x89\xC3\x8D\xC3\x93\xC3\x9A\xC3\x9D") &&
        phase("grave", "\xC3\xA0\xC3\xA8\xC3\xAC\xC3\xB2\xC3\xB9 \xC3\x80\xC3\x88\xC3\x8C\xC3\x92\xC3\x99") &&
        phase("circumflex", "\xC3\xA2\xC3\xAA\xC3\xAE\xC3\xB4\xC3\xBB \xC3\x82\xC3\x8A\xC3\x8E\xC3\x94\xC3\x9B") &&
        phase("sentence", "Franz jagt im komplett verwahrlosten Taxi quer durch Bayern.") &&
        rollover_phase() &&
        phase("end-marker", "TEST-ENDE");

    const keyboard_report_t release = { 0 };
    for (unsigned index = 0U; index < 3U; ++index) {
        (void)send_report(&release, 10U);
    }

    if (s_abort) {
        s_status.state = KEYBOARD_TEST_PATTERN_ABORTED;
        s_status.last_error = ESP_ERR_INVALID_STATE;
    } else if (success) {
        s_status.state = KEYBOARD_TEST_PATTERN_PASSED;
        s_status.last_error = ESP_OK;
    } else {
        s_status.state = KEYBOARD_TEST_PATTERN_FAILED;
    }

    profile_feedback_set(success
        ? PROFILE_FEEDBACK_EXPORT_SUCCESS
        : PROFILE_FEEDBACK_EXPORT_ERROR);
    ESP_LOGI(TAG,
        "Result state=%u phases=%lu reports=%lu/%lu failed=%lu error=%s",
        (unsigned)s_status.state,
        (unsigned long)s_status.phases_completed,
        (unsigned long)s_status.reports_completed,
        (unsigned long)s_status.reports_requested,
        (unsigned long)s_status.reports_failed,
        esp_err_to_name(s_status.last_error));

    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t keyboard_test_pattern_init(void)
{
    s_status = (keyboard_test_pattern_status_t) { 0 };
    s_task = NULL;
    s_abort = false;
    return ESP_OK;
}

esp_err_t keyboard_test_pattern_start(void)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_status = (keyboard_test_pattern_status_t) { 0 };
    s_abort = false;
    return xTaskCreate(
        runner_task,
        "keyboard_test_de",
        6144U,
        NULL,
        7U,
        &s_task) == pdPASS
        ? ESP_OK
        : ESP_ERR_NO_MEM;
}

esp_err_t keyboard_test_pattern_abort(void)
{
    if (s_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_abort = true;
    return ESP_OK;
}

keyboard_test_pattern_status_t keyboard_test_pattern_status(void)
{
    return s_status;
}
