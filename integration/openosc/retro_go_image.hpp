#pragma once

#include "openosc/update/crc32.hpp"
#include "openosc/update/image.hpp"
#include <cstring>

namespace openosc {
inline constexpr uint32_t RetroGoBase = 0x081a0000;
inline constexpr uint32_t RetroGoVectors = RetroGoBase + layout::HeaderBytes;
inline constexpr uint32_t RetroGoMaxPayload = 383u * 1024u;

static_assert(RetroGoVectors + RetroGoMaxPayload == layout::FlashEnd);
static_assert(RetroGoBase % layout::SectorBytes == 0);

inline bool retroGoImageValid(const ImageHeader &header, const void *payload,
                             size_t available, void (*service)() = nullptr) {
    if (std::memcmp(header.magic, ImageMagic, sizeof(ImageMagic)) ||
        header.format != 1 || header.headerBytes != sizeof(header) || header.version[31] ||
        header.boardId != layout::BoardId || header.loadAddress != RetroGoVectors ||
        header.payloadBytes < 8 || header.payloadBytes > RetroGoMaxPayload ||
        header.payloadBytes > available || ImageValidator::headerCrc(header) != header.headerCrc)
        return false;

    uint32_t vectors[2];
    std::memcpy(vectors, payload, sizeof(vectors));
    if (ImageValidator::vectors(header, vectors[0], vectors[1]) != ImageError::None)
        return false;

    const auto *data = static_cast<const uint8_t *>(payload);
    Crc32 crc;
    for (size_t offset = 0; offset < header.payloadBytes;) {
        const size_t left = header.payloadBytes - offset;
        const size_t bytes = left < 4096 ? left : 4096;
        crc.update(data + offset, bytes);
        offset += bytes;
        if (service) service();
    }
    return crc.value() == header.payloadCrc;
}

void tryRetroGo();
}
