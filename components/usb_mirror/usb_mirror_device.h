#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "usb_mirror_descriptor.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Transparent USB mirror device (TinyUSB, full-speed port 0).
 * Used unchanged on the P4 (USB1P1) and on the S3.
 *
 * The PC sees exactly the device whose descriptors are passed in a session:
 *     usb_mirror_device_begin()                  device, configuration, strings
 *     usb_mirror_device_set_report_descriptor()  per HID interface, any order
 *     -> PC port connects when all report descriptors are present
 *     usb_mirror_device_input()                  raw input reports, unchanged
 *     usb_mirror_device_end()                    PC port disconnects
 *
 * TinyUSB is installed in usb_mirror_device_init() and immediately
 * disconnected, so the PC sees nothing until a session is complete.
 *
 * PC -> device requests (SET_REPORT, interrupt OUT) are delivered to the
 * set_report sink with the complete report (Report ID restored).
 * GET_REPORT is answered with STALL and counted (see stats).
 */

/*
 * Called in the TinyUSB task; must not block.
 * report_type: 2 = output, 3 = feature.
 */
typedef void (*usb_mirror_set_report_sink_t)(
    uint8_t interface_number,
    uint8_t report_id,
    uint8_t report_type,
    const uint8_t *data,
    size_t length);

typedef struct {
    usb_mirror_set_report_sink_t set_report;
} usb_mirror_device_config_t;

typedef struct {
    uint8_t session;
    uint8_t state;                  /* p4_s3_mirror_state_t values */
    bool mounted;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t hid_count;
    uint32_t reports_sent;
    uint32_t reports_dropped;
    uint32_t set_report_forwarded;
    uint32_t set_report_dropped;
    uint32_t get_report_stalled;
    uint8_t last_get_interface;
    uint8_t last_get_report_id;
    uint8_t last_get_report_type;
} usb_mirror_device_stats_t;

esp_err_t usb_mirror_device_init(const usb_mirror_device_config_t *config);

/* Maximum mirrorable HID interfaces (min of CFG_TUD_HID and parser limit). */
size_t usb_mirror_device_max_hid(void);

/* Maximum wMaxPacketSize of a mirrorable interrupt endpoint. */
uint16_t usb_mirror_device_max_packet_size(void);

/*
 * strings[i] is the ASCII text for string index i (1..USB_MIRROR_STRING_COUNT-1),
 * NULL entries are mirrored as empty strings. Ends a running session first.
 */
esp_err_t usb_mirror_device_begin(
    uint8_t session,
    const uint8_t device_descriptor[USB_MIRROR_DEVICE_DESC_LENGTH],
    const uint8_t *configuration,
    size_t configuration_length,
    const char *const strings[USB_MIRROR_STRING_COUNT]);

void usb_mirror_device_set_report_descriptor(
    uint8_t session,
    uint8_t interface_number,
    const uint8_t *descriptor,
    size_t length);

bool usb_mirror_device_input(
    uint8_t session,
    uint8_t interface_number,
    const uint8_t *report,
    size_t length);

/* session 0 ends any running session. */
void usb_mirror_device_end(uint8_t session);

bool usb_mirror_device_connected(void);
bool usb_mirror_device_mounted(void);

void usb_mirror_device_get_stats(usb_mirror_device_stats_t *stats);

#ifdef __cplusplus
}
#endif
