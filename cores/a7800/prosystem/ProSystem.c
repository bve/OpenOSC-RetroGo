// ----------------------------------------------------------------------------
//   ___  ___  ___  ___       ___  ____  ___  _  _
//  /__/ /__/ /  / /__  /__/ /__    /   /_   / |/ /
// /    / \  /__/ ___/ ___/ ___/   /   /__  /    /  emulator
//
// ----------------------------------------------------------------------------
// Copyright 2003, 2004 Greg Stanton
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
// ----------------------------------------------------------------------------
// ProSystem.cpp
// ----------------------------------------------------------------------------
#include "ProSystem.h"
#include <string.h>
#include "Equates.h"
#include "Cartridge.h"
#include "Maria.h"
#include "Memory.h"
#include "Region.h"
#include "Riot.h"
#include "Sally.h"
#define PRO_SYSTEM_STATE_HEADER "PRO-SYSTEM STATE"
#define PRO_SYSTEM_SOURCE "ProSystem.cpp"

uint16_t prosystem_frequency = 60;
uint8_t prosystem_frame = 0;
uint16_t prosystem_scanlines = 262;
uint32_t prosystem_cycles = 0;

// ----------------------------------------------------------------------------
// Reset
// ----------------------------------------------------------------------------
void prosystem_Reset(void)
{
   if(!cartridge_IsLoaded())
      return;

   prosystem_frame = 0;
   sally_Reset();
   region_Reset( );
   memory_Reset( );
   maria_Clear( );
   maria_Reset( );
   riot_Reset ( );

   cartridge_Store();

   prosystem_cycles = sally_ExecuteRES( );
}

// ----------------------------------------------------------------------------
// ExecuteFrame
// ----------------------------------------------------------------------------
void prosystem_ExecuteFrame(const uint8_t* input)
{
   riot_SetInput(input);

   for(maria_scanline = 1; maria_scanline <= prosystem_scanlines; maria_scanline++)
   {
      uint32_t cycles;
      if(maria_scanline == maria_displayArea.top)
         memory_ram[MSTAT] = 0;
      if(maria_scanline == maria_displayArea.bottom)
         memory_ram[MSTAT] = 128;

      prosystem_cycles %= 456;
      while(prosystem_cycles < 28)
      {
         cycles = sally_ExecuteInstruction();
         prosystem_cycles += (cycles << 2);

         if(riot_timing)
            riot_UpdateTimer(cycles);

         if(memory_ram[WSYNC] && !(cartridge_flags & CARTRIDGE_WSYNC_MASK))
         {
            prosystem_cycles = 456;
            memory_ram[WSYNC] = false;
            break;
         }
      }

      cycles = maria_RenderScanline( );
      if(cartridge_flags & CARTRIDGE_CYCLE_STEALING_MASK)
         prosystem_cycles += cycles;

      while(prosystem_cycles < 456)
      {
         cycles = sally_ExecuteInstruction( );
         prosystem_cycles += (cycles << 2);
         if(riot_timing)
            riot_UpdateTimer(cycles);

         if(memory_ram[WSYNC] && !(cartridge_flags & CARTRIDGE_WSYNC_MASK))
         {
            prosystem_cycles = 456;
            memory_ram[WSYNC] = false;
            break;
         }
      }
   }

   prosystem_frame++;

   if(prosystem_frame >= prosystem_frequency)
      prosystem_frame = 0;
}
