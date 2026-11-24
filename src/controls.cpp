#include "retrogo/controls.hpp"

namespace retrogo {
bool validBindings(const Bindings &bindings, uint16_t mask) {
    uint32_t occupied = 0;
    for (unsigned i = 0; i < bindings.size(); ++i) {
        const auto key = bindings[i];
        if (key == RG_UNBOUND) continue;
        if (!(mask & (1u << i)) || !assignable(key) || (occupied & (1u << key))) return false;
        occupied |= 1u << key;
    }
    return true;
}

void assign(Bindings &bindings, unsigned action, uint8_t key) {
    if (action >= bindings.size() || (!assignable(key) && key != RG_UNBOUND)) return;
    if (assignable(key)) {
        for (unsigned i = 0; i < bindings.size(); ++i)
            if (i != action && bindings[i] == key) bindings[i] = bindings[action];
    }
    bindings[action] = key;
}

uint32_t translate(const Bindings &bindings, uint32_t physical) {
    uint32_t virtualKeys = 0;
    for (unsigned i = 0; i < bindings.size(); ++i)
        if (assignable(bindings[i]) && (physical & (1u << bindings[i]))) virtualKeys |= 1u << i;
    return virtualKeys;
}

void Input::poll(uint32_t raw, uint32_t now) {
    constexpr uint32_t F4 = 1u << RG_F4_KEY, Menu = 1u << RG_MENU_KEY;
    const uint32_t changed = raw ^ raw_;
    const auto previous = stable_;
    raw_ = raw;
    for (unsigned i = 0; i < std::size(changed_); ++i) {
        const uint32_t bit = 1u << i;
        if (changed & bit) changed_[i] = now;
        if (now - changed_[i] >= DebounceMs) stable_ = (stable_ & ~bit) | (raw & bit);
    }
    const auto edges = stable_ & ~previous;
    pressed_ = edges & ~Power;
    if (suppressF4_) {
        pressed_ &= ~F4;
        if (!((raw | stable_) & F4) && now - changed_[RG_F4_KEY] >= DebounceMs) suppressF4_ = false;
        stable_ &= ~F4;
    }
    if (pressed_ & Menu) menuSince_ = now;
    if (edges & Power) powerSince_ = now;
    // Arm only after a release, including when the key is held through boot.
    if (!((raw | stable_) & Power) && now - changed_[RG_KEY_COUNT] >= DebounceMs) powerArmed_ = true;
}

bool Input::powerOffRequested(uint32_t now) {
    if (!powerArmed_ || !(raw_ & stable_ & Power) || now - powerSince_ < PowerHoldMs) return false;
    powerArmed_ = false;
    return true;
}
}
