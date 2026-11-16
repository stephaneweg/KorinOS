#!/bin/bash
# jsctest.sh -- jsc sur Korin (docs/etude/PC-PILOTES.md, "Jet pour le PC") : le JIT contre le LLInt seul.
#   bash /webkit/jsctest.sh [fichier]     (defaut : /webkit/jsctest.txt ; aussi a la sortie)
OUT=${1:-/webkit/jsctest.txt}
cd /webkit
{
	echo "=== jsc -e print(6*7)"
	/bin/jsc -e 'print(6*7)'
	echo "=== smoke.js (JIT)"
	/bin/jsc smoke.js
	echo "=== jitbench.js : JIT (de base, DFG, FTL)"
	/bin/jsc jitbench.js
	echo "=== jitbench.js : --useFTLJIT=false (de base, DFG)"
	/bin/jsc --useFTLJIT=false jitbench.js
	echo "=== jitbench.js : JSC_useJIT=false (LLInt seul)"
	JSC_useJIT=false /bin/jsc jitbench.js
	echo "=== fin"
} > "$OUT" 2>&1
cat "$OUT"
