#include "retrogo/profile.hpp"

#ifdef RETROGO_PROFILE
#include "retrogo/video.hpp"
#include "openosc/platform/board.hpp"
#include <cstring>

extern "C" {
extern uint32_t _ebss, _estack;
struct RetroGoMetrics {
    uint32_t firstLibraryUs, launchUs, frames, deadlineMisses;
    uint32_t emulationUs, maximumEmulationUs, renderUs, maximumRenderUs;
    uint32_t frameIntervalUs, minIntervalUs, maxIntervalUs;
    uint32_t buttons, lastButtonUs, maximumButtonUs, peakStackBytes;
    uint32_t cpuCopyUs, dmaCopyUs;
};
volatile RetroGoMetrics retrogo_metrics __attribute__((used));
}

namespace retrogo::profile {
namespace {
constexpr uint32_t StackWord = 0x52475354;
uint32_t launch, last, input, stackLimit;
bool buttonPending;

void maximum(volatile uint32_t &field, uint32_t value) {
    if (value > field) field = value;
}

void sampleStack() {
    const volatile uint32_t *unused = &_ebss;
    while (reinterpret_cast<uintptr_t>(unused) < stackLimit && *unused == StackWord) ++unused;
    maximum(retrogo_metrics.peakStackBytes, reinterpret_cast<uintptr_t>(&_estack) -
                                            reinterpret_cast<uintptr_t>(unused));
}
}

void stackStart() {
    // Keep a margin below the current stack for calls and an interrupt that
    // might land while the unused DTCM is filled.
    stackLimit = (__get_MSP() - 512) & ~3u;
    for (auto *word = static_cast<volatile uint32_t *>(&_ebss);
         reinterpret_cast<uintptr_t>(word) < stackLimit; ++word) *word = StackWord;
}

void libraryReady() { retrogo_metrics.firstLibraryUs = FrameClock::now(); }

void loadStart() {
    launch = FrameClock::now();
    last = 0;
    buttonPending = false;
    retrogo_metrics.frames = 0;
    retrogo_metrics.deadlineMisses = 0;
    retrogo_metrics.launchUs = 0;
    retrogo_metrics.emulationUs = retrogo_metrics.maximumEmulationUs = 0;
    retrogo_metrics.renderUs = retrogo_metrics.maximumRenderUs = 0;
    retrogo_metrics.frameIntervalUs = retrogo_metrics.minIntervalUs = retrogo_metrics.maxIntervalUs = 0;
    retrogo_metrics.buttons = retrogo_metrics.lastButtonUs = retrogo_metrics.maximumButtonUs = 0;
    retrogo_metrics.cpuCopyUs = retrogo_metrics.dmaCopyUs = 0;
}

uint32_t counter() { return FrameClock::now(); }
void resumed() { last = 0; }
bool needsCopyBenchmark() { return retrogo_metrics.cpuCopyUs == 0; }

void button() {
    if (!buttonPending) input = FrameClock::now();
    buttonPending = true;
}

void frame(uint32_t start, uint32_t emulated, uint32_t presented) {
    auto &m = retrogo_metrics;
    if (!m.frames) m.launchUs = presented - launch;
    if (last) {
        const uint32_t interval = start - last;
        m.frameIntervalUs = interval;
        if (!m.minIntervalUs || interval < m.minIntervalUs) m.minIntervalUs = interval;
        maximum(m.maxIntervalUs, interval);
    }
    last = start;
    m.emulationUs = emulated - start;
    m.renderUs = presented - emulated;
    maximum(m.maximumEmulationUs, m.emulationUs);
    maximum(m.maximumRenderUs, m.renderUs);
    if (presented - start > 16743) ++m.deadlineMisses;
    if (buttonPending) {
        ++m.buttons;
        m.lastButtonUs = presented - input;
        maximum(m.maximumButtonUs, m.lastButtonUs);
        buttonPending = false;
    }
    if (++m.frames % 120 == 0) sampleStack();
}

void copyBenchmark(uint16_t *back, const uint16_t *source, unsigned width, unsigned height) {
    uint32_t start = FrameClock::now();
    for (unsigned y = 0; y < height; ++y)
        std::memcpy(back + 320 * y, source + width * y, width * sizeof(uint16_t));
    __DSB();
    retrogo_metrics.cpuCopyUs = FrameClock::now() - start;

    // A native-size RGB565 transfer. DMA2D cannot apply this port's scaling.
    __HAL_RCC_DMA2D_CLK_ENABLE();
    __HAL_RCC_DMA2D_FORCE_RESET();
    __HAL_RCC_DMA2D_RELEASE_RESET();
    start = FrameClock::now();
    const uintptr_t address = reinterpret_cast<uintptr_t>(source);
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t *>(address & ~31u),
                          width * height * sizeof(uint16_t) + (address & 31u));
    DMA2D->FGMAR = address;
    DMA2D->FGOR = 0;
    DMA2D->FGPFCCR = 2;
    DMA2D->OPFCCR = 2;
    DMA2D->OMAR = reinterpret_cast<uintptr_t>(back);
    DMA2D->OOR = 320 - width;
    DMA2D->NLR = (width << 16) | height;
    DMA2D->CR = DMA2D_CR_START;
    while ((DMA2D->CR & DMA2D_CR_START) && FrameClock::now() - start < 10000) {}
    if (DMA2D->ISR & DMA2D_ISR_TCIF) retrogo_metrics.dmaCopyUs = FrameClock::now() - start;
    __HAL_RCC_DMA2D_FORCE_RESET();
    __HAL_RCC_DMA2D_RELEASE_RESET();
    __HAL_RCC_DMA2D_CLK_DISABLE();
}
}
#endif
