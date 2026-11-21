#!/bin/sh
# bash : GNU bash 5.2.37 pour Korin OS (docs/BASH.md), construit sur le PC (WSL, Linux), hors du selfhost,
# sur libposix et newlib comme make (tools/gcc/make.sh). A lancer depuis la racine du depot, apres
# tools/posix/build.sh :
#     sh tools/bash/build.sh [archive]
# L'archive : bash-5.2.37 telle quelle (BASH_TAR, par defaut /opt/src/bash/bash_5.2.37.orig.tar.xz, celle
# de Debian : la meme source que ftp.gnu.org). Les retouches : tools/bash/patches/*.patch, dans l'ordre.
# Produit prebuilt/bash (copie sur /bin/bash par build.ps1 et tools/ci/carte_linux.py).
# ARCH=x86_64 : pour Korin sur PC (chaine x86_64-onyx-elf, libposix de build/pc/posix, apres
# ARCH=x86_64 sh tools/posix/build.sh) ; produit build/pc/bash/bash (/bin/bash de tools/pc/sysroot.sh).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCH=${ARCH:-aarch64}
case $ARCH in
aarch64) TARGET=aarch64-onyx-elf; WORK=${WORK:-$HOME/.cache/korin-bash}; SPECS=$ROOT/build/posix/lib/posix.specs
	CPU="-mcpu=cortex-a72 "; OUT=$ROOT/prebuilt/bash; TOUT=$ROOT/build/bash/tests ;;
x86_64) TARGET=x86_64-onyx-elf; WORK=${WORK:-$HOME/.cache/korin-bash-x86_64}; SPECS=$ROOT/build/pc/posix/lib/posix.specs
	CPU=""; OUT=$ROOT/build/pc/bash/bash; TOUT=$ROOT/build/pc/bash/tests ;;
*) echo "bash : ARCH=aarch64 ou x86_64" >&2; exit 1 ;;
esac
TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/$TARGET-14.2}
TC=$TOOL/bin/$TARGET-
TAR=${1:-${BASH_TAR:-/opt/src/bash/bash_5.2.37.orig.tar.xz}}
# (l'archive gardee avec les sources de korin-srcs, si /opt/src n'existe pas)
[ -f "$TAR" ] || [ ! -f "$HOME/.cache/korin-src/bash/bash_5.2.37.orig.tar.xz" ] || TAR=$HOME/.cache/korin-src/bash/bash_5.2.37.orig.tar.xz
V=bash-5.2.37
export PATH="$TOOL/bin:$PATH"
[ -f "$SPECS" ] || { echo "bash : lancer d'abord tools/posix/build.sh (ARCH=$ARCH)" >&2; exit 1; }
[ -f "$TAR" ] || { echo "bash : archive absente : $TAR" >&2; exit 1; }
rm -rf "${WORK:?}/src" "$WORK/build"
mkdir -p "$WORK/src" "$WORK/build" "$WORK/bin"
tar -C "$WORK/src" -xf "$TAR"
for p in "$ROOT"/tools/bash/patches/*.patch; do
	[ -f "$p" ] || continue
	patch -d "$WORK/src/$V" -p1 -s < "$p"
done
# le compilateur : la chaine d'Onyx et les specs de libposix
cat > "$WORK/bin/korin-cc" <<CC
#!/bin/sh
exec ${TC}gcc -specs=$SPECS "\$@"
CC
chmod +x "$WORK/bin/korin-cc"
cd "$WORK/build"
# le code de bash date d'avant GCC 14 : declarations implicites (parse.y sans jobs.h, termcap sans unistd.h),
# variables communes (PC, UP, BC dans readline et termcap)
WARN="-fcommon -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion"
# ce que configure ne peut pas essayer en compilation croisee ; getenv et setenv de bash : non (ceux de
# libposix servent avant main : TERM=xterm-256color sur un pty)
export ac_cv_func_fork=yes ac_cv_func_vfork=yes ac_cv_func_fork_works=yes ac_cv_func_vfork_works=yes
export bash_cv_func_sigsetjmp=present bash_cv_func_strcoll_broken=no bash_cv_getcwd_malloc=yes
export bash_cv_job_control_missing=present bash_cv_sys_named_pipes=missing bash_cv_dev_fd=absent
export bash_cv_dev_stdin=absent bash_cv_must_reinstall_sighandlers=no bash_cv_printf_a_format=yes
export bash_cv_func_ctype_nonascii=no bash_cv_wcwidth_broken=no bash_cv_unusable_rtsigs=no
export bash_cv_ulimit_maxfds=yes bash_cv_getenv_redef=no bash_cv_func_snprintf=yes bash_cv_func_vsnprintf=yes
export bash_cv_wexitstatus_offset=8 bash_cv_opendir_not_robust=no bash_cv_pgrp_pipe=no
export bash_cv_fnmatch_equiv_fallback=no bash_cv_termcap_lib=gnutermcap
export ac_cv_func_mmap_fixed_mapped=no
# les outils de la construction (mkbuiltins, mksignames) : le GCC de WSL, en C17 (GCC 15 prend C23, ou f () ne
# prend aucun argument)
export CC_FOR_BUILD="${CC_FOR_BUILD:-gcc -std=gnu17}"
# un here-document court va dans un tube, ecrit d'un coup avant de lancer la commande, s'il tient dans
# HEREDOC_PIPESIZE (par defaut la taille d'un tube mesuree sur la machine de construction, 64 Ko sous Linux) :
# un tube de Korin fait 64 Ko (vfs, depuis le 21/11/2026 ; 4 Ko avant : bash attendait sans fin, le snapshot de
# shell de Claude Code), mais l'anneau garde un octet vide. Plus long : un fichier temporaire.
HEREDOC="-DHEREDOC_PIPESIZE=65535"
"$WORK/src/$V/configure" --build=x86_64-pc-linux-gnu --host=$TARGET --prefix=/ \
	--without-bash-malloc --disable-nls --enable-job-control --enable-static-link \
	--without-curses --enable-readline --with-installed-readline=no \
	CC="$WORK/bin/korin-cc" AR=${TC}ar RANLIB=${TC}ranlib CFLAGS="-O2 -g0 $CPU$WARN $HEREDOC" \
	> configure.log 2>&1 || { tail -30 configure.log; exit 1; }
make -j8 > make.log 2>&1 || { grep -n 'error:\|undefined reference' make.log | head -40; exit 1; }
mkdir -p "$(dirname "$OUT")"
${TC}strip bash -o "$OUT"
ls -l "$OUT"
# TESTS=1 : la suite d'essais de bash (tests/, et ses aides recho, zecho, printenv, xcase compilees pour
# Korin OS) dans build/bash/tests, a copier sur la carte (docs/BASH.md, « Essais »)
if [ "${TESTS:-0}" = 1 ]; then
	T=$TOUT
	rm -rf "$T"
	mkdir -p "$T"
	cp -r "$WORK/src/$V/tests/." "$T/"
	for h in recho zecho printenv xcase; do
		"$WORK/bin/korin-cc" -O2 -g0 $WARN -DHAVE_CONFIG_H -I"$WORK/build" -I"$WORK/src/$V" -I"$WORK/src/$V/include" \
			-I"$WORK/src/$V/lib" "$WORK/src/$V/support/$h.c" -o "$T/$h.elf"
		${TC}strip "$T/$h.elf" -o "$T/$h"
		rm -f "$T/$h.elf"
	done
	ls "$T" | wc -l
fi
