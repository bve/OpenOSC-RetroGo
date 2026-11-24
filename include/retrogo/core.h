#ifndef OPENOSC_RETROGO_CORE_H
#define OPENOSC_RETROGO_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Cortex-M7 Thumb-2, AAPCS32 hard float, little-endian. Only the host owns peripherals. */
#define RG_CORE_FORMAT 1u
#define RG_CORE_ABI 3u
#define RG_CORE_BASE 0x24080000u
#define RG_CORE_CAPACITY 0x80000u
#define RG_SNAPSHOT_CAPACITY (192u * 1024u)
#define RG_CARTRIDGE_WORKSPACE_CAPACITY (128u * 1024u)
#define RG_FRAME_PIXELS (320u * 240u)
#define RG_EXTENSION_COUNT 4
#define RG_ACTION_COUNT 12
#define RG_UNBOUND 0xffu

enum RgPhysicalKey {
    RG_F1_KEY, RG_F2_KEY, RG_F3_KEY, RG_F4_KEY, RG_MENU_KEY,
    RG_UP_KEY, RG_DOWN_KEY, RG_LEFT_KEY, RG_RIGHT_KEY,
    RG_MODE_KEY, RG_HOLD_KEY, RG_AUTO_KEY, RG_KEY_COUNT
};

enum RgAction {
    RG_UP, RG_DOWN, RG_LEFT, RG_RIGHT, RG_A, RG_B, RG_START, RG_SELECT,
    RG_X, RG_Y, RG_L, RG_R
};

enum RgResult { RG_OK, RG_BAD_ROM, RG_UNSUPPORTED_ROM, RG_ROM_IO, RG_BAD_SAVE, RG_MISSING_BIOS };
enum RgSaveKind { RG_SRAM, RG_STATE };
enum RgEntry { RG_OPEN, RG_RUN, RG_SAVE, RG_LOAD, RG_ENTRY_COUNT };

/* A zero offset disables an optional entry. Thumb offsets include the low bit.
 * The header CRC covers the entire header with header_crc set to zero. The
 * image CRC covers all text, rodata and initialized data bytes, including padding. */
typedef struct {
    char magic[8];
    uint32_t format, abi, header_size, base;
    uint32_t text_size, data_size, bss_offset, bss_size;
    uint32_t image_crc, header_crc;
    uint32_t entry[RG_ENTRY_COUNT];
    uint32_t period_num, period_den; /* microseconds = numerator / denominator */
    uint32_t max_rom_size;
    uint16_t width, height, action_mask, reserved;
    char id[16];
    char extensions[RG_EXTENSION_COUNT][8]; /* Lowercase ASCII, without dot. */
    uint8_t defaults[RG_ACTION_COUNT];      /* Each value is a physical bit index or RG_UNBOUND. */
} RgCoreHeader;

typedef struct {
    uint16_t width, height;
    uint32_t period_num, period_den;
} RgVideoMode;

typedef struct {
    void *context;
    int (*read_rom)(void *context, uint32_t offset, void *out, uint32_t bytes); /* zero on success */
    uint32_t (*seconds)(void *context); /* Monotonic time, or wall-clock seconds when available. */
    /* ABI 2 additions. ABI 1 modules only use the fields above. */
    const char *extension; /* Lowercase extension of the selected file. */
    enum RgResult (*read_bios)(void *context, const char *name, void *out, uint32_t bytes);
    int (*set_video)(void *context, RgVideoMode mode); /* During rg_open only. Zero on success. */
} RgHost;

/* ABI 3 modules receive this structure through the RgHost prefix. Workspace
 * is optional and stays valid until the core closes. Cores with save entry
 * points can receive separate workspace so saving does not overwrite it. */
typedef struct {
    RgHost base;
    void *workspace;
    uint32_t workspace_bytes;
} RgHostV3;

typedef enum RgResult (*RgOpen)(const RgHost *host, uint32_t rom_size);
typedef enum RgResult (*RgRun)(uint32_t buttons, const uint16_t **rgb565);
typedef uint32_t (*RgSave)(enum RgSaveKind kind, void *out, uint32_t capacity);
typedef enum RgResult (*RgLoad)(enum RgSaveKind kind, const void *data, uint32_t size);

#ifdef __cplusplus
}
static_assert(sizeof(RgCoreHeader) == 144);
#endif
#endif
