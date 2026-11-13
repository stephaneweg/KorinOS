# run2.sh : la fin de run.sh seulement (fasm se réassemble, pibuild)
T=/tmp/t
E=/gcc/test
mkdir -p $T
cd $T
/gcc/bin/gcc -O2 $E/pc/timeit.c -o $T/timeit
echo "=== fasm : fasm se réassemble (objet ELF64)"
cd /gcc/share/fasm/source
$T/timeit /bin/fasm korin.asm $T/fasm.o
cd $T
ls -l $T/fasm.o
echo "=== pibuild bin (C) et pibuild bin (asm, fasm)"
mkdir -p $T/hc $T/hasm
cp $E/hello.c $T/hc/main.c
cp $E/fasm/hello.asm $T/hasm/hasm.asm
$T/timeit /bin/pibuild bin $T/hc hc
/bin/hc
$T/timeit /bin/pibuild bin $T/hasm hasm
/bin/hasm
echo "=== fin des essais"
