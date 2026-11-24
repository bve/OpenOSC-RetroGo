#include "retrogo/module.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/watchdog.hpp"
#include "openosc/update/crc32.hpp"
#include "ff.h"
#include <cstring>

namespace retrogo {
namespace {
uint8_t *const arena = reinterpret_cast<uint8_t *>(RG_CORE_BASE);

template<class Function>
Function address(uint32_t offset) {
    return reinterpret_cast<Function>(RG_CORE_BASE + offset);
}
}

ModuleStatus Core::load(const char *path) {
    unload();
    FIL file{};
    const auto opened = f_open(&file, path, FA_READ);
    if (opened != FR_OK) return opened == FR_NO_FILE || opened == FR_NO_PATH
                                  ? ModuleStatus::Missing : ModuleStatus::Io;

    RgCoreHeader candidate{};
    UINT got = 0;
    const auto read = f_read(&file, &candidate, sizeof(candidate), &got);
    auto status = read != FR_OK ? ModuleStatus::Io : got != sizeof(candidate)
               ? ModuleStatus::Damaged : validate(candidate, f_size(&file));
    size_t copied = 0;
    size_t touched = 0;
    while (status == ModuleStatus::Ok && copied < candidate.bss_offset) {
        const auto trailing = candidate.bss_offset - copied;
        const UINT chunk = trailing > 8192 ? 8192 : trailing;
        touched = copied + chunk;
        if (f_read(&file, arena + copied, chunk, &got) != FR_OK || got != chunk) {
            status = ModuleStatus::Io;
            break;
        }
        copied += got;
        openosc::Watchdog::progress();
        openosc::Board::service();
    }
    if (f_close(&file) != FR_OK && status == ModuleStatus::Ok) status = ModuleStatus::Io;
    if (status == ModuleStatus::Ok && openosc::Crc32::compute(arena, copied) != candidate.image_crc)
        status = ModuleStatus::Damaged;
    if (status != ModuleStatus::Ok) {
        if (touched) std::memset(arena, 0, touched);
        return status;
    }

    std::memset(arena + candidate.bss_offset, 0, candidate.bss_size);
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t *>(arena),
                           int32_t(candidate.bss_offset + candidate.bss_size));
    __DSB();
    SCB_InvalidateICache();
    __ISB();
    header_ = candidate;
    video_ = {candidate.width, candidate.height, candidate.period_num, candidate.period_den};
    return ModuleStatus::Ok;
}

void Core::unload() {
    if (!header_.text_size) return;
    std::memset(arena, 0, header_.bss_offset + header_.bss_size);
    header_ = {};
}
RgResult Core::open(const RgHostV3 &host, uint32_t bytes) const {
    if (!header_.entry[RG_OPEN]) return RG_BAD_ROM;
    if (bytes > header_.max_rom_size) return RG_UNSUPPORTED_ROM;
    return address<RgOpen>(header_.entry[RG_OPEN])(&host.base, bytes);
}
RgResult Core::run(uint32_t buttons, const uint16_t **pixels) const {
    *pixels = nullptr;
    if (!header_.entry[RG_RUN]) return RG_BAD_ROM;
    const auto result = address<RgRun>(header_.entry[RG_RUN])(buttons, pixels);
    if (result != RG_OK) return result;
    const auto start = reinterpret_cast<uintptr_t>(*pixels);
    const size_t bytes = size_t(video_.width) * video_.height * sizeof(uint16_t);
    const uintptr_t end = RG_CORE_BASE + header_.bss_offset + header_.bss_size;
    if ((start & 1) || start < RG_CORE_BASE || bytes > end - RG_CORE_BASE || start > end - bytes)
        return RG_BAD_ROM;
    return result;
}
uint32_t Core::save(RgSaveKind kind, void *out, uint32_t capacity) const {
    return snapshots() ? address<RgSave>(header_.entry[RG_SAVE])(kind, out, capacity) : 0;
}
RgResult Core::restore(RgSaveKind kind, const void *data, uint32_t size) const {
    return snapshots() ? address<RgLoad>(header_.entry[RG_LOAD])(kind, data, size) : RG_BAD_SAVE;
}
}
