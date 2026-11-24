GW_DIR := cores/gw/lcd
gw_sources := $(wildcard cores/gw/*.c $(GW_DIR)/cpu/*.c $(GW_DIR)/system/*.c) \
              cores/gw/lzma/LzmaDec.c cores/gw/jpeg/tjpgd.c
gw_flags := -Os -I$(GW_DIR)/cpu -I$(GW_DIR)/system
