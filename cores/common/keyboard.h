#pragma once
#include "retrogo/core.h"
#include <stdbool.h>

typedef struct {
    unsigned row, column;
    uint32_t previous;
    bool visible;
} RgKeyboard;

int rg_keyboard_input(RgKeyboard *keyboard, uint32_t buttons);
void rg_keyboard_draw(const RgKeyboard *keyboard, uint16_t *pixels, unsigned width, unsigned height);
