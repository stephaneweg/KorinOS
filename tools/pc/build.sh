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

# numéros des appels système : tirés de src/inc/syscalls.inc (seule source)
GEN=$OUT/gen/korin
mkdir -p "$GEN"
{
	echo "/* Tiré de src/inc/syscalls.inc par tools/pc/build.sh : ne pas modifier. */"
	echo "#ifndef KORIN_SYSCALL_NR_H"
	echo "#define KORIN_SYSCALL_NR_H"
	sed -n 's/^\(SYS_[A-Z0-9_]*\)[[:space:]]*=[[:space:]]*\(0x[0-9A-Fa-f]*\).*/#define \1 \2/p' src/inc/syscalls.inc
	echo "#endif"
} >"$GEN/syscall_nr.h"

ASM="src/kernel/arch/x86_64/entry.asm src/kernel/arch/x86_64/isr.asm src/kernel/arch/x86_64/switch.asm
src/kernel/arch/pc/font.asm"
C="src/kernel/core/console.c src/kernel/core/lib.c src/kernel/core/pmm.c src/kernel/core/kheap.c
src/kernel/core/sched.c src/kernel/core/proc.c src/kernel/core/thread.c src/kernel/core/mm.c
src/kernel/core/svc.c src/kernel/core/hw.c src/kernel/core/sys.c src/kernel/core/signal.c src/kernel/core/ipc.c
src/kernel/core/image.c src/kernel/core/kits.c
src/kernel/core/elf.c src/kernel/core/initrd.c src/kernel/core/syscall.c
src/kernel/core/monitor.c
src/kernel/arch/x86_64/gdt.c src/kernel/arch/x86_64/idt.c src/kernel/arch/x86_64/paging.c
src/kernel/arch/x86_64/cpu.c src/kernel/arch/x86_64/vm.c src/kernel/arch/x86_64/sigframe.c
src/kernel/arch/pc/acpi.c src/kernel/arch/pc/apic.c src/kernel/arch/pc/ps2.c src/kernel/arch/pc/smp.c
src/kernel/arch/pc/pc_main.c"

CFLAGS="-std=gnu11 -O2 -g0 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pic -fno-pie
-mcmodel=kernel -mno-red-zone -mgeneral-regs-only -fno-asynchronous-unwind-tables
-fno-tree-loop-distribute-patterns -fno-omit-frame-pointer -Isrc/kernel/include -I$OUT/gen"

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

# trampoline des autres cœurs : binaire plat, mis en objet (symboles
# _binary_smp_trampoline_bin_start / _end)
fasm src/kernel/arch/pc/smp_trampoline.asm "$OBJ/smp_trampoline.bin"
SECT=.data=.rodata,alloc,load,readonly,data,contents
(cd "$OBJ" && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 --rename-section $SECT smp_trampoline.bin smp_trampoline.o)
OBJS="$OBJS $OBJ/smp_trampoline.o"

$LD -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 -z noexecstack \
	--no-warn-rwx-segments -T src/kernel/arch/pc/kernel.ld -o "$ESP/korin/kernel.elf" $OBJS

# initrd : les programmes de src/kernel/arch/pc/initrd, en archive ustar
INITRD=$OUT/initrd
rm -rf "$INITRD"
mkdir -p "$INITRD"
NAMES=""
for f in src/kernel/arch/pc/initrd/*.asm; do
	n=$(basename "$f" .asm)
	fasm "$f" "$INITRD/$n"
	NAMES="$NAMES $n"
done
# programmes d'essai en C, sans bibliothèque (syscall direct)
UCFLAGS="-std=gnu11 -O2 -ffreestanding -nostdlib -static -fno-pie -no-pie -fno-stack-protector
-fno-asynchronous-unwind-tables -Wall -Wextra -Werror -I$OUT/gen -Wl,-Ttext-segment=0x400000 -Wl,-e,_start
-Wl,-z,noexecstack"
for f in src/kernel/arch/pc/initrd/*.c; do
	n=$(basename "$f" .c)
	$CC $UCFLAGS "$f" -o "$INITRD/$n"
	NAMES="$NAMES $n"
done
tar --format=ustar -C "$INITRD" -cf "$ESP/korin/initrd.tar" $NAMES

ls -l "$ESP/EFI/BOOT/BOOTX64.EFI" "$ESP/korin/kernel.elf" "$ESP/korin/initrd.tar"
