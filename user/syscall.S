# user-side syscall wrappers：把 NR 塞進 a7，args 已在 a0..a5（caller 放好），
# ecall 觸發 trap，kernel dispatcher 把回傳值寫進 trap_frame.a0，sret 後 a0 即 return。
# C 端只需 long getpid(void); 等 prototype，呼叫起來就跟普通 function 一樣。

.section .text

.globl getpid
getpid:
    li   a7, 0
    ecall
    ret

.globl uart_read
uart_read:
    li   a7, 1
    ecall
    ret

.globl uart_write
uart_write:
    li   a7, 2
    ecall
    ret

.globl exec
exec:
    li   a7, 3
    ecall
    ret

.globl fork
fork:
    li   a7, 4
    ecall
    ret

.globl waitpid
waitpid:
    li   a7, 5
    ecall
    ret

.globl exit
exit:
    li   a7, 6
    ecall
1:  j 1b              # exit 不返回；compiler attribute 也標 noreturn

.globl stop
stop:
    li   a7, 7
    ecall
    ret

.globl display
display:
    li   a7, 8
    ecall
    ret

.globl usleep
usleep:
    li   a7, 9
    ecall
    ret

.globl signal
signal:
    li   a7, 10
    ecall
    ret

.globl kill
kill:
    li   a7, 12
    ecall
    ret
