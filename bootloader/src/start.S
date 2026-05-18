.section .text.boot
.global _start

_start:
    mv      s0, a0                  /* hartid (OpenSBI 給 a0) */
    mv      s1, a1                  /* dtb_pa (a1) */

    la      sp, _end

    /* clear BSS：objcopy 不會把 NOBITS 寫進 binary，
     * loader 載入的記憶體位元值是 DRAM 殘餘，呼叫 C 前必須歸零 */
    la      t0, __bss_start
    la      t1, __bss_stop
    bge     t0, t1, 1f
0:
    sd      zero, 0(t0)
    addi    t0, t0, 8
    blt     t0, t1, 0b
1:
    mv      a0, s0
    mv      a1, s1
    tail    bootloader_main
