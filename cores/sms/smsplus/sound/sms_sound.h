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
 *   Sound chip interface.
 *
 *  Modified for OpenOSC-RetroGo: the host has no audio output, so the
 *  SN76489 and YM2413 sample generators are not built. No sound chip state
 *  is visible to the CPU except the FM detection latch, which is kept.
 *
 ******************************************************************************/

#ifndef _SOUND_H_
#define _SOUND_H_

/* FM emulation selection; only SND_NONE is built */
enum {
  SND_NONE,
  SND_EMU2413,
  SND_YM2413
};

/* Function prototypes */
void psg_write(int data);
void psg_stereo_w(int data);
int fmunit_detect_r(void);
void fmunit_detect_w(int data);
void fmunit_write(int offset, int data);
int sound_init(void);
void sound_shutdown(void);
void sms_sound_reset(void);
void sound_update(int line);

#endif /* _SOUND_H_ */
