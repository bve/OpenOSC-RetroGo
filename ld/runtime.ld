ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx)   : ORIGIN = 0x081A0400, LENGTH = 383K
    RAM (rw)     : ORIGIN = 0x20000000, LENGTH = 0x1FF00
    VIDEO (rw)   : ORIGIN = 0x24000000, LENGTH = 512K
    CORE (rwx)   : ORIGIN = 0x24080000, LENGTH = 512K
    AHB (rw)     : ORIGIN = 0x30000000, LENGTH = 128K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .bootstrap : { KEEP(*(.bootstrap*)) . = ALIGN(4); } > FLASH
    .text : {
        *(.text*) *(.rodata*)
        . = ALIGN(4);
        KEEP(*(.init)) KEEP(*(.fini))
        __preinit_array_start = .; KEEP(*(.preinit_array*)) __preinit_array_end = .;
        __init_array_start = .; KEEP(*(.init_array*)) __init_array_end = .;
        __fini_array_start = .; KEEP(*(.fini_array*)) __fini_array_end = .;
        . = ALIGN(4);
    } > FLASH
    /* OpenOSC's shared startup can relocate Recovery's code to RAM.
       This image executes directly from internal Flash. */
    _stext = ADDR(.text);
    _etext = _stext;
    _sitext = _stext;
    .ARM.extab : { *(.ARM.extab*) } > FLASH
    .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
    _sidata = LOADADDR(.data);
    .bss (NOLOAD) : { . = ALIGN(1024); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(8); _ebss = .; } > RAM
    .framebuffer (NOLOAD) : { KEEP(*(.framebuffer)) } > VIDEO
    .workspace 0x24050000 (NOLOAD) : { *(.workspace) } > VIDEO
    .core (NOLOAD) : { . = . + LENGTH(CORE); } > CORE
    .cartridge (NOLOAD) : { *(.cartridge) } > AHB
    _end = _ebss; PROVIDE(end = _end);
    /DISCARD/ : { *(.comment .note*) }
    ASSERT(_ebss + 49152 <= _estack, "Runtime needs 48 KiB reserved for stack")
    ASSERT(ADDR(.framebuffer) == ORIGIN(VIDEO) && SIZEOF(.framebuffer) == 307200,
           "Panel needs two 320x240 RGB565 buffers")
    ASSERT(ADDR(.workspace) + SIZEOF(.workspace) <= ORIGIN(CORE),
           "CPU workspace overlaps the core arena")
    ASSERT(LOADADDR(.data) + SIZEOF(.data) <= ORIGIN(FLASH) + LENGTH(FLASH),
           "Retro-Go exceeds 383 KiB payload")
}
