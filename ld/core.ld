ENTRY(rg_open)
MEMORY { MODULE (rwx) : ORIGIN = 0x24080000, LENGTH = 512K }
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    .text : {
        KEEP(*(.text.rg_open .text.rg_run .text.rg_save .text.rg_load))
        *(.text .text.*)
        . = ALIGN(4);
        __text_end = .;
    } > MODULE :text
    .data : {
        *(.rodata .rodata.*) *(.data .data.*)
        . = ALIGN(32);
        __image_end = .;
    } > MODULE :data
    .bss __image_end (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(32);
        __bss_end = .;
    } > MODULE :data
    /DISCARD/ : { *(.ARM.exidx* .ARM.extab* .comment .note*) }
    ASSERT(__image_end == __bss_start, "Core sections must be contiguous")
}
