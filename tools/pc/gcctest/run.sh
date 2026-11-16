# run.sh : essais de GCC, make, FreeBASIC et fasm dans Korin PC (docs/etude/PC-PILOTES.md,
# « GCC, FreeBASIC et fasm dans Korin PC »), depuis la console texte : sh /devtools/gcc/test/pc/run.sh
# Les objets et les programmes vont dans /tmp/t (/tmp) ; chaque étape dit sa durée (timeit).
T=/tmp/t
E=/devtools/gcc/test
mkdir -p $T
cd $T
echo "=== timeit (gcc timeit.c, sans mesure)"
/devtools/gcc/bin/gcc -O2 $E/pc/timeit.c -o $T/timeit
echo "=== gcc hello.c"
$T/timeit /devtools/gcc/bin/gcc $E/hello.c -o $T/hello
$T/hello
echo "=== gcc -O2 hello.c (deuxième fois)"
$T/timeit /devtools/gcc/bin/gcc -O2 $E/hello.c -o $T/hello2
$T/hello2
echo "=== g++ hello_io.cpp (iostream)"
$T/timeit /devtools/gcc/bin/g++ $E/pc/hello_io.cpp -o $T/hello_io
$T/hello_io
echo "=== g++ hello.cpp (STL, exceptions)"
$T/timeit /devtools/gcc/bin/g++ -O2 $E/hello.cpp -o $T/hellocpp
$T/hellocpp
echo "=== gcc uiapp.c -luikit (fenêtre UIKit, compilée seulement)"
$T/timeit /devtools/gcc/bin/gcc -O2 -I/gcc/include/kits $E/pc/uiapp.c -luikit -o $T/uiapp
ls -l $T/uiapp
echo "=== make (test/projet)"
cp -r $E/projet $T/projet
cd $T/projet
$T/timeit /devtools/gcc/bin/make essai
cd $T
echo "=== fbc hello.bas"
cp $E/pc/hello.bas $T/hello.bas
$T/timeit /devtools/gcc/bin/fbc $T/hello.bas -x $T/hello_bas
$T/hello_bas
echo "=== fasm hello.asm"
$T/timeit /bin/fasm $E/fasm/hello.asm $T/hello_asm
$T/hello_asm
echo "=== fasm : fasm se réassemble (objet ELF64)"
cd /devtools/gcc/share/fasm/source
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
