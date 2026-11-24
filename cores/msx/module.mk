msx_sources := $(wildcard cores/msx/*.c cores/msx/baremsx/*.c) cores/common/keyboard.c cores/common/z80/embedded.c
msx_flags := -Icores/common -Icores/common/z80
