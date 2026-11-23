#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_WIDTH           128U
#define SSD1306_HEIGHT          64U
#define SSD1306_BUFFER_SIZE     1024U

/*
 * Status view. The renderer adds the labels, every line has the form
 * "LABEL: value" (5x7 font, 21 columns):
 *
 *     USB HID GATEWAY
 *     MODE: <mode>
 *     <info_label>: <info>         e.g. "PROFILE: Default", "VERSION: 1.4.0"
 *     STATE: <detail>
 *     [progress bar]  nn%          only if show_progress
 *     H:OK D:OK A:OK
 *
 * Empty values are shown as "-". Empty info_label defaults to "PROFILE".
 */
typedef struct {
    char mode[12];
    char info_label[8];
    char info[17];
    char detail[22];
    uint8_t progress_percent;
    bool show_progress;
    bool usb_host_ok;
    bool usb_device_ok;
    bool analog_ok;
} ssd1306_status_view_t;

esp_err_t ssd1306_display_init(void);

void ssd1306_display_set_view(const ssd1306_status_view_t *view);

#ifdef __cplusplus
}
#endif
