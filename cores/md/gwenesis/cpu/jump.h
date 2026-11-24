#pragma once
#include <stdint.h>

#if defined(__ARM_ARCH_7EM__)
/* AAPCS32 callee-saved integer and floating-point registers, SP and LR. */
typedef struct {
    uint32_t saved[26];
} MdJumpBuffer __attribute__((aligned(8)));

int md_setjmp(MdJumpBuffer *buffer) __attribute__((returns_twice));
void md_longjmp(MdJumpBuffer *buffer, int value) __attribute__((noreturn));
#else
#include <setjmp.h>
typedef jmp_buf MdJumpBuffer;
#define md_setjmp(buffer) setjmp(*(buffer))
#define md_longjmp(buffer, value) longjmp(*(buffer), value)
#endif
