#!/bin/bash
# jsctest.sh -- jsc sur Korin (docs/etude/PC-PILOTES.md, "Jet pour le PC") : le JIT contre le LLInt seul.
#   bash SD0:/webkit/jsctest.sh [fichier]     (defaut : SD0:/webkit/jsctest.txt ; aussi a la sortie)
OUT=${1:-SD0:/webkit/jsctest.txt}
cd SD0:/webkit
{
	echo "=== jsc -e print(6*7)"
	SD0:/bin/jsc -e 'print(6*7)'
	echo "=== smoke.js (JIT)"
	SD0:/bin/jsc smoke.js
	echo "=== jitbench.js : JIT (de base, DFG, FTL)"
	SD0:/bin/jsc jitbench.js
	echo "=== jitbench.js : --useFTLJIT=false (de base, DFG)"
	SD0:/bin/jsc --useFTLJIT=false jitbench.js
	echo "=== jitbench.js : JSC_useJIT=false (LLInt seul)"
	JSC_useJIT=false SD0:/bin/jsc jitbench.js
	echo "=== fin"
} > "$OUT" 2>&1
cat "$OUT"
