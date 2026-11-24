#pragma once
#include "retrogo/core.h"
#include <array>

namespace retrogo {
using Bindings = std::array<uint8_t, RG_ACTION_COUNT>;
inline constexpr const char *ActionNames[] = {
    "Up", "Down", "Left", "Right", "A", "B", "Start", "Select", "X", "Y", "L", "R"
};
inline constexpr const char *KeyNames[] = {
    "F1", "F2", "F3", "F4", "MENU", "Up", "Down", "Left", "Right", "MODE", "HOLD", "AUTO"
};
static_assert(std::size(KeyNames) == RG_KEY_COUNT);

constexpr bool assignable(unsigned key) { return key < RG_KEY_COUNT && key != RG_MENU_KEY; }
bool validBindings(const Bindings &bindings, uint16_t mask);
void assign(Bindings &bindings, unsigned action, uint8_t key);
uint32_t translate(const Bindings &bindings, uint32_t physical);

class Input {
public:
    static constexpr uint32_t Power = 1u << RG_KEY_COUNT;
    static constexpr uint32_t PowerHoldMs = 2000;
    void poll(uint32_t raw, uint32_t now);
    uint32_t held() const { return stable_ & ~Power; }
    uint32_t pressed() const { return pressed_; }
    bool menuLong(uint32_t now) const {
        return (stable_ & (1u << RG_MENU_KEY)) && now - menuSince_ >= 500;
    }
    bool powerOffRequested(uint32_t now);
private:
    static constexpr uint32_t DebounceMs = 6;
    uint32_t stable_ = 0, raw_ = 0, pressed_ = 0, menuSince_ = 0, powerSince_ = 0;
    uint32_t changed_[RG_KEY_COUNT + 1]{};
    bool suppressF4_ = true;
    bool powerArmed_ = false;
};
}
