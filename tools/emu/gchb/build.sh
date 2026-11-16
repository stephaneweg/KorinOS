#!/bin/sh
# build.sh -- les programmes GameCube a nous (tools/emu/gchb), sans libogc : gcc-powerpc-linux-gnu d'Ubuntu
# (sudo apt-get install gcc-powerpc-linux-gnu), en autonome, puis elf2dol.py. Dans WSL, depuis la racine du depot :
#     sh tools/emu/gchb/build.sh          -> build/emu/gchb/gxdemo.dol
set -eu
D=tools/emu/gchb
O=build/emu/gchb
CC=${CC:-powerpc-linux-gnu-gcc}
mkdir -p $O
F="-mcpu=750 -mbig -m32 -O2 -ffreestanding -fno-builtin -nostdlib -fno-pic -mno-sdata -msdata=none -fno-asynchronous-unwind-tables"
$CC $F -c $D/crt0.S -o $O/crt0.o
$CC $F -c $D/gxdemo.c -o $O/gxdemo.o
$CC $F -static -Wl,--build-id=none -T $D/gchb.ld $O/crt0.o $O/gxdemo.o -lgcc -o $O/gxdemo.elf
python3 -I $D/elf2dol.py $O/gxdemo.elf $O/gxdemo.dol
echo "$O/gxdemo.dol : $(wc -c < $O/gxdemo.dol) octets"
