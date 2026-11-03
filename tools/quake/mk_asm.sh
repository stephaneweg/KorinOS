#!/bin/sh
# Les routines A64 par FASMARM, l'exe du depot lance depuis WSL -> build/quake/quake_a64.o (relocations corrigees).
# Depuis la racine du depot.
set -eu
A=src/apps/quake/asm
mkdir -p build/quake
if [ ! -f build/quake/quake_a64.o ] || [ -n "$(find $A -newer build/quake/quake_a64.o)" ]; then
	rm -f build/quake/quake_a64.o
	(cd $A && ../../../../tools/fasmarm/fasmarm.exe quake_a64.asm ../../../../build/quake/quake_a64.o) > build/quake/fasm.log 2>&1 || { tail -5 build/quake/fasm.log; exit 1; }
	python3 -I tools/quake/fixreloc.py build/quake/quake_a64.o
fi
