#ifndef TRAP_H
#define TRAP_H

#include <stdint.h>
#define SCAUSE_INTERRUPT_BIT (1UL << 63)
#define SCAUSE_CODE_MASK (~(1UL << 63))
#define ENV_CALL_FROM_UMODE 8 // ecall from U-mode 的 scause code 是 8，來自 RISC-V privileged spec, 非 intrupt
#define INT_SUP_TIMER 5 // Supervisor timer interrupt, from RISC-V privileged spec
#define INT_SUP_EXT 9 // from PLIC

/* kernel stack frame saved by handle_exception (35 * 8 = 280 bytes)
 * layout must match the sd/ld offsets in start.S handle_exception */
struct trap_frame {
    uint64_t ra;       /* 8 *  0: x1  */
    uint64_t sp;       /* 8 *  1: x2  (saved user sp from sscratch) */
    uint64_t gp;       /* 8 *  2: x3  */
    uint64_t tp;       /* 8 *  3: x4  */
    uint64_t t0;       /* 8 *  4: x5  */
    uint64_t t1;       /* 8 *  5: x6  */
    uint64_t t2;       /* 8 *  6: x7  */
    uint64_t s0;       /* 8 *  7: x8  */
    uint64_t s1;       /* 8 *  8: x9  */
    uint64_t a0;       /* 8 *  9: x10 */
    uint64_t a1;       /* 8 * 10: x11 */
    uint64_t a2;       /* 8 * 11: x12 */
    uint64_t a3;       /* 8 * 12: x13 */
    uint64_t a4;       /* 8 * 13: x14 */
    uint64_t a5;       /* 8 * 14: x15 */
    uint64_t a6;       /* 8 * 15: x16 */
    uint64_t a7;       /* 8 * 16: x17 */
    uint64_t s2;       /* 8 * 17: x18 */
    uint64_t s3;       /* 8 * 18: x19 */
    uint64_t s4;       /* 8 * 19: x20 */
    uint64_t s5;       /* 8 * 20: x21 */
    uint64_t s6;       /* 8 * 21: x22 */
    uint64_t s7;       /* 8 * 22: x23 */
    uint64_t s8;       /* 8 * 23: x24 */
    uint64_t s9;       /* 8 * 24: x25 */
    uint64_t s10;      /* 8 * 25: x26 */
    uint64_t s11;      /* 8 * 26: x27 */
    uint64_t t3;       /* 8 * 27: x28 */
    uint64_t t4;       /* 8 * 28: x29 */
    uint64_t t5;       /* 8 * 29: x30 */
    uint64_t t6;       /* 8 * 30: x31 */
    uint64_t sepc;     /* 8 * 31 */
    uint64_t sstatus;  /* 8 * 32 */
    uint64_t scause;   /* 8 * 33 */
    uint64_t stval;    /* 8 * 34 */
};

void exec_umode(const char *filename);
void do_trap(struct trap_frame *regs);

#endif /* TRAP_H */
