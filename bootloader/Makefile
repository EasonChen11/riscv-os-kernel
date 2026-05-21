RISCV_GNU ?= riscv64-unknown-elf
CC       = $(RISCV_GNU)-gcc
LD       = $(RISCV_GNU)-ld
OBJCOPY  = $(RISCV_GNU)-objcopy
MKIMAGE  = mkimage
QEMU     ?= qemu-system-riscv64

SRC_DIR   = src
BUILD_DIR = build
INC_DIR   = include

CFLAGS   = -march=rv64gc_zifencei -mcmodel=medany -fno-pie -ffreestanding -nostdlib -g -Wall -I$(INC_DIR)
CFLAGS  += -Wno-unused-variable -Wno-unused-function
TARGET   = bootloader

C_FILES   = $(wildcard $(SRC_DIR)/*.c)
ASM_FILES = $(wildcard $(SRC_DIR)/*.S)
OBJ_FILES  = $(C_FILES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%_c.o)
OBJ_FILES += $(ASM_FILES:$(SRC_DIR)/%.S=$(BUILD_DIR)/%_s.o)

# 預設目標：板子版（link_board.ld + 內嵌 cpio 的 bootloader_board.its）
# QEMU 版透過 `make run` 走 override；參考 lab6 的 Makefile 結構
LDSCRIPT ?= link_board.ld
ITSFILE  ?= bootloader_board.its

# DIR：要包入 cpio 的 lab 資料夾，預設 ../lab6
# KERNEL / INITRD 從 DIR 衍生，也可單獨 override
DIR    ?= ../lab6
KERNEL ?= $(DIR)/kernel
INITRD ?= $(DIR)/initramfs.cpio
PTS    ?= /dev/ttyUSB0
qemu_PTS ?= /dev/pts/4

# build (預設) = 板子版：產生 bootloader_board.its（從 .in template）後 mkimage
build: clean bootloader_board.its $(TARGET).fit

$(TARGET).elf: $(OBJ_FILES)
	@mkdir -p $(BUILD_DIR)
	$(LD) -T $(LDSCRIPT) --no-warn-rwx-segments -o $(BUILD_DIR)/$(TARGET).elf $(OBJ_FILES)

$(TARGET).fit: $(TARGET).elf
	$(OBJCOPY) -O binary $(BUILD_DIR)/$(TARGET).elf $(TARGET)
	$(MKIMAGE) -f $(ITSFILE) $(TARGET).fit

$(BUILD_DIR)/%_c.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%_s.o: $(SRC_DIR)/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# 由 .in template 替換 @INITRD@ 產生最終 .its
bootloader_board.its: bootloader_board.its.in
	sed 's|@INITRD@|$(INITRD)|g' $< > $@

# run = QEMU 版：override LDSCRIPT / ITSFILE，加 -DQEMU
# QEMU 啟動時會印 "char device redirected to /dev/pts/N"，
# 接著另開 terminal：
#   make watch PTS=/dev/pts/N             # screen 看 UART 輸出
#   make load  PTS=/dev/pts/N DIR=../lab6 # sudo 跑 send.py
# 切 lab：DIR=../labN（kernel / cpio 兩者都從 $(DIR) 取）
# 退出 QEMU：另開 terminal `pkill -f qemu-system-riscv64`
run: LDSCRIPT = link.ld
run: ITSFILE  = bootloader_qemu.its
run: CFLAGS  += -DQEMU
run: clean $(TARGET).fit
	@echo "──────────────────────────────────────────────────────"
	@echo "  QEMU starting in pty mode. Look for /dev/pts/N below."
	@echo "  Watch UART:  make watch PTS=/dev/pts/N"
	@echo "  Upload:      make load  PTS=/dev/pts/N DIR=../lab6"
	@echo "──────────────────────────────────────────────────────"
	$(QEMU) -M virt -m 256M -kernel $(TARGET) -initrd $(INITRD) -display none -serial pty

# run_stdio：互動 stdio 模式，只觀察輸出時用（無法 binary 上傳）
run_stdio: LDSCRIPT = link.ld
run_stdio: ITSFILE  = bootloader_qemu.its
run_stdio: CFLAGS  += -DQEMU
run_stdio: clean $(TARGET).fit
	$(QEMU) -M virt -m 256M -kernel $(TARGET) -initrd $(INITRD) -nographic
	# 退出 QEMU：Ctrl-A X

watch:
	@echo "watching $(PTS); exit: Ctrl-A k"
	screen $(PTS)

load:
	sudo python3 send.py $(PTS) $(KERNEL)

qemu_load:
	sudo python3 send.py $(qemu_PTS) $(KERNEL)

clean:
	rm -f $(TARGET) $(TARGET).fit bootloader_board.its
	rm -rf $(BUILD_DIR)

.PHONY: build clean run run_stdio watch load
