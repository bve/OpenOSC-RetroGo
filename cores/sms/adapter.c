/* OpenOSC-RetroGo port of SMS Plus GX for Master System, Game Gear,
 * SG-1000, and ColecoVision. Licensed under the GNU GPL v2 or later, like
 * the emulator it adapts. */
#include "retrogo/core.h"
#include "smsplus/shared.h"

enum {
    PageBytes = 0x4000,
    CacheSlots = 14,
    CopierHeader = 512,
    MaxPages = 256,
    SramBytes = 0x8000,
    ColecoBiosBytes = 0x2000,
    MaxWidth = 256,
    MaxHeight = 224,
    StateVersion = 2,
};

#define COLECO_BIOS_NAME "colecovision.rom"

static RgHost host;
static uint32_t rom_base, rom_bytes, age, last_page;
static uint8_t *last_bytes;
static int rom_error;
static struct {
    uint32_t page; /* Page number plus one; zero marks a free slot. */
    uint32_t age;
} slots[CacheSlots];
static uint8_t cache[CacheSlots][PageBytes] __attribute__((aligned(32)));
static uint8_t sram[SramBytes];
static uint8_t coleco_bios[ColecoBiosBytes];
static uint16_t frame[MaxWidth * MaxHeight];
static uint16_t palette[PALETTE_SIZE];
static unsigned width, height;

static uint32_t crc32_update(uint32_t crc, const uint8_t *bytes, uint32_t length)
{
    crc = ~crc;
    while (length--) {
        crc ^= *bytes++;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
    }
    return ~crc;
}

static void read_page(uint32_t page, uint8_t *out)
{
    const uint32_t offset = page * PageBytes;
    const uint32_t bytes = rom_bytes - offset < PageBytes ? rom_bytes - offset : PageBytes;
    if (rom_error || host.read_rom(host.context, rom_base + offset, out, bytes)) {
        rom_error = 1;
        memset(out, 0xff, PageBytes);
        return;
    }
    memset(out + bytes, 0xff, PageBytes - bytes);
}

/* Slots referenced by the Z80 memory map must stay resident. A page change
 * maps at most seven distinct pages at once, so fourteen slots always leave a
 * victim. */
static uint32_t pinned_slots(void)
{
    uint32_t mask = 0;
    const uint8_t *first = cache[0], *end = cache[CacheSlots];
    for (unsigned i = 0; i < 64; ++i) {
        const uint8_t *p = cpu_readmap[i];
        if (p >= first && p < end) mask |= 1u << ((uint32_t)(p - first) / PageBytes);
    }
    return mask;
}

uint8 *cart_page(uint32 page)
{
    page %= cart.pages;
    if (last_bytes && page == last_page) return last_bytes;
    unsigned victim = CacheSlots;
    for (unsigned i = 0; i < CacheSlots; ++i) {
        if (slots[i].page == page + 1) {
            slots[i].age = ++age;
            last_page = page;
            return last_bytes = cache[i];
        }
    }
    const uint32_t pinned = pinned_slots();
    uint32_t oldest = 0;
    for (unsigned i = 0; i < CacheSlots; ++i) {
        if (pinned & (1u << i)) continue;
        if (!slots[i].page) {
            victim = i;
            break;
        }
        const uint32_t elapsed = age - slots[i].age;
        if (victim == CacheSlots || elapsed > oldest) {
            victim = i;
            oldest = elapsed;
        }
    }
    if (victim == CacheSlots) victim = 0; /* Unreachable with the mapper limits above. */
    read_page(page, cache[victim]);
    slots[victim].page = page + 1;
    slots[victim].age = ++age;
    last_page = page;
    return last_bytes = cache[victim];
}

void render_output_line(int line, const uint8 *pixels, int count)
{
    const int y = line + ((int)height - bitmap.viewport.h) / 2;
    if (y < 0 || y >= (int)height) return;
    if (count > (int)width) count = width;
    render_copy_palette(palette);
    uint16_t *out = frame + y * width;
    for (int x = 0; x < count; ++x) out[x] = palette[pixels[x] & PIXEL_MASK];
}

static int has_extension(const char *name)
{
    const char *ext = host.extension;
    if (!ext) return 0;
    while (*name && *name == *ext) ++name, ++ext;
    return !*name && !*ext;
}

static enum RgResult select_video(void)
{
    const int gg = sms.console == CONSOLE_GG;
    width = gg ? 160 : 256;
    /* Codemasters titles use the 224-line mode. Other modes are centered. */
    height = gg ? 144 : (IS_SMS && cart.mapper == MAPPER_CODIES) ? 224 : 192;
    const RgVideoMode mode = {
        (uint16_t)width, (uint16_t)height,
        sms.display == DISPLAY_PAL ? 89393752u : 90299604u,
        sms.display == DISPLAY_PAL ? 4443u : 5411u,
    };
    /* The manifest mode needs no change; an ABI 1 host can only run that. */
    if (host.set_video) return host.set_video(host.context, mode) ? RG_UNSUPPORTED_ROM : RG_OK;
    return mode.width == 256 && mode.height == 192 && mode.period_den == 5411 ? RG_OK : RG_UNSUPPORTED_ROM;
}

static void restore_slot(void)
{
    if (IS_TMS) return;
    if (IS_SMS) {
        memctrl_w(sms.memctrl);
        return;
    }
    slot.rom = 1;
    slot.pages = cart.pages;
    slot.mapper = cart.mapper;
    slot.fcr = &cart.fcr[0];
    mapper_reset();
    cpu_readmap[0] = cart_rom(0);
    for (int i = 0; i < 4; ++i) {
        if (slot.mapper == MAPPER_KOREA_MSX) mapper_8k_w(i, slot.fcr[i]);
        else mapper_16k_w(i, slot.fcr[i]);
    }
}

enum RgResult rg_open(const RgHost *services, uint32_t bytes)
{
    if (!services || !services->read_rom || !bytes) return RG_BAD_ROM;
    host = *services;
    rom_base = ((bytes < PageBytes ? PageBytes : bytes) / CopierHeader) & 1 ? CopierHeader : 0;
    if (bytes <= rom_base) return RG_BAD_ROM;
    rom_bytes = bytes - rom_base;
    const uint32_t pages = (rom_bytes + PageBytes - 1) / PageBytes;
    if (pages > MaxPages) return RG_UNSUPPORTED_ROM;

    memset(slots, 0, sizeof(slots));
    memset(frame, 0, sizeof(frame));
    memset(sram, 0, sizeof(sram));
    age = last_page = 0;
    last_bytes = NULL;
    rom_error = 0;
    for (unsigned i = 0; i < 64; ++i) cpu_readmap[i] = cpu_writemap[i] = dummy_memory;

    uint32_t crc = 0;
    for (uint32_t offset = 0; offset < rom_bytes; offset += PageBytes) {
        const uint32_t chunk = rom_bytes - offset < PageBytes ? rom_bytes - offset : PageBytes;
        if (host.read_rom(host.context, rom_base + offset, cache[0], chunk)) return RG_ROM_IO;
        crc = crc32_update(crc, cache[0], chunk);
    }

    memset(&cart, 0, sizeof(cart));
    cart.size = rom_bytes;
    cart.pages = pages;
    cart.crc = crc;
    cart.loaded = 1;
    cart.sram = sram;
    system_reset_config();
    option.overscan = 0;
    option.extra_gg = 0;
    if (has_extension("col")) option.console = 6;
    else if (has_extension("sg")) option.console = 5;
    set_config();
    if (has_extension("gg") && !IS_GG) sms.console = CONSOLE_GG;
    else if (has_extension("sms") && sms.console == CONSOLE_GG) sms.console = CONSOLE_SMS2;
    sms.use_fm = 0;
    if (rom_error) return RG_ROM_IO;

    if (sms.console == CONSOLE_COLECO) {
        if (!host.read_bios) return RG_MISSING_BIOS;
        const enum RgResult loaded = host.read_bios(host.context, COLECO_BIOS_NAME, coleco_bios, sizeof(coleco_bios));
        if (loaded != RG_OK) return loaded;
        coleco.rom = coleco_bios;
    }

    const enum RgResult video = select_video();
    if (video != RG_OK) return video;

    /* Latches the upstream code only initializes at program start. */
    memset(&input, 0, sizeof(input));
    memset(object_info, 0, sizeof(object_info));
    memset(sprites, 0, sizeof(sprites));
    object_index_count = 0;
    sprites_found = 0;
    text_counter = 0;
    prev_line = -1;
    paddle_toggle[0] = paddle_toggle[1] = 0;
    lightgun_latch = 0;
    bitmap.width = width;
    bitmap.height = height;
    system_poweron();
    bitmap.viewport.changed = 0;
    return rom_error ? RG_ROM_IO : RG_OK;
}

static void update_input(uint32_t buttons)
{
    const int up = buttons & (1u << RG_UP), down = buttons & (1u << RG_DOWN);
    const int left = buttons & (1u << RG_LEFT), right = buttons & (1u << RG_RIGHT);
    const int a = buttons & (1u << RG_A), b = buttons & (1u << RG_B);
    const int start = buttons & (1u << RG_START), select = buttons & (1u << RG_SELECT);

    input.pad[0] = (up ? INPUT_UP : 0) | (down ? INPUT_DOWN : 0) | (left ? INPUT_LEFT : 0) |
                   (right ? INPUT_RIGHT : 0) | (b ? INPUT_BUTTON1 : 0) | (a ? INPUT_BUTTON2 : 0);
    input.pad[1] = 0;
    input.system = 0;

    if (sms.console == CONSOLE_COLECO) {
        /* Keypad chords from the upstream Game & Watch port: Start selects
         * 1-6 and 0, Select selects #, *, 9-5. The later key in the list
         * Up, Right, Down, Left, A, B wins. */
        const int k = b ? 6 : a ? 5 : left ? 4 : down ? 3 : right ? 2 : up ? 1 : 0;
        coleco.keypad[0] = select ? 11 - k : start ? (1 + k) % 7 : 0xf0;
        coleco.keypad[1] = 0xf0;
    } else if (sms.console == CONSOLE_GG) {
        if (start) input.system |= INPUT_START;
    } else if (start) {
        input.system |= INPUT_PAUSE;
    }
}

enum RgResult rg_run(uint32_t buttons, const uint16_t **out)
{
    if (bitmap.viewport.changed) {
        memset(frame, 0, sizeof(frame));
        bitmap.viewport.changed = 0;
    }
    update_input(buttons);
    system_frame(0);
    *out = frame;
    return rom_error ? RG_ROM_IO : RG_OK;
}

/* Native pointers, the ROM cache, and host services are rebuilt on restore. */
struct State {
    uint32_t version, console, crc, display;
    int32_t cycles, prev_line, sprites_found, text_counter;
    sms_t sms;
    vdp_t vdp;
    z80_state_t z80;
    uint8_t fcr[4];
    uint8_t coleco_mode, coleco_keypad[2], object_count;
    uint8_t paddle[2], lightgun, reserved;
    render_object_t objects[64];
    tms_sprite sprites[4];
    uint8_t dummy[sizeof(dummy_memory)];
    uint8_t sram[SramBytes];
};

uint32_t rg_save(enum RgSaveKind kind, void *out, uint32_t capacity)
{
    if (kind == RG_SRAM) {
        if (IS_TMS || !sms.save) return 0;
        if (capacity < SramBytes) return SramBytes;
        memcpy(out, sram, SramBytes);
        return SramBytes;
    }
    if (kind != RG_STATE) return 0;
    if (capacity < sizeof(struct State)) return sizeof(struct State);
    struct State *s = out;
    memset(s, 0, sizeof(*s));
    s->version = StateVersion;
    s->console = sms.console;
    s->crc = cart.crc;
    s->display = sms.display;
    s->cycles = z80_cycle_count;
    s->prev_line = prev_line;
    s->sprites_found = sprites_found;
    s->text_counter = text_counter;
    s->sms = sms;
    s->vdp = vdp;
    z80_get_state(&s->z80);
    memcpy(s->fcr, cart.fcr, sizeof(s->fcr));
    s->coleco_mode = coleco.pio_mode;
    s->coleco_keypad[0] = coleco.keypad[0];
    s->coleco_keypad[1] = coleco.keypad[1];
    s->object_count = object_index_count;
    s->paddle[0] = paddle_toggle[0];
    s->paddle[1] = paddle_toggle[1];
    s->lightgun = lightgun_latch;
    memcpy(s->objects, object_info, sizeof(s->objects));
    memcpy(s->sprites, sprites, sizeof(s->sprites));
    memcpy(s->dummy, dummy_memory, sizeof(s->dummy));
    memcpy(s->sram, sram, sizeof(s->sram));
    return sizeof(*s);
}

static int valid_state(const struct State *s)
{
    if (s->version != StateVersion || s->console != sms.console || s->crc != cart.crc ||
        s->display != sms.display || s->sms.console != sms.console ||
        s->sms.display != sms.display || s->sms.territory > 1 || s->sms.paused > 1 ||
        s->sms.save > 1 || s->cycles < 0 || s->cycles >= CYCLES_PER_LINE ||
        s->vdp.line < 0 || s->vdp.line > vdp.lpf || s->vdp.lpf != vdp.lpf ||
        s->sprites_found < 0 || s->sprites_found > 4 || s->object_count > 64 ||
        s->text_counter < 0 || s->text_counter > 40 * 32 || s->paddle[0] > 1 ||
        s->paddle[1] > 1 || s->lightgun > 1 || !z80_state_valid(&s->z80))
        return 0;
    for (int i = 0; i < 2; ++i)
        if (s->sms.device[i] > DEVICE_SPORTSPAD) return 0;
    for (int i = 0; i < s->sprites_found; ++i)
        if (s->sprites[i].xpos < -32 || s->sprites[i].xpos > 255) return 0;
    return 1;
}

enum RgResult rg_load(enum RgSaveKind kind, const void *data, uint32_t size)
{
    if (kind == RG_SRAM) {
        if (IS_TMS || size != SramBytes) return RG_BAD_SAVE;
        memcpy(sram, data, SramBytes);
        sms.save = 1;
        return rom_error ? RG_ROM_IO : RG_OK;
    }
    if (kind != RG_STATE || size != sizeof(struct State)) return RG_BAD_SAVE;
    const struct State *s = data;
    if (!valid_state(s)) return RG_BAD_SAVE;

    /* Rebuild fixed mappings first, then overlay the saved machine. */
    system_reset();
    sms = s->sms;
    vdp = s->vdp;
    vdp_init();
    vdp.irq = sms.console == CONSOLE_COLECO ? INPUT_LINE_NMI : INPUT_LINE_IRQ0;
    vdp.ct = (vdp.reg[3] << 6) & 0x3FC0;
    vdp.pg = (vdp.reg[4] << 11) & 0x3800;
    vdp.satb = (vdp.reg[5] << 7) & 0x3F00;
    vdp.sa = (vdp.reg[5] << 7) & 0x3F80;
    vdp.sg = (vdp.reg[6] << 11) & 0x3800;
    vdp.bd = vdp.reg[7] & 0x0F;
    z80_set_state(&s->z80);
    z80_cycle_count = s->cycles;
    memcpy(cart.fcr, s->fcr, sizeof(cart.fcr));
    memcpy(sram, s->sram, sizeof(sram));
    memcpy(dummy_memory, s->dummy, sizeof(dummy_memory));
    coleco.pio_mode = s->coleco_mode;
    coleco.keypad[0] = s->coleco_keypad[0];
    coleco.keypad[1] = s->coleco_keypad[1];
    object_index_count = s->object_count;
    memcpy(object_info, s->objects, sizeof(object_info));
    memcpy(sprites, s->sprites, sizeof(sprites));
    sprites_found = s->sprites_found;
    text_counter = s->text_counter;
    prev_line = s->prev_line;
    paddle_toggle[0] = s->paddle[0];
    paddle_toggle[1] = s->paddle[1];
    lightgun_latch = s->lightgun;
    pio_restore();
    restore_slot();
    for (int i = 0; i < PALETTE_SIZE; ++i) palette_sync(i);
    return rom_error ? RG_ROM_IO : RG_OK;
}
