# Essai des commandes de SD0:/bin (docs/OUTILS.md) : chaque ligne ecrit PASS ou FAIL
T=/tmp/tooltest
mkdir -p $T/a/b/c
[ -d $T/a/b/c ] && echo PASS mkdir-p || echo FAIL mkdir-p
cd $T
echo un > f1
echo deux >> f1
echo trois >> f1
[ "$(cat f1 f1 | wc -l)" = 6 ] && echo PASS cat-wc || echo FAIL cat-wc
[ "$(wc -c < f1)" = 14 ] && echo PASS wc-c || echo FAIL wc-c
[ "$(wc -w f1)" = "3 f1" ] && echo PASS wc-w || echo FAIL wc-w "$(wc -w f1)"
cp f1 f2
[ "$(cat f2)" = "$(cat f1)" ] && echo PASS cp || echo FAIL cp
cp f1 a
[ -f a/f1 ] && echo PASS cp-dossier || echo FAIL cp-dossier
cp -r a a2
[ -f a2/f1 ] && [ -d a2/b/c ] && echo PASS cp-r || echo FAIL cp-r
mv f2 f3
[ -f f3 ] && [ ! -e f2 ] && echo PASS mv || echo FAIL mv
mv f3 a
[ -f a/f3 ] && echo PASS mv-dossier || echo FAIL mv-dossier
mv a2 a3
[ -f a3/f1 ] && echo PASS mv-repertoire || echo FAIL mv-repertoire
cp f1 SD0:/tmp-tooltest
mv SD0:/tmp-tooltest m1
[ "$(cat m1)" = "$(cat f1)" ] && [ ! -e SD0:/tmp-tooltest ] && echo PASS mv-volumes || echo FAIL mv-volumes
touch n1
[ -f n1 ] && echo PASS touch || echo FAIL touch
[ "$(ls a | head -1)" = b ] && echo PASS ls-tri || echo FAIL ls-tri
[ "$(ls a | wc -l)" = 3 ] && echo PASS ls || echo FAIL ls
[ "$(head -2 f1 | tail -1)" = deux ] && echo PASS head-tail || echo FAIL head-tail
[ "$(tail -n 1 f1)" = trois ] && echo PASS tail-n || echo FAIL tail-n
[ "$(head -n 1 f1)" = un ] && echo PASS head-n || echo FAIL head-n
[ "$(echo abc | tr a-c A-C)" = ABC ] && echo PASS tr || echo FAIL tr
[ "$(echo a-b-c | tr -d -)" = abc ] && echo PASS tr-d || echo FAIL tr-d
[ "$(echo a:b:c | cut -d : -f 2)" = b ] && echo PASS cut-f || echo FAIL cut-f
[ "$(echo a:b:c | cut -d: -f1,3)" = a:c ] && echo PASS cut-liste || echo FAIL cut-liste
[ "$(echo abcdef | cut -c 2-4)" = bcd ] && echo PASS cut-c || echo FAIL cut-c
[ "$(cat f1 f1 | sort | uniq | wc -l)" = 3 ] && echo PASS sort-uniq || echo FAIL sort-uniq
[ "$(cat f1 | sort | head -1)" = deux ] && echo PASS sort || echo FAIL sort
[ "$(cat f1 | sort -r | head -1)" = un ] && echo PASS sort-r || echo FAIL sort-r
echo 10 > n2
echo 9 >> n2
echo 100 >> n2
[ "$(sort -n n2 | head -1)" = 9 ] && echo PASS sort-n || echo FAIL sort-n
[ "$(sort -n -r n2 | head -1)" = 100 ] && echo PASS sort-nr || echo FAIL sort-nr
[ "$(cat f1 f1 | sort -u | wc -l)" = 3 ] && echo PASS sort-u || echo FAIL sort-u
[ "$(cat f1 f1 | sort | uniq -c | head -1)" = "2 deux" ] && echo PASS uniq-c || echo FAIL uniq-c

# grep
[ "$(grep eu f1)" = deux ] && echo PASS grep || echo FAIL grep
[ "$(grep -c o f1)" = 1 ] && echo PASS grep-c || echo FAIL grep-c
[ "$(grep -v u f1)" = trois ] && echo PASS grep-v || echo FAIL grep-v
[ "$(grep -n trois f1)" = 3:trois ] && echo PASS grep-n || echo FAIL grep-n
[ "$(grep -i TROIS f1)" = trois ] && echo PASS grep-i || echo FAIL grep-i
[ "$(grep '^d.*x$' f1)" = deux ] && echo PASS grep-ancres || echo FAIL grep-ancres
[ "$(grep -E '^(un|trois)$' f1 | wc -l)" = 2 ] && echo PASS grep-E || echo FAIL grep-E
[ "$(grep 'tr[a-z]*s' f1)" = trois ] && echo PASS grep-classe || echo FAIL grep-classe
[ "$(grep -E 'o+i?s' f1)" = trois ] && echo PASS grep-plus || echo FAIL grep-plus
grep -q absent f1
[ $? = 1 ] && echo PASS grep-code || echo FAIL grep-code
grep -q un f1 && echo PASS grep-q || echo FAIL grep-q
[ "$(grep -l un f1 m1 | wc -l)" = 2 ] && echo PASS grep-l || echo FAIL grep-l

# sed
[ "$(echo abc | sed s/b/X/)" = aXc ] && echo PASS sed-s || echo FAIL sed-s
[ "$(echo aaa | sed s/a/b/g)" = bbb ] && echo PASS sed-g || echo FAIL sed-g
[ "$(echo a.c | sed 's/\./-/')" = a-c ] && echo PASS sed-echap || echo FAIL sed-echap
[ "$(echo /a/b | sed 's,/,:,g')" = :a:b ] && echo PASS sed-delim || echo FAIL sed-delim
[ "$(echo foo.c | sed 's/\(.*\)\.c/\1.o/')" = foo.o ] && echo PASS sed-groupe || echo FAIL sed-groupe
[ "$(echo foo.c | sed -E 's/(.*)\.c/\1.o/')" = foo.o ] && echo PASS sed-E || echo FAIL sed-E
[ "$(echo ab | sed 's/a/[&]/')" = '[a]b' ] && echo PASS sed-et || echo FAIL sed-et
[ "$(sed -n 2p f1)" = deux ] && echo PASS sed-np || echo FAIL sed-np
[ "$(sed 2d f1 | wc -l)" = 2 ] && echo PASS sed-d || echo FAIL sed-d
[ "$(sed -n '$p' f1)" = trois ] && echo PASS sed-dernier || echo FAIL sed-dernier
[ "$(sed -n '/deux/p' f1)" = deux ] && echo PASS sed-motif || echo FAIL sed-motif
[ "$(sed -n '2,3p' f1 | wc -l)" = 2 ] && echo PASS sed-plage || echo FAIL sed-plage
[ "$(sed -e 1d -e 2d f1)" = trois ] && echo PASS sed-e || echo FAIL sed-e
[ "$(sed 1q f1)" = un ] && echo PASS sed-q || echo FAIL sed-q
[ "$(echo abc | sed 's/x*/-/g')" = -a-b-c- ] && echo PASS sed-vide || echo FAIL sed-vide "$(echo abc | sed 's/x*/-/g')"

# find, chemins
[ "$(find a -name 'f*' | sort | head -1)" = a/f1 ] && echo PASS find-name || echo FAIL find-name
[ "$(find a -type d | wc -l)" = 3 ] && echo PASS find-type || echo FAIL find-type
[ "$(find a -maxdepth 1 -type d | wc -l)" = 2 ] && echo PASS find-profondeur || echo FAIL find-profondeur
[ "$(basename SD0:/a/b.c)" = b.c ] && echo PASS basename || echo FAIL basename
[ "$(basename a/b.c .c)" = b ] && echo PASS basename-suffixe || echo FAIL basename-suffixe
[ "$(dirname a/b/c.o)" = a/b ] && echo PASS dirname || echo FAIL dirname
[ "$(dirname c.o)" = . ] && echo PASS dirname-point || echo FAIL dirname-point
[ "$(echo x | tee t1)" = x ] && [ "$(cat t1)" = x ] && echo PASS tee || echo FAIL tee
[ "$(/bin/echo -n a b)" = "a b" ] && echo PASS echo || echo FAIL echo
/bin/true && echo PASS true || echo FAIL true
/bin/false || echo PASS false
/bin/test -f f1 && echo PASS test || echo FAIL test
sleep 1
# les dates : ce que make va lire
touch t2
ls -l t2 > /dev/null
rm -f absent
[ $? = 0 ] && echo PASS rm-f || echo FAIL rm-f
rm absent 2>/dev/null
[ $? = 1 ] && echo PASS rm-code || echo FAIL rm-code
cd /tmp
rm -r $T
[ ! -e $T ] && echo PASS rm-r || echo FAIL rm-r
echo fin
