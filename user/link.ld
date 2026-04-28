/* user binary 用 raw flat layout：所有 section 從 0 開始連續排，objcopy -O binary
 * 之後就是直接可執行的位元串，由 kernel exec_umode 載到 user-side 任意位址。
 * .text._start 強制放最前面，確保 _start 在 binary offset 0。
 */
ENTRY(_start)

SECTIONS
{
    . = 0;

    .text : {
        *(.text._start)
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.riscv.attributes) }
}
