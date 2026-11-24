videopac_sources := $(wildcard cores/videopac/*.c cores/videopac/o2em/*.c)
videopac_flags := -DTARGET_GNW -D__LIBRETRO__ -Icores/videopac/o2em
