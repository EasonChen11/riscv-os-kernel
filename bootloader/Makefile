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



# CPIO_ROOT：解包後的暫存目錄，預設放在 INITRD 旁邊
CPIO_ROOT ?= $(DIR)/cpio_root

cpio:
	cpio -o -H newc < $(DIR)/cpio_files > $(INITRD)

# 將 INITRD 解包到 CPIO_ROOT，方便手動放入新檔案
cpio-unpack:
	mkdir -p $(CPIO_ROOT)
	cd $(CPIO_ROOT) && cpio -idm --no-absolute-filenames < $(INITRD)

# 將 CPIO_ROOT 重新打包回 INITRD，完成後自動清除暫存目錄
cpio-pack:
	cd $(CPIO_ROOT) && find . | cpio -o -H newc > $(INITRD)
	$(MAKE) cpio-clean

# 清除暫存目錄
cpio-clean:
	rm -rf $(CPIO_ROOT)

load:
	sudo python3 send.py $(PTS) $(KERNEL)

qemu_load:
	sudo python3 send.py $(qemu_PTS) $(KERNEL)

clean:
	rm -f $(TARGET) $(TARGET).fit bootloader_board.its
	rm -rf $(BUILD_DIR)

help:
	@echo "Usage: make [target] [PARAM=value ...]"
	@echo ""
	@echo "Build targets:"
	@echo "  build          板子版 .fit（預設）"
	@echo "  run            QEMU pty 模式（另開 terminal 用 watch/load）"
	@echo "  run_stdio      QEMU stdio 模式，僅觀察輸出（無法上傳）"
	@echo "  clean          清除 build/ 及產出的 .fit / .its"
	@echo ""
	@echo "Upload targets:"
	@echo "  load           透過 send.py 上傳 kernel 到板子"
	@echo "  qemu_load      透過 send.py 上傳 kernel 到 QEMU pty"
	@echo "  watch          用 screen 監看 UART 輸出（退出：Ctrl-A k）"
	@echo ""
	@echo "CPIO targets:"
	@echo "  cpio           從 \$$(DIR)/cpio_files 清單打包 initramfs.cpio"
	@echo "  cpio-unpack    解包 initramfs.cpio 到 \$$(CPIO_ROOT)"
	@echo "  cpio-pack      從 \$$(CPIO_ROOT) 重新打包，完成後自動 cpio-clean"
	@echo "  cpio-clean     刪除 \$$(CPIO_ROOT) 暫存目錄"
	@echo ""
	@echo "Parameters (current values):"
	@echo "  DIR       = $(DIR)"
	@echo "  KERNEL    = $(KERNEL)"
	@echo "  INITRD    = $(INITRD)"
	@echo "  CPIO_ROOT = $(CPIO_ROOT)"
	@echo "  PTS       = $(PTS)"
	@echo "  qemu_PTS  = $(qemu_PTS)"

.PHONY: build clean run run_stdio watch load cpio cpio-unpack cpio-pack cpio-clean help
