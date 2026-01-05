# Makefile for x86 Real-Mode Bootloader (MSYS2 / Linux / macOS compatible)

# Tools
NASM    = nasm
QEMU    = qemu-system-i386
DD      = dd

QEMU_FLAGS = -d int,cpu_reset

# Paths
BOOT_DIR = boot
BUILD_DIR = .
DISK_IMG = $(BUILD_DIR)/disk.img
STAGE1_BIN = $(BUILD_DIR)/stage1.bin
STAGE2_BIN = $(BUILD_DIR)/stage2.bin

# Default target
all: $(DISK_IMG)

# Assemble stage1 (MBR)
$(STAGE1_BIN): $(BOOT_DIR)/stage1.asm
	$(NASM) -f bin $< -o $@

# Assemble stage2 (Bootloader)
$(STAGE2_BIN): $(BOOT_DIR)/stage2.asm $(BOOT_DIR)/filesys.asm
	$(NASM) -f bin $< -o $@

# Create 1.44MB floppy image and write MBR
$(DISK_IMG): $(STAGE1_BIN) $(STAGE2_BIN)
	$(DD) if=/dev/zero of=$@ bs=512 count=2880 2>/dev/null || \
		$(DD) if=/dev/zero of=$@ bs=1474560 count=1 2>/dev/null
	$(DD) if=$(STAGE1_BIN) of=$@ bs=512 conv=notrunc 2>/dev/null
	$(DD) if=$(STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null

# Run in QEMU
run: $(DISK_IMG)
	$(QEMU) -fda $(DISK_IMG)

debug: $(DISK_IMG)
	$(QEMU) -fda $(DISK_IMG) $(QEMU_FLAGS)

# Clean build files
clean:
	rm -f $(STAGE1_BIN) $(STAGE2_BIN) $(DISK_IMG)

# Rebuild and run
rebuild: clean all run

.PHONY: all run clean rebuild