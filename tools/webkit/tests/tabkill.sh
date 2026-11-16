#!/bin/bash
# tabkill.sh -- l'essai des onglets de Jet (docs/WEBKIT.md, « Onglets ») : attend que l'onglet N ait son process
# web (ligne « web: [...] tab N: web process pid P » du journal), attend encore S secondes, puis le tue (kill du
# shell de Korin) ; Jet doit fermer cet onglet seul. ps avant et apres.
#   bash /webkit/tabkill.sh [N] [S]        (defaut : 2, 30)
N=${1:-2}
S=${2:-30}
pid=
for i in {1..120}; do
	/bin/journal > /dev/null
	pid=$(grep -a "tab $N: web process pid" /journal.txt | tail -n 1 | sed 's/.*pid //')
	[ -n "$pid" ] && break
	sleep 5
done
if [ -z "$pid" ]; then
	echo "tabkill: pas de process web pour l'onglet $N"
	exit 1
fi
echo "tabkill: onglet $N, process web $pid ; kill dans $S s"
sleep "$S"
/bin/sh -c "ps"
/bin/sh -c "kill $pid"
echo "tabkill: kill $pid fait"
sleep 10
/bin/sh -c "ps"
