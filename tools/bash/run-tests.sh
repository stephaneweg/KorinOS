#!/bin/bash
# les 40 essais de docs/BASH.md (« Essais »), un par un, sur la carte : bash run-tests.sh
# (build/bash/tests copie dans SD0:/t5/tests). Un essai reussit quand diff ne dit rien (les run-* ecrivent
# parfois des « warning: », qui ne comptent pas)
cd /t5/tests || exit 1
export THIS_SH=/bin/bash BASH_TSTOUT=/tmp/bashtst.out PATH=/t5/tests:$PATH
ok=0; ko=""
for t in arith func quote braces dollars precedence strip invert parser posixpat rhs-exp comsub getopts mapfile nameref case nquote more-exp iquote set-e array test glob-test read exp-tests printf herestr heredoc type redir varenv new-exp cond shopt trap tilde posixexp assoc ifs lastpipe; do
	rm -f $BASH_TSTOUT
	bash ./run-$t > /tmp/bt-$t.txt 2>&1
	if ! grep -qv "^warning" /tmp/bt-$t.txt; then ok=$((ok+1)); else ko="$ko $t"; fi
done
echo "bash : $ok reussis sur 40 ; echecs :$ko"
