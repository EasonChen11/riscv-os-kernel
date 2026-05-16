/* lab6 QEMU linker script
 * VMA: high-half VA  (0xffffffc080200000+)
 * LMA: physical PA   (0x80200000+)
 * 透過 AT() 讓 objcopy -O binary 只輸出實際 PA 範圍，否則會生出橫跨 256 TiB 的稀疏檔。
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
    . = 0xffffffc080200000;
    _start = .;
    .text   : AT(0x80200000)                                { *(.text.boot) *(.text) }
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
