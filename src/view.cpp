#include "retrogo/view.hpp"
#include <cstring>

namespace retrogo {
namespace {
constexpr uint16_t Background = 0x0883, Panel = 0x10e6, Highlight = 0x1b8d;
constexpr uint16_t Text = 0xe77d, Muted = 0x9d96, Accent = 0x5f3a, Alert = 0xfe90;

void label(Canvas &canvas, unsigned x, unsigned y, const char *value, unsigned capacity,
           uint16_t color) {
    char clipped[53];
    unsigned bytes = 0, glyphs = 0;
    for (; value[bytes] && bytes < sizeof(clipped) - 4; ++bytes) {
        const auto ch = static_cast<uint8_t>(value[bytes]);
        if ((ch & 0xc0) != 0x80 && glyphs++ >= capacity - 3) break;
        clipped[bytes] = ch;
    }
    if (value[bytes]) {
        clipped[bytes++] = '.';
        clipped[bytes++] = '.';
        clipped[bytes++] = '.';
    }
    clipped[bytes] = 0;
    canvas.text(x, y, clipped, color);
}
}

void View::start(const char *title, const char *subtitle) {
    canvas_.clear(Background);
    canvas_.rect(0, 0, Canvas::Width, 40, Panel);
    canvas_.rect(10, 11, 3, 14, Accent);
    canvas_.text(20, 11, title, Text, 2);
    canvas_.text(Canvas::Width - 12 - 6 * std::strlen(battery_), 15, battery_, Accent);
    label(canvas_, 20, 30, subtitle, 50, Muted);
}

void View::row(unsigned index, const char *left, const char *right, bool selected) {
    const unsigned y = 45 + index * 20;
    if (selected) {
        canvas_.rect(8, y, 304, 18, Highlight);
        canvas_.rect(8, y, 3, 18, Accent);
    } else if (!(index & 1)) {
        canvas_.rect(8, y, 304, 18, Panel);
    }
    label(canvas_, 16, y + 6, left, *right ? 35 : 48, Text);
    if (*right) label(canvas_, 230, y + 6, right, 12, selected ? Accent : Muted);
}

void View::message(const char *main, const char *detail) {
    label(canvas_, 16, 81, main, 48, Text);
    label(canvas_, 16, 101, detail, 48, Muted);
}

void View::footer(const char *legend, const char *status) {
    canvas_.rect(0, 210, Canvas::Width, 30, Panel);
    label(canvas_, 12, 215, legend, 50, Muted);
    if (*status) label(canvas_, 12, 228, status, 50, Alert);
}
}
