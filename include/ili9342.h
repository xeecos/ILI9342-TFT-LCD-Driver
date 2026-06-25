#ifndef ILI9342_H
#define ILI9342_H

#include <stdint.h>

void ili9342_init(void);
void ili9342_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void ili9342_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
void ili9342_draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void ili9342_draw_rgb565_scaled(const uint8_t *src_pixels, uint16_t src_w, uint16_t src_h, uint16_t dst_w, uint16_t dst_h);
void ili9342_set_backlight(uint8_t on);

#endif
