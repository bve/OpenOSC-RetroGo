MD_DIR := cores/md/gwenesis
md_sources := $(wildcard cores/md/*.c $(MD_DIR)/*/*.c) cores/common/z80/embedded.c
md_flags := -Os -DNDEBUG -DTABLES_FULL -I$(MD_DIR)/cpu -I$(MD_DIR)/audio -I$(MD_DIR)/bus \
            -I$(MD_DIR)/io -I$(MD_DIR)/vdp -Icores/common/z80
