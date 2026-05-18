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

LDSCRIPT ?= link.ld
ITSFILE  ?= bootloader.its

build: clean $(TARGET).fit

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

# run: 用 pty 模式跑 QEMU，方便另一個 terminal 用 send.py 上傳 kernel。
#      QEMU 啟動時會印 "char device redirected to /dev/pts/N"，
#      接著另開一個 terminal：
#        make watch PTS=/dev/pts/N                       # screen 看 UART 輸出
#        make load  PTS=/dev/pts/N KERNEL=../lab6/kernel # sudo 跑 send.py
#      退出 QEMU：另開 terminal `pkill -f qemu-system-riscv64`
PTS     ?= /dev/pts/4
KERNEL  ?= ../lab6/kernel
INITRD  ?= ../lab6/initramfs.cpio

run: CFLAGS += -DQEMU
run: build
	@echo "──────────────────────────────────────────────────────"
	@echo "  QEMU starting in pty mode. Look for /dev/pts/N below."
	@echo "  Watch UART:  make watch PTS=/dev/pts/N"
	@echo "  Upload:      make load  PTS=/dev/pts/N KERNEL=../lab6/kernel"
	@echo "──────────────────────────────────────────────────────"
	$(QEMU) -M virt -m 256M -kernel $(TARGET) -initrd $(INITRD) -display none -serial pty

# 互動模式（stdio 直連 UART）：只觀察 bootloader 輸出時用；無法 binary 上傳
run_stdio: CFLAGS += -DQEMU
run_stdio: build
	$(QEMU) -M virt -m 256M -kernel $(TARGET) -initrd $(INITRD) -nographic
	# 退出 QEMU：Ctrl-A X

watch:
	@echo "watching $(PTS); 退出：Ctrl-A k"
	screen $(PTS)

load:
	sudo python3 send.py $(PTS) $(KERNEL)

build_board: LDSCRIPT = link_board.ld
build_board: ITSFILE  = bootloader_board.its
build_board: clean $(TARGET).fit

clean:
	rm -f $(TARGET) $(TARGET).fit
	rm -rf $(BUILD_DIR)

.PHONY: build build_board clean run run_stdio watch load
