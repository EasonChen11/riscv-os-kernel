/*
 * Bootloader entry (QEMU virt / OrangePi RV2)
 *   OpenSBI 在 S-mode 跳進來，PC = link address (0x80200000 / 0x00200000)
 *   a0 = hartid, a1 = dtb_pa
 *
 * 流程：
 *   1. 保存 hartid, dtb_pa
 *   2. 設暫時 sp
 *   3. call bootloader_find_reloc_addr(dtb, kernel_size) → 高位 PA
 *      失敗則用 fallback：QEMU 0x82000000 / 板子 0x20000000
 *   4. 將 _start..._end 整段複製到目標 PA
 *   5. fence.i + 跳到新位置的 relocated label
 *   6. 清 BSS（新位置）；tp 歸零；重設 sp
 *   7. tail bootloader_main(hartid, dtb)
 *
 * 注意：步驟 3 在 BSS 清零前呼叫，bootloader_find_reloc_addr 全程只能用 local 變數。
 */

.section .text.boot
.global _start

_start:
    mv      s0, a0                  /* hartid */
    mv      s1, a1                  /* dtb_pa */

    la      sp, _end                /* 暫時 stack（低 PA） */

    /* 算 kernel_size = _end - _start */
    la      t0, _start
    la      t1, _end
    sub     s2, t1, t0

    /* 呼叫 find_reloc_addr(dtb, kernel_size) */
    mv      a0, s1
    mv      a1, s2
    call    bootloader_find_reloc_addr
    bnez    a0, 1f
#ifdef QEMU
    li      a0, 0x82000000
#else
    li      a0, 0x20000000
#endif
1:
    mv      s3, a0                  /* s3 = reloc target PA */

    /* 若已在目標位置就跳過 copy */
    la      t0, _start
    beq     t0, s3, relocated

    /* Copy _start..._end → s3 */
    la      t0, _start
    la      t1, _end
    mv      t2, s3
copy_loop:
    ld      t3, 0(t0)
    sd      t3, 0(t2)
    addi    t0, t0, 8
    addi    t2, t2, 8
    blt     t0, t1, copy_loop

    fence.i

    /* 跳到新位置的 relocated label */
    la      t0, relocated
    la      t1, _start
    sub     t0, t0, t1              /* offset within image */
    add     t0, s3, t0
    jr      t0

relocated:
    /* clear BSS（新位置） */
    la      t0, __bss_start
    la      t1, __bss_stop
    bge     t0, t1, 2f
clear_bss:
    sd      zero, 0(t0)
    addi    t0, t0, 8
    blt     t0, t1, clear_bss
2:
    mv      tp, zero                /* firmware 殘渣防呆 */
    la      sp, _end                /* sp 指到新位置 _end */

    mv      a0, s0
    mv      a1, s1
    tail    bootloader_main
