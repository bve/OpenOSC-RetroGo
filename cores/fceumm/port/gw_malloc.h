/* OpenOSC-RetroGo: emulator allocations come from a per-game arena. */
#ifndef FCEUMM_PORT_GW_MALLOC_H
#define FCEUMM_PORT_GW_MALLOC_H
#include <stddef.h>
void *itc_calloc(size_t count, size_t size);
void *ahb_calloc(size_t count, size_t size);
void *itc_malloc(size_t size);
void *ahb_malloc(size_t size);
#endif
