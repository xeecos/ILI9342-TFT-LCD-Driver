#include "ili9342.h"
#include "ch32v30x.h"
#include "ch32v30x_gpio.h"
#include "ch32v30x_rcc.h"
#include "ch32v30x_spi.h"
#include <stddef.h>

#define LCD_RS_PIN  GPIO_Pin_1
#define LCD_RST_PIN GPIO_Pin_6
#define LCD_SDA_PIN GPIO_Pin_15
#define LCD_SCL_PIN GPIO_Pin_13
#define LCD_CS_PIN  GPIO_Pin_12
#define LCD_PWM_PIN GPIO_Pin_9

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

void ili9342_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
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
    gpio_init.GPIO_Pin = LCD_SDA_PIN | LCD_SCL_PIN;
    GPIO_Init(GPIOB, &gpio_init);

    GPIO_SetBits(GPIOB, LCD_CS_PIN);
    GPIO_SetBits(GPIOB, LCD_RS_PIN);
    GPIO_SetBits(GPIOC, LCD_RST_PIN);
    GPIO_SetBits(GPIOA, LCD_PWM_PIN);

    SPI_InitTypeDef spi_init = {0};
    spi_init.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi_init.SPI_Mode = SPI_Mode_Master;
    spi_init.SPI_DataSize = SPI_DataSize_8b;
    spi_init.SPI_CPOL = SPI_CPOL_Low;
    spi_init.SPI_CPHA = SPI_CPHA_1Edge;
    spi_init.SPI_NSS = SPI_NSS_Soft;
    spi_init.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
    spi_init.SPI_FirstBit = SPI_FirstBit_MSB;
    spi_init.SPI_CRCPolynomial = 7;
    SPI_Init(SPI2, &spi_init);
    SPI_Cmd(SPI2, ENABLE);

    lcd_write_cmd(0x01);
    for (volatile int i = 0; i < 100000; ++i) {}

    lcd_write_cmd(0x11);
    for (volatile int i = 0; i < 100000; ++i) {}

    lcd_write_cmd(0x29);
    lcd_write_cmd(0x3A);
    lcd_write_data(0x55);
    lcd_write_cmd(0x36);
    lcd_write_data(0xC8);
    lcd_write_cmd(0x2A);
    lcd_write_data(0x00);
    lcd_write_data(0x00);
    lcd_write_data(0x01);
    lcd_write_data(0x3F);
    lcd_write_cmd(0x2B);
    lcd_write_data(0x00);
    lcd_write_data(0x00);
    lcd_write_data(0x00);
    lcd_write_data(0xEF);
    lcd_write_cmd(0x2C);
}

void ili9342_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_write_cmd(0x2A);
    lcd_write_data16(x0);
    lcd_write_data16(x1);
    lcd_write_cmd(0x2B);
    lcd_write_data16(y0);
    lcd_write_data16(y1);
    lcd_write_cmd(0x2C);
}

void ili9342_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    uint32_t pixels = (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1);
    ili9342_set_window(x0, y0, x1, y1);
    for (uint32_t i = 0; i < pixels; ++i) {
        lcd_write_data((color >> 8) & 0xFF);
        lcd_write_data(color & 0xFF);
    }
}

void ili9342_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    ili9342_set_window(x, y, x, y);
    lcd_write_data((color >> 8) & 0xFF);
    lcd_write_data(color & 0xFF);
}

void ili9342_draw_rgb565_scaled(const uint8_t *src_pixels, uint16_t src_w, uint16_t src_h, uint16_t dst_w, uint16_t dst_h)
{
    ili9342_set_window(0, 0, dst_w - 1, dst_h - 1);

    for (uint16_t y = 0; y < dst_h; ++y) {
        uint16_t src_y = (uint16_t)(((uint32_t)y * src_h) / dst_h);
        for (uint16_t x = 0; x < dst_w; ++x) {
            uint16_t src_x = (uint16_t)(((uint32_t)x * src_w) / dst_w);
            uint32_t src_index = (uint32_t)(src_y * src_w + src_x) * 2u;
            uint16_t color = ((uint16_t)src_pixels[src_index] << 8) | src_pixels[src_index + 1];
            lcd_write_data((color >> 8) & 0xFF);
            lcd_write_data(color & 0xFF);
        }
    }
}

void ili9342_set_backlight(uint8_t on)
{
    if (on) {
        GPIO_SetBits(GPIOA, LCD_PWM_PIN);
    } else {
        GPIO_ResetBits(GPIOA, LCD_PWM_PIN);
    }
}
