.section ".text.boot"
.global _start      # update _start to global

_start:
    mv s0, a0              # save hart id
    mv s1, a1              # save dtb address

    # Check if we are already relocated
    la   t0, _start        # linked address of _start (PC-relative)
    la   t1, _end
    sub  s2, t1, t0        # s2 = kernel_size (_end - _start)

    # Set up temporary stack for C call (use _end as temp stack)
    la   sp, _end

    # Call get_reloc_addr(dtb, kernel_size) to determine relocation target
    mv   a0, s1            # dtb address
    mv   a1, s2            # kernel size
    call bootloader_find_reloc_addr    # return value in a0

    # If get_reloc_addr returns 0 (failed), use hardcoded fallback
    beqz a0, use_fallback
    mv   s3, a0            # s3 = relocation target address
    j    check_reloc

use_fallback:
#ifdef QEMU
    li   s3, 0x82000000
#else
    li   s3, 0x20000000
#endif

check_reloc:
    la   t0, _start
    beq  t0, s3, relocated # already at target, skip copy

    # --- Self-relocation: copy _start ~ _end to s3 ---
    la   t0, _start        # source start
    la   t1, _end          # source end
    mv   t2, s3            # destination start
copy_loop:
    ld   t3, 0(t0)
    sd   t3, 0(t2)
    addi t0, t0, 8
    addi t2, t2, 8
    blt  t0, t1, copy_loop

    # 清除 I-cache，確保 CPU 執行剛寫入的新指令而非 cache 中的舊指令
    fence.i

    # Jump to relocated "relocated" label (skip _start re-init)
    la   t0, relocated
    la   t1, _start
    sub  t0, t0, t1        # offset = relocated - _start
    add  t0, s3, t0        # target = reloc_base + offset
    jr   t0

relocated:
    # --- Clear BSS ---
    la   t0, __bss_start
    la   t1, __bss_stop
    bge  t0, t1, clear_bss_done
clear_bss:
    sd   zero, 0(t0)
    addi t0, t0, 8
    blt  t0, t1, clear_bss
clear_bss_done:

    # --- Set up stack ---
    la   sp, _end

    # --- Set trap vector ---
    call add_trap

    # --- Call start_kernel ---
    mv   a0, s0            # pass hart id
    mv   a1, s1            # pass dtb address
    mv   a2, s3            # allocate location
    tail start_kernel
