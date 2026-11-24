#include "retrogo/media.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/watchdog.hpp"
#include "openosc/update/crc32.hpp"
#include <cstdio>
#include <cstring>

namespace retrogo {
bool prepareMedia() {
    const char *const dirs[] = {
        "/openosc", GamesPath, RomsPath, CoresPath, SavesPath, BiosPath,
        "/openosc/games/saves/a", "/openosc/games/saves/b",
        "/openosc/games/controls", "/openosc/games/controls/a", "/openosc/games/controls/b"
    };
    for (const auto *path : dirs) {
        auto result = f_mkdir(path);
        if (result == FR_EXIST) {
            FILINFO item{};
            if (f_stat(path, &item) != FR_OK || !(item.fattrib & AM_DIR)) return false;
        } else if (result != FR_OK) {
            return false;
        }
    }
    return true;
}

bool filePath(char (&output)[PathSize], const char *directory, const char *name) {
    const int length = std::snprintf(output, sizeof(output), "%s/%s", directory, name);
    return length > 0 && unsigned(length) < sizeof(output);
}

const char *extensionOf(const char *name) {
    const char *dot = std::strrchr(name, '.');
    return dot ? dot + 1 : "";
}

Directory::Directory(const char *path) : result_(f_opendir(&dir_, path)), open_(result_ == FR_OK) {}
Directory::~Directory() { if (open_) f_closedir(&dir_); }

bool Directory::next() {
    if (result_ != FR_OK || !open_) return false;
    do {
        result_ = f_readdir(&dir_, &entry_);
        if (result_ != FR_OK || !entry_.fname[0]) return false;
        openosc::Watchdog::progress();
    } while (entry_.fattrib & (AM_DIR | AM_HID | AM_SYS));
    return true;
}

ModuleStatus findCore(const char *extension, RgCoreHeader &chosen, char (&path)[PathSize],
                      const char *cursor, bool reverse) {
    Directory dir(CoresPath);
    ModuleStatus best = ModuleStatus::Missing;
    char candidatePath[PathSize]{};
    while (dir.next()) {
        RgCoreHeader fileType{};
        std::memcpy(fileType.extensions[0], "rgc", 4);
        if (!supports(fileType, extensionOf(dir.name())) || !filePath(candidatePath, CoresPath, dir.name()))
            continue;
        FIL file{};
        if (f_open(&file, candidatePath, FA_READ) != FR_OK) return ModuleStatus::Io;
        RgCoreHeader header{};
        UINT got = 0;
        const auto read = f_read(&file, &header, sizeof(header), &got);
        const auto size = f_size(&file);
        const auto closed = f_close(&file);
        if (read != FR_OK || closed != FR_OK) return ModuleStatus::Io;
        const bool advertised = supports(header, extension);
        // An unreadable header cannot advertise extensions. Match a file with
        // the extension as its stem, e.g. gb.rgc, to report the damaged module.
        const bool byName = std::strncmp(dir.name(), extension, std::strlen(extension)) == 0 &&
                            dir.name()[std::strlen(extension)] == '.';
        if (!advertised && !byName) continue;
        const auto result = got == sizeof(header) ? validate(header, size) : ModuleStatus::Damaged;
        if (result != ModuleStatus::Ok) {
            if (best == ModuleStatus::Missing) best = result;
            continue;
        }
        if (!advertised) continue;
        if (cursor) {
            const int position = std::strcmp(header.id, cursor);
            if (reverse ? position >= 0 : position <= 0) continue;
        }
        const int order = best == ModuleStatus::Ok ? std::strcmp(header.id, chosen.id) : 0;
        if (best == ModuleStatus::Ok && (order ? (reverse ? order < 0 : order > 0) :
            std::strcmp(candidatePath, path) >= 0)) continue;
        best = ModuleStatus::Ok;
        std::strcpy(path, candidatePath);
        chosen = header;
    }
    return dir.result() == FR_OK ? best : ModuleStatus::Io;
}

bool Rom::open(const char *path) {
    close();
    file_ = {};
    if (f_open(&file_, path, FA_READ) != FR_OK) return false;
    open_ = true;
    linkMap_[0] = sizeof(linkMap_) / sizeof(linkMap_[0]);
    file_.cltbl = linkMap_;
    const auto seek = f_lseek(&file_, CREATE_LINKMAP);
    if (seek != FR_OK) file_.cltbl = nullptr;
    if (seek != FR_OK && seek != FR_NOT_ENOUGH_CORE) {
        close();
        return false;
    }
    uint8_t sample[512];
    const uint32_t length = size();
    const uint32_t bytes = length < sizeof(sample) ? length : sizeof(sample);
    openosc::Crc32 crc;
    crc.update(path, std::strlen(path));
    crc.update(&length, sizeof(length));
    if (read(this, 0, sample, bytes)) {
        close();
        return false;
    }
    crc.update(sample, bytes);
    if (length > sizeof(sample)) {
        if (read(this, length - sizeof(sample), sample, sizeof(sample))) {
            close();
            return false;
        }
        crc.update(sample, sizeof(sample));
    }
    identity_ = crc.value();
    unsigned i = 0;
    const auto *extension = extensionOf(path);
    for (; i < sizeof(extension_) - 1 && extension[i]; ++i) {
        const char ch = extension[i];
        extension_[i] = ch >= 'A' && ch <= 'Z' ? ch + 'a' - 'A' : ch;
    }
    extension_[i] = 0;
    return true;
}

void Rom::close() {
    if (!open_) return;
    f_close(&file_);
    open_ = false;
}

int Rom::read(void *context, uint32_t offset, void *out, uint32_t bytes) {
    auto &rom = *static_cast<Rom *>(context);
    if (!rom.open_ || offset > rom.size() || bytes > rom.size() - offset) return 1;
    if (f_tell(&rom.file_) != offset && f_lseek(&rom.file_, offset) != FR_OK) return 1;
    UINT got = 0;
    return f_read(&rom.file_, out, bytes, &got) != FR_OK || got != bytes;
}

uint32_t Rom::seconds(void *) { return openosc::Board::milliseconds() / 1000; }
RgResult Rom::readBios(void *, const char *name, void *out, uint32_t bytes) {
    if (!name || !*name) return RG_MISSING_BIOS;
    for (const char *p = name; *p; ++p) {
        const char c = *p;
        if ((c < 'A' || c > 'Z') && (c < 'a' || c > 'z') && (c < '0' || c > '9') &&
            c != '-' && c != '_' && c != '.') return RG_MISSING_BIOS;
    }
    if (*name == '.') return RG_MISSING_BIOS;
    char path[PathSize];
    if (!filePath(path, BiosPath, name)) return RG_MISSING_BIOS;
    FIL file{};
    const auto opened = f_open(&file, path, FA_READ);
    if (opened == FR_NO_FILE || opened == FR_NO_PATH) return RG_MISSING_BIOS;
    if (opened != FR_OK) return RG_ROM_IO;
    UINT got = 0;
    const auto result = f_size(&file) == bytes
                      ? f_read(&file, out, bytes, &got) : FR_INVALID_PARAMETER;
    const auto closed = f_close(&file);
    if (result == FR_INVALID_PARAMETER || (result == FR_OK && got != bytes)) return RG_MISSING_BIOS;
    return (result == FR_OK && closed == FR_OK) ? RG_OK : RG_ROM_IO;
}

int Rom::setVideo(void *context, RgVideoMode mode) {
    auto &rom = *static_cast<Rom *>(context);
    if (!rom.configuring_ || !validVideoMode(mode)) return 1;
    rom.video_ = mode;
    return 0;
}

RgHost Rom::services(const RgCoreHeader &info) {
    video_ = {info.width, info.height, info.period_num, info.period_den};
    configuring_ = true;
    return {this, read, seconds, extension_, readBios, setVideo};
}
}
