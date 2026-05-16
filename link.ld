/* lab6 board (OrangePi RV2) linker script */
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
    . = 0xffffffc000200000;
    _start = .;
    .text   : AT(0x00200000)                                { *(.text.boot) *(.text) }
    .rodata : AT(LOADADDR(.text)   + SIZEOF(.text))         { *(.rodata) }
    .data   : AT(LOADADDR(.rodata) + SIZEOF(.rodata))       { *(.data) }
    .bss    : AT(LOADADDR(.data)   + SIZEOF(.data)) {
        __bss_start = .;
        *(.sbss) *(.bss)
        __bss_stop = .;
    }
    . = ALIGN(16);
    . = . + 0x4000;
    _end = .;
}
