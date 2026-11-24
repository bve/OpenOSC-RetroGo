#include "retro_go_image.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/handoff.hpp"
#include "openosc/platform/input.hpp"

namespace openosc {
void tryRetroGo() {
    if ((Keypad::read() & (Keypad::F2 | Keypad::F4)) != Keypad::F4) return;

    const auto *header = reinterpret_cast<const ImageHeader *>(RetroGoBase);
    const auto *payload = reinterpret_cast<const void *>(RetroGoVectors);
    if (!retroGoImageValid(*header, payload, RetroGoMaxPayload, Board::service)) return;

    // Recovery may have left its boot splash scanning in AXI SRAM.
    if (__HAL_RCC_LTDC_IS_CLK_ENABLED()) LTDC->GCR &= ~LTDC_GCR_LTDCEN;
    Handoff::jump(RetroGoVectors);
}
}
