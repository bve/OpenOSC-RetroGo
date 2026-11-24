NES_DIR := cores/nes/nofrendo
nes_sources := $(wildcard cores/nes/*.c $(NES_DIR)/cpu/*.c $(NES_DIR)/mappers/*.c $(NES_DIR)/nes/*.c)
nes_flags := -I$(NES_DIR) -I$(NES_DIR)/cpu -I$(NES_DIR)/mappers -I$(NES_DIR)/nes -fno-math-errno
