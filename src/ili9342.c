#include "ili9342.h"
#include "ch32v30x.h"
#include "ch32v30x_gpio.h"
#include "ch32v30x_rcc.h"
#include "ch32v30x_spi.h"
#include "ch32v30x_tim.h"
#include "ch32v30x_dma.h"
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

/*
 * 连续发送 len 个数据字节，CS 在整个过程中保持低电平。
 * 利用 TXE 标志实现流水线：前一个字节还在 SPI 移位输出时，
 * 下一个字节已载入 TX 缓冲，大幅提升吞吐量。
 */
static void lcd_write_data_burst(const uint8_t *data, uint32_t len)
{
    if (len == 0) return;

    GPIO_SetBits(GPIOB, LCD_RS_PIN);
    GPIO_ResetBits(GPIOB, LCD_CS_PIN);

    SPI_I2S_SendData(SPI2, data[0]);
    for (uint32_t i = 1; i < len; i++) {
        while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET) {}
        SPI_I2S_SendData(SPI2, data[i]);
    }
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET) {}

    GPIO_SetBits(GPIOB, LCD_CS_PIN);
}

/*
 * 切换到 SPI 16-bit 数据帧格式。
 * 用于像素数据批量传输：一次发完整 16-bit RGB565 颜色值，
 * SPI 事务数减半，吞吐量翻倍。
 * 注意：必须在 SPI 空闲时切换，否则需先等待 BSY=0。
 */
static void lcd_spi_enter_16bit(void)
{
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET) {}
    SPI_Cmd(SPI2, DISABLE);
    SPI2->CTLR1 |= SPI_CTLR1_DFF;
    SPI_Cmd(SPI2, ENABLE);
}

/* 恢复到 SPI 8-bit 数据帧格式（命令/参数传输需要） */
static void lcd_spi_exit_16bit(void)
{
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET) {}
    SPI_Cmd(SPI2, DISABLE);
    SPI2->CTLR1 &= ~SPI_CTLR1_DFF;
    SPI_Cmd(SPI2, ENABLE);
}

/*
 * 使用 DMA 批量发送 16-bit 像素数据。
 * SPI 必须在 16-bit 模式，DMA 以 HalfWord 为单位自动搬运。
 * DMA 传输完毕后还需等待 SPI BSY，确保最后 1 字节从移位寄存器发出。
 */
static void lcd_dma_send_16bit(const uint16_t *data, uint32_t count)
{
    if (count == 0) return;

    /* 配置本次传输的内存地址与数量 */
    DMA_Cmd(DMA1_Channel5, DISABLE);
    DMA1_Channel5->MADDR = (uint32_t)data;
    DMA_SetCurrDataCounter(DMA1_Channel5, count);

    /* 清除上次传输的标志 */
    DMA_ClearFlag(DMA1_FLAG_TC5);

    /* 启动 DMA — 每次 SPI TXE 自动搬运一个 16-bit 像素 */
    DMA_Cmd(DMA1_Channel5, ENABLE);

    /* 等待 DMA 传输完成 */
    while (DMA_GetFlagStatus(DMA1_FLAG_TC5) == RESET) {}

    /* 关闭 DMA 通道 */
    DMA_Cmd(DMA1_Channel5, DISABLE);
    DMA_ClearFlag(DMA1_FLAG_TC5);

    /* 等待 SPI 发出最后一个字节 */
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) == SET) {}
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
    spi_init.SPI_BaudRatePrescaler   = SPI_BaudRatePrescaler_2;
    spi_init.SPI_FirstBit            = SPI_FirstBit_MSB;
    spi_init.SPI_CRCPolynomial       = 7;
    SPI_Init(SPI2, &spi_init);
    SPI_Cmd(SPI2, ENABLE);

    /* ---- DMA 初始化 (SPI2 TX: DMA1_Channel5) ---- */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(DMA1_Channel5);
    DMA_InitTypeDef dma_init = {0};
    dma_init.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DATAR;
    dma_init.DMA_MemoryBaseAddr     = (uint32_t)0;            /* 每次传输前设置 */
    dma_init.DMA_DIR                = DMA_DIR_PeripheralDST;  /* 内存→外设 */
    dma_init.DMA_BufferSize         = 0;                      /* 每次传输前设置 */
    dma_init.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma_init.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma_init.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    dma_init.DMA_Mode               = DMA_Mode_Normal;
    dma_init.DMA_Priority           = DMA_Priority_High;
    dma_init.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel5, &dma_init);

    /* SPI2 TX DMA 请求使能 — 每次 TXE 时 DMA 自动载入下一个 16 位像素 */
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
    DMA_Cmd(DMA1_Channel5, DISABLE);

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

#define ROW_BATCH 1

void ili9342_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    uint16_t w = x1 - x0 + 1;
    uint16_t h = y1 - y0 + 1;
    if (w == 0 || h == 0) return;

    ili9342_set_window(x0, y0, x1, y1);

    /* 1行缓冲 (320 × 1 = 320 像素 = 640 字节) */
    static uint16_t s_buf[320 * ROW_BATCH];
    uint32_t batch_pixels = (uint32_t)w * ROW_BATCH;
    if (batch_pixels > 320 * ROW_BATCH) return;

    /* 用颜色填充缓冲区 */
    for (uint16_t i = 0; i < batch_pixels; i++) {
        s_buf[i] = color;
    }

    lcd_spi_enter_16bit();
    GPIO_SetBits(GPIOB, LCD_RS_PIN);
    GPIO_ResetBits(GPIOB, LCD_CS_PIN);

    /* 每批 4 行 DMA 一次 */
    uint16_t y = 0;
    while (y + ROW_BATCH <= h) {
        lcd_dma_send_16bit(s_buf, batch_pixels);
        y += ROW_BATCH;
    }

    /* 剩余不足 4 行的部分 */
    uint16_t remaining = h - y;
    if (remaining > 0) {
        lcd_dma_send_16bit(s_buf, (uint32_t)w * remaining);
    }

    GPIO_SetBits(GPIOB, LCD_CS_PIN);
    lcd_spi_exit_16bit();
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
    uint32_t total = (uint32_t)(x_end - x_start + 1) * (uint32_t)(y_end - y_start + 1);
    if (total == 0) return;

    ili9342_set_window(x_start, y_start, x_end, y_end);

    /* 16-bit 模式 + DMA 一次搬完所有像素 */
    lcd_spi_enter_16bit();
    GPIO_SetBits(GPIOB, LCD_RS_PIN);
    GPIO_ResetBits(GPIOB, LCD_CS_PIN);

    lcd_dma_send_16bit(pixels, total);

    GPIO_SetBits(GPIOB, LCD_CS_PIN);
    lcd_spi_exit_16bit();
}

void ili9342_draw_rgb565_scaled(const uint8_t *src_pixels,
                                uint16_t src_w, uint16_t src_h,
                                uint16_t dst_w, uint16_t dst_h)
{
    if (dst_w == 0 || dst_h == 0) return;

    ili9342_set_window(0, 0, dst_w - 1, dst_h - 1);

    /* 16行缓冲 (320 × 16 像素) */
    static uint16_t s_buf[320 * ROW_BATCH];
    uint32_t batch_pixels = (uint32_t)dst_w * ROW_BATCH;
    if (batch_pixels > 320 * ROW_BATCH) return;

    lcd_spi_enter_16bit();
    GPIO_SetBits(GPIOB, LCD_RS_PIN);
    GPIO_ResetBits(GPIOB, LCD_CS_PIN);

    uint16_t y = 0;
    while (y + ROW_BATCH <= dst_h) {
        /* 填充 4 行像素 */
        uint16_t *p = s_buf;
        for (uint16_t row = 0; row < ROW_BATCH; row++) {
            uint16_t src_y = (uint16_t)(((uint32_t)(y + row) * src_h) / dst_h);
            for (uint16_t x = 0; x < dst_w; ++x) {
                uint16_t src_x = (uint16_t)(((uint32_t)x * src_w) / dst_w);
                uint32_t src_index = (uint32_t)(src_y * src_w + src_x) * 2u;
                *p++ = ((uint16_t)src_pixels[src_index] << 8)
                     | src_pixels[src_index + 1];
            }
        }
        lcd_dma_send_16bit(s_buf, batch_pixels);
        y += ROW_BATCH;
    }

    /* 剩余不足 4 行 */
    uint16_t remaining = dst_h - y;
    if (remaining > 0) {
        uint16_t *p = s_buf;
        for (uint16_t row = 0; row < remaining; row++) {
            uint16_t src_y = (uint16_t)(((uint32_t)(y + row) * src_h) / dst_h);
            for (uint16_t x = 0; x < dst_w; ++x) {
                uint16_t src_x = (uint16_t)(((uint32_t)x * src_w) / dst_w);
                uint32_t src_index = (uint32_t)(src_y * src_w + src_x) * 2u;
                *p++ = ((uint16_t)src_pixels[src_index] << 8)
                     | src_pixels[src_index + 1];
            }
        }
        lcd_dma_send_16bit(s_buf, (uint32_t)dst_w * remaining);
    }

    GPIO_SetBits(GPIOB, LCD_CS_PIN);
    lcd_spi_exit_16bit();
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

/* ======================== TE 垂直同步 ======================== */

/*
 * TE (Tearing Effect) 引脚垂直同步配置。
 *
 * TE 信号由 ILI9342 在帧扫描到指定行时产生，MCU 检测此信号后
 * 才写入 GRAM，可以避免画面撕裂（断层）。
 *
 * 硬件接线：ILI9342 TE 引脚 → MCU 任意 GPIO（建议使用中断引脚）。
 *
 * 参数:
 *   enable     - 1=开启 TE, 0=关闭
 *   mode       - 0: V-Blanking 时输出 TE (推荐)
 *                1: V-Blanking + H-Blanking 都输出
 *   scan_lines - TE 触发行号 (0 = 帧末自动触发, 通常设为 display_height)
 *
 * 典型用法（假设 TE 接 PA0）:
 *   ili9342_tearing_configure(1, 0, 240);
 *   然后每次写屏前:
 *     while (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == 0) {}  // 等 TE 高
 *     while (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) != 0) {}  // 等 TE 低
 *     ili9342_fill_rect(...);                                     // 写 GRAM
 */
void ili9342_tearing_configure(uint8_t enable, uint8_t mode, uint16_t scan_lines)
{
    if (enable) {
        /* 先设触发行，再开启 TE */
        uint8_t tearline[] = { (uint8_t)(scan_lines & 0xFF),
                               (uint8_t)(scan_lines >> 8) };
        lcd_write_cmd_args(ILI9342_CMD_TEARLINE, tearline, 2);

        uint8_t temod = mode & 0x01;
        lcd_write_cmd_args(ILI9342_CMD_TEON, &temod, 1);
    } else {
        lcd_write_cmd(ILI9342_CMD_TEOFF);
    }
}
