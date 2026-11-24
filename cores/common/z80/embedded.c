/* Keep diagnostics out of loadable modules. */
#include <stdio.h>

#define fprintf(...) ((void)0)
#define printf(...) ((void)0)
#include "z80.c"
