#include "retrogo/saves.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/watchdog.hpp"
#include "openosc/update/crc32.hpp"
#include <cstdio>
#include <cstring>

namespace retrogo {
namespace {
SaveStatus append(FIL &file, const void *bytes, UINT size) {
    UINT wrote = 0;
    if (f_write(&file, bytes, size, &wrote) != FR_OK) return SaveStatus::Io;
    return wrote == size ? SaveStatus::Ok : SaveStatus::NoSpace;
}
}

SavePair::SavePair(const char *root, const char *id, uint32_t identity, const char *kind)
    : root_(root), id_(id), identity_(identity), kind_(kind) {}

void SavePair::path(unsigned slot, char (&result)[PathSize]) const {
    if (!std::strcmp(root_, SavesPath))
        std::snprintf(result, sizeof(result), "%s/%c/%s-%08lX.%s", root_, 'a' + slot, id_,
                      static_cast<unsigned long>(identity_), kind_);
    else
        std::snprintf(result, sizeof(result), "%s/%c/%s.%s", root_, 'a' + slot, id_, kind_);
}

SavePair::Entry SavePair::inspect(unsigned slot, void *data, uint32_t capacity) const {
    char location[PathSize];
    path(slot, location);
    FIL file{};
    const auto opened = f_open(&file, location, FA_READ);
    if (opened == FR_NO_FILE) return {};
    Entry entry{};
    entry.status = SaveStatus::Io;
    if (opened != FR_OK) return entry;
    auto &h = entry.header;
    UINT got = 0;
    if (f_read(&file, &h, sizeof(h), &got) == FR_OK) {
        entry.status = SaveStatus::Damaged;
        if (got == sizeof(h) && h.magic == Magic && h.payload && h.payload <= capacity &&
            h.guard == openosc::Crc32::compute(&h, sizeof(h) - sizeof(h.guard)) &&
            f_size(&file) == sizeof(h) + h.payload) {
            uint8_t scratch[1024];
            openosc::Crc32 crc;
            entry.status = SaveStatus::Ok;
            for (uint32_t pos = 0; pos < h.payload;) {
                const UINT bytes = h.payload - pos > sizeof(scratch) ? sizeof(scratch) : h.payload - pos;
                auto *destination = data ? static_cast<uint8_t *>(data) + pos : scratch;
                if (f_read(&file, destination, bytes, &got) != FR_OK || got != bytes) {
                    entry.status = SaveStatus::Io;
                    break;
                }
                crc.update(destination, bytes);
                pos += bytes;
                openosc::Watchdog::progress();
            }
            if (entry.status == SaveStatus::Ok && h.crc != crc.value()) entry.status = SaveStatus::Damaged;
        }
    }
    if (f_close(&file) != FR_OK) entry.status = SaveStatus::Io;
    return entry;
}

unsigned SavePair::newest(const Entry (&records)[2]) {
    if (records[0].status != SaveStatus::Ok) return 1;
    if (records[1].status != SaveStatus::Ok) return 0;
    const uint32_t distance = records[1].header.generation - records[0].header.generation;
    return distance && distance < 0x80000000u;
}

SaveStatus SavePair::load(void *data, uint32_t capacity, uint32_t &bytes) const {
    bytes = 0;
    const Entry entries[] = {inspect(0, nullptr, capacity), inspect(1, nullptr, capacity)};
    const auto selected = newest(entries);
    if (entries[selected].status != SaveStatus::Ok) {
        if (entries[0].status == SaveStatus::Io || entries[1].status == SaveStatus::Io) return SaveStatus::Io;
        if (entries[0].status == SaveStatus::Damaged || entries[1].status == SaveStatus::Damaged)
            return SaveStatus::Damaged;
        return SaveStatus::Empty;
    }
    const auto best = inspect(selected, data, capacity);
    if (best.status == SaveStatus::Ok) bytes = best.header.payload;
    return best.status;
}

SaveStatus SavePair::write(const void *data, uint32_t bytes) const {
    if (!bytes || bytes > RG_SNAPSHOT_CAPACITY) return SaveStatus::Damaged;
    const Entry entries[] = {inspect(0, nullptr, RG_SNAPSHOT_CAPACITY), inspect(1, nullptr, RG_SNAPSHOT_CAPACITY)};
    if (entries[0].status == SaveStatus::Io || entries[1].status == SaveStatus::Io) return SaveStatus::Io;
    const auto current = newest(entries);
    const bool existing = entries[current].status == SaveStatus::Ok;
    const auto contents = openosc::Crc32::compute(data, bytes);
    const auto &last = entries[current].header;
    if (existing && last.crc == contents && last.payload == bytes) return SaveStatus::Ok;

    char location[PathSize];
    path(current ^ 1u, location);
    FIL file{};
    const auto opened = f_open(&file, location, FA_WRITE | FA_OPEN_ALWAYS);
    if (opened != FR_OK) return opened == FR_DENIED ? SaveStatus::NoSpace : SaveStatus::Io;
    // Invalidate this generation before changing its data. Keep the previous
    // generation in a different directory and publish this header after sync.
    const Record invalid{};
    auto status = append(file, &invalid, sizeof(invalid));
    if (status == SaveStatus::Ok && f_sync(&file) != FR_OK) status = SaveStatus::Io;
    for (uint32_t pos = 0; status == SaveStatus::Ok && pos < bytes;) {
        const UINT count = bytes - pos > 4096 ? 4096 : bytes - pos;
        status = append(file, static_cast<const uint8_t *>(data) + pos, count);
        pos += count;
        openosc::Watchdog::progress();
        openosc::Board::service();
    }
    if (status == SaveStatus::Ok &&
        (f_truncate(&file) != FR_OK || f_sync(&file) != FR_OK || f_lseek(&file, 0) != FR_OK))
        status = SaveStatus::Io;
    Record publication{Magic, existing ? last.generation + 1 : 1, bytes, contents, 0};
    publication.guard = openosc::Crc32::compute(&publication, sizeof(publication) - sizeof(publication.guard));
    if (status == SaveStatus::Ok) status = append(file, &publication, sizeof(publication));
    if (f_close(&file) != FR_OK) status = SaveStatus::Io;
    if (status != SaveStatus::Ok) return status;
    const auto readback = inspect(current ^ 1u, nullptr, RG_SNAPSHOT_CAPACITY);
    return readback.status == SaveStatus::Ok && readback.header.generation == publication.generation
           ? SaveStatus::Ok : SaveStatus::Io;
}
}
