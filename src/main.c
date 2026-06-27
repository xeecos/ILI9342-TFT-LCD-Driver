#include "ili9342.h"
#include "usb_image.h"
#include "ch32v30x.h"
#include "ch32v30x_gpio.h"
#include "ch32v30x_rcc.h"

/* Debug LED — 可在此修改引脚定义 */
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
    usb_image_init();
    led_init();

    ili9342_fill_rect(0, 0, 319, 239, 0x0000); /* black */
    delay_ms(250);

    usb_image_reset_stream();
    usb_image_build_demo_frame();

    /* 启动快速闪烁，指示系统已初始化完成 */
    for (int i = 0; i < 6; i++) {
        DEBUG_LED_ON();
        delay_ms(80);
        DEBUG_LED_OFF();
        delay_ms(80);
    }

    uint32_t tick = 0;

    while (1) {
        usb_image_poll();

        if (usb_image_has_frame()) {
            const uint8_t *frame = usb_image_get_frame_buffer();
            ili9342_draw_rgb565_scaled(frame, 16, 12, 320, 240);
            usb_image_reset_stream();
            /* 收到帧时：LED 常亮 50ms 再熄灭，作为视觉反馈 */
            DEBUG_LED_ON();
            delay_ms(50);
            DEBUG_LED_OFF();
        }

        /* 心跳闪烁：每 20 次循环（约 400ms）翻转一次 */
        if (++tick % 20 == 0) {
            DEBUG_LED_TOGGLE();
        }

        delay_ms(20);
    }

    return 0;
}