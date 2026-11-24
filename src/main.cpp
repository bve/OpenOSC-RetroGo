#include "retrogo/controls.hpp"
#include "retrogo/media.hpp"
#include "retrogo/module.hpp"
#include "retrogo/profile.hpp"
#include "retrogo/saves.hpp"
#include "retrogo/view.hpp"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_adc.h"
#include "openosc/platform/battery_monitor.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/clocks.hpp"
#include "openosc/platform/dma_memory.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/platform/power.hpp"
#include "openosc/platform/system_reset.hpp"
#include "openosc/platform/usb_core.hpp"
#include "openosc/platform/watchdog.hpp"
#include "openosc/storage/filesystem.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" void Default_Handler();
extern "C" void OTG_HS_IRQHandler() { Default_Handler(); }

namespace retrogo {
namespace {
using namespace openosc;

constexpr uint32_t Accept = Keypad::Menu | Keypad::F1;
static_assert(Keypad::Mode == (1u << RG_MODE_KEY) && Keypad::HoldSave == (1u << RG_HOLD_KEY) &&
              Keypad::AutoRange == (1u << RG_AUTO_KEY) && Keypad::Relative == Input::Power);

alignas(32) uint8_t snapshot[RG_SNAPSHOT_CAPACITY] __attribute__((section(".workspace")));
alignas(32) uint8_t cartridgeWorkspace[RG_CARTRIDGE_WORKSPACE_CAPACITY] __attribute__((section(".cartridge")));

const char *describe(ModuleStatus status) {
    switch (status) {
    case ModuleStatus::Missing: return "Core missing. Copy a .rgc file to CORES.";
    case ModuleStatus::Incompatible: return "Incompatible core format or ABI.";
    case ModuleStatus::Damaged: return "Core checksum or header is invalid.";
    case ModuleStatus::Io: return "Storage error while reading core.";
    default: return "";
    }
}
const char *describe(RgResult status) {
    switch (status) {
    case RG_BAD_ROM: return "Invalid or corrupted ROM.";
    case RG_UNSUPPORTED_ROM: return "ROM size or cartridge type unsupported.";
    case RG_ROM_IO: return "Storage error while reading ROM.";
    case RG_MISSING_BIOS: return "BIOS missing or invalid. Check games/bios.";
    case RG_BAD_SAVE: return "Save file is incompatible with this core.";
    default: return "";
    }
}
const char *describe(SaveStatus status) {
    switch (status) {
    case SaveStatus::Empty: return "No save found.";
    case SaveStatus::Damaged: return "Save file is damaged.";
    case SaveStatus::NoSpace: return "Not enough free space for a new save.";
    case SaveStatus::Io: return "Storage write or read failed.";
    default: return "";
    }
}

void executableRam() {
    DmaMemory::initialize();
    // Keep the LTDC buffers uncached. Only the core and CPU workspace use
    // cacheable AXI memory. The workspace remains nonexecutable.
    const uint32_t regionCount = (MPU->TYPE >> 8) & 0xff;
    constexpr uint32_t Cacheable = (3u << 24) | (1u << 19) | (1u << 17) | (1u << 16) | 1u;
    __DMB();
    MPU->CTRL = 0;
    MPU->RNR = regionCount - 3;
    MPU->RBAR = 0x30000000;
    MPU->RASR = Cacheable | (1u << 28) | (16u << 1);
    MPU->RNR = regionCount - 2;
    MPU->RBAR = 0x24040000;
    // Exclude the overlap with the framebuffers below 0x24050000.
    MPU->RASR = Cacheable | (1u << 28) | (3u << 8) | (17u << 1);
    MPU->RNR = regionCount - 1;
    MPU->RBAR = RG_CORE_BASE;
    MPU->RASR = Cacheable | (18u << 1);
    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
    __DSB();
    __ISB();
}

void step(unsigned &index, unsigned size, uint32_t keys) {
    if (!size) return;
    if (keys & Keypad::Up) index = index ? index - 1 : size - 1;
    if (keys & Keypad::Down) index = index + 1 < size ? index + 1 : 0;
}

bool romEntry(const char *name) { return name[0] != '.' && *extensionOf(name); }

constexpr unsigned CpuSpeeds[] = {200, 280, 340, 360};
constexpr unsigned CpuSpeedCount = sizeof(CpuSpeeds) / sizeof(CpuSpeeds[0]);
constexpr int BrightnessMin = 10, BrightnessMax = 100, BrightnessStep = 5;

SavePair brightnessRecord() { return SavePair(ControlsPath, "screen", 0, "cfg"); }

constexpr unsigned clockMode(unsigned mhz) {
    for (unsigned mode = 0; mode < RunModeCount; ++mode)
        if (RunModes[mode].coreHz == mhz * 1000000u) return mode;
    return RunModeCount;
}

static_assert(clockMode(200) == 0 && clockMode(280) < RunModeCount &&
              clockMode(340) < RunModeCount && clockMode(360) < RunModeCount);

class App {
public:
    [[noreturn]] void run();
private:
    enum class Page { Library, Cores, Playing, Pause, Controls, Capture, Confirm, Settings, Brightness, CpuClock };
    enum class Exit { None, Close, Restart, PowerOff };
    enum PauseAction { Resume, Mapping, Settings, SaveCart, SaveState, LoadState, Close, Restart, PauseCount };
    enum Setting { Brightness, CpuClock, SettingCount };
    static constexpr const char *PauseLabels[PauseCount] = {
        "Resume", "Controls / Button mapping", "Settings", "Save cartridge", "Save state", "Load state",
        "Close game", "Back to OpenOSC"
    };
    static_assert(PauseCount <= View::Rows);

    void render();
    void libraryView(View &view);
    void controlsView(View &view);
    void settingsView(View &view);
    void brightnessView(View &view);
    void clockView(View &view);
    void poll(uint32_t now);
    void library(uint32_t keys);
    void cores(uint32_t keys);
    void pause(uint32_t keys);
    void controls(uint32_t keys);
    void openSettings(Page parent);
    void settingsKeys(uint32_t keys);
    void brightnessKeys(uint32_t keys);
    void loadBrightness();
    void clockKeys(uint32_t keys);
    void countRoms();
    bool selectedRom();
    bool chooseCore();
    void launch();
    void resume();
    void finish(Exit exit, bool preserve = true);
    void leave(Exit exit);
    void gameFrame(uint32_t now);
    void openControls(Page parent);
    void loadBindings();
    void persistBindings(const Bindings &newBindings);
    const char *save(RgSaveKind kind, bool quiet);
    const char *restore(RgSaveKind kind, bool quiet);

    NorFlash nor_;
    BlockDevice disk_{nor_};
    Filesystem fs_{disk_};
    Video video_;
    FrameClock clock_;
    BatteryMonitor battery_;
    Scaler scaler_;
    Input input_;
    Rom rom_;
    RgHostV3 services_{};
    Core core_;
    RgCoreHeader chosen_{};
    Bindings bindings_{};
    char romName_[256]{}, romPath_[PathSize]{}, corePath_[PathSize]{};
    Page page_ = Page::Library, menuParent_ = Page::Pause;
    unsigned choice_ = 0, firstVisible_ = 0, romCount_ = 0, pauseChoice_ = 0;
    unsigned settingChoice_ = 0, clockChoice_ = 0;
    unsigned action_ = 0, actionCount_ = 0, actions_[RG_ACTION_COUNT]{}, mapFirst_ = 0;
    uint8_t proposed_ = RG_UNBOUND;
    uint8_t brightness_ = 65, brightnessPreview_ = 65;
    const char *notice_ = "";
    uint32_t autosave_ = 0, resumeKeys_ = 0, gameEdges_ = 0;
    bool ready_ = false, redraw_ = true, releasedToResume_ = true;
    Exit pendingExit_ = Exit::None;
};

void App::countRoms() {
    romCount_ = 0;
    if (!ready_) return;
    Directory list(RomsPath);
    while (list.next())
        if (romEntry(list.name())) ++romCount_;
    if (list.result() != FR_OK) notice_ = "Cannot read the ROM directory.";
    if (choice_ >= romCount_) choice_ = romCount_ ? romCount_ - 1 : 0;
    firstVisible_ = choice_ < View::Rows ? 0 : choice_ - View::Rows + 1;
}

bool App::selectedRom() {
    if (!ready_ || !romCount_) return false;
    Directory list(RomsPath);
    unsigned index = 0;
    while (list.next()) {
        if (!romEntry(list.name()) || index++ != choice_) continue;
        if (!filePath(romPath_, RomsPath, list.name())) break;
        std::strncpy(romName_, list.name(), sizeof(romName_) - 1);
        return true;
    }
    notice_ = "ROM unavailable. Check the disk.";
    return false;
}

void App::libraryView(View &view) {
    char sub[48];
    if (romCount_) std::snprintf(sub, sizeof(sub), "%u / %u   ROM LIBRARY", choice_ + 1, romCount_);
    else std::strcpy(sub, "ROM LIBRARY");
    view.start("Retro-Go", sub);
    if (!ready_) view.message("Storage unavailable", "Check the FAT disk in OpenOSC Recovery.");
    else if (!romCount_) view.message("No games found", "Copy ROMs to /openosc/games/roms.");
    else {
        Directory list(RomsPath);
        unsigned index = 0;
        while (index < firstVisible_ + View::Rows && list.next()) {
            if (!romEntry(list.name())) continue;
            const unsigned i = index++;
            if (i < firstVisible_) continue;
            view.row(i - firstVisible_, list.name(), "", i == choice_);
        }
    }
    view.footer("MENU Play  F2 Settings  F3 Controls  F4 OpenOSC", notice_);
}

void App::settingsView(View &view) {
    view.start("Settings", "");
    char level[8];
    std::snprintf(level, sizeof(level), "%u%%", unsigned(brightness_));
    view.row(Brightness, "Brightness", level, settingChoice_ == Brightness);
    const auto active = ClockControl::activeMode();
    char frequency[12] = "-- MHz";
    if (active < RunModeCount)
        std::snprintf(frequency, sizeof(frequency), "%u MHz", unsigned(RunModes[active].coreHz / 1000000u));
    view.row(CpuClock, "CPU clock", frequency, settingChoice_ == CpuClock);
    view.footer("MENU Select   F2 Back", notice_);
}

void App::brightnessView(View &view) {
    view.start("Brightness", "");
    char level[8];
    std::snprintf(level, sizeof(level), "%u%%", unsigned(brightnessPreview_));
    view.row(0, "Level", level, true);
    view.footer("LEFT/RIGHT Adjust   MENU Save   F2 Cancel", notice_);
}

void App::clockView(View &view) {
    view.start("CPU clock", "UNTIL RESTART");
    const auto active = ClockControl::activeMode();
    for (unsigned i = 0; i < CpuSpeedCount; ++i) {
        char frequency[32];
        const unsigned speed = CpuSpeeds[i];
        std::snprintf(frequency, sizeof frequency, "%u MHz%s", speed, speed > 280 ? " (overclock)" : "");
        const bool running = clockMode(speed) == active;
        view.row(i, frequency, running ? "Active" : "", i == clockChoice_);
    }
    view.footer("UP/DOWN Choose   MENU Apply   F2 Back", notice_);
}

void App::controlsView(View &view) {
    view.start("Button mapping", chosen_.id);
    for (unsigned i = mapFirst_; i < actionCount_ && i < mapFirst_ + View::Rows; ++i) {
        const auto action = actions_[i];
        const unsigned key = bindings_[action];
        view.row(i - mapFirst_, ActionNames[action], assignable(key) ? KeyNames[key] : "Unbound",
                 action_ == i);
    }
    view.footer("MENU Change  F2 Back  F3 Clear  F4 Defaults", notice_);
}

void App::render() {
    char charge[24] = "BAT --%  --.-- V";
    const auto &battery = battery_.state();
    if (battery.measured) {
        const unsigned centivolts = (battery.millivolts + 5u) / 10u;
        std::snprintf(charge, sizeof(charge), "BAT %u%%  %u.%02u V", unsigned(battery.percent),
                      centivolts / 100, centivolts % 100);
    }
    View view{Canvas(video_.back()), charge};
    switch (page_) {
    case Page::Library: libraryView(view); break;
    case Page::Cores:
        view.start("Choose emulator", romName_);
        view.row(0, chosen_.id, "", true);
        view.footer("UP/DOWN Core  MENU Play  F2 Back  F3 Controls", notice_);
        break;
    case Page::Pause:
        view.start("Paused", romName_);
        for (unsigned i = 0; i < PauseCount; ++i) view.row(i, PauseLabels[i], "", i == pauseChoice_);
        if (pendingExit_ == Exit::PowerOff)
            view.footer("MENU Power off without save   F2 Cancel", notice_);
        else if (pendingExit_ != Exit::None)
            view.footer("MENU Exit without save   F2 Cancel", notice_);
        else
            view.footer("MENU Select    F2 Resume", notice_);
        break;
    case Page::Controls: controlsView(view); break;
    case Page::Settings: settingsView(view); break;
    case Page::Brightness: brightnessView(view); break;
    case Page::CpuClock: clockView(view); break;
    case Page::Capture:
        view.start("Change binding", ActionNames[actions_[action_]]);
        view.message("Press the new physical key");
        view.footer("MENU Keep current binding");
        break;
    case Page::Confirm: {
        view.start("Confirm binding", ActionNames[actions_[action_]]);
        const auto previous = std::find(bindings_.begin(), bindings_.end(), proposed_);
        const unsigned other = previous - bindings_.begin();
        const char *hint = other < bindings_.size() && other != actions_[action_]
                         ? "Swaps the two assignments." : "";
        view.message(KeyNames[proposed_], hint);
        view.footer("MENU Confirm    F2 Cancel");
        break;
    }
    case Page::Playing: return;
    }
    video_.present();
    redraw_ = false;
}

bool App::chooseCore() {
    if (!selectedRom()) return false;
    const auto result = findCore(extensionOf(romName_), chosen_, corePath_);
    if (result != ModuleStatus::Ok) notice_ = describe(result);
    return result == ModuleStatus::Ok;
}

void App::loadBindings() {
    std::copy_n(chosen_.defaults, bindings_.size(), bindings_.begin());
    Bindings persisted{};
    uint32_t bytes = 0;
    auto result = SavePair(ControlsPath, chosen_.id, 0, "bind").load(persisted.data(), persisted.size(), bytes);
    if (result == SaveStatus::Ok && bytes == persisted.size() && validBindings(persisted, chosen_.action_mask))
        bindings_ = persisted;
    else if (result != SaveStatus::Empty)
        notice_ = "Cannot load mapping. Defaults selected.";
}

void App::persistBindings(const Bindings &bindings) {
    const auto result = SavePair(ControlsPath, chosen_.id, 0, "bind").write(bindings.data(), bindings.size());
    if (result == SaveStatus::Ok) {
        bindings_ = bindings;
        notice_ = "Mapping saved.";
    } else {
        notice_ = describe(result);
    }
}

void App::openControls(Page parent) {
    menuParent_ = parent;
    page_ = Page::Controls;
    action_ = actionCount_ = mapFirst_ = 0;
    for (unsigned i = 0; i < RG_ACTION_COUNT; ++i)
        if (chosen_.action_mask & (1u << i)) actions_[actionCount_++] = i;
}

const char *App::save(RgSaveKind kind, bool quiet) {
    if (!core_.snapshots()) return quiet ? nullptr : "Core does not support saving.";
    const uint32_t bytes = core_.save(kind, snapshot, sizeof(snapshot));
    if (!bytes)
        return quiet ? nullptr : kind == RG_SRAM ? "This cartridge has no battery save." :
                                                    "Core does not support save states.";
    if (bytes > sizeof(snapshot)) return "Save exceeds runtime memory limit.";
    const auto result = SavePair(SavesPath, core_.info().id, rom_.identity(),
                                 kind == RG_STATE ? "state" : "sram").write(snapshot, bytes);
    return result == SaveStatus::Ok ? nullptr : describe(result);
}

const char *App::restore(RgSaveKind kind, bool quiet) {
    if (!core_.snapshots()) return quiet ? nullptr : "Core does not support loading.";
    uint32_t bytes = 0;
    const auto result = SavePair(SavesPath, core_.info().id, rom_.identity(),
                                 kind == RG_STATE ? "state" : "sram").load(snapshot, sizeof(snapshot), bytes);
    if (result == SaveStatus::Empty && quiet) return nullptr;
    if (result != SaveStatus::Ok) return describe(result);
    const auto loaded = core_.restore(kind, snapshot, bytes);
    return loaded == RG_OK ? nullptr : describe(loaded);
}

void App::launch() {
    profile::loadStart();
    const auto result = core_.load(corePath_);
    if (result != ModuleStatus::Ok) {
        notice_ = describe(result);
        return;
    }
    chosen_ = core_.info();
    if (!rom_.open(romPath_)) {
        notice_ = "Storage error while opening ROM.";
        core_.unload();
        return;
    }
    services_ = {rom_.services(chosen_), nullptr, 0};
    if (chosen_.abi >= 3) {
        services_.workspace = core_.snapshots() ? cartridgeWorkspace : snapshot;
        services_.workspace_bytes = core_.snapshots() ? sizeof(cartridgeWorkspace) : sizeof(snapshot);
    }
    const auto started = core_.open(services_, rom_.size());
    core_.setVideoMode(rom_.finishOpen());
    if (started != RG_OK) {
        notice_ = describe(started);
        finish(Exit::Close, false);
        return;
    }
    if (const auto message = restore(RG_SRAM, true)) notice_ = message;
    loadBindings();
    autosave_ = Board::milliseconds();
    const auto &mode = core_.videoMode();
    scaler_.configure(mode.width, mode.height);
    releasedToResume_ = true;
    resume();
}

void App::resume() {
    resumeKeys_ = input_.held();
    gameEdges_ = 0;
    page_ = Page::Playing;
    profile::resumed();
    const auto &mode = core_.videoMode();
    clock_.begin(mode.period_num, mode.period_den);
}

void App::finish(Exit exit, bool preserve) {
    redraw_ = true;
    if (preserve) {
        if (const auto message = save(RG_SRAM, true)) {
            notice_ = message;
            pendingExit_ = exit;
            page_ = Page::Pause;
            return;
        }
    }
    pendingExit_ = Exit::None;
    core_.unload();
    rom_.close();
    if (exit != Exit::Close) leave(exit);
    page_ = Page::Library;
    countRoms();
}

void App::leave(Exit exit) {
    if (!fs_.unmount()) {
        notice_ = "Storage sync failed. Retry before leaving.";
        return;
    }
    ready_ = false;
    if (!nor_.restoreLegacyAddressing()) {
        notice_ = "Cannot reset SPI flash address mode.";
        return;
    }
    if (exit == Exit::PowerOff) PowerControl::powerOff();
    // A library exit uses F4. Hand the boot selector a released key.
    View view{Canvas(video_.back())};
    view.start("Back to OpenOSC", "");
    view.message("Release F4 to restart.");
    video_.present();
    unsigned released = 0;
    while (released < 20) {
        released = (Keypad::read() & Keypad::F4) ? 0 : released + 1;
        Watchdog::heartbeat();
        Board::delay(1);
    }
    SystemReset::now();
}

void App::library(uint32_t keys) {
    if (keys & Keypad::F4) {
        finish(Exit::Restart);
        return;
    }
    step(choice_, romCount_, keys);
    if (choice_ < firstVisible_) firstVisible_ = choice_;
    if (choice_ >= firstVisible_ + View::Rows) firstVisible_ = choice_ - View::Rows + 1;
    if ((keys & Accept) && chooseCore()) {
        RgCoreHeader second{};
        char path[PathSize];
        if (findCore(extensionOf(romName_), second, path, chosen_.id) == ModuleStatus::Ok)
            page_ = Page::Cores;
        else
            launch();
    } else if ((keys & Keypad::F3) && chooseCore()) {
        loadBindings();
        openControls(Page::Library);
    } else if (keys & Keypad::F2) {
        openSettings(Page::Library);
    }
}

void App::cores(uint32_t keys) {
    if (keys & Keypad::F2) {
        page_ = Page::Library;
    } else if (keys & Accept) {
        launch();
    } else if (keys & Keypad::F3) {
        loadBindings();
        openControls(Page::Cores);
    } else if (keys & (Keypad::Up | Keypad::Down)) {
        RgCoreHeader next{};
        char path[PathSize];
        const auto *extension = extensionOf(romName_);
        const bool reverse = keys & Keypad::Up;
        auto result = findCore(extension, next, path, chosen_.id, reverse);
        if (result == ModuleStatus::Missing) result = findCore(extension, next, path, nullptr, reverse);
        if (result == ModuleStatus::Ok) {
            chosen_ = next;
            std::strcpy(corePath_, path);
        } else {
            notice_ = describe(result);
        }
    }
}

void App::pause(uint32_t keys) {
    if (!releasedToResume_) return;
    if (pendingExit_ != Exit::None) {
        if (keys & Accept) finish(pendingExit_, false);
        else if (keys & Keypad::F2) {
            pendingExit_ = Exit::None;
            notice_ = "";
        }
        return;
    }
    step(pauseChoice_, PauseCount, keys);
    if (keys & Keypad::F2) {
        resume();
        return;
    }
    if (!(keys & Accept)) return;
    switch (pauseChoice_) {
    case Resume: resume(); break;
    case Mapping: openControls(Page::Pause); break;
    case Settings: openSettings(Page::Pause); break;
    case SaveCart: notice_ = save(RG_SRAM, false); if (!notice_) notice_ = "Cartridge saved."; break;
    case SaveState: notice_ = save(RG_STATE, false); if (!notice_) notice_ = "State saved."; break;
    case LoadState: notice_ = restore(RG_STATE, false); if (!notice_) notice_ = "State loaded."; break;
    case Close: finish(Exit::Close); break;
    case Restart: finish(Exit::Restart); break;
    }
}

void App::openSettings(Page parent) {
    menuParent_ = parent;
    settingChoice_ = Brightness;
    page_ = Page::Settings;
}

void App::settingsKeys(uint32_t keys) {
    if (keys & Keypad::F2) {
        page_ = menuParent_;
        return;
    }
    step(settingChoice_, SettingCount, keys);
    if (!(keys & Accept)) return;
    if (settingChoice_ == Brightness) {
        brightnessPreview_ = brightness_;
        page_ = Page::Brightness;
    } else {
        const auto active = ClockControl::activeMode();
        clockChoice_ = 0;
        for (unsigned i = 0; i < CpuSpeedCount; ++i)
            if (clockMode(CpuSpeeds[i]) == active) clockChoice_ = i;
        page_ = Page::CpuClock;
    }
}

void App::loadBrightness() {
    if (ready_) {
        uint8_t saved = 0;
        uint32_t bytes = 0;
        if (brightnessRecord().load(&saved, sizeof(saved), bytes) == SaveStatus::Ok &&
            bytes == sizeof(saved) && saved >= BrightnessMin && saved <= BrightnessMax)
            brightness_ = saved;
    }
    PowerControl::brightness(brightness_);
}

void App::brightnessKeys(uint32_t keys) {
    if (keys & Keypad::F2) {
        PowerControl::brightness(brightness_);
        page_ = Page::Settings;
        return;
    }
    const int direction = bool(keys & Keypad::Right) - bool(keys & Keypad::Left);
    if (direction) {
        brightnessPreview_ = std::clamp(brightnessPreview_ + direction * BrightnessStep,
                                        BrightnessMin, BrightnessMax);
        PowerControl::brightness(brightnessPreview_);
    }
    if (!(keys & Accept)) return;
    if (brightnessPreview_ != brightness_) {
        const auto result = brightnessRecord().write(&brightnessPreview_, sizeof(brightnessPreview_));
        if (result != SaveStatus::Ok) {
            notice_ = describe(result);
            return;
        }
        brightness_ = brightnessPreview_;
        notice_ = "Brightness saved.";
    }
    page_ = Page::Settings;
}

void App::clockKeys(uint32_t keys) {
    if (keys & Keypad::F2) {
        page_ = Page::Settings;
        notice_ = "";
        return;
    }
    step(clockChoice_, CpuSpeedCount, keys);
    if (!(keys & Accept)) return;
    const auto previous = ClockControl::activeMode();
    const auto wanted = clockMode(CpuSpeeds[clockChoice_]);
    if (wanted == previous) return;

    // The emulator is paused and the storage bus uses HSI. Retune TIM2 once
    // PLL1 is stable, then rebase the game's frame deadline when it resumes.
    clock_.stop();
    const auto apply = [this](unsigned mode) {
        if (!ClockControl::selectPerformance(mode)) return false;
        PowerControl::retime();
        return clock_.initialize();
    };
    if (apply(wanted)) notice_ = "CPU clock applied.";
    else if (apply(previous)) notice_ = "Clock change failed. Previous speed restored.";
    else if (apply(clockMode(200))) notice_ = "Clock change failed. 200 MHz restored.";
    else SystemReset::now();
}

void App::controls(uint32_t keys) {
    if (page_ == Page::Capture) {
        if (keys & Keypad::Menu) page_ = Page::Controls;
        else
            for (unsigned key = 0; key < RG_KEY_COUNT; ++key)
                if (assignable(key) && (keys & (1u << key))) {
                    proposed_ = key;
                    page_ = Page::Confirm;
                    break;
                }
        return;
    }
    if (page_ == Page::Confirm) {
        if (keys & Accept) {
            auto updated = bindings_;
            assign(updated, actions_[action_], proposed_);
            persistBindings(updated);
            page_ = Page::Controls;
        } else if (keys & Keypad::F2) {
            page_ = Page::Controls;
        }
        return;
    }
    if (keys & Keypad::F2) {
        page_ = menuParent_;
        return;
    }
    step(action_, actionCount_, keys);
    if (action_ < mapFirst_) mapFirst_ = action_;
    if (action_ >= mapFirst_ + View::Rows) mapFirst_ = action_ - View::Rows + 1;
    if (keys & Accept) page_ = Page::Capture;
    else if (keys & Keypad::F3) {
        auto updated = bindings_;
        assign(updated, actions_[action_], RG_UNBOUND);
        persistBindings(updated);
    } else if (keys & Keypad::F4) {
        Bindings defaults{};
        std::copy_n(chosen_.defaults, defaults.size(), defaults.begin());
        persistBindings(defaults);
    }
}

void App::gameFrame(uint32_t now) {
    if (input_.menuLong(now) && !(resumeKeys_ & Keypad::Menu)) {
        page_ = Page::Pause;
        pauseChoice_ = Resume;
        releasedToResume_ = false;
        redraw_ = true;
        return;
    }
    const auto edges = input_.pressed();
    if (edges & ~Keypad::Menu) profile::button();
    gameEdges_ |= edges;
    if (!clock_.due()) return;
    const uint16_t *pixels = nullptr;
    const uint32_t start = profile::counter();
    const auto status = core_.run(translate(bindings_, (input_.held() | gameEdges_) & ~resumeKeys_), &pixels);
    gameEdges_ = 0;
    if (status != RG_OK) {
        notice_ = describe(status);
        finish(Exit::Close, false);
        redraw_ = true;
        return;
    }
    const uint32_t emulated = profile::counter();
    auto *back = video_.back();
    scaler_.draw(back, pixels);
    video_.present();
    const uint32_t presented = profile::counter();
    profile::frame(start, emulated, presented);
    if (profile::needsCopyBenchmark())
        profile::copyBenchmark(video_.back(), pixels, core_.videoMode().width, core_.videoMode().height);
    clock_.advance();
    if (now - autosave_ >= 90000) {
        autosave_ = now;
        if (const auto message = save(RG_SRAM, true)) {
            notice_ = message;
            page_ = Page::Pause;
            pauseChoice_ = SaveCart;
            redraw_ = true;
        }
    }
}

void App::poll(uint32_t now) {
    input_.poll(Keypad::read(), now);
    if (input_.powerOffRequested(now)) {
        finish(Exit::PowerOff);
        return;
    }
    resumeKeys_ &= input_.held();
    if (page_ == Page::Playing) {
        gameFrame(now);
        return;
    }
    const auto keys = input_.pressed();
    if (page_ == Page::Pause && !(input_.held() & Keypad::Menu)) releasedToResume_ = true;
    if (!keys) return;
    if (pendingExit_ == Exit::None) notice_ = "";
    redraw_ = true;
    switch (page_) {
    case Page::Library: library(keys); break;
    case Page::Cores: cores(keys); break;
    case Page::Pause: pause(keys); break;
    case Page::Controls: case Page::Capture: case Page::Confirm: controls(keys); break;
    case Page::Settings: settingsKeys(keys); break;
    case Page::Brightness: brightnessKeys(keys); break;
    case Page::CpuClock: clockKeys(keys); break;
    case Page::Playing: break;
    }
}

void App::run() {
    profile::stackStart();
    if (!Board::initialize() || !clock_.initialize()) SystemReset::now();
    UsbCore::powerOff();
    executableRam();
    if (!video_.initialize()) SystemReset::now();
    battery_.initialize();
    ready_ = nor_.initialize() && disk_.initialize() && fs_.mount(false) && prepareMedia();
    loadBrightness();
    countRoms();
    if (ready_ && fs_.refreshFree() && fs_.freeBytes() < 16384) notice_ = "Disk is low on free space.";
    render();
    profile::libraryReady();
    while (true) {
        const uint32_t now = Board::milliseconds();
        Watchdog::heartbeat();
        const auto previousVoltage = battery_.state().millivolts;
        const bool previouslyMeasured = battery_.state().measured;
        battery_.poll(now);
        if (page_ != Page::Playing &&
            (previousVoltage != battery_.state().millivolts || previouslyMeasured != battery_.state().measured))
            redraw_ = true;
        poll(now);
        if (redraw_ && page_ != Page::Playing) render();
        Board::delay(1);
    }
}
}
}

int main() {
    static retrogo::App app;
    app.run();
}
