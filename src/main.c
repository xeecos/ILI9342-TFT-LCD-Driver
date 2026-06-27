#include "ili9342.h"
#include "ch32v30x.h"
#include "ch32v30x_gpio.h"
#include "ch32v30x_rcc.h"

/* Debug LED — PA8 */
#define DEBUG_LED_PORT    GPIOA
#define DEBUG_LED_PIN     GPIO_Pin_8
#define DEBUG_LED_ON()    GPIO_SetBits(DEBUG_LED_PORT, DEBUG_LED_PIN)
#define DEBUG_LED_OFF()   GPIO_ResetBits(DEBUG_LED_PORT, DEBUG_LED_PIN)
#define DEBUG_LED_TOGGLE() GPIO_WriteBit(DEBUG_LED_PORT, DEBUG_LED_PIN, \
    (BitAction)(1 - GPIO_ReadOutputDataBit(DEBUG_LED_PORT, DEBUG_LED_PIN)))

static void delay_ms(unsigned int count)
{
    for (volatile unsigned int i = 0; i < count * 10000U; ++i) {
    }
}

static void led_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.GPIO_Pin = DEBUG_LED_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DEBUG_LED_PORT, &gpio_init);

    DEBUG_LED_OFF();
}

int main(void)
{
    ili9342_init();
    ili9342_set_backlight(1);
    led_init();

    /* 启动快速闪烁，指示系统已初始化完成 */
    for (int i = 0; i < 6; i++) {
        DEBUG_LED_ON();
        delay_ms(80);
        DEBUG_LED_OFF();
        delay_ms(80);
    }

    const uint16_t colors[] = {
        0xF800, /* 红 */
        0x07E0, /* 绿 */
        0x001F, /* 蓝 */
        0xFFE0, /* 黄 */
        0x07FF, /* 青 */
        0xF81F, /* 紫 */
        0xFFFF, /* 白 */
        0x0000, /* 黑 */
    };
    const int num_colors = sizeof(colors) / sizeof(colors[0]);
    int color_idx = 0;

    while (1) {
        ili9342_fill_rect(0, 0, 319, 239, colors[color_idx]);
        DEBUG_LED_TOGGLE();
        color_idx = (color_idx + 1) % num_colors;
        delay_ms(1000);
    }

    return 0;
}