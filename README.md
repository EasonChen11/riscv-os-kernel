# riscv-os-kernel

A small RISC-V 64-bit kernel in C and assembly that runs on the OrangePi RV2 (SpacemiT K1) and on QEMU's `virt` machine.

## Features

The C and assembly sources under `src/` total about 4,600 lines. Every item below was checked against the source file named in parentheses.

- Boot with a flattened device tree: the kernel parses the DTB for the UART, PLIC, memory size, initrd and `/reserved-memory` (`src/fdt.c`, `src/bootinfo.c`, `src/mem_reserve.c`).
- Sv39 virtual memory: the kernel runs in the upper half at `0xffffffc000000000` and uses a linear map of RAM. MMIO regions are mapped with 4 KiB pages after the DTB is parsed (`src/vm.c`, `include/pgtable.h`).
- Physical memory: a buddy allocator with orders up to 16 and per-frame reference counts, and a pool allocator for small objects up to 2048 bytes (`src/buddy.c`, `src/dynalloc.c`).
- Interrupts and timers: trap handling in `src/trap.S` and `src/trap.c`, the PLIC (`src/plic.c`), a sorted one-shot timer queue that uses the SBI timer call (`src/timer.c`, `src/sbi.c`), and a priority-ordered deferred task queue that runs at the end of trap handling (`src/task.c`).
- UART driver for 16550 and DW-style registers with an interrupt-driven RX/TX ring buffer (`src/uart.c`).
- Threads and scheduling: kernel threads, a preemptive round-robin scheduler with a 1/32 second tick, and zombie reaping in the idle loop (`src/thread.c`, `src/thread.S`, `src/schedule.c`).
- User processes: `fork` with copy-on-write pages, `exec` of flat binaries loaded at virtual address 0 from the initramfs, `waitpid`, `exit`, `usleep` (`src/proc.c`, `src/exec.c`, `src/syscall.c`). The user stack grows on demand.
- `mmap` of anonymous memory with demand paging through the page fault handler (`src/mm.c`).
- POSIX-style signals: `signal`, `kill` and `sigreturn` with a trampoline page mapped into the process (`src/signal.c`, `src/trampoline.S`).
- Virtual file system with `open`, `close`, `read`, `write`, `lseek64`, `ioctl`, `mkdir`, `mount` and `chdir`. A tmpfs is the root, a read-only ramfs built from the initramfs cpio is mounted at `/ramfs`, and `/dev/uart` and `/dev/fb` are device files (`src/vfs.c`, `src/tmpfs.c`, `src/ramfs.c`, `src/dev_uart.c`, `src/dev_fb.c`).
- initramfs: a parser for the newc cpio format (`src/cpio.c`).
- Framebuffer output: 1920x1080 XRGB8888. On QEMU the kernel registers a `ramfb` device through `fw_cfg`; on the board it writes to the framebuffer set up by U-Boot (`src/video.c`).
- A serial shell with the commands `help`, `hello`, `info`, `ls`, `cat`, `exec` and `setTimeout` (`src/shell.c`).

The repository does not ship any user programs. `exec` needs a flat binary that is linked to run at address 0 and uses the syscall numbers in `include/syscall.h`; put it in `rootfs/` to run it.

## Boot flow

OpenSBI runs in M-mode and jumps to the next stage in S-mode with the hart id in `a0` and the DTB address in `a1`. The next stage is either the kernel directly (QEMU `-kernel`, or the FIT image on the board) or the bootloader.

The bootloader in `bootloader/` is a small program for fast development on the board. It does the following:

1. Parses the DTB and picks a 2 MiB aligned address near the top of RAM that does not overlap the DTB, the initrd or `/reserved-memory` (`bootloader/src/bootloader.c`).
2. Copies itself to that address and continues from there, so that the load address of the kernel stays free (`bootloader/src/start.S`).
3. Waits on the UART for an 8-byte header made of the magic `BOOT` and a 32-bit little-endian size, then reads that many bytes into the kernel load address (`0x200000` on the board, `0x80200000` on QEMU).
4. Executes `fence.i` and jumps to the kernel with the hart id and DTB pointer.

`tools/send.py` sends the header and the kernel image over the serial port.

The kernel entry point is `src/start.S`. It clears `.bss`, builds the Sv39 page tables, jumps to the upper-half addresses, removes the identity map, installs the trap vector and calls `start_kernel` in `src/main.c`. That function initializes the subsystems in the order listed in its comments and finally starts the shell thread.

## Build and run

Toolchain:

```
sudo apt install gcc-riscv64-unknown-elf qemu-system-misc u-boot-tools
```

The `initramfs.cpio` target also uses `cpio`, which is installed on most systems. Tested with Ubuntu 24.04 / GCC 13.2.0.

QEMU:

```
make qemu
```

This builds the kernel with the QEMU linker script, packs `rootfs/` into `initramfs.cpio` and starts `qemu-system-riscv64`. The shell is on the terminal (`-serial stdio`) and the framebuffer is on VNC display `:0`.

OrangePi RV2:

```
make
```

This produces `kernel.fit`, a U-Boot FIT image that contains the kernel, `x1_orangepi-rv2.dtb` and `initramfs.cpio`. Write it to the SD card with `tools/copyfit.sh kernel.fit`. The script expects an SD card partition labeled `opirv2` and copies the image to it as `kernel.fit`.

To iterate without touching the SD card, build the bootloader once and send kernels over the serial port:

```
make -C bootloader                                   # produces bootloader/bootloader.fit
python3 tools/send.py /dev/ttyUSB0 kernel            # after the bootloader is waiting on the UART
```

Copy `bootloader/bootloader.fit` to the SD card in the same way as `kernel.fit`. For QEMU, `make -C bootloader run` starts the bootloader on a pseudo-terminal and prints the `/dev/pts/N` device to pass to `tools/send.py`. Build the kernel with `make qemu` first so that `kernel` is the QEMU variant.

Changing the contents of `rootfs/` is enough to change what `ls` and `cat` show in the shell. File names must be at most 15 characters and the directory must be flat.

## Repository layout

```
src/, include/   kernel sources
link.ld          linker script for the board
qemu.ld          linker script for QEMU
kernel.its       FIT image description for the board
rootfs/          files packed into initramfs.cpio
bootloader/      UART bootloader (its own Makefile and linker scripts)
tools/           send.py (UART upload), copyfit.sh (SD card copy)
x1_orangepi-rv2.dtb   device tree for the board, from the vendor
```

Source comments are written in Traditional Chinese.

Developed as part of a graduate operating systems course.
