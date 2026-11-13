#!/bin/bash
# wasmtest.sh -- WebAssembly dans jsc (docs/WEBKIT.md, « WebAssembly ») : wasmbench.js avec le JIT de WebAssembly
# (BBQ, OMG : Korin PC depuis le 14/11), puis l'interpréteur seul (IPInt) dans le même binaire ; un jsc plus ancien
# s'il est là (SD0:/bin/jsc-ipint : sans le JIT de WebAssembly).
#   bash SD0:/webkit/wasmtest.sh [fichier]     (defaut : SD0:/webkit/wasmtest.txt ; aussi a la sortie)
OUT=${1:-SD0:/webkit/wasmtest.txt}
cd SD0:/webkit
{
	echo "=== jsc wasmbench.js (JIT de WebAssembly)"
	SD0:/bin/jsc wasmbench.js
	echo "=== jsc --useBBQJIT=false --useOMGJIT=false wasmbench.js (IPInt)"
	SD0:/bin/jsc --useBBQJIT=false --useOMGJIT=false wasmbench.js
	if [ -x SD0:/bin/jsc-ipint ]; then
		echo "=== jsc-ipint wasmbench.js (jsc construit sans le JIT de WebAssembly)"
		SD0:/bin/jsc-ipint wasmbench.js
	fi
	echo "=== fin"
} > "$OUT" 2>&1
cat "$OUT"
