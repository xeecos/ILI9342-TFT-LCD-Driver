#include "usb_image.h"
#include "ch32v30x.h"
#include "ch32v30x_gpio.h"
#include <string.h>

#define USB_DP_PIN GPIO_Pin_7
#define USB_DM_PIN GPIO_Pin_6

#define USB_FRAME_SYNC0 0xAAu
#define USB_FRAME_SYNC1 0x55u
#define USB_FRAME_CMD_IMAGE 0x01u
#define USB_RX_QUEUE_SIZE 64u

typedef enum {
    USB_PARSE_WAIT_SYNC0 = 0,
    USB_PARSE_WAIT_SYNC1,
    USB_PARSE_WAIT_CMD,
    USB_PARSE_WAIT_WIDTH_L,
    USB_PARSE_WAIT_WIDTH_H,
    USB_PARSE_WAIT_HEIGHT_L,
    USB_PARSE_WAIT_HEIGHT_H,
    USB_PARSE_WAIT_LEN_0,
    USB_PARSE_WAIT_LEN_1,
    USB_PARSE_WAIT_LEN_2,
    USB_PARSE_WAIT_LEN_3,
    USB_PARSE_WAIT_PAYLOAD,
    USB_PARSE_DONE
} usb_parse_state_t;

static uint8_t g_frame_buffer[USB_IMAGE_BUFFER_SIZE];
static uint8_t g_demo_payload[USB_DEMO_PAYLOAD_SIZE];
static uint8_t g_demo_packet[USB_DEMO_PACKET_SIZE];
static volatile uint8_t g_rx_queue[USB_RX_QUEUE_SIZE];
static volatile uint8_t g_rx_head = 0;
static volatile uint8_t g_rx_tail = 0;
static volatile uint8_t g_rx_count = 0;
static volatile uint8_t g_frame_ready = 0;
static usb_parse_state_t g_parse_state = USB_PARSE_WAIT_SYNC0;
static uint16_t g_width = 0;
static uint16_t g_height = 0;
static uint32_t g_payload_len = 0;
static uint32_t g_payload_index = 0;
static uint8_t g_header_complete = 0;
static uint32_t g_stream_index = 0;

static void usb_reset_parser(void)
{
    g_parse_state = USB_PARSE_WAIT_SYNC0;
    g_width = 0;
    g_height = 0;
    g_payload_len = 0;
    g_payload_index = 0;
    g_header_complete = 0;
    g_stream_index = 0;
}

static void usb_image_process_byte(uint8_t byte)
{
    switch (g_parse_state) {
        case USB_PARSE_WAIT_SYNC0:
            if (byte == USB_FRAME_SYNC0) {
                g_parse_state = USB_PARSE_WAIT_SYNC1;
            }
            break;
        case USB_PARSE_WAIT_SYNC1:
            if (byte == USB_FRAME_SYNC1) {
                g_parse_state = USB_PARSE_WAIT_CMD;
            } else {
                usb_reset_parser();
            }
            break;
        case USB_PARSE_WAIT_CMD:
            if (byte == USB_FRAME_CMD_IMAGE) {
                g_parse_state = USB_PARSE_WAIT_WIDTH_L;
            } else {
                usb_reset_parser();
            }
            break;
        case USB_PARSE_WAIT_WIDTH_L:
            g_width = byte;
            g_parse_state = USB_PARSE_WAIT_WIDTH_H;
            break;
        case USB_PARSE_WAIT_WIDTH_H:
            g_width |= ((uint16_t)byte << 8);
            g_parse_state = USB_PARSE_WAIT_HEIGHT_L;
            break;
        case USB_PARSE_WAIT_HEIGHT_L:
            g_height = byte;
            g_parse_state = USB_PARSE_WAIT_HEIGHT_H;
            break;
        case USB_PARSE_WAIT_HEIGHT_H:
            g_height |= ((uint16_t)byte << 8);
            g_parse_state = USB_PARSE_WAIT_LEN_0;
            break;
        case USB_PARSE_WAIT_LEN_0:
            g_payload_len = byte;
            g_parse_state = USB_PARSE_WAIT_LEN_1;
            break;
        case USB_PARSE_WAIT_LEN_1:
            g_payload_len |= ((uint32_t)byte << 8);
            g_parse_state = USB_PARSE_WAIT_LEN_2;
            break;
        case USB_PARSE_WAIT_LEN_2:
            g_payload_len |= ((uint32_t)byte << 16);
            g_parse_state = USB_PARSE_WAIT_LEN_3;
            break;
        case USB_PARSE_WAIT_LEN_3:
            g_payload_len |= ((uint32_t)byte << 24);
            if (g_payload_len > USB_IMAGE_BUFFER_SIZE) {
                usb_reset_parser();
            } else {
                g_header_complete = 1;
                g_payload_index = 0;
                memset(g_frame_buffer, 0, sizeof(g_frame_buffer));
                g_parse_state = USB_PARSE_WAIT_PAYLOAD;
            }
            break;
        case USB_PARSE_WAIT_PAYLOAD:
            if (g_header_complete) {
                if (g_payload_index < USB_IMAGE_BUFFER_SIZE) {
                    g_frame_buffer[g_payload_index++] = byte;
                }
                if (g_payload_index >= USB_IMAGE_BUFFER_SIZE) {
                    g_frame_ready = 1;
                    g_parse_state = USB_PARSE_DONE;
                }
                if (g_payload_index >= g_payload_len) {
                    g_frame_ready = 1;
                    g_parse_state = USB_PARSE_DONE;
                }
            }
            break;
        case USB_PARSE_DONE:
            break;
    }
}

static uint8_t usb_rx_queue_push(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i) {
        if (g_rx_count >= USB_RX_QUEUE_SIZE) {
            return 0;
        }
        g_rx_queue[g_rx_tail] = data[i];
        g_rx_tail = (uint8_t)((g_rx_tail + 1u) % USB_RX_QUEUE_SIZE);
        g_rx_count++;
    }
    return 1;
}

static uint8_t usb_rx_queue_pop(uint8_t *byte)
{
    if (g_rx_count == 0) {
        return 0;
    }
    *byte = g_rx_queue[g_rx_head];
    g_rx_head = (uint8_t)((g_rx_head + 1u) % USB_RX_QUEUE_SIZE);
    g_rx_count--;
    return 1;
}

void usb_image_build_demo_frame(void)
{
    uint16_t width = 16u;
    uint16_t height = 12u;
    uint32_t payload_len = (uint32_t)width * height * USB_IMAGE_PIXEL_BYTES;
    if (payload_len > USB_IMAGE_BUFFER_SIZE) {
        payload_len = USB_IMAGE_BUFFER_SIZE;
    }

    for (uint16_t y = 0; y < height; ++y) {
        for (uint16_t x = 0; x < width; ++x) {
            uint8_t r = (uint8_t)((x * 255u) / (width - 1u));
            uint8_t g = (uint8_t)((y * 255u) / (height - 1u));
            uint8_t b = (uint8_t)(((x + y) * 255u) / (width + height - 2u));
            uint16_t color = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            uint32_t index = ((uint32_t)y * width + x) * 2u;
            if (index + 1u < USB_DEMO_PAYLOAD_SIZE) {
                g_demo_payload[index] = (uint8_t)((color >> 8) & 0xFFu);
                g_demo_payload[index + 1u] = (uint8_t)(color & 0xFFu);
            }
        }
    }

    g_demo_packet[0] = USB_FRAME_SYNC0;
    g_demo_packet[1] = USB_FRAME_SYNC1;
    g_demo_packet[2] = USB_FRAME_CMD_IMAGE;
    g_demo_packet[3] = (uint8_t)(width & 0xFFu);
    g_demo_packet[4] = (uint8_t)((width >> 8) & 0xFFu);
    g_demo_packet[5] = (uint8_t)(height & 0xFFu);
    g_demo_packet[6] = (uint8_t)((height >> 8) & 0xFFu);
    g_demo_packet[7] = (uint8_t)(payload_len & 0xFFu);
    g_demo_packet[8] = (uint8_t)((payload_len >> 8) & 0xFFu);
    g_demo_packet[9] = (uint8_t)((payload_len >> 16) & 0xFFu);
    g_demo_packet[10] = (uint8_t)((payload_len >> 24) & 0xFFu);

    if (payload_len + 11u <= USB_DEMO_PACKET_SIZE) {
        memcpy(&g_demo_packet[11], g_demo_payload, payload_len);
    }

    g_frame_ready = 0;
    memset(g_frame_buffer, 0, sizeof(g_frame_buffer));
    usb_reset_parser();
    usb_image_feed_bytes(g_demo_packet, 11u + payload_len);
}

void usb_image_reset_stream(void)
{
    usb_reset_parser();
    g_frame_ready = 0;
    memset(g_frame_buffer, 0, sizeof(g_frame_buffer));
}

void usb_image_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.GPIO_Pin = USB_DP_PIN | USB_DM_PIN;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_Init(GPIOB, &gpio_init);

    memset(g_frame_buffer, 0, sizeof(g_frame_buffer));
    g_frame_ready = 0;
    g_rx_head = 0;
    g_rx_tail = 0;
    g_rx_count = 0;
    usb_reset_parser();
}

void usb_image_poll(void)
{
    uint8_t byte = 0;
    while (usb_rx_queue_pop(&byte) != 0) {
        usb_image_process_byte(byte);
    }
}

void usb_image_on_receive(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return;
    }
    (void)usb_rx_queue_push(data, len);
}

void usb_image_feed_bytes(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return;
    }

    for (uint32_t i = 0; i < len; ++i) {
        usb_image_process_byte(data[i]);
    }
}

uint8_t usb_image_has_frame(void)
{
    return g_frame_ready;
}

const uint8_t *usb_image_get_frame_buffer(void)
{
    return g_frame_buffer;
}
