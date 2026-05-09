/* Linker script for QEMU's mps2-an386 board (Cortex-M4).
 *
 * Memory map (subset; full map in QEMU hw/arm/mps2.c):
 *   0x00000000–0x003FFFFF   ZBT SSRAM1, 4 MB    — code + .rodata
 *   0x20000000–0x203FFFFF   ZBT SSRAM2, 4 MB    — .data, .bss, stack
 *
 * Stack grows down from the top of RAM. Heap is unused (bench has no malloc).
 */

ENTRY(Reset_Handler)

MEMORY {
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}

SECTIONS {
    .vectors : {
        KEEP(*(.vectors))
    } > FLASH

    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.glue_7) *(.glue_7t)
        . = ALIGN(4);
    } > FLASH

    /* Constructor arrays — empty in our pure-C code, kept for forward-compat. */
    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*) .init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH
    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*) .fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    _data_load_start = LOADADDR(.data);
    .data : {
        . = ALIGN(4);
        _data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        _data_end = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > RAM

    _stack_top = ORIGIN(RAM) + LENGTH(RAM);

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.note*) *(.comment) }
}
