#!/bin/bash
# Paquet testing_prep_nodejs : les essais des étapes 1 à 9 de docs/TERMINAL-ET-FORK.md (terminal ANSI, vterm, fork,
# exec, bash, contrôle des tâches, git, rg, copie sur écriture) sur le vrai Pi, les uns après les autres.
#
#   bash SD0:/test/prep_nodejs/run.sh            tous les essais automatiques (quelques minutes)
#   bash SD0:/test/prep_nodejs/run.sh court      sans les boucles longues (forktest, exectest)
#
# Chaque essai écrit son journal dans RAM0:/prep/<nom>.log ; le résumé finit par le nombre d'essais réussis.
# Les essais à faire à la main (Ctrl+Z, redimensionner, ansitest) sont dans SD0:/test/prep_nodejs/LISEZMOI.txt.

COURT=0; [ "$1" = court ] && COURT=1
LOG=RAM0:/prep
mkdir -p $LOG
OK=0; KO=0; RESUME=""

note () {	# note NOM CODE DETAIL
	if [ "$2" = 0 ]; then OK=$((OK + 1)); RESUME+="  OK    $1  $3"$'\n'
	else KO=$((KO + 1)); RESUME+="  ECHEC $1  $3 (journal : $LOG/$1.log)"$'\n'; fi
}

essai () {	# essai NOM MOTIF_DE_REUSSITE commande...  : réussi si le journal contient le motif
	local nom=$1 motif=$2; shift 2
	echo "== $nom : $*"
	"$@" > $LOG/$nom.log 2>&1
	local code=$? resume
	resume=$(grep -a -E "$motif" $LOG/$nom.log | tail -1)
	if [ -n "$resume" ]; then note $nom 0 "$resume"
	else
		note $nom 1 "code $code"
		echo "-- fin du journal de $nom (code $code) :"; grep -a "^FAIL" $LOG/$nom.log | head -10; tail -5 $LOG/$nom.log
	fi
}

echo "Korin OS : essais avant Node.js ($(date))"
echo "bash $BASH_VERSION ; $(uname -a 2>/dev/null)"

# posixtest : celui du paquet (son paquet system-tests n'est pas publié), sinon celui de /bin
# (certains de ses essais le veulent dans SD0:/bin : il s'y sert de lui-même comme interprète de script)
ICI=SD0:/test/prep_nodejs/bin
for f in posixtest posixtest-cxx; do [ -f SD0:/bin/$f ] || cp $ICI/$f SD0:/bin/$f; done
PT=/bin/posixtest; PX=/bin/posixtest-cxx
essai posixtest     "^posixtest: .*passed, 0 failed"   $PT
essai posixtest-cxx "^posixtest-cxx: .*passed, 0 failed"   $PX
essai ttytest       ", 0 FAIL"           /bin/ttytest
essai jobtest       ", 0 FAIL"           /bin/jobtest
essai pipetest      ", 0 FAIL"           /bin/pipetest
if [ $COURT = 0 ]; then
	essai forktest  ", 0 FAIL"           /bin/forktest 1000
	essai exectest  ", 0 FAIL"           /bin/exectest 500
else
	essai forktest  ", 0 FAIL"           /bin/forktest 50
	essai exectest  ", 0 FAIL"           /bin/exectest 50
fi
[ -x /bin/cowtest ] && essai cowtest ", 0 FAIL" /bin/cowtest

# bash : un script avec tubes, $(...), sous-shells, fonctions, here-docs
cat > $LOG/b.sh <<'EOF'
f () { echo "f:$1"; }
x=$(echo un | tr a-z A-Z); [ "$x" = UN ] || exit 1
[ "$(f deux)" = "f:deux" ] || exit 2
n=$( (for i in 1 2 3 4 5; do echo $i; done) | wc -l ); [ "$n" -eq 5 ] || exit 3
cat <<FIN | grep -q milieu || exit 4
debut
milieu
FIN
s=0; for i in $(seq 1 100 2>/dev/null || echo 1 2 3); do s=$((s + i)); done; [ $s -gt 0 ] || exit 5
sleep 5 & p=$!; kill $p; wait $p; [ $? -ne 0 ] || exit 6
(sleep 3) | { read -t 1 r; [ $? -gt 128 ]; } || exit 7
echo "bash : PASS"
EOF
essai bash "bash : PASS" /bin/bash $LOG/b.sh

# un script #! rendu exécutable par chmod +x
printf '#!/bin/bash\necho "script : PASS"\n' > $LOG/s.sh
chmod +x $LOG/s.sh
essai script-chmod "script : PASS" $LOG/s.sh

# git : un dépôt dans RAM0:
cat > $LOG/g.sh <<'EOF'
cd RAM0:/ && rm -rf RAM0:/gitprep && mkdir RAM0:/gitprep && cd RAM0:/gitprep || exit 1
git init -q . && git config user.name essai && git config user.email essai@korin.local || exit 2
echo un > a.txt && git add a.txt && git commit -q -m un || exit 3
git switch -q -c b && echo deux >> a.txt && git commit -q -am deux || exit 4
git switch -q main && git merge -q b || exit 5
[ "$(git log --oneline | wc -l)" -eq 2 ] || exit 6
git status --short | wc -l | grep -q '^0$' || exit 7
echo "git : PASS ($(git --version))"
EOF
essai git "git : PASS" /bin/bash $LOG/g.sh

# rg : les appels de Claude Code sur les sources de /test
cat > $LOG/r.sh <<'EOF'
rg --version | head -1 | grep -q '^ripgrep ' || exit 1
d=RAM0:/rgprep; rm -rf $d; mkdir -p $d/src; printf 'un\nfonction alpha\n' > $d/src/a.c; printf 'beta\n' > $d/src/b.h
rg -l --null --hidden --max-columns 500 alpha $d | grep -a -q "src/a.c" || exit 2
rg --json -n alpha $d | grep -q '"type":"match"' || exit 3
rg --files --null --glob '*.h' --sort=modified --no-ignore --hidden $d | grep -a -q "src/b.h" || exit 4
rg --files --null --glob '*.h' --sort=modified --no-ignore --hidden $d | grep -a -q "src/a.c" && exit 5
echo "rg : PASS"
EOF
essai rg "rg : PASS" /bin/bash $LOG/r.sh

# gcc : un hello compilé sur place, exécutable sans chmod
cat > $LOG/c.sh <<'EOF'
cd RAM0:/ || exit 1
printf '#include <stdio.h>\nint main(void){puts("gcc : PASS");return 0;}\n' > hello.c
/gcc/bin/gcc hello.c -o hello || exit 2
[ -x hello ] || exit 3
./hello
EOF
if [ -x /gcc/bin/gcc ]; then essai gcc "gcc : PASS" /bin/bash $LOG/c.sh; fi
# (gcc : code 3 = hello pas exécutable : un gcc d'avant 1.0.9, dont ld ne pose pas le bit x : pkg update gcc)

echo
echo "Résumé :"
printf "%s" "$RESUME"
echo "$OK réussis, $KO en échec"
[ $KO = 0 ]
