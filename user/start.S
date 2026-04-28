# user-mode 入口：kernel exec_umode 把 PC 設這裡然後 sret 進 U-mode。
# user_main 寫在 shell.c（C function）。如果 user_main return 就自動 exit(0)，
# 不該 fall through 跑 image 後續的 syscall.S / lib.c bytes（會當成指令解碼）。

.section .text._start
.globl _start
_start:
    call user_main
    li   a0, 0
    li   a7, 6        # SYS_EXIT
    ecall
1:  j 1b              # exit 不該回，防呆
