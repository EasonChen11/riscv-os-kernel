#ifndef EXEC_H
#define EXEC_H

#include "trap.h"

// 第一次跳 U-mode：kernel thread 直接走 inline asm sscratch+sret 路徑，
// 一去不回（自身變成 user process）。lab4 既有用法，shell 仍可呼叫。
void exec_umode(const char *path);

// SYS_EXEC 的核心：當前 process 已在 U-mode、trap 進來換 image。
// 改 trap_frame 而不另起 sret；trap exit 自然 sret 到新 image entry。
// 回 -1 表示找不到 image；成功不回（sret 到 user 直接執行新 image）。
long do_exec(const char *path, struct trap_frame *tf);

#endif /* EXEC_H */
