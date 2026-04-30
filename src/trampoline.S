# sigreturn trampoline：deliver_signal memcpy 到 user signal stack 頂端，
# user handler ret 跳這裡 → ecall 進 SYS_SIGRETURN。
# _end label 提供 size 給 caller (sigreturn_trampoline_end - sigreturn_trampoline)。
.section .rodata
.global sigreturn_trampoline
.global sigreturn_trampoline_end

sigreturn_trampoline:
    li   a7, 11
    ecall
sigreturn_trampoline_end:
