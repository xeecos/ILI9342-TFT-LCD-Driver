#include "ili9342.h"
#include "ch32v30x.h"
#include "ch32v30x_gpio.h"
#include "ch32v30x_rcc.h"
#include "ch32v30x_spi.h"
#include "ch32v30x_tim.h"
#include <stddef.h>

/* ======================== 引脚定义 ======================== */
#define LCD_RS_PIN  GPIO_Pin_1
#define LCD_RST_PIN GPIO_Pin_6
#define LCD_SDA_PIN GPIO_Pin_15
#define LCD_SCL_PIN GPIO_Pin_13
#define LCD_CS_PIN  GPIO_Pin_12
#define LCD_PWM_PIN GPIO_Pin_9

/* ======================== 寄存器宏 ======================== */
#define REG_EXTC      0xC8
#define REG_PWR_CTL1  0xC0
#define REG_PWR_CTL2  0xC1
#define REG_VCOM_CTL1 0xC5
#define REG_RGB_IFACE 0xB0
#define REG_IFACE_CTL 0xF6
#define REG_PGAM_CTL  0xE0
#define REG_NGAM_CTL  0xE1
#define REG_DISP_CTL  0xB6

/* ==================== 底层 SPI 接口 ==================== */

static void lcd_write_cmd(uint8_t cmd)
{
    GPIO_ResetBits(GPIOB, LCD_RS_PIN);
    GPIO_ResetBits(GPIOB, LCD_CS_PIN);
    SPI_I2S_SendData(SPI2, cmd);
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET) {}
    GPIO_SetBits(GPIOB, LCD_CS_PIN);
}

static void lcd_write_data(uint8_t data)
{
    GPIO_SetBits(GPIOB, LCD_RS_PIN);
    GPIO_ResetBits(GPIOB, LCD_CS_PIN);
    SPI_I2S_SendData(SPI2, data);
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET) {}
    GPIO_SetBits(GPIOB, LCD_CS_PIN);
}

static void lcd_write_data16(uint16_t data)
{
    lcd_write_data((data >> 8) & 0xFF);
    lcd_write_data(data & 0xFF);
}

static void lcd_write_cmd_args(uint8_t cmd, const uint8_t *args, uint8_t n)
{
    lcd_write_cmd(cmd);
    for (uint8_t i = 0; i < n; i++) {
        lcd_write_data(args[i]);
    }
}

/* ======================== 延时 ======================== */

static void delay_ms(unsigned int ms)
{
    for (volatile unsigned int i = 0; i < ms * 10000U; ++i) {
    }
}

/* ======================== MADCTL 状态 ======================== */

static uint8_t g_madctl = 0;

/* ==================== 初始化序列（参考 ESP-IDF 驱动优化） ==================== */

/*
 * 初始化命令表项:
 *   { 命令,  参数字节, 参数数组, 延时(ms) }
 */
typedef struct {
    uint8_t cmd;
    uint8_t data_bytes;
    const uint8_t *data;
    uint16_t delay_ms;
} lcd_init_cmd_t;

/* 供应商自定义初始化参数（来自 ESP-IDF ili9342 驱动） */
static const uint8_t init_data_extc[]      = { 0xFF, 0x93, 0x42 };
static const uint8_t init_data_pwr1[]      = { 0x12, 0x12 };        /* VRH, VC */
static const uint8_t init_data_pwr2[]      = { 0x03 };              /* SAP, BT */
static const uint8_t init_data_vcom[]      = { 0xF2 };
static const uint8_t init_data_rgb_iface[] = { 0xE0 };
static const uint8_t init_data_iface_ctl[] = { 0x01, 0x00, 0x00 };
static const uint8_t init_data_pgam[]      = { 0x00, 0x0C, 0x11, 0x04, 0x11,
                                                0x08, 0x37, 0x89, 0x4C, 0x06,
                                                0x0C, 0x0A, 0x2E, 0x34, 0x0F };
static const uint8_t init_data_ngam[]      = { 0x00, 0x0B, 0x11, 0x05, 0x13,
                                                0x09, 0x33, 0x67, 0x48, 0x07,
                                                0x0E, 0x0B, 0x2E, 0x33, 0x0F };
static const uint8_t init_data_disp_ctl[]  = { 0x08, 0x82, 0x1D, 0x04 };

static const lcd_init_cmd_t s_init_sequence[] = {
    /* Sleep Out — 退出睡眠模式 */
    { ILI9342_CMD_SLPOUT, 0, NULL, 120 },

    /* Memory Access Control — 默认 BGR */
    { ILI9342_CMD_MADCTL, 1, (const uint8_t[]){ ILI9342_MADCTL_BGR }, 0 },

    /* Pixel Format Set — RGB565 */
    { ILI9342_CMD_COLMOD, 1, (const uint8_t[]){ 0x55 }, 0 },

    /* 开启扩展命令 */
    { REG_EXTC,      sizeof(init_data_extc),      init_data_extc,      0 },
    /* 电源控制 1 */
    { REG_PWR_CTL1,  sizeof(init_data_pwr1),      init_data_pwr1,      0 },
    /* 电源控制 2 */
    { REG_PWR_CTL2,  sizeof(init_data_pwr2),      init_data_pwr2,      0 },
    /* VCOM 控制 */
    { REG_VCOM_CTL1, sizeof(init_data_vcom),      init_data_vcom,      0 },
    /* RGB 接口设置 */
    { REG_RGB_IFACE, sizeof(init_data_rgb_iface), init_data_rgb_iface, 0 },
    /* 接口控制 */
    { REG_IFACE_CTL, sizeof(init_data_iface_ctl), init_data_iface_ctl, 0 },
    /* 正伽玛校正 */
    { REG_PGAM_CTL,  sizeof(init_data_pgam),      init_data_pgam,      0 },
    /* 负伽玛校正 */
    { REG_NGAM_CTL,  sizeof(init_data_ngam),      init_data_ngam,      0 },
    /* 显示控制 */
    { REG_DISP_CTL,  sizeof(init_data_disp_ctl),  init_data_disp_ctl,  0 },

    /* Display On */
    { ILI9342_CMD_DISPON, 0, NULL, 0 },
};

void ili9342_init(void)
{
    /* ---- GPIO & SPI 时钟 ---- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB
                           | RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

    /* ---- GPIO 初始化 ---- */
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;

    gpio_init.GPIO_Pin = LCD_CS_PIN;
    GPIO_Init(GPIOB, &gpio_init);
    gpio_init.GPIO_Pin = LCD_RS_PIN;
    GPIO_Init(GPIOB, &gpio_init);
    gpio_init.GPIO_Pin = LCD_RST_PIN;
    GPIO_Init(GPIOC, &gpio_init);
    gpio_init.GPIO_Pin = LCD_PWM_PIN;
    GPIO_Init(GPIOA, &gpio_init);

    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio_init.GPIO_Pin  = LCD_SDA_PIN | LCD_SCL_PIN;
    GPIO_Init(GPIOB, &gpio_init);

    /* 初始电平 */
    GPIO_SetBits(GPIOB, LCD_CS_PIN);
    GPIO_SetBits(GPIOB, LCD_RS_PIN);
    GPIO_SetBits(GPIOC, LCD_RST_PIN);

    /* ---- SPI 初始化 ---- */
    SPI_InitTypeDef spi_init = {0};
    spi_init.SPI_Direction           = SPI_Direction_2Lines_FullDuplex;
    spi_init.SPI_Mode                = SPI_Mode_Master;
    spi_init.SPI_DataSize            = SPI_DataSize_8b;
    spi_init.SPI_CPOL                = SPI_CPOL_Low;
    spi_init.SPI_CPHA                = SPI_CPHA_1Edge;
    spi_init.SPI_NSS                 = SPI_NSS_Soft;
    spi_init.SPI_BaudRatePrescaler   = SPI_BaudRatePrescaler_8;
    spi_init.SPI_FirstBit            = SPI_FirstBit_MSB;
    spi_init.SPI_CRCPolynomial       = 7;
    SPI_Init(SPI2, &spi_init);
    SPI_Cmd(SPI2, ENABLE);

    /* ---- 背光 PWM 初始化 (TIM1_CH2, PA9) ---- */
    /* 重新配置 PA9 为复用推挽输出 */
    gpio_init.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio_init.GPIO_Pin   = LCD_PWM_PIN;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio_init);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

    TIM_TimeBaseInitTypeDef tim_base = {0};
    tim_base.TIM_Prescaler         = 144 - 1;        /* 144 MHz / 144 = 1 MHz */
    tim_base.TIM_CounterMode       = TIM_CounterMode_Up;
    tim_base.TIM_Period            = 1000 - 1;        /* 1 MHz / 1000 = 1 kHz */
    tim_base.TIM_ClockDivision     = TIM_CKD_DIV1;
    tim_base.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &tim_base);

    TIM_OCInitTypeDef tim_oc = {0};
    tim_oc.TIM_OCMode       = TIM_OCMode_PWM1;
    tim_oc.TIM_OutputState  = TIM_OutputState_Enable;
    tim_oc.TIM_OutputNState = TIM_OutputNState_Disable;
    tim_oc.TIM_Pulse        = 0;                     /* 初始占空比 0% */
    tim_oc.TIM_OCPolarity   = TIM_OCPolarity_High;
    tim_oc.TIM_OCNPolarity  = TIM_OCNPolarity_High;
    tim_oc.TIM_OCIdleState  = TIM_OCIdleState_Set;
    tim_oc.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
    TIM_OC2Init(TIM1, &tim_oc);

    TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);                /* 高级定时器主输出使能 */
    TIM_Cmd(TIM1, ENABLE);

    /* ---- 硬件复位 ---- */
    GPIO_SetBits(GPIOC, LCD_RST_PIN);
    delay_ms(1);
    GPIO_ResetBits(GPIOC, LCD_RST_PIN);
    delay_ms(10);
    GPIO_SetBits(GPIOC, LCD_RST_PIN);
    delay_ms(120);

    /* ---- 初始化命令序列 ---- */
    g_madctl = ILI9342_MADCTL_BGR;   /* 与序列中的 MADCTL 值一致 */

    for (size_t i = 0; i < sizeof(s_init_sequence) / sizeof(s_init_sequence[0]); i++) {
        const lcd_init_cmd_t *c = &s_init_sequence[i];
        lcd_write_cmd_args(c->cmd, c->data, c->data_bytes);
        if (c->delay_ms) {
            delay_ms(c->delay_ms);
        }
    }
}

/* ======================== 窗口设置 ======================== */

void ili9342_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_write_cmd(ILI9342_CMD_CASET);
    lcd_write_data16(x0);
    lcd_write_data16(x1);
    lcd_write_cmd(ILI9342_CMD_RASET);
    lcd_write_data16(y0);
    lcd_write_data16(y1);
    lcd_write_cmd(ILI9342_CMD_RAMWR);
}

/* ======================== 绘图函数 ======================== */

void ili9342_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    uint32_t pixels = (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1);
    ili9342_set_window(x0, y0, x1, y1);

    uint8_t hi = (color >> 8) & 0xFF;
    uint8_t lo = color & 0xFF;
    for (uint32_t i = 0; i < pixels; ++i) {
        lcd_write_data(hi);
        lcd_write_data(lo);
    }
}

void ili9342_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    ili9342_set_window(x, y, x, y);
    lcd_write_data((color >> 8) & 0xFF);
    lcd_write_data(color & 0xFF);
}

void ili9342_draw_bitmap(uint16_t x_start, uint16_t y_start,
                         uint16_t x_end,   uint16_t y_end,
                         const uint16_t *pixels)
{
    uint16_t w = x_end - x_start + 1;
    uint16_t h = y_end - y_start + 1;

    ili9342_set_window(x_start, y_start, x_end, y_end);

    GPIO_SetBits(GPIOB, LCD_RS_PIN);
    GPIO_ResetBits(GPIOB, LCD_CS_PIN);
    for (uint16_t y = 0; y < h; y++) {
        for (uint16_t x = 0; x < w; x++) {
            uint16_t color = pixels[y * w + x];
            SPI_I2S_SendData(SPI2, (color >> 8) & 0xFF);
            while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET) {}
            SPI_I2S_SendData(SPI2, color & 0xFF);
            while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET) {}
        }
    }
    GPIO_SetBits(GPIOB, LCD_CS_PIN);
}

void ili9342_draw_rgb565_scaled(const uint8_t *src_pixels,
                                uint16_t src_w, uint16_t src_h,
                                uint16_t dst_w, uint16_t dst_h)
{
    ili9342_set_window(0, 0, dst_w - 1, dst_h - 1);

    for (uint16_t y = 0; y < dst_h; ++y) {
        uint16_t src_y = (uint16_t)(((uint32_t)y * src_h) / dst_h);
        for (uint16_t x = 0; x < dst_w; ++x) {
            uint16_t src_x = (uint16_t)(((uint32_t)x * src_w) / dst_w);
            uint32_t src_index = (uint32_t)(src_y * src_w + src_x) * 2u;
            uint16_t color = ((uint16_t)src_pixels[src_index] << 8)
                           | src_pixels[src_index + 1];
            lcd_write_data((color >> 8) & 0xFF);
            lcd_write_data(color & 0xFF);
        }
    }
}

/* ======================== 背光 PWM ======================== */

#define BACKLIGHT_MAX 1000

void ili9342_set_brightness(uint16_t level)
{
    if (level > BACKLIGHT_MAX) level = BACKLIGHT_MAX;
    TIM_SetCompare2(TIM1, level);
}

void ili9342_set_backlight(uint8_t on)
{
    ili9342_set_brightness(on ? BACKLIGHT_MAX : 0);
}

/* ======================== 显示开关 ======================== */

void ili9342_display_on(void)
{
    lcd_write_cmd(ILI9342_CMD_DISPON);
}

void ili9342_display_off(void)
{
    lcd_write_cmd(ILI9342_CMD_DISPOFF);
}

/* ======================== 颜色反转 ======================== */

void ili9342_invert_color(uint8_t invert)
{
    lcd_write_cmd(invert ? ILI9342_CMD_INVON : ILI9342_CMD_INVOFF);
}

/* ======================== 旋转 / 镜像 ======================== */

/*
 * Rotation (0-3):
 *   0: 0°     — MV=0, MX=0, MY=0
 *   1: 90°    — MV=1, MX=1, MY=0
 *   2: 180°   — MV=0, MX=1, MY=1
 *   3: 270°   — MV=1, MX=0, MY=1
 */
static const uint8_t s_rotation_bits[] = {
    0,
    ILI9342_MADCTL_MV | ILI9342_MADCTL_MX,
    ILI9342_MADCTL_MX | ILI9342_MADCTL_MY,
    ILI9342_MADCTL_MV | ILI9342_MADCTL_MY,
};

void ili9342_set_rotation(uint8_t rotation)
{
    if (rotation > 3) return;

    /* 保留 BGR 位，只修改 MV/MX/MY */
    g_madctl = (g_madctl & ~(ILI9342_MADCTL_MV | ILI9342_MADCTL_MX
                             | ILI9342_MADCTL_MY))
             | s_rotation_bits[rotation];

    lcd_write_cmd_args(ILI9342_CMD_MADCTL, &g_madctl, 1);
}

void ili9342_set_mirror(uint8_t mirror_x, uint8_t mirror_y)
{
    if (mirror_x) g_madctl |=  ILI9342_MADCTL_MX;
    else          g_madctl &= ~ILI9342_MADCTL_MX;

    if (mirror_y) g_madctl |=  ILI9342_MADCTL_MY;
    else          g_madctl &= ~ILI9342_MADCTL_MY;

    lcd_write_cmd_args(ILI9342_CMD_MADCTL, &g_madctl, 1);
}

void ili9342_swap_xy(uint8_t swap)
{
    if (swap) g_madctl |=  ILI9342_MADCTL_MV;
    else      g_madctl &= ~ILI9342_MADCTL_MV;

    lcd_write_cmd_args(ILI9342_CMD_MADCTL, &g_madctl, 1);
}
