#!/bin/sh
# Ce que des ROM mettent a l'epreuve dans l'interprete de reference : les lignes de n64_cpu.cpp et n64_bus.cpp
# jamais executees (WSL, racine du depot) :  sh tools/emu/cover_n64.sh images rom...
R=$(pwd)
W=build/emu/cov_n64
N=$1
shift
S=$R/tools/emu/n64ref
mkdir -p $W
cd $W
rm -f *.gcda
[ -x ref ] || aarch64-linux-gnu-g++ -static -O0 -w -std=gnu++17 -ffp-contract=off --coverage -Dprivate=public -I$S $R/tools/emu/ref_n64.cpp $S/n64/*.cpp -o ref || exit 1
for r in "$@"; do qemu-aarch64 ./ref "$R/$r" $N > /dev/null 2>&1; done
aarch64-linux-gnu-gcov -n ref-n64_cpu.gcda ref-n64_bus.gcda 2>/dev/null | grep -A1 "n64_cpu.cpp\|n64_bus.cpp"
aarch64-linux-gnu-gcov ref-n64_cpu.gcda ref-n64_bus.gcda > /dev/null 2>&1
grep -h '#####' n64_cpu.cpp.gcov n64_bus.cpp.gcov | head -${MAXL:-80}
