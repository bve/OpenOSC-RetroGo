#include "rtc.h"
#include "loader.h"

struct rtc rtc;

void rtc_update(void)
{
    const uint32_t now = cart_seconds();
    if (!(rtc.flags & 0x40)) {
        const uint64_t elapsed = (uint32_t)(now - rtc.epoch);
        const uint64_t total = elapsed + rtc.s + rtc.m * 60u + rtc.h * 3600u + rtc.d * 86400u;
        rtc.s = total % 60;
        rtc.m = (total / 60) % 60;
        rtc.h = (total / 3600) % 24;
        const uint32_t days = total / 86400;
        if (days >= 512) rtc.flags |= 0x80;
        rtc.d = days & 511;
    }
    rtc.epoch = now;
}

void rtc_latch(byte b)
{
    if ((rtc.latch ^ b) & b & 1) {
        rtc_update();
        rtc.regs[0] = rtc.s;
        rtc.regs[1] = rtc.m;
        rtc.regs[2] = rtc.h;
        rtc.regs[3] = rtc.d;
        rtc.regs[4] = (rtc.flags & 0xc0) | (rtc.d >> 8);
        rtc.regs[5] = rtc.regs[6] = rtc.regs[7] = 0xff;
    }
    rtc.latch = b;
}

void rtc_write(byte b)
{
    rtc_update();
    switch (rtc.sel) {
    case 8: rtc.s = b % 60; break;
    case 9: rtc.m = b % 60; break;
    case 10: rtc.h = b % 24; break;
    case 11: rtc.d = (rtc.d & 256) | b; break;
    case 12:
        rtc.flags = b & 0xc0;
        rtc.d = (rtc.d & 255) | ((b & 1) << 8);
        break;
    }
}
