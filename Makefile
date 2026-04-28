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
	$(QEMU) -M virt -m 256M -kernel $(TARGET) -initrd initramfs.cpio -display none -serial stdio -device ramfb
	# stdio 接 UART：手動 type 應該 char-by-char 進 shell；
	# 如果 host terminal 在 cooked mode 沒看到 echo，按 Enter 仍會送整行進 QEMU。
	# pipe 測試（stdin 來自 echo/printf）建議 (sleep 2; printf "...") | make run，
	# 否則 boot 早期 RX FIFO 還沒就緒會吃掉前幾個 char。
	# 退出 QEMU：Ctrl-A X

# Cleanup Rule
clean:
	rm -f $(TARGET) $(TARGET).fit
	rm -rf $(BUILD_DIR)

.PHONY: build clean run
