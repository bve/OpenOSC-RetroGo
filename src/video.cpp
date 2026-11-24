#include "retrogo/video.hpp"
#include "openosc/display/display.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/interrupts.hpp"

namespace retrogo {
namespace {
alignas(64) uint16_t frames[2][RG_FRAME_PIXELS] __attribute__((section(".framebuffer"), used));
}

bool Video::initialize() {
    openosc::Display display;
    return display.initialize(false);
}

uint16_t *Video::back() {
    const auto start = openosc::Board::milliseconds();
    while (LTDC->SRCR & LTDC_SRCR_VBR) {
        openosc::Board::service();
        if (openosc::Board::milliseconds() - start > 30) {
            if (!openosc::Display::restart(frames[front_])) return frames[front_];
            break;
        }
    }
    return frames[front_ ^ 1];
}

void Video::present() {
    front_ ^= 1;
    __DSB();
    LTDC_Layer1->CFBAR = reinterpret_cast<uintptr_t>(frames[front_]);
    LTDC->SRCR = LTDC_SRCR_VBR;
}

bool FrameClock::initialize() {
    const uint32_t source = openosc::TimerClock::apb1();
    if (source < 1000000 || source % 1000000) return false;
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM2_FORCE_RESET();
    __HAL_RCC_TIM2_RELEASE_RESET();
    TIM2->PSC = source / 1000000 - 1;
    TIM2->ARR = 0xffffffff;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_CEN;
    return true;
}

void FrameClock::stop() { TIM2->CR1 &= ~TIM_CR1_CEN; }
uint32_t FrameClock::now() { return TIM2->CNT; }
bool FrameClock::due() const { return int32_t(now() - deadline_) >= 0; }

void FrameClock::begin(uint32_t numerator, uint32_t denominator) {
    whole_ = numerator / denominator;
    fraction_ = numerator % denominator;
    denominator_ = denominator;
    deadline_ = now();
    carry_ = 0;
}

void FrameClock::advance() {
    deadline_ += whole_;
    carry_ += fraction_;
    if (carry_ >= denominator_) {
        carry_ -= denominator_;
        ++deadline_;
    }
    const uint32_t current = now();
    if (int32_t(current - deadline_) >= int32_t(whole_ * 2)) deadline_ = current;
}
}
