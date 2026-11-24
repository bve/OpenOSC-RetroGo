/* OpenOSC-RetroGo platform layer for FCEUmm: ROM paging, allocation, the
 * save-state stream, video output, and stubs for excluded components.
 * Distributed under the GNU GPL v2 or later, like FCEUmm. */
#include "port.h"
#include <stdarg.h>
#include <string.h>
#include "fceu-types.h"
#include "fceu.h"
#include "fceu-cart.h"
#include "ppu.h"
#include "md5.h"
#include "x6502.h"
#include "filesystem.h"

RgHost port_host;
uint32_t port_rom_bytes;
int port_rom_error;
uint16_t port_frame[PortWidth * PortHeight];

/* -------------------------------------------------------------------------
 * ROM pager. PRG ROM is cached in 8K pages and CHR ROM in 1K pages. A page
 * stays resident while any CPU or PPU mapping entry points into it. */

uint8 FCEU_ROMToken[2];

typedef struct {
    uint32_t page; /* Page number plus one; zero marks a free slot. */
    uint32_t age;
} Slot;

typedef struct {
    uint8_t *bytes, *spill;
    Slot *slots;
    uint8_t *resident;
    unsigned count, primary, page_bytes, resident_count;
    uint32_t file_offset, size, age;
} Pager;

enum { PrgSlots = 32, ChrSlots = 128, LocalSlots = 8, MaxRomBytes = 8 * 1024 * 1024 };
static uint8_t local_cache[128 * 1024] __attribute__((aligned(32)));
static uint8_t empty_chr[PortChrPage];
static Slot prg_slots[PrgSlots], chr_slots[ChrSlots];
static uint8_t prg_resident[MaxRomBytes / PortPrgPage], chr_resident[MaxRomBytes / PortChrPage];
static Pager prg, chr;

static uint8_t *slot_address(const Pager *p, unsigned slot)
{
    if (slot < p->primary) return p->bytes + slot * p->page_bytes;
    return p->spill + (slot - p->primary) * p->page_bytes;
}

static void pager_reset(Pager *p, uint32_t offset, uint32_t size)
{
    memset(p->slots, 0, p->count * sizeof(Slot));
    memset(p->resident, 0, p->resident_count);
    p->file_offset = offset;
    p->size = size;
    p->age = 0;
}

static void remember(Pager *p, unsigned slot, unsigned page)
{
    const unsigned previous = p->slots[slot].page;
    if (previous && previous <= p->resident_count) p->resident[previous - 1] = 0;
    p->slots[slot].page = page + 1;
    p->slots[slot].age = ++p->age;
    if (page < p->resident_count) p->resident[page] = slot + 1;
}

static void mark(uint32_t *mask, const Pager *p, const uint8_t *address)
{
    unsigned slot;
    const uintptr_t start = (uintptr_t)address - (uintptr_t)p->bytes;
    if (start < p->primary * p->page_bytes) {
        slot = start / p->page_bytes;
    } else {
        const uintptr_t spill = (uintptr_t)address - (uintptr_t)p->spill;
        if (spill >= (p->count - p->primary) * p->page_bytes) return;
        slot = p->primary + spill / p->page_bytes;
    }
    mask[slot / 32] |= 1u << (slot % 32);
}

/* Mapping tables hold pointers biased by their base address. */
static void pinned(const Pager *p, uint32_t *mask)
{
    memset(mask, 0, ((p->count + 31) / 32) * sizeof(uint32_t));
    if (p == &prg) {
        for (int i = 0; i < 32; ++i) mark(mask, p, Page[i] + (i << 11));
    } else {
        for (int i = 0; i < 8; ++i) {
            mark(mask, p, VPage[i] + (i << 10));
            mark(mask, p, MMC5SPRVPage[i] + (i << 10));
            mark(mask, p, MMC5BGVPage[i] + (i << 10));
        }
        for (int i = 0; i < 4; ++i) mark(mask, p, vnapage[i]);
    }
}

static uint8_t *pager_get(Pager *p, uint32_t offset)
{
    const uint32_t page = offset / p->page_bytes, within = offset % p->page_bytes;
    if (page < p->resident_count && p->resident[page]) {
        const unsigned slot = p->resident[page] - 1;
        p->slots[slot].age = ++p->age;
        return slot_address(p, slot) + within;
    }
    unsigned victim = p->count;
    uint32_t mask[(ChrSlots + 31) / 32];
    pinned(p, mask);
    uint32_t oldest = 0;
    for (unsigned i = 0; i < p->count; ++i) {
        if (mask[i / 32] & (1u << (i % 32))) continue;
        if (!p->slots[i].page) {
            victim = i;
            break;
        }
        const uint32_t elapsed = p->age - p->slots[i].age;
        if (victim == p->count || elapsed > oldest) {
            victim = i;
            oldest = elapsed;
        }
    }
    if (victim == p->count) victim = 0; /* Unreachable: mappings pin fewer pages than slots. */
    uint8_t *out = slot_address(p, victim);
    const uint32_t start = page * p->page_bytes;
    /* Past the end of a non-power-of-two image reads 0xFF, as upstream pads. */
    const uint32_t bytes = start >= p->size ? 0 : p->size - start < p->page_bytes ? p->size - start : p->page_bytes;
    if (bytes && (port_rom_error ||
                  port_host.read_rom(port_host.context, p->file_offset + start, out, bytes))) {
        port_rom_error = 1;
        memset(out, 0xff, p->page_bytes);
    } else {
        memset(out + bytes, 0xff, p->page_bytes - bytes);
    }
    remember(p, victim, page);
    return out + within;
}

uint8 *FCEU_PortPRG(uint32 offset) { return pager_get(&prg, offset); }
uint8 *FCEU_PortCHR(uint32 offset) { return pager_get(&chr, offset); }

/* CRC-32 (IEEE), table-free. */
unsigned int crc32_le(unsigned int crc, unsigned char const *buf, unsigned int len)
{
    crc = ~crc;
    while (len--) {
        crc ^= *buf++;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
    }
    return ~crc;
}

/* Keep any ROM region that fits in its cache during the initial checksum pass. */
static int scan_rom(Pager *p, uint32_t *crc, uint32_t *all, struct md5_context *md5)
{
    uint8_t scratch[PortPrgPage];
    const int fits = p->size <= p->count * p->page_bytes;
    const unsigned chunk = fits ? p->page_bytes : sizeof(scratch);
    for (uint32_t offset = 0; offset < p->size; offset += chunk) {
        const uint32_t remaining = p->size - offset;
        const uint32_t length = remaining < chunk ? remaining : chunk;
        const unsigned page = offset / chunk;
        uint8_t *out = fits ? slot_address(p, page) : scratch;
        if (port_host.read_rom(port_host.context, p->file_offset + offset, out, length)) {
            port_rom_error = 1;
            return 0;
        }
        *crc = crc32_le(*crc, out, length);
        *all = crc32_le(*all, out, length);
        md5_update(md5, out, length);
        if (fits) {
            memset(out + length, 0xff, chunk - length);
            remember(p, page, page);
        }
    }
    return 1;
}

int FCEU_PortLayout(uint32 prg_offset, uint32 prg_size, uint32 chr_offset, uint32 chr_size,
                    uint32 *prg_crc, uint32 *chr_crc, uint32 *crc, struct md5_context *md5)
{
    if ((uint64_t)chr_offset + chr_size > port_rom_bytes) return 0;
    if (prg.bytes != local_cache && !chr_size) {
        // CHR-RAM cartridges can use the entire combined cache for PRG.
        prg.spill = local_cache;
        prg.count = PrgSlots;
        chr.bytes = empty_chr;
        chr.primary = chr.count = 1;
    }
    pager_reset(&prg, prg_offset, prg_size);
    pager_reset(&chr, chr_offset, chr_size);
    *prg_crc = *chr_crc = *crc = 0;
    return scan_rom(&prg, prg_crc, crc, md5) && scan_rom(&chr, chr_crc, crc, md5);
}

/* -------------------------------------------------------------------------
 * Allocation: FCEUmm's Game & Watch path allocates from fixed pools and never
 * frees. A per-game arena serves those requests and is reset on open. */

static uint8_t arena[PortArenaBytes] __attribute__((aligned(8)));
static uint32_t arena_used;

static void *arena_alloc(size_t bytes, int zero)
{
    const size_t rounded = (bytes + 7) & ~(size_t)7;
    if (rounded > sizeof(arena) - arena_used) return NULL;
    void *out = arena + arena_used;
    arena_used += rounded;
    if (zero) memset(out, 0, bytes);
    return out;
}

void *itc_calloc(size_t count, size_t size) { return count && size > SIZE_MAX / count ? NULL : arena_alloc(count * size, 1); }
void *ahb_calloc(size_t count, size_t size) { return itc_calloc(count, size); }
void *itc_malloc(size_t size) { return arena_alloc(size, 0); }
void *ahb_malloc(size_t size) { return arena_alloc(size, 0); }
uint32_t port_arena_used(void) { return arena_used; }

/* -------------------------------------------------------------------------
 * Save-state stream. */

struct fs_file {
    uint8_t *buffer;
    uint32_t capacity, position, length;
    int writing, failed;
};
static struct fs_file stream;

void port_stream(uint8_t *buffer, uint32_t capacity, int writing)
{
    stream = (struct fs_file){buffer, capacity, 0, writing ? 0 : capacity, writing, 0};
}
uint32_t port_stream_length(void) { return stream.writing ? stream.length : stream.position; }
int port_stream_failed(void) { return stream.failed; }

fs_file_t *fs_open(const char *path, int mode, int flags)
{
    (void)path;
    (void)flags;
    return (mode == FS_WRITE) == stream.writing ? &stream : NULL;
}

int fs_read(fs_file_t *f, void *out, size_t size)
{
    if (!f || f->writing) return 0;
    /* FCEUmm detects the end of a state with a read at EOF. */
    if (size > f->length - f->position) size = f->length - f->position;
    memcpy(out, f->buffer + f->position, size);
    f->position += size;
    return (int)size;
}

int fs_write(fs_file_t *f, const void *data, size_t size)
{
    if (!f || !f->writing) return 0;
    if (f->buffer) {
        if (size > f->capacity - f->position) {
            f->failed = 1;
            return 0;
        }
        memcpy(f->buffer + f->position, data, size);
    }
    f->position += size;
    if (f->position > f->length) f->length = f->position;
    return (int)size;
}

int fs_seek(fs_file_t *f, int offset, int whence)
{
    if (!f) return -1;
    const int64_t base = whence == LFS_SEEK_SET ? 0 : whence == LFS_SEEK_END ? f->length : f->position;
    const int64_t target = base + offset;
    if (target < 0 || target > f->length) {
        f->failed = 1;
        return -1;
    }
    f->position = (uint32_t)target;
    return (int)target;
}

void fs_close(fs_file_t *f) { (void)f; }

/* -------------------------------------------------------------------------
 * Video. The PPU hands over each finished line of 8-bit palette indexes. */

static uint16_t palette[256];

void FCEUD_SetPalette(uint8 index, uint8 r, uint8 g, uint8 b)
{
    palette[index] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

void FCEUD_OutputLine(int line, const uint8 *pixels)
{
    uint16_t *out = port_frame + line * PortWidth;
    for (int x = 0; x < PortWidth; ++x) out[x] = palette[pixels[x]];
}

void FCEUD_FillFrame(uint8 index)
{
    for (unsigned i = 0; i < PortWidth * PortHeight; ++i) port_frame[i] = palette[index];
}

/* -------------------------------------------------------------------------
 * Frontend globals and stubs for components that are not built. */

uint8_t *UNIFchrrama = 0;
unsigned overclock_enabled = 0, overclocked = 0, skip_7bit_overclocking = 1;
unsigned totalscanlines = 240, normal_scanlines = 240, extrascanlines = 0, vblankscanlines = 0;

/* Audio sampling is disabled, so the output filters are no-ops. */
void SexyFilter(int32 *in, int32 *out, int32 count) { (void)in; (void)out; (void)count; }
void SexyFilter2(int32 *in, int32 count) { (void)in; (void)count; }
void MakeFilters(int32 rate) { (void)rate; }
int32 NeoFilterSound(int32 *in, int32 *out, uint32 inlen, int32 *leftover)
{
    (void)in; (void)out; (void)inlen; *leftover = 0;
    return 0;
}

/* The Famicom Disk System needs a BIOS and is not built. */
int FDSLoad(const char *name, const char *rom, uint32_t rom_size) { (void)name; (void)rom; (void)rom_size; return 0; }
static uint8 open_bus(uint32 A) { (void)A; return X.DB; }
uint8 FDSRead4030(uint32 A) { return open_bus(A); }
uint8 FDSRead4031(uint32 A) { return open_bus(A); }
uint8 FDSRead4032(uint32 A) { return open_bus(A); }
uint8 FDSRead4033(uint32 A) { return open_bus(A); }
uint8 FDSSRead(uint32 A) { return open_bus(A); }
uint8 FDSWaveRead(uint32 A) { return open_bus(A); }
void FDSSWrite(uint32 A, uint8 V) { (void)A; (void)V; }
void FCEU_FDSInsert(int oride) { (void)oride; }
void FCEU_FDSEject(void) {}
void FCEU_FDSSelect(void) {}
void FDSSoundPower(void) {}
void FDSWaveWrite(uint32 A, uint8 V) { (void)A; (void)V; }

/* Frontend state normally defined by the libretro driver. */
unsigned dendy = 0;

/* Audio filter accumulators are part of the sound state chunk. */
int64 sexyfilter_acc1 = 0, sexyfilter_acc2 = 0;

/* Cheats are not built. */
void FCEU_CheatAddRAM(int s, uint32 A, uint8 *p) { (void)s; (void)A; (void)p; }

/* The C library heap is not used: the build renames malloc, calloc,
   realloc, and free to these. Only teardown paths that the port never runs
   free memory, and those blocks belong to the arena. */
void *fceumm_malloc(size_t size) { return arena_alloc(size, 0); }
void *fceumm_calloc(size_t count, size_t size) { return itc_calloc(count, size); }
void *fceumm_realloc(void *ptr, size_t size) { (void)ptr; (void)size; return NULL; }
void fceumm_free(void *ptr) { (void)ptr; }

/* Diagnostic formatting is discarded; the build renames sprintf and
   vsprintf to these. */
int fceumm_sprintf(char *out, const char *format, ...) { (void)format; if (out) *out = 0; return 0; }
int fceumm_vsprintf(char *out, const char *format, va_list args) { (void)format; (void)args; if (out) *out = 0; return 0; }

/* Power-on RAM fill. A fixed sequence keeps power-on state reproducible;
   the build renames rand and srand to these. */
static uint32_t rand_state = 1;
int fceumm_rand(void)
{
    rand_state = rand_state * 1103515245u + 12345u;
    return (int)((rand_state >> 16) & 0x7fff);
}
void fceumm_srand(unsigned seed) { rand_state = seed; }

/* Diagnostics have no output device. */
void FCEUD_DispMessage(enum retro_log_level level, unsigned duration, const char *str) { (void)level; (void)duration; (void)str; }
void FCEUD_PrintError(char *s) { (void)s; }
void FCEUD_Message(char *s) { (void)s; }
int fceumm_printf(const char *format, ...) { (void)format; return 0; }

void port_reset(const RgHostV3 *host)
{
    const int expanded = host->workspace && host->workspace_bytes >= RG_CARTRIDGE_WORKSPACE_CAPACITY;
    const unsigned prg_count = expanded ? PrgSlots / 2 : LocalSlots;
    const unsigned chr_count = expanded ? ChrSlots : ChrSlots / 2;
    prg = (Pager){ .bytes = expanded ? host->workspace : local_cache, .slots = prg_slots,
                   .resident = prg_resident, .count = prg_count, .primary = prg_count,
                   .page_bytes = PortPrgPage, .resident_count = sizeof(prg_resident) };
    chr = (Pager){ .bytes = expanded ? local_cache : local_cache + LocalSlots * PortPrgPage,
                   .slots = chr_slots, .resident = chr_resident, .count = chr_count, .primary = chr_count,
                   .page_bytes = PortChrPage, .resident_count = sizeof(chr_resident) };
    arena_used = 0;
    port_rom_error = 0;
    pager_reset(&prg, 0, 0);
    pager_reset(&chr, 0, 0);
    memset(port_frame, 0, sizeof(port_frame));
    memset(palette, 0, sizeof(palette));
    rand_state = 1;
}
