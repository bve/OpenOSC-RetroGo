#include "retrogo/module.hpp"
#include "retrogo/controls.hpp"
#include "openosc/update/crc32.hpp"
#include <cstring>

namespace retrogo {
namespace {
bool name(const char *value, size_t capacity, bool allowDash) {
    size_t i = 0;
    for (; i < capacity && value[i]; ++i) {
        const char c = value[i];
        if ((c < 'a' || c > 'z') && (c < '0' || c > '9') && !(allowDash && c == '-'))
            return false;
    }
    return i != 0 && (i < capacity || !allowDash);
}

}

ModuleStatus validate(const RgCoreHeader &h, uint32_t fileSize) {
    if (std::memcmp(h.magic, "RGCORE1", 8) || h.format != RG_CORE_FORMAT ||
        (!h.abi || h.abi > RG_CORE_ABI) || h.header_size != sizeof(h) || h.base != RG_CORE_BASE)
        return ModuleStatus::Incompatible;
    auto copy = h;
    copy.header_crc = 0;
    if (openosc::Crc32::compute(&copy, sizeof(copy)) != h.header_crc) return ModuleStatus::Damaged;
    if (h.text_size < 2 || h.text_size > RG_CORE_CAPACITY ||
        h.data_size > RG_CORE_CAPACITY - h.text_size ||
        h.bss_offset != h.text_size + h.data_size || h.bss_offset % 32 ||
        h.bss_size > RG_CORE_CAPACITY - h.bss_offset ||
        fileSize != sizeof(h) + h.bss_offset)
        return ModuleStatus::Damaged;
    for (unsigned i = 0; i < RG_ENTRY_COUNT; ++i) {
        const uint32_t entry = h.entry[i];
        if (i >= RG_SAVE && !entry) continue;
        if (!(entry & 1) || (entry & ~1u) > h.text_size - 2) return ModuleStatus::Damaged;
    }
    Bindings defaults;
    std::memcpy(defaults.data(), h.defaults, defaults.size());
    if (bool(h.entry[RG_SAVE]) != bool(h.entry[RG_LOAD]) ||
        !validVideoMode({h.width, h.height, h.period_num, h.period_den}) || !h.max_rom_size || !h.action_mask || (h.action_mask >> RG_ACTION_COUNT) ||
        h.reserved || !name(h.id, sizeof(h.id), true) || !validBindings(defaults, h.action_mask))
        return ModuleStatus::Incompatible;
    if (!h.extensions[0][0]) return ModuleStatus::Incompatible;
    for (const auto &extension : h.extensions)
        if (extension[0] && !name(extension, sizeof(extension), false))
            return ModuleStatus::Incompatible;
    return ModuleStatus::Ok;
}

bool validVideoMode(const RgVideoMode &mode) {
    if (!mode.width || !mode.height || mode.width > 320 || mode.height > 240 ||
        !mode.period_den || mode.period_den > 10000) return false;
    const auto period = mode.period_num / mode.period_den;
    return period >= 8000 && period <= 40000;
}

bool supports(const RgCoreHeader &h, const char *extension) {
    for (const auto &value : h.extensions) {
        if (!value[0]) continue;
        unsigned i = 0;
        for (; i < sizeof(value); ++i) {
            char c = extension[i];
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (c != value[i]) break;
            if (!c) return true;
        }
        if (i == sizeof(value) && !extension[i]) return true;
    }
    return false;
}
}
