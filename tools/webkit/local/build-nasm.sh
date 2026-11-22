#!/bin/bash
# build-nasm.sh -- NASM 2.16.03 dans Korin (pas de paquet ; son configure d'autoconf ne reconnaît pas Korin) : la
# source officielle, un config.h tiré de config/config.h.in (les fonctions de newlib + libposix, les attributs de
# GCC 14), les 84 sources de Makefile.in compilées par GCC. -> ~/jet-build/bin/nasm (sert à build-jpeg.sh).
set -e
W=${WORK:-$HOME/jet-build}
mkdir -p "$W/src" "$W/bin"
cd "$W/src"
if [ ! -d nasm-2.16.03 ]; then
	python3 -I - <<'EOF'
import urllib.request as u, tarfile, io, hashlib
d = u.urlopen("https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/nasm-2.16.03.tar.gz", timeout=180).read()
print("nasm-2.16.03.tar.gz", len(d), "octets, sha256", hashlib.sha256(d).hexdigest())
tarfile.open(fileobj=io.BytesIO(d)).extractall(".")
EOF
fi
cd nasm-2.16.03
# config.h : chaque #undef de config.h.in que Korin a (ou un attribut de fonction de GCC) devient #define, le reste
# reste non défini ; la suite de config.h.in (les macros d'attributs) est gardée
python3 -I - <<'EOF'
import re
have = """HAVE_INTTYPES_H HAVE_STDINT_H HAVE_STDBOOL_H HAVE_STDNORETURN_H HAVE_STRINGS_H HAVE_STRING_H HAVE_UNISTD_H
HAVE_SYS_TYPES_H HAVE_SYS_STAT_H HAVE_FCNTL_H HAVE_SNPRINTF HAVE_VSNPRINTF HAVE_STRCASECMP HAVE_STRNCASECMP HAVE_STRLCPY
HAVE_STRNLEN HAVE_MEMPCPY HAVE_DECL_MEMPCPY HAVE_STPCPY HAVE_DECL_STPCPY HAVE_DECL_STRCASECMP HAVE_DECL_STRNCASECMP
HAVE_DECL_STRLCPY HAVE_DECL_STRNLEN HAVE_DECL_STRSEP HAVE_STRSEP HAVE_ACCESS HAVE_GETPAGESIZE HAVE_FILENO HAVE_FTRUNCATE
HAVE_REALPATH HAVE_STAT HAVE_FSTAT HAVE_ISASCII HAVE_ISCNTRL HAVE_TYPEOF HAVE___BUILTIN_EXPECT HAVE___BUILTIN_CLZ
HAVE___BUILTIN_CLZL HAVE___BUILTIN_CLZLL HAVE___BUILTIN_CONSTANT_P HAVE___BUILTIN_BSWAP16 HAVE___BUILTIN_BSWAP32
HAVE___BUILTIN_BSWAP64 WORDS_LITTLEENDIAN""".split()
# (pas HAVE_STRRCHRNUL : stdlib/strrchrnul.c fournit la fonction)
out = []
for l in open("config/config.h.in"):
    m = re.match(r'#\s*undef (\w+)\s*$', l)
    if m:
        k = m[1]
        if k in have or k.startswith("HAVE_FUNC_ATTRIBUTE") or k.startswith("HAVE_FUNC_PTR_ATTRIBUTE"):
            out.append("#define %s 1\n" % k)
        elif k == "PACKAGE_VERSION":
            out.append('#define PACKAGE_VERSION "2.16.03"\n')
        else:
            out.append("/* #undef %s */\n" % k)
        continue
    out.append(l)
open("config/config.h", "w").write("/* config.h -- pour Korin OS, tiré de config.h.in par tools/webkit/local/build-nasm.sh */\n" + "".join(out))
# les sources : LIBOBJ_NW, WARNOBJ et NASM de Makefile.in
mk = open("Makefile.in").read().replace("\\\n", " ")
def var(n):
    m = re.search(r'^' + n + r'\s*=(.*)$', mk, re.M)
    return m.group(1).split() if m else []
objs = [o.replace("$(O)", "o") for o in var("LIBOBJ_NW") + var("WARNOBJ") + var("NASM")]
open("objs.txt", "w").write("\n".join(o[:-2] + ".c" for o in objs))
EOF
mkdir -p obj
for s in $(cat objs.txt); do
	gcc -O2 -std=gnu11 -fcommon -D_GNU_SOURCE -DHAVE_CONFIG_H -Iinclude -Iconfig -Ix86 -Iasm -Ioutput -Inasmlib -I. \
		-c "$s" -o "obj/$(echo "$s" | tr / _ | sed 's/\.c$/.o/')"
done
gcc -o nasm obj/*.o
cp nasm "$W/bin/nasm"
chmod +x "$W/bin/nasm"	# (hors de /bin, le bit d'exécution est à donner)
"$W/bin/nasm" -v
