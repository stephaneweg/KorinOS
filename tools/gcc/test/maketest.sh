# Essai de make (docs/GCC.md) : construction, rien a refaire, reconstruction partielle, -j
M=/gcc/bin/make
P=${1:-/tmp/projet}
mkdir -p $P
cp /gcc/test/projet/Makefile /gcc/test/projet/commun.h /gcc/test/projet/calcul.c /gcc/test/projet/double.c /gcc/test/projet/principal.cpp /gcc/test/projet/par.mk $P
cd $P
$M essai > s1.txt 2>&1
cat s1.txt
grep -q 'PROJET OK' s1.txt && echo PASS construction || echo FAIL construction
[ "$(grep -c 'gcc -O1' s1.txt)" = 2 ] && echo PASS deux-fichiers-c || echo FAIL deux-fichiers-c
sleep 3
$M > s2.txt 2>&1
grep -q 'rien\|Nothing\|up to date\|a jour' s2.txt && echo PASS rien-a-faire || { echo FAIL rien-a-faire; cat s2.txt; }
sleep 3
touch calcul.c
$M > s3.txt 2>&1
[ "$(grep -c 'gcc -O1 -c calcul.c' s3.txt)" = 1 ] && [ "$(grep -c 'double.c' s3.txt)" = 0 ] && echo PASS un-seul-recompile || { echo FAIL un-seul-recompile; cat s3.txt; }
grep -q 'lien fait' s3.txt && echo PASS relie || echo FAIL relie
sleep 3
touch commun.h
$M > s4.txt 2>&1
[ "$(grep -c ' -c ' s4.txt)" = 3 ] && echo PASS en-tete-tout-refait || { echo FAIL en-tete-tout-refait; cat s4.txt; }
$M clean > s5.txt 2>&1
[ ! -e obj ] && echo PASS clean || { echo FAIL clean; cat s5.txt; }
$M -j2 essai > s6.txt 2>&1
grep -q 'PROJET OK' s6.txt && echo PASS parallele || { echo FAIL parallele; cat s6.txt; }
$M -j2 -f par.mk > s8.txt 2>&1
grep -q 'a fini' s8.txt && echo PASS deux-a-la-fois || { echo FAIL deux-a-la-fois; cat s8.txt; }
rm b.ok
$M -f par.mk > s9.txt 2>&1
grep -q 'a fini' s9.txt && echo FAIL sans-j-a-deja-b || echo PASS sans-j-un-a-la-fois
rm b.ok
$M -C $P clean > s7.txt 2>&1
[ ! -e obj ] && echo PASS make-C || { echo FAIL make-C; cat s7.txt; }
cd /tmp
rm -r $P
echo fin
