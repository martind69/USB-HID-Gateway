#pragma once
#include <stddef.h>
#include <stdint.h>
#define SSD1306_FONT_WIDTH 5U
#define SSD1306_FONT_HEIGHT 7U
void ssd1306_fb_clear(uint8_t *framebuffer,size_t size);
void ssd1306_fb_pixel(uint8_t *framebuffer,uint8_t x,uint8_t y,bool on);
void ssd1306_fb_char(uint8_t *framebuffer,uint8_t x,uint8_t page,char character);
void ssd1306_fb_text(uint8_t *framebuffer,uint8_t x,uint8_t page,const char *text);
void ssd1306_fb_progress(uint8_t *framebuffer,uint8_t x,uint8_t page,uint8_t width,uint8_t percent);
