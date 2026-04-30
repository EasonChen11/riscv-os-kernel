# user-side syscall wrappers：a7 = NR, args 已在 a0..a5

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
1:  j 1b              # exit 不返回

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
