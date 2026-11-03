# mkspecs.awk : le fichier specs de GCC sous ARMPI (lu sans -specs). Le fichier principal n'accepte
# pas %rename : les valeurs de posix.specs sont fondues dans celles de gcc -dumpspecs.
#     awk -f mkspecs.awk posix.specs dumpspecs > specs
BEGIN { RS = ""; FS = "\n" }

function value(rec) { return substr(rec, length($1) + 2) }

# remplace la premiere occurrence de a par b, sans les caracteres speciaux de gsub
function subst(s, a, b,    i) {
	i = index(s, a)
	return i ? substr(s, 1, i - 1) b substr(s, i + length(a)) : s
}

NR == FNR {
	if ($1 ~ /^\*/)
		add[$1] = value($0)
	next
}

{
	v = value($0)
	if ($1 in add) {
		o = v
		v = subst(subst(add[$1], "%(posix_orig_link)", o), "%(posix_orig_lib)", o)
		if (substr(v, 1, 2) == "+ ")	# « + » : ajoute a la valeur d'origine
			v = o " " substr(v, 3)
	}
	printf "%s\n%s\n\n", $1, v		# (une valeur vide garde sa ligne vide)
}
