# env.sh -- l'environnement commun des scripts de tools/webkit (a sourcer, dans WSL) : le portage
# de WebKit (le navigateur Jet d'Onyx) sur ARMPI, docs/WEBKIT.md.
#
#   ARMPI        la racine du depot (ce worktree ou le depot principal)
#   EXTRA        les sources tierces (src/extra du depot principal : WebKit et ses dependances,
#                hors de git) ; par defaut $ARMPI/src/extra s'il y a webkit/, sinon celui du depot principal
#   WK_OUT       tout ce qui est construit, hors du depot (defaut ~/armpi-webkit-build) :
#                  sysroot/   libposix + les dependances (en-tetes, archives, .pc, specs)
#                  ports/     arbres de construction des dependances, outils d'essai (bin/*.elf)
#                  webkit/    copie de travail de WebKit (copiee de $EXTRA/webkit : le disque de WSL
#                             est bien plus rapide que /mnt/c pour des milliers de fichiers)
#                  jsc-*/, webkit-*/  arbres de construction de WebKit
#   JOBS         travaux paralleles (defaut : nombre de processeurs)
#   ARCH         aarch64 (defaut : le Pi) ou x86_64 (Korin PC, docs/etude/PC-PILOTES.md "Jet pour le PC") :
#                la chaine x86_64-onyx-elf, libposix du PC, WK_OUT par defaut ~/korin-webkit-x86_64 ; sans
#                ARCH, les variables du Pi sont celles d'avant (ONYX_ARCH et ONYX_CPU_FLAGS en plus)
#
# On garde le nom de systeme CMake « Onyx » et la macro __onyx__ (WTF_OS_ONYX) : la serie de patchs
# d'Onyx s'applique telle quelle ; __armpi__ dit qu'on est sur ARMPI (libposix).
_wk_here=$(cd "$(dirname "${WK_ENV_SELF:-$0}")" && pwd)
case $_wk_here in */tools/webkit/*) _wk_here=${_wk_here%/tools/webkit/*}/tools/webkit ;; esac
: "${ARMPI:=$(cd "$_wk_here/../.." && pwd)}"
if [ -z "${EXTRA:-}" ]; then
	if [ -d "$ARMPI/src/extra/webkit" ]; then EXTRA=$ARMPI/src/extra; else EXTRA=${ARMPI%%/.claude/worktrees/*}/src/extra; fi
fi
: "${ARCH:=aarch64}"
case $ARCH in
aarch64) : "${WK_OUT:=$HOME/armpi-webkit-build}" ;;
x86_64) : "${WK_OUT:=$HOME/korin-webkit-x86_64}" ;;
*) echo "env.sh : ARCH vaut aarch64 ou x86_64" >&2; exit 1 ;;
esac
: "${ONYX_SYSROOT:=$WK_OUT/sysroot}"
: "${PORTS_OUT:=$WK_OUT/ports}"
: "${WEBKIT_DIR:=$WK_OUT/webkit}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
ONYX_TOOLCHAIN_PREFIX=aarch64-onyx-elf-
TC_BIN=${KORIN_TOOLCHAINS:-/opt/toolchains}/aarch64-onyx-elf-14.2/bin
ONYX_CPU_FLAGS=-mcpu=cortex-a72
if [ "$ARCH" = x86_64 ]; then
	ONYX_TOOLCHAIN_PREFIX=x86_64-onyx-elf-
	TC_BIN=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin
	# (le processeur de Korin PC : le ZBook, i7-11800H, Tiger Lake ; décision de l'utilisateur du 22/11/2026. Pas
	# d'AVX-512 : le noyau ne l'autorise pas, XCR0 = 0x7. Le noyau, libposix et les apps du PC restent en x86-64 de
	# base. docs/agents/JET-LOCAL-BUILD.md)
	ONYX_CPU_FLAGS="-march=tigerlake -mno-avx512f -mtune=tigerlake"
fi
ONYX_ARCH=$ARCH
case ":$PATH:" in *":$TC_BIN:"*) ;; *) PATH=$TC_BIN:$PATH ;; esac
TOOLCHAIN_FILE=$ARMPI/tools/webkit/armpi-toolchain.cmake
CC=${ONYX_TOOLCHAIN_PREFIX}gcc
CXX=${ONYX_TOOLCHAIN_PREFIX}g++
AR=${ONYX_TOOLCHAIN_PREFIX}ar
RANLIB=${ONYX_TOOLCHAIN_PREFIX}ranlib
STRIP=${ONYX_TOOLCHAIN_PREFIX}strip
CFLAGS="${ONYX_CPU_FLAGS:+$ONYX_CPU_FLAGS }-specs=$ONYX_SYSROOT/lib/onyx-cc.specs -O2 -ffunction-sections -fdata-sections -fno-pic -fno-pie -isystem $ONYX_SYSROOT/include -DFD_SETSIZE=1024"
CXXFLAGS="$CFLAGS"
LDFLAGS="-specs=$ONYX_SYSROOT/lib/onyx.specs -L$ONYX_SYSROOT/lib"
PKG_CONFIG_LIBDIR=$ONYX_SYSROOT/lib/pkgconfig
PKG_CONFIG_SYSROOT_DIR=
export PATH ONYX_ARCH ONYX_CPU_FLAGS ARMPI EXTRA WK_OUT ONYX_SYSROOT PORTS_OUT WEBKIT_DIR JOBS ONYX_TOOLCHAIN_PREFIX TOOLCHAIN_FILE \
	CC CXX AR RANLIB STRIP CFLAGS CXXFLAGS LDFLAGS PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
export PYTHONDONTWRITEBYTECODE=1
unset _wk_here
