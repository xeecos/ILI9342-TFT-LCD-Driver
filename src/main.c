#include "ili9342.h"
#include "usb_image.h"

static void delay_ms(unsigned int count)
{
    for (volatile unsigned int i = 0; i < count * 10000U; ++i) {
    }
}

int main(void)
{
    ili9342_init();
    ili9342_set_backlight(1);
    usb_image_init();

    ili9342_fill_rect(0, 0, 319, 239, 0x0000); /* black */
    delay_ms(250);

    usb_image_reset_stream();
    usb_image_build_demo_frame();

    while (1) {
        usb_image_poll();
        if (usb_image_has_frame()) {
            const uint8_t *frame = usb_image_get_frame_buffer();
            ili9342_draw_rgb565_scaled(frame, 16, 12, 320, 240);
            usb_image_reset_stream();
        }
        delay_ms(20);
    }

    return 0;
}