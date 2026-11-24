/* OpenOSC-RetroGo services for FCEUmm. Distributed under the GNU GPL v2 or
 * later, like FCEUmm. */
#ifndef FCEUMM_PORT_H
#define FCEUMM_PORT_H

#include <stdint.h>
#include "retrogo/core.h"

enum {
    PortPrgPage = 8192,
    PortChrPage = 1024,
    PortArenaBytes = 76 * 1024,
    PortWidth = 256,
    PortHeight = 240,
    PortHeaderBytes = 16 + 512, /* iNES header and optional trainer */
};

/* ROM file access for the pager; rom_error latches the first failure. */
extern RgHost port_host;
extern uint32_t port_rom_bytes;
extern int port_rom_error;
extern uint16_t port_frame[PortWidth * PortHeight];

void port_reset(const RgHostV3 *host);
uint32_t port_arena_used(void);

/* Bounded in-memory stream behind fs_open(). A null buffer counts bytes. */
void port_stream(uint8_t *buffer, uint32_t capacity, int writing);
uint32_t port_stream_length(void);
int port_stream_failed(void);

#endif
