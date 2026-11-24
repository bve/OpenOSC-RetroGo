#pragma once
#include "retrogo/core.h"

namespace retrogo {
class Canvas {
public:
    explicit Canvas(uint16_t *pixels) : pixels_(pixels) {}
    static constexpr unsigned Width = 320, Height = 240;
    void clear(uint16_t color);
    void rect(unsigned x, unsigned y, unsigned width, unsigned height, uint16_t color);
    void text(unsigned x, unsigned y, const char *message, uint16_t color, unsigned scale = 1);
private:
    uint16_t *pixels_;
};

class Scaler {
public:
    void configure(unsigned width, unsigned height);
    void draw(uint16_t *out, const uint16_t *image) const;
private:
    unsigned width_ = 0, height_ = 0, outWidth_ = 0, outHeight_ = 0;
    uint16_t columns_[Canvas::Width]{}, rows_[Canvas::Height]{};
};

class Video {
public:
    bool initialize();
    uint16_t *back();
    void present();
private:
    unsigned front_ = 0;
};

class FrameClock {
public:
    bool initialize();
    void stop();
    static uint32_t now();
    void begin(uint32_t numerator, uint32_t denominator);
    bool due() const;
    void advance();
private:
    uint32_t deadline_ = 0, whole_ = 0, fraction_ = 0, denominator_ = 1, carry_ = 0;
};
}
