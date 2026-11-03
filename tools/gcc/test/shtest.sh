# Essai de sh (docs/SHELL.md) : chaque ligne ecrit PASS ou FAIL
T=/tmp/shtest
mkdir $T
cd $T

# guillemets, variables
a="un deux"
[ "$a" = "un deux" ] && echo PASS guillemets || echo FAIL guillemets
b='$a'
[ "$b" = '$a' ] && echo PASS apostrophes || echo FAIL apostrophes
c=x\ y
[ "$c" = "x y" ] && echo PASS backslash || echo FAIL backslash
d=${a}z
[ "$d" = "un deuxz" ] && echo PASS accolades || echo FAIL accolades
[ "${absente:-defaut}" = defaut ] && echo PASS defaut || echo FAIL defaut
[ "${a:+oui}" = oui ] && echo PASS plus || echo FAIL plus
[ "${#a}" = 7 ] && echo PASS longueur || echo FAIL longueur
n=0
for m in $a; do n=$((n + 1)); done
[ $n = 2 ] && echo PASS decoupage || echo FAIL decoupage
n=0
for m in "$a"; do n=$((n + 1)); done
[ $n = 1 ] && echo PASS sans-decoupage || echo FAIL sans-decoupage
[ $((2 + 3 * 4)) = 14 ] && echo PASS arith || echo FAIL arith

# controle
if false; then echo FAIL if; elif true; then echo PASS elif; else echo FAIL else; fi
if true
then
	echo PASS if-lignes
fi
i=0
while [ $i -lt 3 ]; do i=$((i + 1)); done
[ $i = 3 ] && echo PASS while || echo FAIL while
i=0
until [ $i -ge 2 ]; do i=$((i + 1)); done
[ $i = 2 ] && echo PASS until || echo FAIL until
r=
for i in 1 2 3 4; do
	if [ $i = 2 ]; then continue; fi
	if [ $i = 4 ]; then break; fi
	r=$r$i
done
[ "$r" = 13 ] && echo PASS break-continue || echo FAIL break-continue
case abc.c in
*.h) echo FAIL case ;;
*.c|*.cpp) echo PASS case ;;
*) echo FAIL case ;;
esac
true && echo PASS et
false || echo PASS ou
false && echo FAIL et-non
! false && echo PASS non
{ echo PASS accolades-bloc; }

# code de sortie, sous-shell
( cd /tmp; exit 3 )
[ $? = 3 ] && echo PASS sous-shell || echo FAIL sous-shell
[ "$(pwd)" = "RAM0:/tmp/shtest" ] && echo PASS cd-garde || echo FAIL cd-garde "$(pwd)"
r=$(echo sortie)
[ "$r" = sortie ] && echo PASS capture || echo FAIL capture
r=`echo arriere`
[ "$r" = arriere ] && echo PASS accent || echo FAIL accent
r="$(echo "a b" | cat2)"
sh -c 'exit 7'
[ $? = 7 ] && echo PASS sh-c || echo FAIL sh-c
[ "$(sh -c 'echo $1 $2' nom un deux)" = "un deux" ] && echo PASS sh-c-args || echo FAIL sh-c-args

# redirections
echo ligne1 > f1
echo ligne2 >> f1
[ "$(/bin/posixtest-cat f1 2>/dev/null)" = "" ] && echo PASS programme-absent || echo FAIL programme-absent
r=$(cat f1)
[ "$r" = "ligne1
ligne2" ] && echo PASS redirection || echo FAIL redirection
echo cache > /dev/null
echo sur-erreur 1>&2 2>/dev/null
ls /inexistant > f2 2>&1
[ -s f2 ] && echo PASS 2-vers-1 || echo FAIL 2-vers-1
[ -f f1 ] && [ ! -d f1 ] && [ -d . ] && echo PASS test-fichiers || echo FAIL test-fichiers
[ ! -e absent ] && echo PASS test-absent || echo FAIL test-absent

# motifs
echo a > x1.c
echo b > x2.c
echo c > x3.h
r=$(echo *.c)
[ "$r" = "x1.c x2.c" ] && echo PASS motif || echo FAIL motif "$r"
r=$(echo *.zz)
[ "$r" = "*.zz" ] && echo PASS motif-vide || echo FAIL motif-vide
r=$(echo "*.c")
[ "$r" = "*.c" ] && echo PASS motif-cite || echo FAIL motif-cite

# tubes
/gcc/bin/gcc -o wc1 /gcc/test/compte.c
r=$(echo un deux trois | ./wc1)
[ "$r" = 14 ] && echo PASS tube-programme || echo FAIL tube-programme "$r"
r=$(cat f1 | ./wc1)
[ "$r" = 14 ] && echo PASS tube-integree || echo FAIL tube-integree "$r"
r=$(cat f1 | ./wc1 | ./wc1)
[ "$r" = 3 ] && echo PASS tube-trois || echo FAIL tube-trois "$r"
./wc1 < f1 > f3
[ "$(cat f3)" = 14 ] && echo PASS programme-redirige || echo FAIL programme-redirige
export VENV=transmise
./wc1 VENV > f4
[ "$(cat f4)" = transmise ] && echo PASS environnement || echo FAIL environnement
AFFECT=devant ./wc1 AFFECT > f5
[ "$(cat f5)" = devant ] && echo PASS affectation-devant || echo FAIL affectation-devant
[ -z "$AFFECT" ] && echo PASS affectation-locale || echo FAIL affectation-locale
for f in 1 2 3; do echo $f; done > f6
[ "$(cat f6 | ./wc1)" = 6 ] && echo PASS compose-redirige || echo FAIL compose-redirige

# une longue ligne (au-dela des 1 024 octets du noyau) vers un programme C
l=
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
	l="$l aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
done
[ "$(./wc1 -n $l)" = 20 ] && echo PASS ligne-longue || echo FAIL ligne-longue

# set -e
sh -c 'set -e; false; echo FAIL set-e'
[ $? != 0 ] && echo PASS set-e || echo FAIL set-e-code
sh -c 'set -e; false || true; if false; then :; fi; echo PASS set-e-cond'
cd /tmp
rm -f $T/f1 $T/f2 $T/f3 $T/f4 $T/f5 $T/f6 $T/x1.c $T/x2.c $T/x3.h $T/wc1
rm $T
echo fin
