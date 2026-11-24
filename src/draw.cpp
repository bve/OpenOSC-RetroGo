#include "retrogo/video.hpp"
#include "openosc/font.hpp"
#include <algorithm>
#include <cstring>

namespace retrogo {
void Canvas::clear(uint16_t color) {
    std::fill_n(pixels_, RG_FRAME_PIXELS, color);
}

void Canvas::rect(unsigned x, unsigned y, unsigned width, unsigned height, uint16_t color) {
    if (x >= Width || y >= Height) return;
    width = std::min(width, Width - x);
    height = std::min(height, Height - y);
    for (unsigned row = y; row < y + height; ++row)
        std::fill_n(pixels_ + row * Width + x, width, color);
}

void Canvas::text(unsigned x, unsigned y, const char *message, uint16_t color, unsigned scale) {
    using namespace openosc::font;
    static constexpr uint8_t Percent[] = {0x23, 0x13, 0x08, 0x64, 0x62};
    for (; *message && x < Width; ++message, x += 6 * scale) {
        auto ch = static_cast<unsigned char>(*message);
        if ((ch & 0xc0) == 0x80) {
            x -= 6 * scale;
            continue;
        }
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        if (ch >= 128) ch = '?';
        unsigned index = 0;
        while (Alphabet[index] && Alphabet[index] != ch) ++index;
        const uint8_t *glyph = ch == '%' ? Percent : Alphabet[index] ? Glyphs[index] : nullptr;
        if (!glyph) continue;
        for (unsigned col = 0; col < 5; ++col)
            for (unsigned row = 0; row < 7; ++row)
                if (glyph[col] & (1u << row))
                    rect(x + col * scale, y + row * scale, scale, scale, color);
    }
}

void Scaler::configure(unsigned width, unsigned height) {
    width_ = width;
    height_ = height;
    outWidth_ = std::min(Canvas::Width, Canvas::Height * width_ / height_);
    outHeight_ = std::min(Canvas::Height, Canvas::Width * height_ / width_);
    if (!outWidth_) outWidth_ = 1;
    if (!outHeight_) outHeight_ = 1;
    for (unsigned x = 0; x < outWidth_; ++x) columns_[x] = x * width_ / outWidth_;
    for (unsigned y = 0; y < outHeight_; ++y) rows_[y] = y * height_ / outHeight_;
}

void Scaler::draw(uint16_t *out, const uint16_t *image) const {
    if (!width_ || !height_) return;
    if (width_ == Canvas::Width && height_ == Canvas::Height) {
        std::memcpy(out, image, RG_FRAME_PIXELS * sizeof(*out));
        return;
    }
    const unsigned top = (Canvas::Height - outHeight_) / 2;
    const unsigned left = (Canvas::Width - outWidth_) / 2;
    std::fill_n(out, top * Canvas::Width, 0);
    for (unsigned y = 0; y < outHeight_; ++y) {
        auto *row = out + (y + top) * Canvas::Width;
        std::fill_n(row, left, 0);
        std::fill_n(row + left + outWidth_, Canvas::Width - left - outWidth_, 0);
        if (y && rows_[y] == rows_[y - 1]) {
            std::memcpy(row + left, row + left - Canvas::Width, outWidth_ * sizeof(uint16_t));
        } else {
            const auto *source = image + rows_[y] * width_;
            for (unsigned x = 0; x < outWidth_; ++x) row[left + x] = source[columns_[x]];
        }
    }
    std::fill_n(out + (top + outHeight_) * Canvas::Width,
                (Canvas::Height - top - outHeight_) * Canvas::Width, 0);
}
}
