#include "image.h"
#include "gw_system.h"
#include "gw_romloader.h"
#include <stddef.h>

enum { MaxImage = 320 * 1024 };
static _Alignas(4) uint8_t image[MaxImage];
static _Alignas(max_align_t) uint16_t pixels[GwWidth * GwHeight];
static unsigned keys, phase;

gwromheader_t gw_head;
unsigned short *gw_background, *gw_segments_x, *gw_segments_y, *gw_segments_width, *gw_segments_height;
unsigned char *gw_segments, *gw_program, *gw_melody;
unsigned int *gw_segments_offset, *gw_keyboard;

static int field_ok(uint32_t offset, uint32_t bytes, uint32_t size, unsigned align)
{
    return offset >= sizeof(gw_head) && offset <= size && bytes <= size - offset && offset % align == 0;
}

static enum RgResult configure(unsigned size)
{
    memcpy(&gw_head, image, sizeof(gw_head));
    const int early = !memcmp(gw_head.cpu_name, ROM_CPU_SM500, 5) ||
                      !memcmp(gw_head.cpu_name, ROM_CPU_SM5A, 5);
    const int melody = !memcmp(gw_head.cpu_name, ROM_CPU_SM511, 5) ||
                       !memcmp(gw_head.cpu_name, ROM_CPU_SM512, 5);
    if (!early && !melody && memcmp(gw_head.cpu_name, ROM_CPU_SM510, 5))
        return RG_UNSUPPORTED_ROM;
    if (gw_head.keyboard_size < 10 * sizeof(unsigned int) ||
        !gw_head.program_size || gw_head.program_size > 4096 ||
        (melody && gw_head.melody_size < 256))
        return RG_BAD_ROM;

#define FIELD(name, align) field_ok(gw_head.name, gw_head.name ## _size, size, align)
    if (!FIELD(segments_pixel, 1) ||
        !FIELD(segments_offset, 4) || !FIELD(segments_x, 2) || !FIELD(segments_y, 2) ||
        !FIELD(segments_width, 2) || !FIELD(segments_height, 2) || !FIELD(program, 1) ||
        !FIELD(keyboard, 4) || (gw_head.melody_size && !FIELD(melody, 1)))
        return RG_BAD_ROM;
#undef FIELD

    const unsigned count = early ? 72 : 256;
    if (gw_head.segments_offset_size < count * 4 || gw_head.segments_x_size < count * 2 ||
        gw_head.segments_y_size < count * 2 || gw_head.segments_width_size < count * 2 ||
        gw_head.segments_height_size < count * 2)
        return RG_BAD_ROM;
    gw_segments = image + gw_head.segments_pixel;
    gw_segments_offset = (unsigned int *)(image + gw_head.segments_offset);
    gw_segments_x = (uint16_t *)(image + gw_head.segments_x);
    gw_segments_y = (uint16_t *)(image + gw_head.segments_y);
    gw_segments_width = (uint16_t *)(image + gw_head.segments_width);
    gw_segments_height = (uint16_t *)(image + gw_head.segments_height);
    gw_program = image + gw_head.program;
    gw_keyboard = (unsigned int *)(image + gw_head.keyboard);
    gw_melody = gw_head.melody_size ? image + gw_head.melody : NULL;
    const unsigned pack = gw_head.flags & FLAG_SEGMENTS_2BITS ? 4 :
                          gw_head.flags & FLAG_SEGMENTS_4BITS ? 2 : 1;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned x = gw_segments_x[i], y = gw_segments_y[i];
        const unsigned w = gw_segments_width[i], h = gw_segments_height[i];
        const unsigned offset = gw_segments_offset[i], space = gw_head.segments_pixel_size * pack;
        if (x > GwWidth || w > GwWidth - x || y > GwHeight || h > GwHeight - y ||
            offset > space || w * h > space - offset)
            return RG_BAD_ROM;
    }
    return RG_OK;
}

unsigned int gw_get_buttons(void) { return keys; }

enum RgResult rg_open(const RgHost *host, uint32_t bytes)
{
    if (!host || !host->read_rom) return RG_BAD_ROM;
    uint32_t size, jpeg_offset;
    enum RgResult result = gw_read_image(host, bytes, image, sizeof(image), pixels,
                                        &size, &jpeg_offset);
    if (result != RG_OK) return result;
    if (size < sizeof(gw_head)) return RG_BAD_ROM;
    result = configure(size);
    if (result != RG_OK) return result;

    if (gw_head.background_pixel_size) {
        if (gw_head.background_pixel_size != sizeof(pixels) ||
            !field_ok(gw_head.background_pixel, sizeof(pixels), size, 2)) return RG_BAD_ROM;
        gw_background = (uint16_t *)(image + gw_head.background_pixel);
    } else {
        const uint32_t start = (size + 1) & ~1u;
        if (start > sizeof(image) - sizeof(pixels)) return RG_UNSUPPORTED_ROM;
        gw_background = (uint16_t *)(image + start);
        result = gw_read_background(host, bytes, jpeg_offset, gw_background, pixels);
        if (result != RG_OK) return result;
    }

    keys = phase = 0;
    if (!gw_system_config()) return RG_BAD_ROM;
    gw_system_start();
    gw_system_reset();
    memcpy(pixels, gw_background, sizeof(pixels));
    return RG_OK;
}

enum RgResult rg_run(uint32_t actions, const uint16_t **frame)
{
    static const uint8_t buttons[] = {
        GW_BUTTON_UP, GW_BUTTON_DOWN, GW_BUTTON_LEFT, GW_BUTTON_RIGHT,
        GW_BUTTON_A, GW_BUTTON_B, GW_BUTTON_GAME, GW_BUTTON_TIME
    };
    keys = 0;
    for (unsigned i = 0; i < sizeof(buttons); ++i)
        if (actions & (1u << i)) keys |= buttons[i];
    /* The 32.768 kHz oscillator supplies an average of 546.13 clocks per frame. */
    phase += GW_SYS_FREQ;
    gw_system_run((phase / (4 * GW_REFRESH_RATE)) * 4);
    phase %= 4 * GW_REFRESH_RATE;
    gw_system_blit(pixels);
    *frame = pixels;
    return RG_OK;
}

struct Snapshot {
    uint32_t phase;
    gw_state_t machine;
};

uint32_t rg_save(enum RgSaveKind kind, void *out, uint32_t capacity)
{
    if (kind != RG_STATE) return 0;
    if (capacity >= sizeof(struct Snapshot)) {
        struct Snapshot *state = out;
        state->phase = phase;
        gw_state_save((uint8_t *)&state->machine);
    }
    return sizeof(struct Snapshot);
}

enum RgResult rg_load(enum RgSaveKind kind, const void *data, uint32_t bytes)
{
    if (kind != RG_STATE || bytes != sizeof(struct Snapshot)) return RG_BAD_SAVE;
    const struct Snapshot *state = data;
    if (state->phase >= 4 * GW_REFRESH_RATE || !gw_state_load((uint8_t *)&state->machine))
        return RG_BAD_SAVE;
    phase = state->phase;
    return RG_OK;
}
