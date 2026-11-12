#!/bin/bash
# git sur les trois volumes (docs/POSIX.md « L'arbre POSIX des chemins »), sur la carte : bash run-tests.sh
# depuis l'arbre POSIX (formes /sd0, /sd1, /tmp) et sous la forme de volume (SD0:)
pass=0; fail=0
ck () { if eval "$2" >/tmp/gt.out 2>&1; then pass=$((pass+1)); else fail=$((fail+1)); echo "ECHEC $1"; tail -3 /tmp/gt.out; fi; }
ck "config --global" "git config --global user.name Essai && git config --global user.email essai@korin.invalid && git config --global init.defaultBranch main && test -f /sd0/.gitconfig"
for base in /sd0/gt /sd1/gt /tmp/gt SD0:/gt2; do
	rm -rf $base; mkdir -p $base
	ck "$base init" "cd $base && git init -q r && cd r && pwd"
	cd $base/r 2>/dev/null || continue
	ck "$base pwd POSIX" "case \$(pwd -P) in /*) true;; *) false;; esac"
	ck "$base add/commit" "echo a > a.txt && mkdir -p d && echo b > d/b.txt && git add -A && git commit -qm un"
	ck "$base status" "echo c >> a.txt && git status --short | grep -q 'M a.txt'"
	ck "$base diff" "git diff | grep -q '^+c'"
	ck "$base commit -a" "git commit -qam deux && test \$(git rev-list --count HEAD) = 2"
	ck "$base branch/switch/merge" "git switch -qc br && echo x > x.txt && git add x.txt && git commit -qm x && git switch -q main && git merge -q br && test -f x.txt"
	ck "$base log" "git log --oneline | wc -l | grep -q 3"
	ck "$base subdir rev-parse" "cd d && test \"\$(git rev-parse --show-toplevel)\" = \"\$(cd .. && pwd -P)\" && git rev-parse --show-prefix | grep -q '^d/'"
	ck "$base clone vers /tmp" "rm -rf /tmp/cl && git clone -q $base/r /tmp/cl && test -f /tmp/cl/d/b.txt"
	ck "$base fsck/gc" "git fsck --no-progress && git gc -q"
	cd /
done
echo "git : $pass reussis, $fail echecs"
