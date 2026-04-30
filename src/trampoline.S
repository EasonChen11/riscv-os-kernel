# sigreturn trampoline：deliver_signal 把這段 8-byte 機器碼 memcpy 進新配的
# user signal stack 頂端。user handler 跑完 ret 進這段 → ecall 觸發 SYS_SIGRETURN
# 把 trap_frame 復原 + 釋放 signal stack。
#
# 放 .rodata：kernel 自己不會跳進來執行，純當 data byte source 給 memcpy。
# start/end 兩個 label 提供大小：sigreturn_trampoline_end - sigreturn_trampoline。
.section .rodata
.global sigreturn_trampoline
.global sigreturn_trampoline_end

sigreturn_trampoline:
    li   a7, 11            # SYS_SIGRETURN
    ecall
sigreturn_trampoline_end:
