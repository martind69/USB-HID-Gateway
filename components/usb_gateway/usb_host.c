#include "usb_host.h"
#include "usb_host_hid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "diagnostics.h"
#include "g815_capture.h"
#include "esp_bit_defs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#define USB_HOST_CLIENT_EVENT_QUEUE_SIZE 8
#define USB_HOST_MAX_DEVICE_ADDRESS 128
#define USB_HOST_DAEMON_STACK_SIZE 4096
#define USB_HOST_CLIENT_STACK_SIZE 6144
#define G815_VENDOR_ID 0x046DU
#define G815_PRODUCT_ID 0xC33FU

typedef struct {
    bool open_pending;
    bool close_pending;
    bool opened;
    uint8_t address;
    usb_device_handle_t handle;
} usb_host_device_slot_t;

static const char *TAG = "usb_host";
static usb_host_client_handle_t s_client_handle;
static usb_host_device_slot_t s_devices[USB_HOST_MAX_DEVICE_ADDRESS];
static uint8_t s_capture_address;

static void usb_host_library_task(void *argument)
{
    (void)argument;

    while (true) {
        uint32_t event_flags = 0U;
        const esp_err_t result = usb_host_lib_handle_events(
            portMAX_DELAY,
            &event_flags);

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "USB Host library event handling failed: %s",
                esp_err_to_name(result));
            continue;
        }

        if ((event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) != 0U) {
            ESP_LOGI(TAG, "USB Host has no registered clients");
        }

        if ((event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) != 0U) {
            ESP_LOGI(TAG, "USB Host has released all device resources");
        }
    }
}

static void usb_host_client_event_callback(
    const usb_host_client_event_msg_t *event,
    void *argument)
{
    (void)argument;

    if (event == NULL) {
        return;
    }

    switch (event->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            if (event->new_dev.address < USB_HOST_MAX_DEVICE_ADDRESS) {
                usb_host_device_slot_t *slot =
                    &s_devices[event->new_dev.address];
                slot->address = event->new_dev.address;
                slot->open_pending = true;
                ESP_LOGI(
                    TAG,
                    "Device enumerated, address=%u",
                    (unsigned)event->new_dev.address);
            }
            break;

        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            for (size_t index = 1U;
                 index < USB_HOST_MAX_DEVICE_ADDRESS;
                 ++index) {
                if (s_devices[index].opened &&
                    s_devices[index].handle == event->dev_gone.dev_hdl) {
                    s_devices[index].close_pending = true;
                    break;
                }
            }
            break;

        case USB_HOST_CLIENT_EVENT_DEV_REMOVED:
            ESP_LOGI(
                TAG,
                "Device removed, address=%u",
                (unsigned)event->dev_removed.address);
            if (event->dev_removed.address < USB_HOST_MAX_DEVICE_ADDRESS) {
                usb_host_device_slot_t *slot =
                    &s_devices[event->dev_removed.address];
                if (!slot->opened) {
                    memset(slot, 0, sizeof(*slot));
                }
            }
            break;

        case USB_HOST_CLIENT_EVENT_DEV_SUSPENDED:
            ESP_LOGI(TAG, "USB device suspended");
            break;

        case USB_HOST_CLIENT_EVENT_DEV_RESUMED:
            ESP_LOGI(TAG, "USB device resumed");
            break;

        default:
            ESP_LOGW(
                TAG,
                "Unhandled USB Host client event: %d",
                (int)event->event);
            break;
    }
}

static void start_g815_capture(
    const usb_host_device_slot_t *slot,
    const usb_device_info_t *info,
    const usb_device_desc_t *device_descriptor,
    const usb_config_desc_t *configuration_descriptor)
{
    if (slot == NULL ||
        info == NULL ||
        device_descriptor == NULL ||
        configuration_descriptor == NULL ||
        device_descriptor->idVendor != G815_VENDOR_ID ||
        device_descriptor->idProduct != G815_PRODUCT_ID ||
        g815_capture_is_active()) {
        return;
    }

    const g815_capture_device_info_t capture_info = {
        .vendor_id = device_descriptor->idVendor,
        .product_id = device_descriptor->idProduct,
        .bcd_device = device_descriptor->bcdDevice,
        .address = slot->address,
        .configuration_value = info->bConfigurationValue,
        .interface_count = configuration_descriptor->bNumInterfaces,
    };

    esp_err_t error = g815_capture_start(&capture_info);
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Cannot start G815 capture: %s",
            esp_err_to_name(error));
        return;
    }

    s_capture_address = slot->address;

    error = g815_capture_write_device_descriptor(
        (const uint8_t *)device_descriptor,
        sizeof(*device_descriptor));
    if (error != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Cannot store G815 device descriptor: %s",
            esp_err_to_name(error));
    }

    error = g815_capture_write_configuration_descriptor(
        (const uint8_t *)configuration_descriptor,
        configuration_descriptor->wTotalLength);
    if (error != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Cannot store G815 configuration descriptor: %s",
            esp_err_to_name(error));
    }

    ESP_LOGI(
        TAG,
        "G815 capture started: addr=%u VID=%04X PID=%04X",
        slot->address,
        device_descriptor->idVendor,
        device_descriptor->idProduct);
}

static void usb_host_open_device(usb_host_device_slot_t *slot)
{
    usb_device_info_t info = { 0 };
    const usb_device_desc_t *device_descriptor = NULL;
    const usb_config_desc_t *configuration_descriptor = NULL;
    diagnostics_host_device_t diagnostic_device = { 0 };

    esp_err_t result = usb_host_device_open(
        s_client_handle,
        slot->address,
        &slot->handle);
    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Cannot open USB device at address %u: %s",
            (unsigned)slot->address,
            esp_err_to_name(result));
        return;
    }

    slot->opened = true;

    result = usb_host_device_info(slot->handle, &info);
    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Cannot read USB device info at address %u: %s",
            slot->address,
            esp_err_to_name(result));
        goto error_close;
    }

    result = usb_host_get_device_descriptor(
        slot->handle,
        &device_descriptor);
    if (result != ESP_OK || device_descriptor == NULL) {
        ESP_LOGE(
            TAG,
            "Cannot read device descriptor at address %u: %s",
            slot->address,
            esp_err_to_name(result));
        goto error_close;
    }

    ESP_LOGI(
        TAG,
        "USB device addr=%u VID=%04X PID=%04X speed=%s "
        "EP0_MPS=%u bcdUSB=0x%04X configuration=%u",
        (unsigned)slot->address,
        (unsigned)device_descriptor->idVendor,
        (unsigned)device_descriptor->idProduct,
        info.speed == USB_SPEED_HIGH ? "HS" :
            info.speed == USB_SPEED_FULL ? "FS" : "LS",
        (unsigned)device_descriptor->bMaxPacketSize0,
        (unsigned)device_descriptor->bcdUSB,
        (unsigned)info.bConfigurationValue);

    result = usb_host_get_active_config_descriptor(
        slot->handle,
        &configuration_descriptor);
    if (result == ESP_OK && configuration_descriptor != NULL) {
        ESP_LOGI(
            TAG,
            "USB configuration addr=%u interfaces=%u attributes=0x%02X",
            (unsigned)slot->address,
            (unsigned)configuration_descriptor->bNumInterfaces,
            (unsigned)configuration_descriptor->bmAttributes);
        diagnostic_device.interface_count =
            configuration_descriptor->bNumInterfaces;
    } else {
        ESP_LOGW(
            TAG,
            "Configuration descriptor unavailable for addr=%u: %s",
            slot->address,
            esp_err_to_name(result));
    }

    diagnostic_device.present = true;
    diagnostic_device.address = slot->address;
    diagnostic_device.vid = device_descriptor->idVendor;
    diagnostic_device.pid = device_descriptor->idProduct;
    diagnostics_host_device_added(&diagnostic_device);

    start_g815_capture(
        slot,
        &info,
        device_descriptor,
        configuration_descriptor);
    return;

error_close:
    (void)usb_host_device_close(s_client_handle, slot->handle);
    slot->handle = NULL;
    slot->opened = false;
}

static void stop_g815_capture(uint8_t address)
{
    if (address != s_capture_address || !g815_capture_is_active()) {
        return;
    }

    (void)g815_capture_event(
        G815_CAPTURE_EVENT_DETACH,
        0xFFU,
        0U,
        0U,
        NULL,
        0U,
        ESP_OK);

    const esp_err_t error = g815_capture_stop();
    if (error != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Cannot stop G815 capture: %s",
            esp_err_to_name(error));
    }

    s_capture_address = 0U;
}

static void usb_host_close_device(usb_host_device_slot_t *slot)
{
    if (slot->opened && slot->handle != NULL) {
        stop_g815_capture(slot->address);
        diagnostics_host_device_removed(slot->address);

        const esp_err_t result = usb_host_device_close(
            s_client_handle,
            slot->handle);
        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Closing USB device addr=%u failed: %s",
                (unsigned)slot->address,
                esp_err_to_name(result));
        }
    }

    memset(slot, 0, sizeof(*slot));
}

static void usb_host_process_pending_devices(void)
{
    for (size_t index = 1U;
         index < USB_HOST_MAX_DEVICE_ADDRESS;
         ++index) {
        usb_host_device_slot_t *slot = &s_devices[index];

        if (slot->close_pending) {
            usb_host_close_device(slot);
            continue;
        }

        if (slot->open_pending) {
            slot->open_pending = false;
            usb_host_open_device(slot);
        }
    }
}

static void usb_host_client_task(void *argument)
{
    (void)argument;

    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = USB_HOST_CLIENT_EVENT_QUEUE_SIZE,
        .flags = {
            .notify_dev_removed = 1,
        },
        .async = {
            .client_event_callback = usb_host_client_event_callback,
            .callback_arg = NULL,
        },
    };

    esp_err_t result = usb_host_client_register(
        &client_config,
        &s_client_handle);
    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "USB Host client registration failed: %s",
            esp_err_to_name(result));
        diagnostics_set_host_state(DIAG_STATE_ERROR, result);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "USB Host client registered; waiting for devices");

    while (true) {
        result = usb_host_client_handle_events(
            s_client_handle,
            pdMS_TO_TICKS(100U));
        if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
            ESP_LOGE(
                TAG,
                "USB Host client event handling failed: %s",
                esp_err_to_name(result));
        }
        usb_host_process_pending_devices();
    }
}

void usb_host_init(void)
{
    esp_log_level_set("USBH", ESP_LOG_DEBUG);
    esp_log_level_set("ENUM", ESP_LOG_DEBUG);
    esp_log_level_set("HUB", ESP_LOG_DEBUG);
    esp_log_level_set("EXT_PORT", ESP_LOG_DEBUG);

    const usb_host_config_t host_config = {
        .peripheral_map = BIT0,
        .fsls_only = true,
    };

    esp_err_t result = usb_host_install(&host_config);
    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "USB Host installation failed: %s",
            esp_err_to_name(result));
        diagnostics_set_host_state(DIAG_STATE_ERROR, result);
        return;
    }

    result = usb_host_hid_init();
    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "USB Host HID driver installation failed: %s",
            esp_err_to_name(result));
    }

    BaseType_t task_result = xTaskCreate(
        usb_host_library_task,
        "usb_host_lib",
        USB_HOST_DAEMON_STACK_SIZE,
        NULL,
        configMAX_PRIORITIES - 3,
        NULL);
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Cannot create USB Host library task");
        diagnostics_set_host_state(DIAG_STATE_ERROR, ESP_ERR_NO_MEM);
        return;
    }

    task_result = xTaskCreate(
        usb_host_client_task,
        "usb_host_client",
        USB_HOST_CLIENT_STACK_SIZE,
        NULL,
        configMAX_PRIORITIES - 4,
        NULL);
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Cannot create USB Host client task");
        diagnostics_set_host_state(DIAG_STATE_ERROR, ESP_ERR_NO_MEM);
        return;
    }

    diagnostics_set_host_state(DIAG_STATE_READY, ESP_OK);
    ESP_LOGI(
        TAG,
        "USB Host HID ingress ready; raw report forwarding to the PC "
        "is not yet connected");
    ESP_LOGI(
        TAG,
        "USB Host controller: HS OTG operating FS/LS-only for hub compatibility");
}

void usb_host_process(void)
{
    /* USB Host library and client events run in dedicated tasks. */
}
