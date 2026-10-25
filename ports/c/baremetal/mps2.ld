/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Starboard Protocol — linker script for the QEMU MPS2 Cortex-M boards
 * (AN385 M3, AN386 M4, AN500 M7): code in the 4 MB SSRAM1 at 0x0, data,
 * heap and stack in the 4 MB SSRAM2/3 at 0x20000000. */
MEMORY
{
    CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}
ENTRY(Reset_Handler)
SECTIONS
{
    .text : {
        KEEP(*(.vectors))
        *(.text*)
        KEEP(*(.init)) KEEP(*(.fini))
        *(.rodata*)
        . = ALIGN(4);
        __init_array_start = .; KEEP(*(.init_array*)) __init_array_end = .;
        __preinit_array_start = .; KEEP(*(.preinit_array*)) __preinit_array_end = .;
        __fini_array_start = .; KEEP(*(.fini_array*)) __fini_array_end = .;
    } > CODE
    .ARM.exidx : { *(.ARM.exidx*) } > CODE
    .data : { *(.data*) } > RAM
    .bss (NOLOAD) : {
        __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(8); __bss_end__ = .;
    } > RAM
    end = .;
    _end = .;
    __end__ = .;
    __HeapLimit = ORIGIN(RAM) + LENGTH(RAM) - 0x10000;
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    __stack = __stack_top;
}
