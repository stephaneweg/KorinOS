#!/bin/bash
# jetcc.sh FICHIER... : recompile un source de Jet (src/extra/onyxports/jet/<nom>.cpp du dépôt) en
# /devtools/jet-sdk/webcore/jet_<nom>.o, avec la commande d'une source de WebKit (WKRunLoop.cpp, en-têtes privés et
# configuration), comme tools/webkit/build-jet.sh ; ensuite : jetlink. (Banc d'essai local, pas la construction publiée.)
SDK=${JETSDK:-/devtools/jet-sdk}
REPO=${REPO:-/home/public/korin}
J=$REPO/src/extra/onyxports/jet
ref=UIProcess/API/C/onyx/WKRunLoop.cpp
line=$(grep -m1 -F "$ref" "$SDK/commands.txt") || { echo "jetcc : pas de commande pour $ref" >&2; exit 1; }
cmd=${line#*	}
rc=0
for f in "$@"; do
	n=$(basename "$f" .cpp)
	c=$(printf '%s' "$cmd" | sed -E 's/ -MD -MT [^ ]+ -MF [^ ]+//; s/ -o [^ ]+/ -o jet_'"$n"'.o/; s| -c [^ ]+| -c '"$J/$n"'.cpp -I'"$J"' -I'"$REPO"'/src/kits/uikit/include -I/devtools/gcc/include/kits -idirafter '"$SDK"'/webcore/WebCore/PrivateHeaders/WebCore|')
	echo "jetcc : $n.cpp -> jet_$n.o"
	t0=$(date +%s)
	if (cd "$SDK/webcore" && eval "$c"); then echo "jetcc : fait en $(( $(date +%s) - t0 )) s"; else echo "jetcc : ÉCHEC ($n)" >&2; rc=1; fi
done
exit $rc
