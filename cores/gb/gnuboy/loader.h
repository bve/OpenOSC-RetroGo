#ifndef __LOADER_H__
#define __LOADER_H__
#include "defs.h"

const byte *rom_loadbank(unsigned bank, unsigned pinned);
uint32_t cart_seconds(void);

#endif
