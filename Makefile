# Toolchain Definitions
RISCV_GNU ?= riscv64-unknown-elf
CC       = $(RISCV_GNU)-gcc
LD       = $(RISCV_GNU)-ld
OBJCOPY  = $(RISCV_GNU)-objcopy
MKIMAGE  = mkimage
QEMU     ?= qemu-system-riscv64

# Directory Definitions
SRC_DIR   = src
BUILD_DIR = build
INC_DIR   = include

# 編譯器旗標
# -march=rv64gc_zifencei  選擇 RISC-V 64-bit，啟用 G(IMAFD)+C 擴展及 Zifencei 擴展，
#   讓 assembler 認識 fence.i 指令（用於寫入指令後清除 I-cache）。
# -mcmodel=medany  使用 PC-relative 定址（±2GB），允許 kernel 在記憶體任意位置執行。
# -fno-pie         不生成位置無關執行檔（PIE），bare-metal kernel 不需要動態連結器的重定位。
# -ffreestanding   告訴編譯器這不是標準環境，不假設有 main()、標準函式庫函數或 startup code。
# -nostdlib        不連結標準函式庫（libc、libgcc 等），所有函式自己實作。
# -g               啟用偵錯符號，方便 GDB 除錯。
# -Wall            啟用所有編譯器警告，提升程式碼品質。
# -I$(INC_DIR)     指定標頭檔搜尋路徑，讓 #include "xxx.h" 能找到 include/ 下的檔案。

CFLAGS   = -march=rv64gc_zifencei -mcmodel=medany -fno-pie -ffreestanding -nostdlib -g -Wall -I$(INC_DIR)
CFLAGS  += -Wno-unused-variable -Wno-unused-function # 忽略未使用的變數和函式警告，因為 kernel 可能有一些暫時未使用的程式碼。
TARGET   = kernel

# Source File Discovery
C_FILES   = $(wildcard $(SRC_DIR)/*.c)
ASM_FILES = $(wildcard $(SRC_DIR)/*.S)

# Object File Mapping
OBJ_FILES = $(C_FILES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%_c.o)
OBJ_FILES += $(ASM_FILES:$(SRC_DIR)/%.S=$(BUILD_DIR)/%_s.o)

# Main Build Target
build: clean $(TARGET).fit

# Linker Script (default: board, overridden by run target for QEMU)
LDSCRIPT ?= link.ld

# Linking
$(TARGET).elf: $(OBJ_FILES)
	@mkdir -p $(BUILD_DIR)
	$(LD) -T $(LDSCRIPT) -o $(BUILD_DIR)/$(TARGET).elf $(OBJ_FILES)

# Image Packaging: ELF -> Binary -> FIT
$(TARGET).fit: $(TARGET).elf
	$(OBJCOPY) -O binary $(BUILD_DIR)/$(TARGET).elf $(TARGET)
	$(MKIMAGE) -f $(TARGET).its $(TARGET).fit

# Compilation Rule for C files
$(BUILD_DIR)/%_c.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# Compilation Rule for Assembly files
$(BUILD_DIR)/%_s.o: $(SRC_DIR)/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# Run Rule (QEMU Simulation)
run: CFLAGS += -DQEMU
run: LDSCRIPT = qemu.ld
run: build
	$(QEMU) -M virt -m 256M -kernel $(TARGET) -initrd initramfs.cpio -vnc :0 -serial stdio -device ramfb
	# stdio 接 UART：手動 type 應該 char-by-char 進 shell；
	# 如果 host terminal 在 cooked mode 沒看到 echo，按 Enter 仍會送整行進 QEMU。
	# pipe 測試（stdin 來自 echo/printf）建議 (sleep 2; printf "...") | make run，
	# 否則 boot 早期 RX FIFO 還沒就緒會吃掉前幾個 char。
	# 退出 QEMU：Ctrl-A X

# ─── User Program (osctest.bin) ────────────────────────────────────────
# 跑在 U-mode 的測試程式。kernel shell 用 `exec osctest.bin` 跳過去。
# 跟 kernel 不共用 source 也不共用 link.ld：user binary 是 raw flat layout，
# 所有 section 從 0 排，objcopy -O binary 後直接被 exec_umode 載入執行。
USER_DIR     = user
USER_BUILD   = $(USER_DIR)/build
USER_BIN     = osctest.bin
USER_ELF     = $(USER_BUILD)/osctest.elf
USER_LD      = $(USER_DIR)/link.ld

USER_C_SRCS   = $(wildcard $(USER_DIR)/*.c)
USER_S_SRCS   = $(wildcard $(USER_DIR)/*.S)
USER_OBJS     = $(USER_C_SRCS:$(USER_DIR)/%.c=$(USER_BUILD)/%_c.o)
USER_OBJS    += $(USER_S_SRCS:$(USER_DIR)/%.S=$(USER_BUILD)/%_s.o)

# 跟 kernel 共用 CFLAGS（rv64gc / freestanding / nostdlib），但 user binary 用 PIE：
# user binary 由 cpio 載入，runtime base address 不固定（隨 cpio entry offset），
# absolute address 在 -Ttext=0 + medany 下會被 ld lower 成「binary offset」
# （hardcoded immediate），跑時 base ≠ 0 直接 load fault。
# -fpie 生成 PC-relative reference (auipc + addi)，runtime base 不影響。
USER_CFLAGS  = -march=rv64gc_zifencei -mcmodel=medany -fpie -fomit-frame-pointer -ffreestanding -nostdlib -g -Wall -I$(USER_DIR)
# -fomit-frame-pointer：讓 local var (&cnt) 用 sp + offset 算。預設 -O0 用 s0
# 當 frame pointer，s0 是 callee-saved 不被 syscall 改 / trap_frame 又會複製
# parent 的 s0 → fork 後 child 的 &cnt 變成 parent ustack 位址。do_fork 只調
# trap_frame.sp，沒法調 s0。omit frame pointer 後 &cnt 跟 sp 同步，符合 lab5
# fork_test reference 行為。

usrprg: $(USER_BIN)

$(USER_BUILD)/%_c.o: $(USER_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_BUILD)/%_s.o: $(USER_DIR)/%.S
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_ELF): $(USER_OBJS) $(USER_LD)
	$(LD) -T $(USER_LD) --no-relax -o $@ $(USER_OBJS)
	# --no-relax：阻止 ld 把 auipc+addi 的 PC-relative reference 折成 absolute
	# immediate（在 -Ttext=0 下 image base=0 = link-time PC，relax 後失去 PIE 性質）

$(USER_BIN): $(USER_ELF)
	$(OBJCOPY) -O binary $< $@

# ─── initramfs 重打 ────────────────────────────────────────────────────
# 從現有 initramfs.cpio 解壓到 $(USER_BUILD)/initrd_root，加上剛 build 的
# osctest.bin，再 cpio -o 重打回 initramfs.cpio。
# 這樣保留 lab4 留下的 prog.bin（沒 source 在 repo），又把新 user binary 進去。
INITRD_ROOT  = $(USER_BUILD)/initrd_root

cpio: $(USER_BIN)
	@rm -rf $(INITRD_ROOT)
	@mkdir -p $(INITRD_ROOT)
	@cd $(INITRD_ROOT) && cpio -id < ../../../initramfs.cpio 2>/dev/null
	@cp $(USER_BIN) $(INITRD_ROOT)/$(USER_BIN)
	@cd $(INITRD_ROOT) && find . -type f | cpio -H newc -o > ../../../initramfs.cpio 2>/dev/null
	@echo "initramfs.cpio updated. Contents:"
	@cpio -t < initramfs.cpio 2>/dev/null

# Cleanup Rule
clean:
	rm -f $(TARGET) $(TARGET).fit $(USER_BIN)
	rm -rf $(BUILD_DIR) $(USER_BUILD)

.PHONY: build clean run usrprg cpio
