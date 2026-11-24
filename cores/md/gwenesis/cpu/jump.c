#include "jump.h"

#if defined(__ARM_ARCH_7EM__)
__attribute__((naked))
int md_setjmp(MdJumpBuffer *buffer)
{
    __asm volatile(
        "stmia r0!, {r4-r11}\n"
        "mov r1, sp\n"
        "str r1, [r0], #4\n"
        "mov r1, lr\n"
        "str r1, [r0], #4\n"
        "vstmia r0!, {d8-d15}\n"
        "movs r0, #0\n"
        "bx lr\n");
}

__attribute__((naked))
void md_longjmp(MdJumpBuffer *buffer, int value)
{
    __asm volatile(
        "ldmia r0!, {r4-r11}\n"
        "ldr r2, [r0], #4\n"
        "mov sp, r2\n"
        "ldr r2, [r0], #4\n"
        "vldmia r0!, {d8-d15}\n"
        "mov r0, r1\n"
        "bx r2\n");
}
#endif
