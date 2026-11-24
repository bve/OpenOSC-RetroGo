#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define JOY_A       0x01
#define JOY_B       0x02
#define JOY_SELECT  0x04
#define JOY_RUN     0x08
#define JOY_UP      0x10
#define JOY_RIGHT   0x20
#define JOY_DOWN    0x40
#define JOY_LEFT    0x80

#define PCE_MAX_WIDTH 512
#define PCE_MAX_HEIGHT 242
#define XBUF_WIDTH (PCE_MAX_WIDTH + 64)

uint8_t *osd_gfx_framebuffer(void);
