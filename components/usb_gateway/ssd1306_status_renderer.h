#pragma once
#include <stdint.h>
#include "ssd1306_display.h"
void ssd1306_render_status(uint8_t framebuffer[SSD1306_BUFFER_SIZE],const ssd1306_status_view_t *view);
