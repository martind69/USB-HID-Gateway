#include "p4_s3_link.h"

#include <string.h>

#include "link_metrics.h"
#include "shared_spi_bus.h"
#include "usb_proxy_control.h"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define LINK_TX_QUEUE_DEPTH             32U
#define LINK_ACK_QUEUE_DEPTH            8U
#define LINK_TASK_STACK_SIZE            4096U
#define LINK_TASK_PRIORITY              18U

/* Maximum wait for the STATUS response to a normal frame. */
#define LINK_NORMAL_RESPONSE_TIMEOUT_US 100000LL

/* Mirror session control may connect the S3 USB device: longer window. */
#define LINK_MIRROR_RESPONSE_TIMEOUT_US 1000000LL

/* Poll interval while waiting for the STATUS response to an OTA frame. */
#define LINK_OTA_POLL_DELAY_MS          2U

/*
 * Poll interval while the transmit queue is empty.
 * The S3 is an SPI slave and can only deliver S3 -> P4 frames (PC SET_REPORT
 * requests, session requests) when the P4 clocks a transaction.
 */
#define LINK_IDLE_POLL_MS               2U

typedef struct {
    p4_s3_frame_t frame;
} queued_frame_t;

typedef enum {
    INBOUND_NONE = 0,
    INBOUND_STATUS,
    INBOUND_DATA,
} inbound_result_t;

static const char *TAG = "p4_s3_link";

static QueueHandle_t s_tx_queue;
static QueueHandle_t s_ack_queue;
static SemaphoreHandle_t s_command_lock;
static SemaphoreHandle_t s_exclusive_lock;
static spi_device_handle_t s_device;
static portMUX_TYPE s_sequence_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_sequence;
static uint32_t s_dropped;
static volatile bool s_exclusive;
static volatile p4_s3_link_event_handler_t s_event_handler;

static uint8_t next_sequence(void)
{
    portENTER_CRITICAL(&s_sequence_lock);
    const uint8_t sequence = s_sequence++;
    portEXIT_CRITICAL(&s_sequence_lock);
    return sequence;
}

static esp_err_t transfer(
    const p4_s3_frame_t *transmit_frame,
    p4_s3_frame_t *receive_frame)
{
    spi_transaction_t transaction = {
        .length = P4_S3_FRAME_SIZE * 8U,
        .tx_buffer = transmit_frame->raw,
        .rx_buffer = receive_frame->raw,
    };
    return spi_device_transmit(s_device, &transaction);
}

static void publish_acknowledgement(const p4_s3_ack_t *acknowledgement)
{
    if (xQueueSend(s_ack_queue, acknowledgement, 0U) == pdPASS) {
        return;
    }
    p4_s3_ack_t discarded;
    (void)xQueueReceive(s_ack_queue, &discarded, 0U);
    (void)xQueueSend(s_ack_queue, acknowledgement, 0U);
}

/*
 * Evaluate one frame received from the S3.
 * STATUS frames are only accepted when a response is expected and the
 * sequence number matches. MIRROR_REQUEST goes to the event handler; every
 * other frame type is PC-to-device traffic for usb_proxy_control.
 */
static inbound_result_t handle_inbound(
    const p4_s3_frame_t *frame,
    bool expect_status,
    uint8_t expected_sequence,
    uint64_t started_us)
{
    if (p4_s3_frame_is_empty(frame)) {
        return INBOUND_NONE;
    }
    if (!p4_s3_frame_validate(frame)) {
        link_metrics_on_crc_error();
        return INBOUND_NONE;
    }
    const uint8_t type = frame->raw[P4_S3_OFFSET_TYPE];
    const uint8_t sequence = frame->raw[P4_S3_OFFSET_SEQUENCE];
    const uint8_t length = frame->raw[P4_S3_OFFSET_LENGTH];
    if (type == P4_S3_MSG_NOP) {
        return INBOUND_NONE;
    }
    if (type == P4_S3_MSG_STATUS) {
        if (!expect_status) {
            return INBOUND_NONE;
        }
        if (sequence != expected_sequence) {
            link_metrics_on_sequence_error();
            return INBOUND_NONE;
        }
        p4_s3_ack_t acknowledgement = {
            .sequence = sequence,
            .status = length > 0U ? frame->raw[P4_S3_OFFSET_PAYLOAD] : 0xFFU,
            .round_trip_us = (uint32_t)((uint64_t)esp_timer_get_time() - started_us),
            .payload_length = length,
        };
        memcpy(
            acknowledgement.payload,
            &frame->raw[P4_S3_OFFSET_PAYLOAD],
            acknowledgement.payload_length);
        link_metrics_on_rx(acknowledgement.round_trip_us);
        publish_acknowledgement(&acknowledgement);
        return INBOUND_STATUS;
    }
    if (type == P4_S3_MSG_MIRROR_REQUEST) {
        const p4_s3_link_event_handler_t handler = s_event_handler;
        if (handler != NULL) {
            handler(type, &frame->raw[P4_S3_OFFSET_PAYLOAD], length);
        }
        return INBOUND_DATA;
    }
    const esp_err_t error = usb_proxy_control_from_s3(
        type,
        &frame->raw[P4_S3_OFFSET_PAYLOAD],
        length);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Inbound type 0x%02X rejected: %s",
            type, esp_err_to_name(error));
    }
    return INBOUND_DATA;
}

static void synchronize_link(const p4_s3_frame_t *nop_frame)
{
    for (unsigned attempt = 0U; attempt < 3U; ++attempt) {
        p4_s3_frame_t received = { 0 };
        (void)transfer(nop_frame, &received);
        esp_rom_delay_us(CONFIG_USB_GATEWAY_S3_REARM_GUARD_US);
    }
    p4_s3_ack_t stale;
    while (xQueueReceive(s_ack_queue, &stale, 0U) == pdPASS) {
    }
    ESP_LOGI(TAG, "P4/S3 startup synchronization complete");
}

/* Returns true when the S3 delivered data, so the next poll is immediate. */
static bool poll_idle(const p4_s3_frame_t *nop_frame)
{
    p4_s3_frame_t received = { 0 };
    const esp_err_t error = transfer(nop_frame, &received);
    esp_rom_delay_us(CONFIG_USB_GATEWAY_S3_REARM_GUARD_US);
    if (error != ESP_OK) {
        return false;
    }
    return handle_inbound(&received, false, 0U, 0U) == INBOUND_DATA;
}

static void process_command(
    const queued_frame_t *queued,
    const p4_s3_frame_t *nop_frame)
{
    const uint8_t message_type = queued->frame.raw[P4_S3_OFFSET_TYPE];
    const uint8_t sequence = queued->frame.raw[P4_S3_OFFSET_SEQUENCE];
    const uint64_t started_us = (uint64_t)esp_timer_get_time();
    p4_s3_frame_t received = { 0 };

    if (transfer(&queued->frame, &received) != ESP_OK) {
        link_metrics_on_timeout();
        return;
    }
    link_metrics_on_tx();

    /* The S3 answers the current frame in the next transaction. */
    bool acknowledged =
        handle_inbound(&received, true, sequence, started_us) == INBOUND_STATUS;

    /* OTA commands wait without timeout; S3 flash erase may take seconds. */
    const bool unlimited_wait = p4_s3_is_ota_message(message_type);
    const int64_t deadline_us = esp_timer_get_time() +
        (p4_s3_is_mirror_control(message_type)
            ? LINK_MIRROR_RESPONSE_TIMEOUT_US
            : LINK_NORMAL_RESPONSE_TIMEOUT_US);

    while (!acknowledged &&
        (unlimited_wait || esp_timer_get_time() < deadline_us)) {
        vTaskDelay(pdMS_TO_TICKS(
            unlimited_wait ? LINK_OTA_POLL_DELAY_MS
                           : CONFIG_USB_GATEWAY_S3_STATUS_POLL_MS));
        memset(&received, 0, sizeof(received));
        if (transfer(nop_frame, &received) == ESP_OK) {
            acknowledged = handle_inbound(
                &received, true, sequence, started_us) == INBOUND_STATUS;
        }
    }
    if (!acknowledged) {
        link_metrics_on_timeout();
        ESP_LOGE(TAG, "Response timeout type=0x%02X sequence=%u",
            message_type, sequence);
    }
    esp_rom_delay_us(CONFIG_USB_GATEWAY_S3_REARM_GUARD_US);
}

static void link_task(void *argument)
{
    (void)argument;
    queued_frame_t queued;
    p4_s3_frame_t nop_frame;
    bool fast_poll = false;

    p4_s3_frame_build(&nop_frame, P4_S3_MSG_NOP, 0U, NULL, 0U);
    synchronize_link(&nop_frame);

    while (true) {
        const TickType_t wait_ticks =
            fast_poll ? 0U : pdMS_TO_TICKS(LINK_IDLE_POLL_MS);
        if (xQueueReceive(s_tx_queue, &queued, wait_ticks) != pdPASS) {
            fast_poll = poll_idle(&nop_frame);
            continue;
        }
        fast_poll = false;
        process_command(&queued, &nop_frame);
    }
}

esp_err_t p4_s3_link_init(void)
{
    if (s_tx_queue != NULL || s_ack_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_tx_queue = xQueueCreate(LINK_TX_QUEUE_DEPTH, sizeof(queued_frame_t));
    s_ack_queue = xQueueCreate(LINK_ACK_QUEUE_DEPTH, sizeof(p4_s3_ack_t));
    s_command_lock = xSemaphoreCreateMutex();
    s_exclusive_lock = xSemaphoreCreateMutex();
    if (s_tx_queue == NULL || s_ack_queue == NULL ||
        s_command_lock == NULL || s_exclusive_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return shared_spi_bus_add_s3(&s_device);
}

void p4_s3_link_start(void)
{
    if (xTaskCreate(
            link_task,
            "p4_s3_link",
            LINK_TASK_STACK_SIZE,
            NULL,
            LINK_TASK_PRIORITY,
            NULL) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create P4/S3 link task");
    }
}

void p4_s3_link_set_event_handler(p4_s3_link_event_handler_t handler)
{
    s_event_handler = handler;
}

static bool enqueue_frame(
    uint8_t type,
    const uint8_t *payload,
    size_t length,
    uint8_t *sequence_out)
{
    if (s_tx_queue == NULL || length > P4_S3_PAYLOAD_SIZE) {
        ++s_dropped;
        return false;
    }
    if (s_exclusive && !p4_s3_is_ota_message(type)) {
        ++s_dropped;
        return false;
    }
    const uint8_t sequence = next_sequence();
    queued_frame_t queued;
    p4_s3_frame_build(
        &queued.frame,
        type,
        sequence,
        payload,
        (uint8_t)length);
    if (xQueueSend(s_tx_queue, &queued, 0U) != pdPASS) {
        ++s_dropped;
        link_metrics_on_queue_overflow();
        return false;
    }
    if (sequence_out != NULL) {
        *sequence_out = sequence;
    }
    return true;
}

bool p4_s3_link_enqueue(uint8_t type, const uint8_t *payload, size_t length)
{
    return enqueue_frame(type, payload, length, NULL);
}

bool p4_s3_link_wait_status(p4_s3_ack_t *acknowledgement, uint32_t timeout_ms)
{
    return s_ack_queue != NULL && acknowledgement != NULL &&
        xQueueReceive(
            s_ack_queue,
            acknowledgement,
            pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

esp_err_t p4_s3_link_command(
    uint8_t type,
    const uint8_t *payload,
    size_t length,
    p4_s3_ack_t *acknowledgement,
    uint32_t timeout_ms)
{
    if (acknowledgement == NULL || s_command_lock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const TickType_t wait_ticks = timeout_ms == UINT32_MAX
        ? portMAX_DELAY
        : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_command_lock, wait_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    /*
     * Acknowledgements of fire-and-forget frames (input reports) share the
     * queue. Only the STATUS whose sequence matches this command counts.
     */
    p4_s3_ack_t stale;
    while (xQueueReceive(s_ack_queue, &stale, 0U) == pdPASS) {
    }
    esp_err_t error = ESP_ERR_TIMEOUT;
    uint8_t sequence = 0U;
    if (!enqueue_frame(type, payload, length, &sequence)) {
        error = ESP_ERR_NO_MEM;
    } else {
        const TickType_t started = xTaskGetTickCount();
        while (true) {
            TickType_t remaining = portMAX_DELAY;
            if (timeout_ms != UINT32_MAX) {
                const TickType_t elapsed = xTaskGetTickCount() - started;
                if (elapsed >= wait_ticks) {
                    break;
                }
                remaining = wait_ticks - elapsed;
            }
            if (xQueueReceive(s_ack_queue, acknowledgement, remaining) != pdPASS) {
                break;
            }
            if (acknowledgement->sequence == sequence) {
                error = acknowledgement->status != 0U ? ESP_FAIL : ESP_OK;
                break;
            }
        }
    }
    xSemaphoreGive(s_command_lock);
    return error;
}

esp_err_t p4_s3_link_begin_exclusive(void)
{
    if (s_exclusive_lock == NULL ||
        xSemaphoreTake(s_exclusive_lock, pdMS_TO_TICKS(1000U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    s_exclusive = true;
    xQueueReset(s_tx_queue);
    return ESP_OK;
}

void p4_s3_link_end_exclusive(void)
{
    s_exclusive = false;
    if (s_exclusive_lock != NULL) {
        xSemaphoreGive(s_exclusive_lock);
    }
}

uint32_t p4_s3_link_dropped(void)
{
    return s_dropped;
}
