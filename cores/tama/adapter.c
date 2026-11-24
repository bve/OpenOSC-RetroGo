#include "retrogo/core.h"
#include "tamalib/tamalib.h"
#include <string.h>

enum { Clock = 32768, Width = 128, Height = 88, Cell = 4, MaxRom = 12288 };
static uint16_t pixels[Width * Height];
static u12_t program[8192];
static uint8_t matrix[LCD_HEIGHT][LCD_WIDTH], icons[ICON_NUM];
static uint64_t target;
static unsigned remainder;

static void set_pixel(u8_t x, u8_t y, bool_t on)
{
    matrix[y][x] = !!on;
}

static void set_icon(u8_t icon, bool_t on)
{
    icons[icon] = !!on;
}

static void halt(void) {}
static void buzzer(u8_t unused) { (void)unused; }
static hal_t hal = {
    .halt = halt, .set_lcd_matrix = set_pixel, .set_lcd_icon = set_icon,
    .set_sound_period = buzzer, .play_sound = buzzer
};

enum RgResult rg_open(const RgHost *host, uint32_t bytes)
{
    if (!host || !host->read_rom || (bytes != 8192 && bytes != MaxRom))
        return RG_BAD_ROM;
    if (host->read_rom(host->context, 0, program, bytes)) return RG_ROM_IO;
    const uint8_t *raw = (const uint8_t *)program;
    for (unsigned i = 0; i < bytes / 2; ++i)
        program[i] = ((raw[i * 2] & 15) << 8) | raw[i * 2 + 1];
    for (unsigned i = bytes / 2; i < sizeof(program) / sizeof(*program); ++i)
        program[i] = 0xffb; /* NOP in the unmapped program range. */

    tamalib_register_hal(&hal);
    tamalib_init(program, Clock);
    state_t *state = tamalib_get_state();
    *state->tick_counter = *state->clk_timer_timestamp = *state->prog_timer_timestamp = 0;
    *state->prog_timer_enabled = *state->prog_timer_data = *state->prog_timer_rld = 0;
    *state->previous_cycles = *state->call_depth = 0;
    for (unsigned i = 0; i < INT_SLOT_NUM; ++i) {
        state->interrupts[i].factor_flag_reg = 0;
        state->interrupts[i].mask_reg = 0;
        state->interrupts[i].triggered = 0;
    }
    memset(matrix, 0, sizeof(matrix));
    memset(icons, 0, sizeof(icons));
    target = remainder = 0;
    return RG_OK;
}

static void render(void)
{
    const uint16_t ink = 0x2183, background = 0xcf15, off = 0xb691;
    for (unsigned i = 0; i < Width * Height; ++i) pixels[i] = background;
    for (unsigned y = 0; y < LCD_HEIGHT; ++y)
        for (unsigned x = 0; x < LCD_WIDTH; ++x)
            for (unsigned dy = 0; dy < Cell - 1; ++dy)
                for (unsigned dx = 0; dx < Cell - 1; ++dx)
                    pixels[(12 + y * Cell + dy) * Width + x * Cell + dx] = matrix[y][x] ? ink : off;

    /* Food, light, play, medicine, clean, status, discipline, attention. */
    static const uint8_t symbols[8][7] = {
        { 0, 65, 127, 65, 34, 28, 0 },
        { 28, 34, 34, 20, 20, 28, 8 },
        { 0, 8, 28, 62, 28, 8, 0 },
        { 0, 8, 8, 62, 8, 8, 0 },
        { 0, 42, 0, 42, 0, 42, 0 },
        { 127, 65, 73, 85, 99, 65, 127 },
        { 0, 2, 4, 8, 80, 32, 0 },
        { 0, 8, 8, 8, 8, 0, 8 }
    };
    for (unsigned icon = 0; icon < ICON_NUM; ++icon)
        for (unsigned y = 0; y < 7; ++y)
            for (unsigned x = 0; x < 7; ++x)
                if (symbols[icon][y] & (1u << x))
                    pixels[(1 + (icon / 4) * 79 + y) * Width + (icon % 4) * 32 + 12 + x] =
                        icons[icon] ? ink : off;
}

enum RgResult rg_run(uint32_t buttons, const uint16_t **frame)
{
    tamalib_set_button(BTN_LEFT, !!(buttons & (1u << RG_LEFT)));
    tamalib_set_button(BTN_MIDDLE, !!(buttons & (1u << RG_B)));
    tamalib_set_button(BTN_RIGHT, !!(buttons & (1u << RG_A)));
    state_t *state = tamalib_get_state();
    remainder += Clock;
    target += remainder / 60;
    remainder %= 60;
    while (*state->tick_counter < target)
        if (cpu_step()) return RG_BAD_ROM;
    render();
    *frame = pixels;
    return RG_OK;
}

struct State {
    uint32_t version, remainder;
    uint64_t target, ticks, clock_timer, prog_timer;
    u13_t pc;
    u12_t x, y;
    u8_t a, b, np, sp, flags, previous_cycles, enabled, prog_data, prog_reload;
    MEM_BUFFER_TYPE memory[MEM_BUFFER_SIZE];
    interrupt_t interrupts[INT_SLOT_NUM];
};

uint32_t rg_save(enum RgSaveKind kind, void *out, uint32_t capacity)
{
    if (kind != RG_STATE) return 0;
    if (capacity >= sizeof(struct State)) {
        state_t *cpu = tamalib_get_state();
        struct State *s = out;
        memset(s, 0, sizeof(*s));
        s->version = 1;
        s->remainder = remainder;
        s->target = target;
        s->ticks = *cpu->tick_counter;
        s->clock_timer = *cpu->clk_timer_timestamp;
        s->prog_timer = *cpu->prog_timer_timestamp;
        s->pc = *cpu->pc;
        s->x = *cpu->x;
        s->y = *cpu->y;
        s->a = *cpu->a;
        s->b = *cpu->b;
        s->np = *cpu->np;
        s->sp = *cpu->sp;
        s->flags = *cpu->flags;
        s->previous_cycles = *cpu->previous_cycles;
        s->enabled = *cpu->prog_timer_enabled;
        s->prog_data = *cpu->prog_timer_data;
        s->prog_reload = *cpu->prog_timer_rld;
        memcpy(s->memory, cpu->memory, sizeof(s->memory));
        memcpy(s->interrupts, cpu->interrupts, sizeof(s->interrupts));
    }
    return sizeof(struct State);
}

enum RgResult rg_load(enum RgSaveKind kind, const void *data, uint32_t size)
{
    if (kind != RG_STATE || size != sizeof(struct State)) return RG_BAD_SAVE;
    const struct State *s = data;
    if (s->version != 1 || s->remainder >= 60 || s->pc >= 8192 ||
        s->x >= 4096 || s->y >= 4096 || s->a >= 16 || s->b >= 16 || s->np >= 32 ||
        s->flags >= 16 || s->enabled > 1 || s->target > s->ticks ||
        s->clock_timer > s->ticks || s->ticks - s->clock_timer > Clock ||
        s->prog_timer > s->ticks ||
        (s->enabled && s->ticks - s->prog_timer > Clock / 256))
        return RG_BAD_SAVE;

    state_t *cpu = tamalib_get_state();
    remainder = s->remainder;
    target = s->target;
    *cpu->tick_counter = s->ticks;
    *cpu->clk_timer_timestamp = s->clock_timer;
    *cpu->prog_timer_timestamp = s->prog_timer;
    *cpu->pc = s->pc;
    *cpu->x = s->x;
    *cpu->y = s->y;
    *cpu->a = s->a;
    *cpu->b = s->b;
    *cpu->np = s->np;
    *cpu->sp = s->sp;
    *cpu->flags = s->flags;
    *cpu->previous_cycles = s->previous_cycles;
    *cpu->prog_timer_enabled = s->enabled;
    *cpu->prog_timer_data = s->prog_data;
    *cpu->prog_timer_rld = s->prog_reload;
    memcpy(cpu->memory, s->memory, sizeof(s->memory));
    memcpy(cpu->interrupts, s->interrupts, sizeof(s->interrupts));
    tamalib_refresh_hw();
    return RG_OK;
}
