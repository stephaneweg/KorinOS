#!/bin/sh
# posix : la couche POSIX d'ARMPI (src/lib/posix, docs/POSIX.md), reprise de libonyxposix
# d'Onyx, sur newlib 4.4 de la chaine d'Onyx (aarch64-onyx-elf-14.2, TLS natif). Hors du
# selfhost (comme MediaKit) : a lancer dans WSL depuis la racine du depot :
#     sh tools/posix/build.sh
# (chaine : /opt/toolchains/aarch64-onyx-elf-14.2, tools/toolchain/fetch.sh d'Onyx).
#
# Produit :
#   build/posix/include/          en-tetes (src/lib/posix/include)
#   build/posix/lib/libposix.a    la bibliotheque statique
#   build/posix/lib/crt0posix.o   le point d'entree des programmes C
#   build/posix/lib/posix.ld      le script d'edition de liens (ELF64 d'ARMPI, 0x40_0000_0000)
#   build/posix/lib/posix.specs   pour gcc / g++ : -specs=build/posix/lib/posix.specs
#   build/posix/lib/dlhost.ld, dlhost-cxx.ld, dlmod.o   dlopen : les exports d'un hote, l'objet des modules
#   prebuilt/posixtest, prebuilt/posixtest-cxx, prebuilt/ttytest, prebuilt/jobtest, et les modules
#   d'essai du groupe dl, prebuilt/dltest/*.so (copies sur SD0:/bin par build.ps1)
set -e
TC=${TC:-/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC=$ROOT/src/lib/posix
SYSROOT=${SYSROOT:-$ROOT/build/posix}
OUT=${OUT:-/tmp/armpi-posix}
mkdir -p "$OUT/obj" "$SYSROOT/lib" "$SYSROOT/include"

# numeros des appels : generes depuis la seule source, src/inc/syscalls.inc
awk 'BEGIN { print "/* genere par tools/posix/build.sh depuis src/inc/syscalls.inc : ne pas editer */"; print "#ifndef _ARMPI_SYS_H"; print "#define _ARMPI_SYS_H" }
     /^(SYS|THREAD_PRIO|THL)_[A-Z0-9_]+[ \t]*=/ {
	n = $1; sub(/[ \t]*=.*/, "", n); v = $0; sub(/^[^=]*=[ \t]*/, "", v); sub(/[ \t]*;.*/, "", v); sub(/[ \t]+$/, "", v)
	printf "#define %s %s\n", n, v }
     END { print "#endif" }' "$ROOT/src/inc/syscalls.inc" | tr -d '\r' > "$OUT/armpi_sys.h"

CFLAGS="-mcpu=cortex-a72 -O2 -g0 -Wall -Wextra -Wno-unused-parameter -fno-stack-protector \
 -fno-pic -fno-pie -ffunction-sections -fdata-sections -fno-builtin-malloc \
 -D_GNU_SOURCE -DFD_SETSIZE=1024 -isystem $SRC/include -I$SRC -I$OUT"
OBJS=""
for f in "$SRC"/*.c; do
	b=$(basename "$f" .c)
	o="$OUT/obj/$b.o"
	extra=""
	case $b in tls|start|dl|dlopen) extra="-mcmodel=large" ;; esac	# symboles absolus du script de liens
	${TC}gcc $CFLAGS $extra -c "$f" -o "$o"
	OBJS="$OBJS $o"
done
${TC}gcc $CFLAGS -c "$SRC/crt0posix.S" -o "$SYSROOT/lib/crt0posix.o"
${TC}gcc $CFLAGS -c "$SRC/dlmod.S" -o "$SYSROOT/lib/dlmod.o"
# le derouleur de l'hote, exporte (__posix_unw_*), et ses relais pour les modules (libgcc_mod.a)
${TC}gcc $CFLAGS -c "$SRC/dlunwind.S" -o "$OUT/obj/dlunwind.o"
OBJS="$OBJS $OUT/obj/dlunwind.o"
${TC}gcc $CFLAGS -c "$SRC/dlunwmod.S" -o "$OUT/obj/dlunwmod.o"
rm -f "$SYSROOT/lib/libposix.a"
${TC}ar rcs "$SYSROOT/lib/libposix.a" $OBJS
cp "$SRC/posix.ld" "$SYSROOT/lib/posix.ld"
# libdl, libpthread, librt, libutil : vides, tout est dans libposix (comme la glibc 2.34) ; les
# configure qui ajoutent -ldl ou -lpthread se relient sans retouche (la spec passe -L)
for l in dl pthread rt util; do rm -f "$SYSROOT/lib/lib$l.a"; ${TC}ar rcs "$SYSROOT/lib/lib$l.a"; done
rm -rf "$SYSROOT/include"
cp -r "$SRC/include" "$SYSROOT/include"

# dlopen (dlopen.c) : les symboles qu'un hote (-rdynamic) exporte vers ses modules, ceux de
# libposix, newlib, libm et libgcc (dlhost.ld, que la spec passe) et ceux de libstdc++
# (dlhost-cxx.ld, qu'un hote C++ ajoute a sa ligne de liens)
NEWLIB=$(dirname "$(${TC}gcc -print-file-name=libc.a)")
LIBGCC=$(${TC}gcc -print-libgcc-file-name)
TC=$TC sh "$ROOT/tools/posix/dlexport.sh" "$SYSROOT/lib/dlhost.ld" 1 4 "$SYSROOT/lib/libposix.a" \
	"$NEWLIB/libc.a" "$NEWLIB/libm.a" "$LIBGCC"
TC=$TC sh "$ROOT/tools/posix/dlexport.sh" "$SYSROOT/lib/dlhost-cxx.ld" 5 1 "$SYSROOT/lib/libposix.a" \
	"$NEWLIB/libc.a" "$NEWLIB/libm.a" "$LIBGCC" "$NEWLIB/libstdc++.a"

# specs : crt0posix.o et la bibliotheque d'abord (-u pour les remplacements : signal.o avant
# celui de newlib, les gardes C++ par futex, l'ancre des threads de gthr-posix), le script de
# liens, puis newlib apres les bibliotheques du programme. Deux variantes (docs/POSIX.md
# « dlopen ») : -rdynamic, un hote de modules (table des symboles, exports de dlhost.ld, reserve
# TLS de 32 Ko par thread, -tls-reserve=N pour une autre taille) ; -shared, un module (-fPIC) : ni crt0, ni libposix, ni newlib (il
# prend ceux de l'hote), libgcc sans derouleur (libgcc_mod.a) seulement, et dlmod.o.
L=$SYSROOT/lib
# libgcc_mod.a : libgcc pour les modules (-shared), sans le derouleur (unwind-dw2, unwind-dw2-fde,
# emutls) : a sa place, dlunwmod.o, des relais vers celui de l'hote (__posix_unw_*, dlunwind.S),
# comme pour la libc. Sinon chaque module en a une copie privee, sans les .eh_frame que dlopen
# inscrit chez l'hote : une panique Rust levee dans le module (_Unwind_RaiseException) n'y trouve
# aucun cadre et finit en abort (docs/RUST.md, docs/POSIX.md « dlopen »). unwind-c.o
# (__gcc_personality_v0, sans etat) reste.
cp "$LIBGCC" "$L/libgcc_mod.a"
${TC}ar d "$L/libgcc_mod.a" unwind-dw2.o unwind-dw2-fde.o unwind-sjlj.o emutls.o
${TC}ar r "$L/libgcc_mod.a" "$OUT/obj/dlunwmod.o"
cat > "$L/posix.specs" <<EOF
%rename link posix_orig_link
%rename lib posix_orig_lib

*startfile:
%{!shared:$L/crt0posix.o $L/libposix.a}

*endfile:


*link:
%{!shared:%{rdynamic:--no-dynamic-linker %{tls-reserve=*:--defsym=__posix_dl_tls_reserve_abs=%*}%{!tls-reserve=*:--defsym=__posix_dl_tls_reserve_abs=0x8000} $L/dlhost.ld} -T $L/posix.ld -z max-page-size=0x1000 --build-id=none --gc-sections --eh-frame-hdr -u __cxa_guard_acquire -u __posix_pthread_anchor -u abort} %{shared:-z max-page-size=0x1000 --build-id=none --eh-frame-hdr -z now $L/dlmod.o} -L $L %(posix_orig_link)

*lib:
%{!shared:--start-group $L/libposix.a -lc -lm --end-group} %(posix_orig_lib)

*libgcc:
%{shared:-lgcc_mod;:-lgcc}

*cpp:
+ -isystem $SYSROOT/include -DFD_SETSIZE=1024 -D__unix__=1 -D__unix=1 -D__armpi__=1 %{pthread:-D_REENTRANT}

EOF

# NO_TESTS=1 : la bibliotheque seule (tools/webkit/sysroot.sh), sans toucher a prebuilt/
[ "${NO_TESTS:-0}" = 1 ] && exit 0

# posixtest (et posixtest-cxx) : l'essai d'Onyx, porte
TF="-mcpu=cortex-a72 -O2 -g0 -Wall -Wno-unused-parameter -Wno-format-truncation -D_GNU_SOURCE -DFD_SETSIZE=1024 -specs=$L/posix.specs"
# posixtest est un hote de dlopen (groupe dl) : -rdynamic, et libstdc++ exportee (dlhost-cxx.ld)
# pour son module C++ ; les modules d'essai (test/dl) vont a cote de lui, dans prebuilt/dltest
${TC}gcc $TF -c "$SRC/test/posixtest.c" -o "$OUT/posixtest.o"
${TC}g++ $TF -c "$SRC/test/posixtest-dl.cpp" -o "$OUT/posixtest-dl.o"
${TC}g++ $TF -rdynamic "$OUT/posixtest.o" "$OUT/posixtest-dl.o" "$L/dlhost-cxx.ld" -o "$OUT/posixtest.elf"
MF="-mcpu=cortex-a72 -O2 -g0 -Wall -specs=$L/posix.specs -fPIC -shared"
mkdir -p "$ROOT/prebuilt/dltest"
${TC}gcc $MF -Wl,-soname,dltest-a.so "$SRC/test/dl/dltest-a.c" -o "$OUT/dltest-a.so"
${TC}gcc $MF "$SRC/test/dl/dltest-b.c" "$OUT/dltest-a.so" -Wl,-rpath,'$ORIGIN' -o "$OUT/dltest-b.so"
${TC}g++ $MF -nostdlib++ "$SRC/test/dl/dltest-cxx.cpp" -o "$OUT/dltest-cxx.so"
# un programme relie a une bibliotheque partagee (ses DT_NEEDED charges au demarrage)
${TC}gcc $MF -Wl,-soname,libdltestl.so "$SRC/test/dl/dltest-l.c" -o "$OUT/libdltestl.so"
${TC}gcc $TF -rdynamic -tls-reserve=0x10000 "$SRC/test/dl/dllinktest.c" \
	-L"$OUT" -ldltestl -Wl,-rpath,'$ORIGIN' -o "$OUT/dllinktest.elf"
for m in dltest-a dltest-b dltest-cxx libdltestl; do ${TC}strip "$OUT/$m.so" -o "$ROOT/prebuilt/dltest/$m.so"; done
${TC}strip "$OUT/dllinktest.elf" -o "$ROOT/prebuilt/dltest/dllinktest"
${TC}g++ $TF -std=gnu++20 "$SRC/test/posixtest-cxx.cpp" -o "$OUT/posixtest-cxx.elf"
${TC}gcc $TF "$SRC/test/ttytest.c" -o "$OUT/ttytest.elf"
${TC}gcc $TF "$SRC/test/jobtest.c" -o "$OUT/jobtest.elf"
${TC}strip "$OUT/posixtest.elf" -o "$ROOT/prebuilt/posixtest"
${TC}strip "$OUT/posixtest-cxx.elf" -o "$ROOT/prebuilt/posixtest-cxx"
${TC}strip "$OUT/ttytest.elf" -o "$ROOT/prebuilt/ttytest"
${TC}strip "$OUT/jobtest.elf" -o "$ROOT/prebuilt/jobtest"
${TC}readelf -lh "$ROOT/prebuilt/posixtest" | grep -E "Type:|Entry|LOAD|TLS" || true
ls -l "$SYSROOT/lib" "$ROOT/prebuilt/posixtest" "$ROOT/prebuilt/posixtest-cxx" "$ROOT/prebuilt/ttytest" "$ROOT/prebuilt/jobtest"
