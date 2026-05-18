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

run: CFLAGS += -DQEMU
run: build
	$(QEMU) -M virt -m 256M -kernel $(TARGET) -nographic
	# 退出 QEMU：Ctrl-A X

build_board: LDSCRIPT = link_board.ld
build_board: ITSFILE  = bootloader_board.its
build_board: clean $(TARGET).fit

clean:
	rm -f $(TARGET) $(TARGET).fit
	rm -rf $(BUILD_DIR)

.PHONY: build build_board clean run
