OPENOSC ?= ../OpenOSC
OPENOSC := $(abspath $(OPENOSC))
ARM_PREFIX ?= $(HOME)/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-
CC_ARM := $(ARM_PREFIX)gcc
CXX_ARM := $(ARM_PREFIX)g++
OBJCOPY := $(ARM_PREFIX)objcopy
NM := $(ARM_PREFIX)nm
PYTHON ?= python3
PIO ?= pio
OPENOSC_REF ?=
PROFILE ?= 0
BUILD ?= $(if $(filter 1,$(PROFILE)),build/profile,build)
MODELS := zt703s zt706s
ARM = -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
COMMON = $(ARM) -ffunction-sections -fdata-sections -g2 -fstack-usage -MMD -MP
LINK = $(ARM) -nostartfiles --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections,--print-memory-usage
INCLUDES = -Iinclude -I$(BUILD)/generated -I$(OPENOSC)/include -I$(OPENOSC)/vendor/cmsis \
           -I$(OPENOSC)/vendor/hal/Inc -I$(OPENOSC)/vendor/fatfs
RUNTIME = $(COMMON) -Os -flto -DSTM32H7B0xx -DUSE_HAL_DRIVER -DHSE_VALUE=25000000U $(INCLUDES) $(if $(filter 1,$(PROFILE)),-DRETROGO_PROFILE)
CXXFLAGS_RT = -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -Wextra -Wno-unused-parameter
PLATFORM = platform/battery_monitor.cpp platform/board.cpp platform/cooperative_service.cpp platform/clocks.cpp platform/control_latch.cpp platform/dma_memory.cpp \
           platform/input.cpp platform/interrupts.cpp platform/power.cpp platform/usb_core.cpp platform/watchdog.cpp \
           display/drawing.cpp display/panel.cpp display/boot_splash.cpp startup/startup.cpp startup/vectors.S \
           storage/block_device.cpp storage/fat_disk.cpp storage/filesystem.cpp storage/directories.cpp \
           storage/nor_io.cpp storage/nor_probe.cpp storage/spi_bus.cpp update/crc32.cpp
HAL = $(patsubst $(OPENOSC)/%,%,$(wildcard $(OPENOSC)/vendor/hal/Src/*.c))
APPLICATION_DRIVERS = openosc/src/platform/input.o openosc/src/platform/battery_monitor.o \
                      openosc/vendor/hal/Src/stm32h7xx_hal_adc.o openosc/vendor/hal/Src/stm32h7xx_hal_adc_ex.o
FAT = vendor/fatfs/ff.c vendor/fatfs/ffunicode.c
SOURCES = $(wildcard src/*.cpp)
CORE_RECIPES := $(wildcard cores/*/module.mk)
CORE_PACKAGES = $(CORE_RECIPES:cores/%/module.mk=$(BUILD)/dist/%.rgc)
GEN = $(BUILD)/generated
GEN_HEADERS = $(GEN)/ffconf.h $(GEN)/boot_splash_images.hpp $(GEN)/openosc/settings/run_mode.hpp

.PHONY: all combined check runtime runtime-check core
all: combined

COMBINED = $(PYTHON) tools/build_combined.py --openosc "$(OPENOSC)" --ref "$(OPENOSC_REF)" \
           --build "$(BUILD)/combined" --pio "$(PIO)" --make "$(MAKE)" --arm-prefix "$(ARM_PREFIX)" \
           --host-cxx "$(HOST_CXX)" --profile "$(PROFILE)"

combined:
	+$(COMBINED)

check:
	+$(COMBINED) --check

runtime: $(addprefix $(BUILD)/,$(addsuffix /retro-go.bin,$(MODELS))) core
core: $(CORE_PACKAGES)

$(GEN)/ffconf.h: $(OPENOSC)/include/ffconf.h Makefile
	@mkdir -p $(@D)
	sed 's/^#define FF_USE_FASTSEEK 0$$/#define FF_USE_FASTSEEK 1/' $< > $@

$(GEN)/boot_splash_images.hpp: $(OPENOSC)/scripts/boot_splash.py
	@mkdir -p $(@D)
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) -c 'import sys; from pathlib import Path; root=Path(sys.argv[1]); sys.path.insert(0, str(root/"scripts")); import boot_splash; boot_splash.generate(root, Path(sys.argv[2]), False)' "$(OPENOSC)" "$@"

$(GEN)/openosc/settings/run_mode.hpp: $(OPENOSC)/include/openosc/settings/run_mode.hpp tools/runtime_clocks.py
	$(PYTHON) tools/runtime_clocks.py profiles "$<" "$@"

$(GEN)/spi_bus.cpp: $(OPENOSC)/src/storage/spi_bus.cpp tools/runtime_clocks.py
	$(PYTHON) tools/runtime_clocks.py spi "$<" "$@"

define Model
$(1)_OBJECTS = $$(addprefix $(BUILD)/$(1)/,$$(SOURCES:.cpp=.o)) \
  $$(addprefix $(BUILD)/$(1)/openosc/src/,$$(patsubst %.S,%.o,$$(PLATFORM:.cpp=.o))) \
  $$(addprefix $(BUILD)/$(1)/openosc/,$$(patsubst %.c,%.o,$$(HAL) $$(FAT)))

# Enable the instrument keypad and battery drivers.
$$(addprefix $(BUILD)/$(1)/,$$(APPLICATION_DRIVERS)): private RUNTIME += -DOPENOSC_APPLICATION

$(BUILD)/$(1)/src/%.o: src/%.cpp $(GEN_HEADERS) Makefile
	@mkdir -p $$(@D)
	$$(CXX_ARM) $$(RUNTIME) $$(CXXFLAGS_RT) -DOPENOSC_BOARD=$(2) -c $$< -o $$@

$(BUILD)/$(1)/openosc/%.o: $(OPENOSC)/%.cpp $(GEN_HEADERS) Makefile
	@mkdir -p $$(@D)
	$$(CXX_ARM) $$(RUNTIME) $$(CXXFLAGS_RT) -DOPENOSC_BOARD=$(2) -c $$< -o $$@

$(BUILD)/$(1)/openosc/src/storage/spi_bus.o: $(GEN)/spi_bus.cpp $(GEN_HEADERS) Makefile
	@mkdir -p $$(@D)
	$$(CXX_ARM) $$(RUNTIME) $$(CXXFLAGS_RT) -DOPENOSC_BOARD=$(2) -c $$< -o $$@

$(BUILD)/$(1)/openosc/%.o: $(OPENOSC)/%.c $(GEN_HEADERS) Makefile
	@mkdir -p $$(@D)
	$$(CC_ARM) $$(RUNTIME) -DOPENOSC_BOARD=$(2) -c $$< -o $$@

$(BUILD)/$(1)/openosc/%.o: $(OPENOSC)/%.S Makefile
	@mkdir -p $$(@D)
	$$(CC_ARM) $$(COMMON) -c $$< -o $$@

$(BUILD)/$(1)/retro-go.elf: $$($(1)_OBJECTS) ld/runtime.ld Makefile
	$$(CXX_ARM) $$(LINK) -flto -Tld/runtime.ld -Wl,-Map,$$(@:.elf=.map) $$($(1)_OBJECTS) -o $$@

-include $$($(1)_OBJECTS:.o=.d)
endef
$(eval $(call Model,zt703s,703))
$(eval $(call Model,zt706s,706))

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

MODULE_FLAGS = $(COMMON) -O3 -fno-strict-aliasing -fno-unwind-tables -fno-asynchronous-unwind-tables -Iinclude

define Module
$(1)_objects = $$(addprefix $(BUILD)/modules/$(1)/,$$(patsubst %.cpp,%.o,$$($(1)_sources:.c=.o)))

$(BUILD)/modules/$(1)/%.o: %.c Makefile cores/$(1)/module.mk
	@mkdir -p $$(@D)
	$$(CC_ARM) $$(MODULE_FLAGS) $$($(1)_flags) -c $$< -o $$@

$(BUILD)/modules/$(1)/%.o: %.cpp Makefile cores/$(1)/module.mk
	@mkdir -p $$(@D)
	$$(CXX_ARM) $$(MODULE_FLAGS) -fno-rtti -fno-exceptions $$($(1)_flags) -c $$< -o $$@

$(BUILD)/modules/$(1)/module.elf: $$($(1)_objects) ld/core.ld
	$$(CXX_ARM) $$(LINK) -Tld/core.ld -Wl,-Map,$$(@:.elf=.map) $$($(1)_objects) -lm -o $$@

$(BUILD)/dist/$(1).rgc: $(BUILD)/modules/$(1)/module.elf $(BUILD)/modules/$(1)/module.bin cores/$(1)/manifest.json tools/package.py
	$$(PYTHON) tools/package.py $(BUILD)/modules/$(1)/module.elf $(BUILD)/modules/$(1)/module.bin cores/$(1)/manifest.json $$@ --nm $$(NM)
-include $$($(1)_objects:.o=.d)
endef
include $(CORE_RECIPES)
$(foreach module,$(CORE_RECIPES:cores/%/module.mk=%),$(eval $(call Module,$(module))))

HOST_CC ?= cc
HOST_CXX ?= c++
HOST_INCLUDES = -Itests/support -Iinclude -I$(GEN) -I$(OPENOSC)/include -I$(OPENOSC)/vendor/fatfs

$(BUILD)/tests/ff.o: $(OPENOSC)/vendor/fatfs/ff.c $(GEN)/ffconf.h
	@mkdir -p $(@D)
	$(HOST_CC) -O2 $(HOST_INCLUDES) -c $< -o $@

$(BUILD)/tests/ffunicode.o: $(OPENOSC)/vendor/fatfs/ffunicode.c $(GEN)/ffconf.h
	@mkdir -p $(@D)
	$(HOST_CC) -O2 $(HOST_INCLUDES) -c $< -o $@

$(BUILD)/tests/runtime: tests/runtime_host.cpp src/module_format.cpp src/saves.cpp src/media.cpp \
                      src/controls.cpp src/draw.cpp tests/support/memory_fat.cpp $(OPENOSC)/src/update/crc32.cpp \
                      $(BUILD)/tests/ff.o $(BUILD)/tests/ffunicode.o $(wildcard include/retrogo/*) tests/support/memory_fat.hpp
	@mkdir -p $(@D)
	$(HOST_CXX) -std=c++17 -O2 $(HOST_INCLUDES) $(filter %.cpp %.o,$^) -o $@

$(BUILD)/tests/core: tests/core_host.c $(gb_sources) $(wildcard cores/gb/gnuboy/*.h) include/retrogo/core.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -fno-strict-aliasing -Iinclude -Icores/gb $(filter %.c,$^) -o $@

define NativeModuleTest
$(BUILD)/tests/$(1): $(BUILD)/tests/module_host.o $$($(1)_sources) $$(shell find cores/$(1) cores/common -type f -name '*.h') include/retrogo/core.h
	@mkdir -p $$(@D)
	$$(HOST_CC) -std=gnu11 -O2 -fno-strict-aliasing -Iinclude $$($(1)_flags) $$(filter %.c %.o,$$^) -lm -o $$@
endef
$(foreach module,a2600 a7800 cpc fceumm gw md msx nes pce sms tama videopac wsv,$(eval $(call NativeModuleTest,$(module))))

$(BUILD)/tests/module_host.o: tests/module_host.c include/retrogo/core.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=gnu11 -O2 -Iinclude -c $< -o $@

$(BUILD)/tests/tgbdual: $(BUILD)/tests/module_host.o $(tgbdual_sources) $(wildcard cores/tgbdual/tgbdual/*.h*) include/retrogo/core.h
	$(HOST_CXX) -O2 -fno-strict-aliasing -Iinclude $(tgbdual_flags) $(filter %.cpp %.o,$^) -o $@

.PHONY: test-roms
test-roms:
	$(PYTHON) tests/make_test_rom.py $(BUILD)/test-roms
	$(PYTHON) tests/make_module_roms.py $(BUILD)/test-roms

runtime-check: runtime test-roms $(BUILD)/tests/core $(BUILD)/tests/runtime \
               $(addprefix $(BUILD)/tests/,a2600 a7800 cpc fceumm gw md msx nes pce sms tama tgbdual videopac wsv)
	$(BUILD)/tests/runtime $(CORE_PACKAGES)
	$(BUILD)/tests/core "$(BUILD)/test-roms/Pixel Lab DMG.gb"
	$(BUILD)/tests/core "$(BUILD)/test-roms/Pixel Lab Color.gbc"
	$(BUILD)/tests/a2600 "$(BUILD)/test-roms/Test NTSC.a26" 320 228 60 1
	$(BUILD)/tests/a2600 "$(BUILD)/test-roms/Test PAL.a26" 320 240 50 1
	$(BUILD)/tests/a7800 "$(BUILD)/test-roms/Test NTSC.a78" 320 222 60 0
	$(BUILD)/tests/a7800 "$(BUILD)/test-roms/Test PAL.a78" 320 240 50 0
	$(BUILD)/tests/cpc "$(BUILD)/test-roms/Test.dsk" 320 240 50 0
	$(BUILD)/tests/fceumm "$(BUILD)/test-roms/Test.nes" 256 240 60 1
	$(BUILD)/tests/fceumm "$(BUILD)/test-roms/PAL NES2.nes" 256 240 50 0
	$(BUILD)/tests/fceumm "$(BUILD)/test-roms/MMC5.nes" 256 240 60 0
	$(BUILD)/tests/gw "$(BUILD)/test-roms/Test.gw" 320 240 60 0
	$(BUILD)/tests/gw "$(BUILD)/test-roms/Test LZMA.gw" 320 240 60 0
	$(BUILD)/tests/gw "$(BUILD)/test-roms/Test JPEG.gw" 320 240 60 0
	$(BUILD)/tests/md "$(BUILD)/test-roms/Test NTSC.md" 320 240 60 0
	$(BUILD)/tests/md "$(BUILD)/test-roms/Test PAL.md" 320 240 50 0
	$(BUILD)/tests/msx "$(BUILD)/test-roms/Test.msx" 256 192 60 1
	$(BUILD)/tests/msx "$(BUILD)/test-roms/Test.mx2" 256 212 60 1
	$(BUILD)/tests/msx "$(BUILD)/test-roms/Test.mxp" 256 212 60 1
	$(BUILD)/tests/msx "$(BUILD)/test-roms/MSX sector.dsk" 256 212 60 0
	$(BUILD)/tests/nes "$(BUILD)/test-roms/Test.nes" 256 240 60 1
	$(BUILD)/tests/pce "$(BUILD)/test-roms/Test.pce" 256 224 60 1
	$(BUILD)/tests/sms "$(BUILD)/test-roms/Test.sms" 256 192 60 1
	$(BUILD)/tests/sms "$(BUILD)/test-roms/Test.gg" 160 144 60 0
	$(BUILD)/tests/sms "$(BUILD)/test-roms/Test.sg" 256 192 60 0
	$(BUILD)/tests/sms "$(BUILD)/test-roms/Test.col" 256 192 60 1 2
	$(BUILD)/tests/tama "$(BUILD)/test-roms/Test.tama" 128 88 60 0
	$(BUILD)/tests/tgbdual "$(BUILD)/test-roms/Pixel Lab DMG.gb" 160 144 60 0
	$(BUILD)/tests/tgbdual "$(BUILD)/test-roms/Pixel Lab Color.gbc" 160 144 60 0
	$(BUILD)/tests/videopac "$(BUILD)/test-roms/Test.o2" 320 240 60 0
	$(BUILD)/tests/wsv "$(BUILD)/test-roms/Test.wsv" 160 160 50 1

.PHONY: abi-fixtures

$(BUILD)/tests/abi.elf: tests/abi_core.c ld/core.ld include/retrogo/core.h
	@mkdir -p $(@D)
	$(CC_ARM) $(ARM) -O2 -ffunction-sections -fdata-sections -Iinclude $(LINK) -Tld/core.ld $< -o $@

$(BUILD)/test-cores/abi-test.rgc: $(BUILD)/tests/abi.elf $(BUILD)/tests/abi.bin tests/abi_core.json tools/package.py
	$(PYTHON) tools/package.py $(BUILD)/tests/abi.elf $(BUILD)/tests/abi.bin tests/abi_core.json $@ --nm $(NM)

abi-fixtures: $(BUILD)/test-cores/abi-test.rgc test-roms
	printf 'ABI1' > $(BUILD)/test-roms/ABI-Test.lab
