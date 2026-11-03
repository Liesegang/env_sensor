#ifndef CARD_IMAGE_TRANSFER_H_
#define CARD_IMAGE_TRANSFER_H_
#include <stddef.h>
#include <stdint.h>
int card_image_transfer_init(void);
int card_image_load_file(const char *prefix,uint8_t *frame,size_t length);
uint32_t card_image_display_started(void);
void card_image_display_result(uint32_t request_token,int error);
#endif
