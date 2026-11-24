#ifndef GW_IMAGE_H
#define GW_IMAGE_H

#include "retrogo/core.h"

enum { GwWidth = 320, GwHeight = 240 };

enum RgResult gw_read_image(const RgHost *host, uint32_t bytes, uint8_t *image,
                            uint32_t capacity, void *scratch, uint32_t *size,
                            uint32_t *jpeg_offset);
enum RgResult gw_read_background(const RgHost *host, uint32_t bytes, uint32_t offset,
                                 uint16_t *background, void *scratch);

#endif
