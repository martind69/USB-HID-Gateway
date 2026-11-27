#include "report_tap.h"

#include <string.h>

#include "hid_report_decoder.h"
#include "profile_export.h"
#include "usb_host_hid.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAP_QUEUE_DEPTH         64U
#define TAP_REPORT_MAX          64U
#define TAP_TASK_STACK          4096U
#define TAP_TASK_PRIORITY       8U
#define TAP_MAX_USAGES          INPUT_STATE_MAX_PRESSED

typedef struct {
    uint8_t interface_id;
    uint8_t length;                 /* 0 = interface removed */
    uint8_t data[TAP_REPORT_MAX];
} tap_item_t;

typedef struct {
    input_usage_t usages[TAP_MAX_USAGES];
    size_t usage_count;
    float dx;
    float dy;
    float wheel;
} decode_context_t;

static const char *TAG = "report_tap";

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_lock;
static input_state_t s_state;
static volatile report_tap_edge_handler_t s_edge_handler;
static report_tap_stats_t s_stats;

static void collect_usage(
    uint16_t usage_page,
    uint16_t usage,
    int32_t value,
    bool relative,
    void *context)
{
    decode_context_t *decode = context;
    if (relative) {
        if (usage_page == HID_PAGE_GENERIC_DESKTOP) {
            if (usage == HID_USAGE_GD_X) {
                decode->dx += (float)value;
            } else if (usage == HID_USAGE_GD_Y) {
                decode->dy += (float)value;
            } else if (usage == HID_USAGE_GD_WHEEL) {
                decode->wheel += (float)value;
            }
        }
        return;
    }
    if (value == 0 ||
        (usage_page != HID_PAGE_KEYBOARD &&
         usage_page != HID_PAGE_BUTTON &&
         usage_page != HID_PAGE_CONSUMER)) {
        return;
    }
    /* Keyboard error codes (rollover etc.) are not keys. */
    if (usage_page == HID_PAGE_KEYBOARD && usage <= 0x03U) {
        return;
    }
    if (decode->usage_count < TAP_MAX_USAGES) {
        decode->usages[decode->usage_count++] = (input_usage_t) {
            .usage_page = usage_page,
            .usage = usage,
        };
    }
}

/* Runs with s_lock held: only records; handlers are called after release. */
typedef struct {
    input_usage_t usage;
    bool pressed;
    uint8_t modifiers;
} edge_t;

typedef struct {
    edge_t edges[2U * INPUT_STATE_MAX_PRESSED];
    size_t count;
} edge_list_t;

static void record_edge(input_usage_t usage, bool pressed, uint8_t modifiers, void *context)
{
    edge_list_t *list = context;
    if (list->count < sizeof(list->edges) / sizeof(list->edges[0])) {
        list->edges[list->count++] = (edge_t) {
            .usage = usage,
            .pressed = pressed,
            .modifiers = modifiers,
        };
    }
}

static void dispatch_edges(const edge_list_t *list)
{
    const report_tap_edge_handler_t handler = s_edge_handler;
    for (size_t index = 0U; index < list->count; ++index) {
        const edge_t *edge = &list->edges[index];
        if (edge->usage.usage_page == HID_PAGE_KEYBOARD && edge->usage.usage <= 0xFFU) {
            profile_export_on_key(edge->modifiers, (uint8_t)edge->usage.usage, edge->pressed);
        }
        if (handler != NULL) {
            handler(edge->usage, edge->pressed, edge->modifiers);
        }
    }
}

static void process_item(const tap_item_t *item)
{
    static edge_list_t edges;
    edges.count = 0U;

    if (item->length == 0U) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        input_state_remove_interface(&s_state, item->interface_id, record_edge, &edges);
        xSemaphoreGive(s_lock);
        dispatch_edges(&edges);
        return;
    }

    usb_host_hid_interface_info_t info;
    if (!usb_host_hid_get_interface_info(item->interface_id, &info) ||
        info.layout == NULL || !info.descriptor_parsed) {
        ++s_stats.undecodable;
        return;
    }
    static decode_context_t decode;
    memset(&decode, 0, sizeof(decode));
    if (hid_decoder_decode(info.layout, item->data, item->length,
            collect_usage, &decode) != HID_DECODER_OK) {
        ++s_stats.undecodable;
        return;
    }
    const uint8_t report_id = info.layout->uses_report_ids ? item->data[0] : 0U;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    input_state_apply_report(
        &s_state,
        item->interface_id,
        report_id,
        decode.usages,
        decode.usage_count,
        decode.dx,
        decode.dy,
        decode.wheel,
        record_edge,
        &edges);
    xSemaphoreGive(s_lock);
    ++s_stats.processed;
    dispatch_edges(&edges);
}

static void tap_task(void *argument)
{
    (void)argument;
    static tap_item_t item;
    while (true) {
        if (xQueueReceive(s_queue, &item, portMAX_DELAY) == pdPASS) {
            process_item(&item);
        }
    }
}

esp_err_t report_tap_init(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }
    input_state_reset(&s_state);
    s_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(TAP_QUEUE_DEPTH, sizeof(tap_item_t));
    if (s_lock == NULL || s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(tap_task, "report_tap", TAP_TASK_STACK, NULL,
            TAP_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Report tap ready");
    return ESP_OK;
}

void report_tap_set_edge_handler(report_tap_edge_handler_t handler)
{
    s_edge_handler = handler;
}

void report_tap_submit(uint8_t interface_id, const uint8_t *report, size_t length)
{
    if (s_queue == NULL || report == NULL || length == 0U) {
        return;
    }
    tap_item_t item = {
        .interface_id = interface_id,
        .length = (uint8_t)(length > TAP_REPORT_MAX ? TAP_REPORT_MAX : length),
    };
    memcpy(item.data, report, item.length);
    ++s_stats.submitted;
    if (xQueueSend(s_queue, &item, 0U) != pdPASS) {
        ++s_stats.dropped;
    }
}

void report_tap_interface_removed(uint8_t interface_id)
{
    if (s_queue == NULL) {
        return;
    }
    const tap_item_t item = { .interface_id = interface_id, .length = 0U };
    if (xQueueSend(s_queue, &item, 0U) != pdPASS) {
        ++s_stats.dropped;
    }
}

void report_tap_take_snapshot(input_state_t *snapshot)
{
    if (snapshot == NULL || s_lock == NULL) {
        if (snapshot != NULL) {
            input_state_reset(snapshot);
        }
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    input_state_take_snapshot(&s_state, snapshot);
    xSemaphoreGive(s_lock);
}

void report_tap_get_stats(report_tap_stats_t *stats)
{
    if (stats != NULL) {
        *stats = s_stats;
    }
}
