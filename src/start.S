/*
 * Lab6 boot path (QEMU virt, OpenSBI hand-off at S-mode, PC = PA 0x80200000):
 *   1. clear BSS via low-PA  (objcopy -O binary 不含 NOBITS sections，
 *                              BSS 必須在 setup_vm 用 pgd[]/pmd_*[] 前歸零)
 *   2. call setup_vm         (建 identity + higher-half map、寫 satp)
 *   3. 跳到 2f 的高半 VA copy
 *   4. reload sp = _end (now VA)、dtb_pa + PAGE_OFFSET → dtb_va、tp=0
 *   5. call drop_identity_map
 *   6. call add_trap         (stvec = handle_exception VA)
 *   7. tail start_kernel(hartid, dtb_va, 0)
 */

.section ".text.boot"
.global _start

_start:
    mv      s0, a0                  /* hart id (OpenSBI 給 a0) */
    mv      s1, a1                  /* dtb PA (a1) */

    la      sp, _end                /* low PA (PC-relative, pre-MMU) */

    /* clear BSS pre-MMU 透過 PA；objcopy 不會把 NOBITS 寫進 binary，
     * loader 載入的記憶體位元值是 DRAM 殘餘，pgd[] 開機若是 garbage MMU 立刻爛掉 */
    la      t0, __bss_start
    la      t1, __bss_stop
    bge     t0, t1, 1f
0:
    sd      zero, 0(t0)
    addi    t0, t0, 8
    blt     t0, t1, 0b
1:
    call    setup_vm                /* MMU on：identity 暫時保留供我們跳到高半 VA */

    /* la 是 PC-relative；此刻 PC 仍在低 PA，la t0, 2f 得低 PA → 加 PAGE_OFFSET = 高 VA */
    la      t0, 2f
    li      t1, 0xffffffc000000000
    add     t0, t0, t1
    jr      t0

.align 2
2:
    la      sp, _end                /* 現在 la 給高 VA */

    li      t1, 0xffffffc000000000
    add     s1, s1, t1              /* dtb_pa → dtb_va */

    mv      tp, zero                /* firmware 可能在 tp 留垃圾 */

    call    drop_identity_map
    call    add_trap                /* stvec = handle_exception VA */

    mv      a0, s0
    mv      a1, s1
    mv      a2, zero                /* reloc_addr 已停用 */
    tail    start_kernel
