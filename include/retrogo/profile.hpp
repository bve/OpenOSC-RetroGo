#pragma once
#include <cstdint>

namespace retrogo::profile {
#ifdef RETROGO_PROFILE
void stackStart();
void libraryReady();
void loadStart();
uint32_t counter();
void resumed();
void button();
bool needsCopyBenchmark();
void frame(uint32_t start, uint32_t emulated, uint32_t presented);
void copyBenchmark(uint16_t *back, const uint16_t *source, unsigned width, unsigned height);
#else
inline void stackStart() {}
inline void libraryReady() {}
inline void loadStart() {}
inline uint32_t counter() { return 0; }
inline void resumed() {}
inline void button() {}
inline bool needsCopyBenchmark() { return false; }
inline void frame(uint32_t, uint32_t, uint32_t) {}
inline void copyBenchmark(uint16_t *, const uint16_t *, unsigned, unsigned) {}
#endif
}
