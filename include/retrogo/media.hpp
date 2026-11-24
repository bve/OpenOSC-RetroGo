#pragma once
#include "retrogo/module.hpp"
#include "ff.h"

namespace retrogo {
inline constexpr char GamesPath[] = "/openosc/games";
inline constexpr char RomsPath[] = "/openosc/games/roms";
inline constexpr char CoresPath[] = "/openosc/games/cores";
inline constexpr char SavesPath[] = "/openosc/games/saves";
inline constexpr char ControlsPath[] = "/openosc/games/controls";
inline constexpr char BiosPath[] = "/openosc/games/bios";
inline constexpr unsigned PathSize = 320;

bool prepareMedia();
bool filePath(char (&output)[PathSize], const char *directory, const char *name);
const char *extensionOf(const char *name);

class Directory {
public:
    explicit Directory(const char *path);
    Directory(const Directory &) = delete;
    Directory &operator=(const Directory &) = delete;
    ~Directory();
    bool next();
    FRESULT result() const { return result_; }
    const char *name() const { return entry_.fname; }
private:
    DIR dir_{};
    FILINFO entry_{};
    FRESULT result_;
    bool open_;
};

ModuleStatus findCore(const char *extension, RgCoreHeader &header, char (&path)[PathSize],
                      const char *cursor = nullptr, bool reverse = false);

class Rom {
public:
    bool open(const char *path);
    void close();
    uint32_t size() const { return f_size(&file_); }
    uint32_t identity() const { return identity_; }
    RgHost services(const RgCoreHeader &info);
    RgVideoMode finishOpen() { configuring_ = false; return video_; }
private:
    static int read(void *context, uint32_t offset, void *out, uint32_t bytes);
    static uint32_t seconds(void *);
    static RgResult readBios(void *, const char *name, void *out, uint32_t bytes);
    static int setVideo(void *context, RgVideoMode mode);
    FIL file_{};
    DWORD linkMap_[1026]{};
    uint32_t identity_ = 0;
    char extension_[9]{};
    RgVideoMode video_{};
    bool open_ = false, configuring_ = false;
};
}
