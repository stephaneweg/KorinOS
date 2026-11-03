"""gen-table : la table des fonctions natives de l'hote skia, pour PINVOKE_OVERRIDE (sans dlopen).

Usage : python3 gen-table.py sortie.c nm.txt BIBLIO=assembly.dll [BIBLIO=assembly.dll...]
nm.txt : la sortie de nm -g --defined-only des bibliotheques statiques. Seuls les noms que
l'assembly importe entrent dans la table : le reste de la bibliotheque reste hors du programme
si rien d'autre ne le reference.
"""
import re
import sys


def main():
    out, nm = sys.argv[1], sys.argv[2]
    defined = set()
    for line in open(nm, encoding="utf-8", errors="replace"):
        f = line.split()
        if len(f) == 3 and f[1] == "T":
            defined.add(f[2])
    c = ['/* genere par tools/avalonia/gen-table.py */', '#include <stdlib.h>', '#include <string.h>',
         'struct entry { const char* name; const void* fn; };',
         'static int cmp (const void* k, const void* e) { return strcmp ((const char*) k, ((const struct entry*) e)->name); }']
    libs = []
    for spec in sys.argv[3:]:
        lib, dll = spec.split("=")
        data = open(dll, "rb").read()
        strings = set(s.decode("ascii", "replace") for s in re.findall(rb"[a-z][a-z0-9_]*(?=\0)", data))	# (sans le zero devant : un nom peut finir un autre dans le tas des chaines, <0>__armgl_proc)
        names = sorted(n for n in strings if n in defined)
        print(lib, len(names), "fonctions")
        ident = lib.replace(".", "_")
        c += [f"extern void {n} (void);" for n in names]
        c.append(f"static const struct entry t_{ident}[] = {{")
        c += [f'\t{{ "{n}", (const void*) {n} }},' for n in names]
        c.append("};")
        libs.append((lib, ident, len(names)))
    c.append('const void* armpi_native_resolve (const char* library, const char* entry)\n{\n\tconst struct entry* e;')
    for lib, ident, n in libs:
        c.append(f'\tif (strcmp (library, "{lib}") == 0)\n\t{{\n\t\te = bsearch (entry, t_{ident}, {n}, sizeof (struct entry), cmp);\n\t\treturn e ? e->fn : 0;\n\t}}')
    c.append("\treturn 0;\n}")
    open(out, "w", encoding="ascii").write("\n".join(c) + "\n")


main()
