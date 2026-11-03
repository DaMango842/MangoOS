#!/bin/bash
cd /d/Project/MangoOS
export PATH=/opt/cross/bin:/usr/bin:$PATH
for s in kernel32/kernel.c kernel32/vga.c kernel32/video.c kernel32/font16.c kernel32/serial.c kernel32/keyboard.c kernel32/shell.c kernel32/disk.c kernel32/mangofs.c kernel32/rtc.c kernel32/mouse.c; do
  b=$(basename ${s%.c})
  i686-elf-gcc -m32 -ffreestanding -fno-pic -fno-stack-protector -fno-builtin -O2 -Wall -Wextra -std=c11 -I kernel32 -c $s -o image/$b.o || echo "FAILED: $s"
done