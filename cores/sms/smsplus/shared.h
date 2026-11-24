/******************************************************************************
 *  Sega Master System / GameGear Emulator
 *  Copyright (C) 1998-2007  Charles MacDonald
 *
 *  additionnal code by Eke-Eke (SMS Plus GX)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *  Modified for OpenOSC-RetroGo: fixed-width types, no platform headers,
 *  no FM or PSG sample generation, a host-supplied ROM page cache, and the
 *  MIT-licensed superzazu/z80 CPU core.
 *
 ******************************************************************************/

#ifndef _SHARED_H_
#define _SHARED_H_

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;

#ifndef IS_LITTLE_ENDIAN
#define IS_LITTLE_ENDIAN 1
#endif

#define IRAM_ATTR
#define DRAM_ATTR

#include "sms_z80.h"
#include "sms.h"
#include "pio.h"
#include "memz80.h"
#include "vdp.h"
#include "render.h"
#include "tms.h"
#include "sms_sound.h"
#include "system.h"
#include "loadrom.h"

#endif /* _SHARED_H_ */
