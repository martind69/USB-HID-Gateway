#include "ssd1306_status_renderer.h"

#include <stdio.h>
#include <string.h>

#include "ssd1306_font.h"

/* 6 px per character (5 px glyph + 1 px gap) on 128 px. */
#define TEXT_COLUMNS        21U
#define PROGRESS_WIDTH      100U
#define PERCENT_X           104U

#define PAGE_TITLE          0U
#define PAGE_MODE           1U
#define PAGE_INFO           2U
#define PAGE_STATE          3U
#define PAGE_PROGRESS       4U
#define PAGE_HEALTH         6U

/* Renders "LABEL: value", the value is truncated to the remaining columns. */
static void render_field(
    uint8_t *framebuffer,
    uint8_t page,
    const char *label,
    const char *value)
{
    char line[TEXT_COLUMNS + 1U];
    const char *text = (value != NULL && value[0] != '\0') ? value : "-";
    const size_t label_length = strlen(label);
    const int available = (label_length + 2U < TEXT_COLUMNS)
        ? (int)(TEXT_COLUMNS - label_length - 2U)
        : 0;

    (void)snprintf(line, sizeof(line), "%s: %.*s", label, available, text);
    ssd1306_fb_text(framebuffer, 0U, page, line);
}

void ssd1306_render_status(
    uint8_t *framebuffer,
    const ssd1306_status_view_t *view)
{
    if (framebuffer == NULL || view == NULL) {
        return;
    }

    ssd1306_fb_clear(framebuffer, SSD1306_BUFFER_SIZE);
    ssd1306_fb_text(framebuffer, 0U, PAGE_TITLE, "USB HID GATEWAY");

    render_field(framebuffer, PAGE_MODE, "MODE", view->mode);
    render_field(
        framebuffer,
        PAGE_INFO,
        view->info_label[0] != '\0' ? view->info_label : "PROFILE",
        view->info);
    render_field(framebuffer, PAGE_STATE, "STATE", view->detail);

    if (view->show_progress) {
        const uint8_t percent = view->progress_percent > 100U
            ? 100U
            : view->progress_percent;
        char percent_text[5];

        ssd1306_fb_progress(framebuffer, 0U, PAGE_PROGRESS, PROGRESS_WIDTH, percent);
        (void)snprintf(percent_text, sizeof(percent_text), "%3u%%", (unsigned)percent);
        ssd1306_fb_text(framebuffer, PERCENT_X, PAGE_PROGRESS, percent_text);
    }

    char health_line[TEXT_COLUMNS + 1U];
    (void)snprintf(
        health_line,
        sizeof(health_line),
        "H:%s D:%s A:%s",
        view->usb_host_ok ? "OK" : "ER",
        view->usb_device_ok ? "OK" : "ER",
        view->analog_ok ? "OK" : "ER");
    ssd1306_fb_text(framebuffer, 0U, PAGE_HEALTH, health_line);
}
