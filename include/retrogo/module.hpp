#pragma once
#include "retrogo/core.h"
#include <cstddef>
#include <cstdint>

namespace retrogo {
enum class ModuleStatus { Ok, Missing, Incompatible, Damaged, Io };
ModuleStatus validate(const RgCoreHeader &header, uint32_t fileSize);
bool validVideoMode(const RgVideoMode &mode);
bool supports(const RgCoreHeader &header, const char *extension);

class Core {
public:
    Core() = default;
    Core(const Core &) = delete;
    Core &operator=(const Core &) = delete;
    ModuleStatus load(const char *path);
    void unload();
    const RgCoreHeader &info() const { return header_; }
    void setVideoMode(RgVideoMode mode) { video_ = mode; }
    const RgVideoMode &videoMode() const { return video_; }
    RgResult open(const RgHostV3 &host, uint32_t bytes) const;
    RgResult run(uint32_t buttons, const uint16_t **pixels) const;
    uint32_t save(RgSaveKind kind, void *out, uint32_t capacity) const;
    RgResult restore(RgSaveKind kind, const void *data, uint32_t size) const;
    bool snapshots() const { return header_.entry[RG_SAVE] && header_.entry[RG_LOAD]; }
private:
    RgCoreHeader header_{};
    RgVideoMode video_{};
};
}
