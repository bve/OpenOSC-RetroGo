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
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *   Audio-free sound chip interface.
 *
 *  Modified for OpenOSC-RetroGo from SMS Plus GX sound.c: PSG and FM writes
 *  are accepted and discarded. The SN76489 and YM2413 have no CPU-readable
 *  registers and raise no interrupts, so dropping sample generation does not
 *  change emulation. The Mark III FM unit detection port keeps its upstream
 *  behavior with FM emulation disabled.
 *
 ******************************************************************************/

#include "shared.h"

int sound_init(void) { return 1; }
void sound_shutdown(void) {}
void sms_sound_reset(void) {}
void sound_update(int line) { (void)line; }
void psg_write(int data) { (void)data; }
void psg_stereo_w(int data) { (void)data; }

int fmunit_detect_r(void)
{
  return sms.fm_detect;
}

void fmunit_detect_w(int data)
{
  if(!sms.use_fm) return;
  sms.fm_detect = data;
}

void fmunit_write(int offset, int data)
{
  (void)offset;
  (void)data;
}
