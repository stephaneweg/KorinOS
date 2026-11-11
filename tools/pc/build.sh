#!/bin/sh
# Construit Korin OS pour PC x86-64 (docs/etude/ETUDE-X86.md), dans WSL :
#   build/pc/esp/EFI/BOOT/BOOTX64.EFI   chargeur UEFI (FASM)
#   build/pc/esp/korin/kernel.elf       noyau (FASM pour l'asm x86, gcc pour le C)
# Le volume build/pc/esp se copie tel quel sur une carte SD ou une clé FAT32.
set -e
cd "$(dirname "$0")/../.."

OUT=build/pc
OBJ=$OUT/obj
ESP=$OUT/esp
FASM=ref/fasm/FASM.EXE          # FASM 1.73 de Windows, appelé par l'interop de WSL
CC=${CC:-gcc}
LD=${LD:-ld}

mkdir -p "$OBJ" "$ESP/EFI/BOOT" "$ESP/korin"

fasm() {
	"$FASM" "$1" "$2" >"$OBJ/fasm.log" 2>&1 || { cat "$OBJ/fasm.log"; exit 1; }
}

fasm src/kernel/arch/pc/boot/bootx64.asm "$ESP/EFI/BOOT/BOOTX64.EFI"

ASM="src/kernel/arch/x86_64/entry.asm src/kernel/arch/x86_64/isr.asm src/kernel/arch/pc/font.asm"
C="src/kernel/core/console.c src/kernel/core/lib.c src/kernel/core/pmm.c src/kernel/core/monitor.c
src/kernel/arch/x86_64/gdt.c src/kernel/arch/x86_64/idt.c src/kernel/arch/x86_64/paging.c
src/kernel/arch/pc/acpi.c src/kernel/arch/pc/apic.c src/kernel/arch/pc/ps2.c src/kernel/arch/pc/pc_main.c"

CFLAGS="-std=gnu11 -O2 -g0 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pic -fno-pie
-mcmodel=kernel -mno-red-zone -mgeneral-regs-only -fno-asynchronous-unwind-tables
-fno-tree-loop-distribute-patterns -fno-omit-frame-pointer -Isrc/kernel/include"

OBJS=""
for f in $ASM; do
	o="$OBJ/$(basename "$f" .asm).asm.o"
	fasm "$f" "$o"
	OBJS="$OBJS $o"
done
for f in $C; do
	o="$OBJ/$(basename "$f" .c).o"
	$CC $CFLAGS -c "$f" -o "$o"
	OBJS="$OBJS $o"
done

$LD -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -z noexecstack \
	--no-warn-rwx-segments -T src/kernel/arch/pc/kernel.ld -o "$ESP/korin/kernel.elf" $OBJS

ls -l "$ESP/EFI/BOOT/BOOTX64.EFI" "$ESP/korin/kernel.elf"
