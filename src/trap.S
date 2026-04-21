.global add_trap
add_trap:
    la t0, handle_exception
    csrw stvec, t0
    ret # == jr ra 返回原本位置

.align 2  # 要求 trap handler 的位址必須 4-byte 對齊(2^2=4)
handle_exception:
    csrrw sp, sscratch, sp   # 換到 kernel stack，sscratch ← user sp
    bnez sp, save_context    # sp != 0 表示從 U-mode 來
    csrr sp, sscratch        # sp == 0 表示從 S-mode 來，還原 sp

save_context:
    addi sp, sp, -8 * 35
    sd ra,  8 *  0(sp)
    sd gp,  8 *  2(sp)
    sd tp,  8 *  3(sp)
    sd t0,  8 *  4(sp)
    sd t1,  8 *  5(sp)
    sd t2,  8 *  6(sp)
    sd s0,  8 *  7(sp)
    sd s1,  8 *  8(sp)
    sd a0,  8 *  9(sp)
    sd a1,  8 * 10(sp)
    sd a2,  8 * 11(sp)
    sd a3,  8 * 12(sp)
    sd a4,  8 * 13(sp)
    sd a5,  8 * 14(sp)
    sd a6,  8 * 15(sp)
    sd a7,  8 * 16(sp)
    sd s2,  8 * 17(sp)
    sd s3,  8 * 18(sp)
    sd s4,  8 * 19(sp)
    sd s5,  8 * 20(sp)
    sd s6,  8 * 21(sp)
    sd s7,  8 * 22(sp)
    sd s8,  8 * 23(sp)
    sd s9,  8 * 24(sp)
    sd s10, 8 * 25(sp)
    sd s11, 8 * 26(sp)
    sd t3,  8 * 27(sp)
    sd t4,  8 * 28(sp)
    sd t5,  8 * 29(sp)
    sd t6,  8 * 30(sp)
    # user sp 現在在 sscratch，用 s0-s4 暫存 CSR 值
    csrr s0, sscratch
    csrr s1, sepc
    csrr s2, sstatus
    csrr s3, scause
    csrr s4, stval
    sd s0,  8 *  1(sp)   # sp slot
    sd s1,  8 * 31(sp)   # sepc
    sd s2,  8 * 32(sp)   # sstatus
    sd s3,  8 * 33(sp)   # scause
    sd s4,  8 * 34(sp)   # stval

    csrw sscratch, zero   # 清零：標記目前在 S-mode（非 U-mode trap）
    mv a0, sp
    call do_trap

ret_from_exception:
    ld s0, 8 * 32(sp)        # 讀回 sstatus
    andi s0, s0, (1 << 8)    # 檢查 SPP bit
    bnez s0, 1f              # SPP != 0 → 返回 S-mode，不重設 sscratch

    addi s0, sp, 8 * 35
    csrw sscratch, s0        # 返回 U-mode：把 kernel stack 頂端存入 sscratch
1:
    ld s1,  8 * 31(sp)
    ld s2,  8 * 32(sp)
    csrw sepc, s1
    csrw sstatus, s2
    ld ra,  8 *  0(sp)
    ld gp,  8 *  2(sp)
    ld tp,  8 *  3(sp)
    ld t0,  8 *  4(sp)
    ld t1,  8 *  5(sp)
    ld t2,  8 *  6(sp)
    ld s0,  8 *  7(sp)
    ld s1,  8 *  8(sp)
    ld a0,  8 *  9(sp)
    ld a1,  8 * 10(sp)
    ld a2,  8 * 11(sp)
    ld a3,  8 * 12(sp)
    ld a4,  8 * 13(sp)
    ld a5,  8 * 14(sp)
    ld a6,  8 * 15(sp)
    ld a7,  8 * 16(sp)
    ld s2,  8 * 17(sp)
    ld s3,  8 * 18(sp)
    ld s4,  8 * 19(sp)
    ld s5,  8 * 20(sp)
    ld s6,  8 * 21(sp)
    ld s7,  8 * 22(sp)
    ld s8,  8 * 23(sp)
    ld s9,  8 * 24(sp)
    ld s10, 8 * 25(sp)
    ld s11, 8 * 26(sp)
    ld t3,  8 * 27(sp)
    ld t4,  8 * 28(sp)
    ld t5,  8 * 29(sp)
    ld t6,  8 * 30(sp)
    ld sp,  8 *  1(sp)   # 最後才還原 sp（user sp），因為 sp 一旦換回 User 的堆疊，你就再也拿不到存放在 Kernel Stack 裡的資料
    sret
