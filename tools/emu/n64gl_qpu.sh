#!/bin/sh
# n64gl_qpu : construit le banc (PC, WSL) avec qpusim d'Onyx et l'execute sur build/emu/n64gl_qpu.h (n64gl_qpu.py).
#     sh tools/emu/n64gl_qpu.sh          (depuis la racine du depot)
set -eu
. "$(dirname "$0")/../mesa/env.sh"
Q=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx}/tools/qpu
G=$OUT/n64gl_qpu
mkdir -p "$G"
for f in mesa/broadcom/qpu/qpu_instr.c mesa/broadcom/qpu/qpu_pack.c mesa/broadcom/qpu/qpu_disasm.c ralloc_stub.c; do
	gcc -O1 -std=gnu11 -w -I"$Q/mesa" -c "$Q/$f" -o "$G/$(basename "$f" .c).o"
done
g++ -O1 -std=c++20 -w -I"$Q" -I"$Q/mesa" -I"$ARMPI_ROOT/build/emu" \
	"$ARMPI_ROOT/tools/emu/n64gl_qpu.cpp" "$Q/qpusim.cpp" "$G"/*.o -lm -o "$G/n64gl_qpu"
"$G/n64gl_qpu"
