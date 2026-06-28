#ifndef ILI9342_H
#define ILI9342_H

#include <stdint.h>

/* ======================== ILI9342 命令宏 ======================== */
#define ILI9342_CMD_SWRESET  0x01
#define ILI9342_CMD_SLPOUT   0x11
#define ILI9342_CMD_INVOFF   0x20
#define ILI9342_CMD_INVON    0x21
#define ILI9342_CMD_DISPOFF  0x28
#define ILI9342_CMD_DISPON   0x29
#define ILI9342_CMD_CASET    0x2A
#define ILI9342_CMD_RASET    0x2B
#define ILI9342_CMD_RAMWR    0x2C
#define ILI9342_CMD_MADCTL   0x36
#define ILI9342_CMD_COLMOD   0x3A
#define ILI9342_CMD_TEOFF    0x34
#define ILI9342_CMD_TEON     0x35
#define ILI9342_CMD_TEARLINE 0x44

/* ==================== MADCTL 位定义 ==================== */
#define ILI9342_MADCTL_MY   0x80  /* Row Address Order */
#define ILI9342_MADCTL_MX   0x40  /* Column Address Order */
#define ILI9342_MADCTL_MV   0x20  /* Row/Column Exchange */
#define ILI9342_MADCTL_ML   0x10  /* Vertical Refresh Order */
#define ILI9342_MADCTL_BGR  0x08  /* RGB/BGR Order */
#define ILI9342_MADCTL_MH   0x04  /* Horizontal Refresh Order */

/* ======================== API 函数 ======================== */

/* 初始化 LCD（GPIO、SPI、上电时序、寄存器序列） */
void ili9342_init(void);

/* 设置像素窗口（CASET + RASET + RAMWR） */
void ili9342_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/* 矩形区域填充 */
void ili9342_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/* 画单像素 */
void ili9342_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/* 画位图（直接设置窗口并写入像素数据） */
void ili9342_draw_bitmap(uint16_t x_start, uint16_t y_start,
                         uint16_t x_end,   uint16_t y_end,
                         const uint16_t *pixels);

/* 缩放 draw_rgb565（源图 → 目标窗口） */
void ili9342_draw_rgb565_scaled(const uint8_t *src_pixels,
                                uint16_t src_w, uint16_t src_h,
                                uint16_t dst_w, uint16_t dst_h);

/* 背光控制（1=亮，0=灭） */
void ili9342_set_backlight(uint8_t on);

/* PWM 亮度调节（0-1000，0=灭，1000=最亮） */
void ili9342_set_brightness(uint16_t level);

/* 显示开关 */
void ili9342_display_on(void);
void ili9342_display_off(void);

/* 颜色反转 */
void ili9342_invert_color(uint8_t invert);

/* 设置旋转方向（0-3，对应 0°/90°/180°/270°） */
void ili9342_set_rotation(uint8_t rotation);

/* 设置 MADCTL 寄存器的镜像/交换位（高级用法） */
void ili9342_set_mirror(uint8_t mirror_x, uint8_t mirror_y);
void ili9342_swap_xy(uint8_t swap);

/* TE 垂直同步控制 */
void ili9342_tearing_configure(uint8_t enable, uint8_t mode, uint16_t scan_lines);

#endif /* ILI9342_H */
