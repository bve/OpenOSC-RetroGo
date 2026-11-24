fceumm_sources := $(wildcard cores/fceumm/*.c cores/fceumm/port/*.c cores/fceumm/fceumm/*.c cores/fceumm/fceumm/boards/*.c)
fceumm_flags := -Icores/fceumm -Icores/fceumm/port -Icores/fceumm/fceumm -Icores/fceumm/fceumm/boards \
                -DTARGET_GNW -DFCEU_LOW_RAM -DFCEU_NO_MALLOC -DFCEU_NO_OVERCLOCKING -DFCEU_NO_EXTFLASH -DFCEU_VERSION_NUMERIC=9813 \
                -DIS_LITTLE_ENDIAN -DLSB_FIRST -D__LIBRETRO__ -DVIDEO_RGB565 -Dprintf=fceumm_printf -Dmalloc=fceumm_malloc -Dcalloc=fceumm_calloc \
                -Drealloc=fceumm_realloc -Dfree=fceumm_free -Dsprintf=fceumm_sprintf -Dvsprintf=fceumm_vsprintf \
                -Drand=fceumm_rand -Dsrand=fceumm_srand -DNDEBUG -include assert.h \
                -Wno-sequence-point -Wno-parentheses
