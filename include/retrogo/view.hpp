#pragma once
#include "retrogo/video.hpp"
#include "retrogo/controls.hpp"

namespace retrogo {
class View {
public:
    explicit View(Canvas canvas, const char *battery = "") : canvas_(canvas), battery_(battery) {}
    void start(const char *title, const char *subtitle);
    void row(unsigned index, const char *left, const char *right, bool selected);
    void message(const char *main, const char *detail = "");
    void footer(const char *legend, const char *status = "");
    static constexpr unsigned Rows = 8;
private:
    Canvas canvas_;
    const char *battery_;
};
}
