#!/bin/sh
# bash : GNU bash 5.2.37 pour Korin OS (docs/BASH.md), construit sur le PC (WSL, Linux), hors du selfhost,
# sur libposix et newlib comme make (tools/gcc/make.sh). A lancer depuis la racine du depot, apres
# tools/posix/build.sh :
#     sh tools/bash/build.sh [archive]
# L'archive : bash-5.2.37 telle quelle (BASH_TAR, par defaut /opt/src/bash/bash_5.2.37.orig.tar.xz, celle
# de Debian : la meme source que ftp.gnu.org). Les retouches : tools/bash/patches/*.patch, dans l'ordre.
# Produit prebuilt/bash (copie sur SD0:/bin/bash par build.ps1 et tools/ci/carte_linux.py).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
WORK=${WORK:-$HOME/.cache/korin-bash}
TAR=${1:-${BASH_TAR:-/opt/src/bash/bash_5.2.37.orig.tar.xz}}
V=bash-5.2.37
SPECS=$ROOT/build/posix/lib/posix.specs
export PATH="$TOOL/bin:$PATH"
[ -f "$SPECS" ] || { echo "bash : lancer d'abord tools/posix/build.sh" >&2; exit 1; }
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
"$WORK/src/$V/configure" --build=x86_64-pc-linux-gnu --host=aarch64-onyx-elf --prefix=/ \
	--without-bash-malloc --disable-nls --disable-job-control --enable-static-link \
	--without-curses --enable-readline --with-installed-readline=no \
	CC="$WORK/bin/korin-cc" AR=${TC}ar RANLIB=${TC}ranlib CFLAGS="-O2 -g0 -mcpu=cortex-a72 $WARN" \
	> configure.log 2>&1 || { tail -30 configure.log; exit 1; }
make -j8 > make.log 2>&1 || { grep -n 'error:\|undefined reference' make.log | head -40; exit 1; }
${TC}strip bash -o "$ROOT/prebuilt/bash"
ls -l "$ROOT/prebuilt/bash"
# TESTS=1 : la suite d'essais de bash (tests/, et ses aides recho, zecho, printenv, xcase compilees pour
# Korin OS) dans build/bash/tests, a copier sur la carte (docs/BASH.md, « Essais »)
if [ "${TESTS:-0}" = 1 ]; then
	T=$ROOT/build/bash/tests
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
