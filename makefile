# Makefile for MangoOS (MSYS2 / Linux / macOS compatible).
#
# Sources are collected with wildcards, so adding a new kernel32/*.c,
# kernel32/proc/*.c or kernel32/fc/*.c file needs no makefile change.
# The object files are bundled into image/kernel32.a (a static archive)
# and linked with the two hand-written assembly objects.  gcc -MMD -MP
# writes dependency files (image/*.d) so a header change only
# recompiles the files that actually include it.  kernel32/fc/fc_rom.c
# is generated from the user-provided .nes in roms/ (git-ignored) by
# tools/fc/rom2c.py and rebuilt automatically when the ROM changes.
# FC sources are prefixed fc_ (fc_ppu.c, ...) so every object name is
# unique in the archive.

NASM    = nasm
QEMU    = qemu-system-i386

# i686-elf cross toolchain (MSYS2: opt/cross, pacman -S cross/binutils cross/gcc)
CC      = i686-elf-gcc
LD      = i686-elf-ld
AR      = i686-elf-ar
OBJCOPY = i686-elf-objcopy
PYTHON  = python

QEMU_FLAGS = -d int,out_asm

BOOT_DIR = boot
KERNEL32_DIR = kernel32
ROMS_DIR = roms
BUILD_DIR = image
TOOLS_DIR = tools
DISK_IMG = $(BUILD_DIR)/disk.img
STAGE1_BIN = $(BUILD_DIR)/stage1.bin
STAGE2_BIN = $(BUILD_DIR)/stage2.bin
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
K32_LIB = $(BUILD_DIR)/kernel32.a
ROM_C = $(KERNEL32_DIR)/fc/fc_rom.c
# ROM to embed: filename in roms/ (may contain spaces).  Set it to "" to
# pick the first .nes alphabetically.  Override per build for other carts,
# e.g.:
#   make ROM="AccuracyCoin.nes" run
#
# NOTE: fc_rom.c is a generated, git-ignored artefact that persists between
# builds, so it can disagree with the ROM named here (e.g. after
# `make ROM="AccuracyCoin.nes" run`).  A plain `make` always regenerates it
# from $(ROM), so any build resolves the mismatch.
ROM = Super Mario Bros. (W) [!].nes

INCLUDES = -I $(BOOT_DIR)

# C kernel build (32-bit protected mode, freestanding)
K32_ASM = $(KERNEL32_DIR)/entry.asm $(KERNEL32_DIR)/proc/switch.asm \
           $(KERNEL32_DIR)/syscall_entry.asm $(KERNEL32_DIR)/irq0.asm \
           $(KERNEL32_DIR)/irq1.asm $(KERNEL32_DIR)/exception_entry.asm
K32_CSRC = $(sort $(wildcard $(KERNEL32_DIR)/*.c) \
           $(wildcard $(KERNEL32_DIR)/proc/*.c) \
           $(wildcard $(KERNEL32_DIR)/fc/*.c) \
           $(wildcard $(KERNEL32_DIR)/sound/*.c) \
           $(wildcard $(KERNEL32_DIR)/drivers/audio/*.c) \
           $(ROM_C))
K32_OBJS = $(BUILD_DIR)/entry.o $(BUILD_DIR)/switch.o $(BUILD_DIR)/syscall_entry.o $(BUILD_DIR)/irq0.o $(BUILD_DIR)/irq1.o $(BUILD_DIR)/exception_entry.o \
            $(foreach src,$(K32_CSRC),$(BUILD_DIR)/$(notdir $(basename $(src))).o)
K32_DEPS = $(foreach src,$(K32_CSRC),$(BUILD_DIR)/$(notdir $(basename $(src))).d)
CFLAGS = -m32 -ffreestanding -fno-pic -fno-stack-protector -fno-builtin \
         -O2 -Wall -Wextra -std=c11 -I $(KERNEL32_DIR) -MMD -MP

all: $(DISK_IMG)

$(STAGE1_BIN): $(BOOT_DIR)/stage1.asm
	$(NASM) -f bin $< -o $@

$(STAGE2_BIN): $(BOOT_DIR)/stage2.asm $(BOOT_DIR)/printlib.asm $(BOOT_DIR)/filesys/disk.asm $(BOOT_DIR)/filesys/geometry.asm $(BOOT_DIR)/vbe.asm
	$(NASM) -f bin $< -o $@ -l $(BUILD_DIR)/stage2.lst

# --- per-file rules: an interrupted build only recompiles what is stale ---

$(BUILD_DIR)/entry.o: $(KERNEL32_DIR)/entry.asm
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/switch.o: $(KERNEL32_DIR)/proc/switch.asm
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/syscall_entry.o: $(KERNEL32_DIR)/syscall_entry.asm
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/irq0.o: $(KERNEL32_DIR)/irq0.asm
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/irq1.o: $(KERNEL32_DIR)/irq1.asm
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/exception_entry.o: $(KERNEL32_DIR)/exception_entry.asm
	$(NASM) -f elf32 $< -o $@

define C_RULE
$(BUILD_DIR)/$(notdir $(basename $(1))).o: $(1)
	$(CC) $(CFLAGS) -c $$< -o $$@
endef
$(foreach src,$(K32_CSRC),$(eval $(call C_RULE,$(src))))

# Bundle the kernel32 objects into one archive: the linker only needs
# one prerequisite and new files are picked up by the wildcard above.
$(K32_LIB): $(K32_OBJS)
	$(AR) rcs $@ $(K32_OBJS)

# rom.c is generated from the (git-ignored) ROM the user dropped into
# roms/. A FORCE rule regenerates it on every build: rom2c.py is fast,
# and it skips the write when the output is unchanged, so the .nes may
# contain spaces (which make cannot tokenize) and stale ROMs still
# rebuild the emulator quickly.
.PHONY: FORCE
FORCE:

$(ROM_C): $(TOOLS_DIR)/fc/rom2c.py FORCE
	$(PYTHON) $(TOOLS_DIR)/fc/rom2c.py "$(ROM)"

$(KERNEL_ELF): $(BUILD_DIR)/entry.o $(BUILD_DIR)/switch.o $(K32_LIB) $(KERNEL32_DIR)/linker.ld
	$(LD) -T $(KERNEL32_DIR)/linker.ld -nostdlib -o $@ $(BUILD_DIR)/entry.o $(BUILD_DIR)/switch.o $(K32_LIB)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $(KERNEL_ELF) $@
	@echo "Kernel: $(KERNEL_ELF) -> $@ ($$(stat -c %s $@) bytes)"

$(DISK_IMG): $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	powershell -NoProfile -File $(TOOLS_DIR)/build_img.ps1

# Serial goes to a TCP port instead of stdio: '-serial stdio' wires the
# terminal's stdin to the guest UART, and keystrokes typed while the
# terminal window has focus get swallowed there instead of reaching the
# guest PS/2 keyboard (shell could not receive any input).  Watch the
# debug output live with:  python tools/serialcat.py
SERIAL_URL = tcp:127.0.0.1:4632,server,nowait

run: $(DISK_IMG)
	$(QEMU) -hda $(DISK_IMG) -rtc base=localtime -boot c -serial stdio -m 16 -audiodev dsound,id=audio0 -machine pcspk-audiodev=audio0 -device sb16,audiodev=audio0

run-debug: $(DISK_IMG)
	$(QEMU) -hda $(DISK_IMG) -rtc base=localtime -boot c -serial $(SERIAL_URL) -m 16 -audiodev dsound,id=audio0 -machine pcspk-audiodev=audio0 -device sb16,audiodev=audio0 -no-reboot

debug: $(DISK_IMG)
	$(QEMU) -hda $(DISK_IMG) $(QEMU_FLAGS) -audiodev dsound,id=audio0 -machine pcspk-audiodev=audio0 -device sb16,audiodev=audio0

# Convenience wrappers: switch carts without retyping the ROM name.
# Any other cart still works the same way:
#   make ROM="<name>.nes" run
run-accuracy:
	$(MAKE) ROM="AccuracyCoin.nes" run

run-smb:
	$(MAKE) ROM="Super Mario Bros. (W) [!].nes" run

clean:
	rm -f $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(KERNEL_ELF) $(K32_LIB) $(K32_OBJS) $(K32_DEPS) $(DISK_IMG) $(BUILD_DIR)/stage2.lst $(wildcard $(BUILD_DIR)/*.o) $(wildcard $(BUILD_DIR)/*.d)

rebuild: clean all

# include auto-generated dependency files (recreated by -MMD on compile)
-include $(K32_DEPS)

.PHONY: all run clean debug rebuild run-accuracy run-smb