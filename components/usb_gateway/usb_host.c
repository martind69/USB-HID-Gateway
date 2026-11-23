#include "usb_host.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "diagnostics.h"
#include "g815_capture.h"
#include "gateway_config.h"
#include "p4_s3_link.h"
#include "p4_s3_protocol.h"
#include "usb_device.h"
#include "usb_host_control_bridge.h"
#include "usb_host_hid.h"
#include "usb_mirror_descriptor.h"

#include "esp_bit_defs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#define USB_HOST_CLIENT_EVENT_QUEUE_SIZE    8
#define USB_HOST_MAX_DEVICE_ADDRESS         128
#define USB_HOST_DAEMON_STACK_SIZE          4096
#define USB_HOST_CLIENT_STACK_SIZE          6144

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
static volatile uint8_t s_route[USB_HOST_MAX_DEVICE_ADDRESS];
static uint8_t s_capture_address;

gateway_port_target_t usb_host_route(uint8_t device_address)
{
    if (device_address >= USB_HOST_MAX_DEVICE_ADDRESS) {
        return GATEWAY_PORT_TARGET_NONE;
    }
    return (gateway_port_target_t)s_route[device_address];
}

static void usb_host_library_task(void *argument)
{
    (void)argument;
    while (true) {
        uint32_t event_flags = 0U;
        const esp_err_t result = usb_host_lib_handle_events(
            portMAX_DELAY,
            &event_flags);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "USB Host library event handling failed: %s",
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
                usb_host_device_slot_t *slot = &s_devices[event->new_dev.address];
                slot->address = event->new_dev.address;
                slot->open_pending = true;
                ESP_LOGI(TAG, "Device enumerated, address=%u",
                    (unsigned)event->new_dev.address);
            }
            break;

        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            for (size_t index = 1U; index < USB_HOST_MAX_DEVICE_ADDRESS; ++index) {
                if (s_devices[index].opened &&
                    s_devices[index].handle == event->dev_gone.dev_hdl) {
                    s_devices[index].close_pending = true;
                    break;
                }
            }
            break;

        case USB_HOST_CLIENT_EVENT_DEV_REMOVED:
            ESP_LOGI(TAG, "Device removed, address=%u",
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
            ESP_LOGW(TAG, "Unhandled USB Host client event: %d", (int)event->event);
            break;
    }
}

/* Capture session for the device mirrored by the S3. */
static void start_capture(
    const usb_host_device_slot_t *slot,
    const usb_device_info_t *info,
    const usb_device_desc_t *device_desc,
    const usb_config_desc_t *config_desc)
{
    if (g815_capture_is_active()) {
        return;
    }
    const g815_capture_device_info_t capture_info = {
        .vendor_id = device_desc->idVendor,
        .product_id = device_desc->idProduct,
        .bcd_device = device_desc->bcdDevice,
        .address = slot->address,
        .configuration_value = info->bConfigurationValue,
        .interface_count = config_desc->bNumInterfaces,
    };
    esp_err_t error = g815_capture_start(&capture_info);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Capture not started: %s", esp_err_to_name(error));
        return;
    }
    s_capture_address = slot->address;
    error = g815_capture_write_device_descriptor(
        (const uint8_t *)device_desc,
        sizeof(*device_desc));
    if (error == ESP_OK) {
        error = g815_capture_write_configuration_descriptor(
            (const uint8_t *)config_desc,
            config_desc->wTotalLength);
    }
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Cannot store descriptors in capture: %s",
            esp_err_to_name(error));
    }
}

static void stop_capture(uint8_t address)
{
    if (address != s_capture_address || !g815_capture_is_active()) {
        return;
    }
    (void)g815_capture_event(
        G815_CAPTURE_EVENT_DETACH, 0xFFU, 0U, 0U, NULL, 0U, ESP_OK);
    const esp_err_t error = g815_capture_stop();
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Cannot stop capture: %s", esp_err_to_name(error));
    }
    s_capture_address = 0U;
}

/*
 * Assigns the device to a mirror target according to gateway.json and
 * starts the corresponding path. Device identity is only compared in
 * gateway_config, never here.
 */
static void assign_device(
    usb_host_device_slot_t *slot,
    const usb_device_info_t *info,
    const usb_device_desc_t *device_desc,
    const usb_config_desc_t *config_desc)
{
    usb_mirror_layout_t layout;
    const esp_err_t layout_error = usb_mirror_parse_configuration(
        (const uint8_t *)config_desc,
        config_desc->wTotalLength,
        usb_device_mirror_max_hid(),
        64U,
        &layout);
    if (layout_error == ESP_ERR_INVALID_SIZE) {
        ESP_LOGE(TAG, "addr=%u: malformed configuration descriptor", slot->address);
    }
    const gateway_device_identity_t identity = {
        .vendor_id = device_desc->idVendor,
        .product_id = device_desc->idProduct,
        .has_keyboard = layout.has_keyboard,
        .has_mouse = layout.has_mouse,
    };
    gateway_port_target_t target = gateway_port_claim(slot->address, &identity);

    if (target == GATEWAY_PORT_TARGET_P4_FS) {
        const esp_err_t error = usb_device_mirror_begin(
            slot->address, device_desc, config_desc, info);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "addr=%u: p4_fs mirror rejected (%s), device used as tap",
                slot->address, esp_err_to_name(error));
            gateway_port_release(slot->address);
            target = GATEWAY_PORT_TARGET_TAP;
        }
    } else if (target == GATEWAY_PORT_TARGET_S3) {
        (void)p4_s3_link_enqueue(P4_S3_MSG_G815_ATTACH, NULL, 0U);
        start_capture(slot, info, device_desc, config_desc);
    }
    if (target == GATEWAY_PORT_TARGET_P4_FS || target == GATEWAY_PORT_TARGET_S3) {
        usb_host_control_bridge_attach(target, slot->handle, slot->address);
    }
    s_route[slot->address] = (uint8_t)target;

    ESP_LOGI(TAG, "addr=%u %04X:%04X keyboard=%u mouse=%u -> %s (%s)",
        slot->address, device_desc->idVendor, device_desc->idProduct,
        layout.has_keyboard, layout.has_mouse,
        gateway_port_target_name(target),
        gateway_config_get()->from_file ? "gateway.json" : "default rules");
}

static void usb_host_open_device(usb_host_device_slot_t *slot)
{
    usb_device_info_t info = { 0 };
    const usb_device_desc_t *device_desc = NULL;
    const usb_config_desc_t *config_desc = NULL;
    diagnostics_host_device_t diagnostic_device = { 0 };

    esp_err_t result = usb_host_device_open(
        s_client_handle,
        slot->address,
        &slot->handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Cannot open USB device at address %u: %s",
            (unsigned)slot->address, esp_err_to_name(result));
        return;
    }
    slot->opened = true;

    result = usb_host_device_info(slot->handle, &info);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Cannot read USB device info at address %u: %s",
            slot->address, esp_err_to_name(result));
        goto error_close;
    }
    result = usb_host_get_device_descriptor(slot->handle, &device_desc);
    if (result != ESP_OK || device_desc == NULL) {
        ESP_LOGE(TAG, "Cannot read device descriptor at address %u: %s",
            slot->address, esp_err_to_name(result));
        goto error_close;
    }
    result = usb_host_get_active_config_descriptor(slot->handle, &config_desc);
    if (result != ESP_OK || config_desc == NULL) {
        ESP_LOGE(TAG, "Cannot read configuration descriptor at address %u: %s",
            slot->address, esp_err_to_name(result));
        goto error_close;
    }
    ESP_LOGI(TAG,
        "USB device addr=%u VID=%04X PID=%04X speed=%s EP0_MPS=%u "
        "bcdUSB=0x%04X configuration=%u interfaces=%u",
        (unsigned)slot->address,
        (unsigned)device_desc->idVendor,
        (unsigned)device_desc->idProduct,
        info.speed == USB_SPEED_HIGH ? "HS" :
            info.speed == USB_SPEED_FULL ? "FS" : "LS",
        (unsigned)device_desc->bMaxPacketSize0,
        (unsigned)device_desc->bcdUSB,
        (unsigned)info.bConfigurationValue,
        (unsigned)config_desc->bNumInterfaces);

    diagnostic_device.present = true;
    diagnostic_device.address = slot->address;
    diagnostic_device.vid = device_desc->idVendor;
    diagnostic_device.pid = device_desc->idProduct;
    diagnostic_device.interface_count = config_desc->bNumInterfaces;
    diagnostics_host_device_added(&diagnostic_device);

    assign_device(slot, &info, device_desc, config_desc);
    return;

error_close:
    (void)usb_host_device_close(s_client_handle, slot->handle);
    slot->handle = NULL;
    slot->opened = false;
}

static void usb_host_close_device(usb_host_device_slot_t *slot)
{
    if (slot->opened && slot->handle != NULL) {
        const gateway_port_target_t target = usb_host_route(slot->address);
        s_route[slot->address] = (uint8_t)GATEWAY_PORT_TARGET_NONE;
        if (target == GATEWAY_PORT_TARGET_S3) {
            (void)p4_s3_link_enqueue(P4_S3_MSG_G815_DETACH, NULL, 0U);
        }
        /* Ends a p4_fs session; also drops cached report descriptors. */
        usb_device_mirror_end(slot->address);
        usb_host_control_bridge_detach(slot->address);
        gateway_port_release(slot->address);
        stop_capture(slot->address);
        diagnostics_host_device_removed(slot->address);
        const esp_err_t result = usb_host_device_close(s_client_handle, slot->handle);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "Closing USB device addr=%u failed: %s",
                (unsigned)slot->address, esp_err_to_name(result));
        }
    }
    memset(slot, 0, sizeof(*slot));
}

static void usb_host_process_pending_devices(void)
{
    for (size_t index = 1U; index < USB_HOST_MAX_DEVICE_ADDRESS; ++index) {
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
    esp_err_t result = usb_host_client_register(&client_config, &s_client_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "USB Host client registration failed: %s",
            esp_err_to_name(result));
        diagnostics_set_host_state(DIAG_STATE_ERROR, result);
        vTaskDelete(NULL);
        return;
    }
    result = usb_host_control_bridge_init(s_client_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "EP0 bridge initialization failed: %s",
            esp_err_to_name(result));
    }
    ESP_LOGI(TAG, "USB Host client registered; waiting for devices");
    while (true) {
        result = usb_host_client_handle_events(s_client_handle, pdMS_TO_TICKS(100U));
        if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "USB Host client event handling failed: %s",
                esp_err_to_name(result));
        }
        usb_host_process_pending_devices();
    }
}

void usb_host_init(void)
{
    const usb_host_config_t host_config = {
        .peripheral_map = BIT0,
        .fsls_only = true,
    };
    esp_err_t result = usb_host_install(&host_config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "USB Host installation failed: %s", esp_err_to_name(result));
        diagnostics_set_host_state(DIAG_STATE_ERROR, result);
        return;
    }
    result = usb_host_hid_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "USB Host HID driver installation failed: %s",
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
    ESP_LOGI(TAG, "USB Host ready: HS OTG operating FS/LS-only for hub compatibility");
}

void usb_host_process(void)
{
    /* USB Host library and client events run in dedicated tasks. */
}
