#ifndef USB_IMAGE_H
#define USB_IMAGE_H

#include <stdint.h>

#define USB_IMAGE_WIDTH 160u
#define USB_IMAGE_HEIGHT 120u
#define USB_IMAGE_PIXEL_BYTES 2u
#define USB_IMAGE_BUFFER_SIZE 384u
#define USB_STREAM_CHUNK_SIZE 32u
#define USB_DEMO_PAYLOAD_SIZE 384u
#define USB_DEMO_PACKET_SIZE 400u

void usb_image_init(void);
void usb_image_poll(void);
void usb_image_feed_bytes(const uint8_t *data, uint32_t len);
void usb_image_on_receive(const uint8_t *data, uint32_t len);
void usb_image_build_demo_frame(void);
uint8_t usb_image_has_frame(void);
const uint8_t *usb_image_get_frame_buffer(void);
void usb_image_reset_stream(void);

#endif
