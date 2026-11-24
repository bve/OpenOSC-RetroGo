/* Font glyphs from OpenOSC, distributed under GPL-3.0-or-later. */
#include "keyboard.h"
#include <string.h>

static const char keys[] = "1234567890qwertyuiopasdfghjkl\"zxcvbnm .:";

static const char Alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:-./+=_()?*";
static const uint8_t Glyphs[][5] = {
    {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},
    {0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e},
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},
    {0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,1},
    {0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e},{0x7f,9,9,9,6},
    {0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},
    {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
    {0,0x36,0x36,0,0},{8,8,8,8,8},{0,0x60,0x60,0,0},
    {0x20,0x10,8,4,2},{8,8,0x3e,8,8},{0x14,0x14,0x14,0x14,0x14},
    {0x40,0x40,0x40,0x40,0x40},{0,0x1c,0x22,0x41,0},
    {0,0x41,0x22,0x1c,0},{2,1,0x51,9,6},{0x14,8,0x3e,8,0x14},
};

int rg_keyboard_input(RgKeyboard *keyboard, uint32_t buttons)
{
    const uint32_t pressed = buttons & ~keyboard->previous;
    keyboard->previous = buttons;
    if (pressed & (1u << RG_SELECT)) keyboard->visible = !keyboard->visible;
    if (buttons & (1u << RG_START)) return 13;
    if (!keyboard->visible) return 0;

    if (pressed & (1u << RG_LEFT)) keyboard->column = (keyboard->column + 9) % 10;
    if (pressed & (1u << RG_RIGHT)) keyboard->column = (keyboard->column + 1) % 10;
    if (pressed & (1u << RG_UP)) keyboard->row = (keyboard->row + 3) % 4;
    if (pressed & (1u << RG_DOWN)) keyboard->row = (keyboard->row + 1) % 4;
    if (buttons & (1u << RG_B)) return 8;
    return buttons & (1u << RG_A) ? keys[keyboard->row * 10 + keyboard->column] : 0;
}

void rg_keyboard_draw(const RgKeyboard *keyboard, uint16_t *pixels, unsigned width, unsigned height)
{
    if (!keyboard->visible) return;
    const unsigned selected = keyboard->row * 10 + keyboard->column;
    const unsigned cell = (width - 16) / 10;
    for (unsigned index = 0; index < 40; ++index) {
        const unsigned x = 8 + (index % 10) * cell, y = height - 89 + (index / 10) * 21;
        const uint16_t color = index == selected ? 0x04fa : 0x18c3;
        for (unsigned dy = 0; dy < 20; ++dy)
            for (unsigned dx = 0; dx < cell - 1; ++dx)
                pixels[(y + dy) * width + x + dx] = color;
        char letter = keys[index];
        if (letter >= 'a' && letter <= 'z') letter -= 'a' - 'A';
        const char *glyph = strchr(Alphabet, letter);
        if (glyph) {
            for (unsigned dx = 0; dx < 5; ++dx)
                for (unsigned dy = 0; dy < 7; ++dy)
                    if (Glyphs[glyph - Alphabet][dx] & (1 << dy))
                        pixels[(y + dy + 6) * width + x + dx + (cell - 5) / 2] = 0xffff;
        } else if (letter == '"') {
            pixels[(y + 6) * width + x + cell / 2 - 1] = pixels[(y + 6) * width + x + cell / 2 + 1] = 0xffff;
            pixels[(y + 7) * width + x + cell / 2 - 1] = pixels[(y + 7) * width + x + cell / 2 + 1] = 0xffff;
        }
    }
}
