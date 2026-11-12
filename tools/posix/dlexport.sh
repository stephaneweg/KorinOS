#!/bin/sh
# dlexport.sh -- la liste des symboles qu'un hote de dlopen exporte (libposix dl.c, docs/POSIX.md
# « dlopen ») : un script ld fait de EXTERN (...), que posix.specs passe avec -rdynamic.
#
#     sh tools/posix/dlexport.sh SORTIE PREMIERE NOMBRE ARCHIVE...
#
# Les ARCHIVES sont donnees dans l'ordre ou ld les lit (libposix, libc, libm, libgcc ;
# libstdc++ ensuite pour la liste du C++). Seuls les symboles des archives a partir du rang
# PREMIERE (1 = la premiere), et NOMBRE archives, sont ecrits ; les autres servent a resoudre.
#
# Un --whole-archive de newlib ne se relie pas : des membres doublent ceux que libposix remplace
# (environ, signal, mktemp, les verrous de newlib), d'autres appellent __errno ou _impure_ptr que
# cette newlib (reent en thread-local) n'a pas. On garde donc les seuls membres « surs » : pris par
# ld pour l'un de leurs symboles, ils ne doublent aucun symbole fort deja la, et tout ce qu'ils
# appellent est fourni par un membre sur (point fixe). Leurs symboles globaux sont exportes.
set -e
OUT=$1; FIRST=$2; COUNT=$3; shift 3
NM=${NM:-${TC}nm}
i=0
for a in "$@"; do
	i=$((i + 1))
	$NM -A -g --format=posix "$a" 2>/dev/null | sed "s/^/$i /"
done | awk -v first="$FIRST" -v count="$COUNT" '
# "rang archive[membre]: nom type [valeur taille]"
{
	rank = $1; m = $2; sub(/:$/, "", m); m = rank "|" m; name = $3; t = $4
	if (!(m in seen)) { seen[m] = 1; order[++nm] = m; mrank[m] = rank }
	if (t == "U") { und[m] = und[m] " " name; next }
	if (t == "w" || t == "v") next
	weak = (t == "W" || t == "V" || t == "C")
	if (!weak) strong[m, name] = 1
	defs[m] = defs[m] " " name
	if (!(name in prov)) prov[name] = m	# le premier membre qui le definit : celui que ld prend
	if (!weak) nstrong[name]++
}
END {
	# ce que le script de liens ou le programme fournissent
	split("main _init _fini _DYNAMIC _GLOBAL_OFFSET_TABLE_ __ehdr_start __dso_handle __init_array_start __init_array_end __fini_array_start __fini_array_end __preinit_array_start __preinit_array_end _end end __bss_start __bss_end__ __end__ _edata __tls_get_addr", L, " ")
	for (k in L) given[L[k]] = 1
	for (i = 1; i <= nm; i++) ok[order[i]] = 1
	do {
		changed = 0
		for (i = 1; i <= nm; i++) {
			m = order[i]
			if (!ok[m] || mrank[m] == 1) continue	# (libposix : se relie toujours)
			bad = 0
			n = split(defs[m], d, " ")
			for (j = 1; j <= n && !bad; j++)	# double un symbole fort pris ailleurs
				if (strong[m, d[j]] && prov[d[j]] != m && ok[prov[d[j]]] && strong[prov[d[j]], d[j]]) { bad = 1; why = "doublon de " d[j] " (" prov[d[j]] ")" }
			n = split(und[m], u, " ")
			for (j = 1; j <= n && !bad; j++) {
				s = u[j]
				if (s in given || s ~ /^__posix_/) continue
				if (!(s in prov) || !ok[prov[s]]) { bad = 1; why = s }
			}
			if (bad) { ok[m] = 0; changed = 1; if (ENVIRON["DLX_DEBUG"] != "") print "rejet " m " : " why > "/dev/stderr" }
		}
	} while (changed)
	print "/* genere par tools/posix/dlexport.sh : ne pas editer */"
	for (i = 1; i <= nm; i++) {
		m = order[i]
		if (!ok[m] || mrank[m] < first || mrank[m] >= first + count) continue
		n = split(defs[m], d, " ")
		for (j = 1; j <= n; j++)
			if (prov[d[j]] == m && !(d[j] in done)) { done[d[j]] = 1; print "EXTERN(" d[j] ")" }
	}
}' > "$OUT"
