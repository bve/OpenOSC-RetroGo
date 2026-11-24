#ifndef __MEM_H__
#define __MEM_H__

#include "defs.h"

#define MBC_NONE 0
#define MBC_MBC1 1
#define MBC_MBC2 2
#define MBC_MBC3 3
#define MBC_MBC5 5
#define MBC_RUMBLE 15

struct mbc
{
	int type;
	int model;
	int rombank;
	int rambank;
	int romsize;
	int enableram;
	int batt;
	int rtc;
	const byte *rmap[0x10];
	byte *wmap[0x10];
};

struct rom
{
	char name[20];
	int checksum;
};

struct ram
{
	byte hi[256];
	byte ibank[8][4096];
	byte (*sbank)[8192];
	unsigned cart_bytes;
};


extern struct mbc mbc;
extern struct rom rom;
extern struct ram ram;


void mem_updatemap();
void mem_write(word a, byte b);
byte mem_read(word a);
void mbc_reset();

static inline byte readb(word a)
{
	const byte *p = mbc.rmap[a >> 12];
	return p ? p[a & 0xfff] : mem_read(a);
}

static inline void writeb(word a, byte b)
{
	byte *p = mbc.wmap[a >> 12];
	if (p) p[a & 0xfff] = b;
	else mem_write(a, b);
}

static inline word readw(word a)
{
	const byte *p = mbc.rmap[a >> 12];
	if (p && (a & 0xfff) != 0xfff) {
		p += a & 0xfff;
		return p[0] | (p[1] << 8);
	}
	const byte low = readb(a);
	return low | (readb(a + 1) << 8);
}

static inline void writew(word a, word w)
{
	writeb(a, w);
	writeb(a + 1, w >> 8);
}
#endif
