#include "ssd1306_status_renderer.h"

#include "ssd1306_font.h"

#include <stdio.h>

void ssd1306_render_status(
    uint8_t *framebuffer,
    const ssd1306_status_view_t *view)
{
    if (framebuffer == NULL || view == NULL) {
        return;
    }

    char mode_line[18];
    char profile_line[22];
    char health_line[20];

    ssd1306_fb_clear(framebuffer, SSD1306_BUFFER_SIZE);
    ssd1306_fb_text(framebuffer, 0U, 0U, "USB HID GATEWAY");

    (void)snprintf(
        mode_line,
        sizeof(mode_line),
        "MODE %.11s",
        view->mode);
    ssd1306_fb_text(framebuffer, 0U, 1U, mode_line);

    (void)snprintf(
        profile_line,
        sizeof(profile_line),
        "PROFILE %.13s",
        view->profile);
    ssd1306_fb_text(framebuffer, 0U, 2U, profile_line);

    ssd1306_fb_text(framebuffer, 0U, 3U, view->detail);
    ssd1306_fb_progress(
        framebuffer,
        0U,
        4U,
        100U,
        view->progress_percent);

    (void)snprintf(
        health_line,
        sizeof(health_line),
        "H:%s D:%s A:%s",
        view->usb_host_ok ? "OK" : "ER",
        view->usb_device_ok ? "OK" : "ER",
        view->analog_ok ? "OK" : "ER");
    ssd1306_fb_text(framebuffer, 0U, 6U, health_line);
}
