#!/bin/sh
# devtools/build.sh : les outils de construction de WebKit (et d'autres projets) pour Korin PC, construits
# dans le cloud ou WSL par la chaîne croisée x86_64-onyx-elf sur la libposix du PC, chacun en paquet à part
# (tools/pkg/packages-pc.ini ; docs/agents/JET-LOCAL-BUILD.md, point 4 : reconstruire WebKit dans Korin avec
# CMake et Ninja, sans le SDK préparé dans le cloud). nasm et ninja : tools/webkit/local.
#
#     sh tools/devtools/build.sh                tous : unifdef m4 bison flex gperf pkgconf perl ruby cmake
#     sh tools/devtools/build.sh gperf flex     seulement ceux-ci
#
# Avant : ARCH=x86_64 sh tools/posix/build.sh (build/pc/posix). Sorties : build/pc/devtools/<outil>/, un arbre
# à la racine de Korin (devtools/gcc/bin/<outil>, devtools/gcc/share/...) : celui de son paquet
# (python3 tools/pkg/pub.py <outil> --new --tree build/pc/devtools/<outil>). Sources : sites officiels,
# versions et SHA-256 fixés, dans ~/.cache/korin-src/devtools. Le processeur : x86-64 de base (comme les
# programmes du PC ; seuls WebKit et ses bibliothèques visent Tiger Lake).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TARGET=x86_64-onyx-elf
TOOL=${KORIN_TOOLCHAINS:-/opt/toolchains}/$TARGET-14.2
TC=$TOOL/bin/$TARGET-
SPECS=$ROOT/build/pc/posix/lib/posix.specs
DL=${KORIN_SRC_CACHE:-$HOME/.cache/korin-src}/devtools
WORK=${WORK:-$HOME/.cache/korin-devtools}
OUT=$ROOT/build/pc/devtools
JOBS=${JOBS:-$(nproc)}
PREFIX=/devtools/gcc
export PATH="$TOOL/bin:$PATH"
[ -f "$SPECS" ] || { echo "devtools : lancer d'abord ARCH=x86_64 sh tools/posix/build.sh" >&2; exit 1; }
mkdir -p "$DL" "$WORK/bin" "$OUT"
# (un -lc donné en plus, avant libposix, ferait prendre à newlib ce que libposix remplace : getentropy... ; les
# specs mettent libc à sa place)
cat > "$WORK/bin/korin-cc" <<EOF
#!/bin/sh
for a; do shift; [ "\$a" = -lc ] || set -- "\$@" "\$a"; done
exec ${TC}gcc -specs=$SPECS "\$@"
EOF
cat > "$WORK/bin/korin-c++" <<EOF
#!/bin/sh
for a; do shift; [ "\$a" = -lc ] || set -- "\$@" "\$a"; done
exec ${TC}g++ -specs=$SPECS "\$@"
EOF
chmod +x "$WORK/bin/korin-cc" "$WORK/bin/korin-c++"
CC=$WORK/bin/korin-cc
CXX=$WORK/bin/korin-c++
HOSTCONF="--build=x86_64-pc-linux-gnu --host=$TARGET --prefix=$PREFIX --disable-nls --disable-dependency-tracking"
CROSS="CC=$CC CXX=$CXX AR=${TC}ar RANLIB=${TC}ranlib STRIP=${TC}strip"
# (gnulib : ce que configure ne peut pas essayer en croisé ; la libposix de Korin)
export gl_cv_func_malloc_0_nonnull=yes ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes
export gl_cv_func_getcwd_null=yes gl_cv_func_getcwd_path_max=yes gl_cv_func_working_mktime=yes
export ac_cv_func_fork=yes ac_cv_func_fork_works=yes ac_cv_func_vfork=no ac_cv_func_vfork_works=no
export gl_cv_func_strerror_0_works=yes gl_cv_func_printf_positions=yes

say () { printf '\n=== %s ===\n' "$*"; }
die () { echo "devtools : $*" >&2; exit 1; }

# fetch <fichier> <url> <sha256>
fetch () {
	if [ -f "$DL/$1" ] && [ "$(sha256sum "$DL/$1" | cut -d' ' -f1)" = "$3" ]; then return 0; fi
	curl -fsSL --retry 3 -o "$DL/$1.part" "$2" || die "téléchargement : $2"
	[ "$(sha256sum "$DL/$1.part" | cut -d' ' -f1)" = "$3" ] || { rm -f "$DL/$1.part"; die "sha256 faux : $2"; }
	mv "$DL/$1.part" "$DL/$1"
}

# unpack <fichier> <dossier> : les sources à neuf dans $WORK/src/<dossier>
unpack () {
	rm -rf "${WORK:?}/src/$2"
	mkdir -p "$WORK/src"
	tar -C "$WORK/src" -xf "$DL/$1"
	[ -d "$WORK/src/$2" ] || die "archive $1 : pas de $2"
	for p in "$ROOT"/tools/devtools/patches/"${2%-*}"-*.patch; do
		[ -f "$p" ] || continue
		patch -d "$WORK/src/$2" -p1 -s < "$p" || die "patch $p"
	done
}

# tree <outil> : son arbre à neuf -> $T
tree () {
	T=$OUT/$1
	rm -rf "${T:?}"
	mkdir -p "$T$PREFIX/bin"
}

# install_bin <binaire> [nom]
install_bin () {
	${TC}strip "$1" -o "$T$PREFIX/bin/${2:-$(basename "$1")}"
	chmod +x "$T$PREFIX/bin/${2:-$(basename "$1")}"
}

# gnu <dossier> <journal> [options de configure] : configure, make (dans $WORK/build/<dossier>)
gnu () {
	_d=$1; shift
	rm -rf "${WORK:?}/build/$_d"
	mkdir -p "$WORK/build/$_d"
	cd "$WORK/build/$_d"
	# shellcheck disable=SC2086
	"$WORK/src/$_d/configure" $HOSTCONF $CROSS CFLAGS="-O2 -g0" CXXFLAGS="-O2 -g0" "$@" >configure.log 2>&1 ||
		{ tail -30 configure.log; die "$_d : configure"; }
	# (MAKE_DIRS : seulement ces sous-dossiers, dans l'ordre ; les essais de gnulib ne se compilent pas tous ici)
	if [ -n "${MAKE_DIRS:-}" ]; then
		for _m in $MAKE_DIRS; do
			make -j"$JOBS" -C "$_m" >>make.log 2>&1 || { grep -n 'error\|undefined reference' make.log | head -40; die "$_d : make $_m"; }
		done
	else
		make -j"$JOBS" >make.log 2>&1 || { grep -n 'error\|undefined reference' make.log | head -40; die "$_d : make"; }
	fi
	MAKE_DIRS=
}

# ---- unifdef 2.12 (BSD) : un seul source
b_unifdef () {
	say unifdef
	fetch unifdef-2.12.tar.xz https://dotat.at/prog/unifdef/unifdef-2.12.tar.xz \
		43ce0f02ecdcdc723b2475575563ddb192e988c886d368260bc0a63aee3ac400
	unpack unifdef-2.12.tar.xz unifdef-2.12
	tree unifdef
	cd "$WORK/src/unifdef-2.12"
	$CC -O2 -g0 -o unifdef.elf unifdef.c
	install_bin unifdef.elf unifdef
	mkdir -p "$T$PREFIX/share/doc/unifdef"
	cp COPYING "$T$PREFIX/share/doc/unifdef/"
}

# ---- GNU m4 1.4.19 (bison l'appelle à chaque grammaire)
b_m4 () {
	say m4
	fetch m4-1.4.19.tar.xz https://ftp.gnu.org/gnu/m4/m4-1.4.19.tar.xz \
		63aede5c6d33b6d9b13511cd0be2cac046f2e70fd0a07aa9573a04a82783af96
	unpack m4-1.4.19.tar.xz m4-1.4.19
	MAKE_DIRS="lib src" gnu m4-1.4.19
	tree m4
	install_bin src/m4
}

# ---- GNU bison 3.8.2 : bison, ses squelettes dans /devtools/gcc/share/bison (m4 : le paquet m4)
b_bison () {
	say bison
	fetch bison-3.8.2.tar.xz https://ftp.gnu.org/gnu/bison/bison-3.8.2.tar.xz \
		9bba0214ccf7f1079c5d59210045227bcf619519840ebfa80cd3849cff5a5bf2
	unpack bison-3.8.2.tar.xz bison-3.8.2
	MAKE_DIRS=. gnu bison-3.8.2 M4=$PREFIX/bin/m4 ac_cv_path_M4=$PREFIX/bin/m4 gl_cv_prog_m4_gnu=yes
	tree bison
	install_bin src/bison
	mkdir -p "$T$PREFIX/share"
	cp -r "$WORK/src/bison-3.8.2/data" "$T$PREFIX/share/bison"
	rm -f "$T$PREFIX/share/bison/Makefile"*
}

# ---- flex 2.6.4 (le scanner de flex lui-même : son scan.c fourni, pas de stage1flex à lancer)
b_flex () {
	say flex
	fetch flex-2.6.4.tar.gz https://github.com/westes/flex/releases/download/v2.6.4/flex-2.6.4.tar.gz \
		e87aae032bf07c26f85ac0ed3250998c37621d95f8bd748b31f15b33c45ee995
	unpack flex-2.6.4.tar.gz flex-2.6.4
	gnu flex-2.6.4 M4=$PREFIX/bin/m4 ac_cv_path_M4=$PREFIX/bin/m4 || true
	tree flex
	install_bin src/flex
	mkdir -p "$T$PREFIX/lib" "$T$PREFIX/include"
	cp src/.libs/libfl.a "$T$PREFIX/lib/" 2>/dev/null || cp src/libfl.a "$T$PREFIX/lib/" 2>/dev/null || true
	cp "$WORK/src/flex-2.6.4/src/FlexLexer.h" "$T$PREFIX/include/"
}

# ---- GNU gperf 3.1 (C++)
b_gperf () {
	say gperf
	fetch gperf-3.1.tar.gz https://ftp.gnu.org/gnu/gperf/gperf-3.1.tar.gz \
		588546b945bba4b70b6a3a616e80b4ab466e3f33024a352fc2198112cdbb3ae2
	unpack gperf-3.1.tar.gz gperf-3.1
	gnu gperf-3.1 CXXFLAGS="-O2 -g0 -std=gnu++14"
	tree gperf
	install_bin src/gperf
}

# ---- pkgconf 2.3.0 (pkg-config de CMake et des configure)
b_pkgconf () {
	say pkgconf
	fetch pkgconf-2.3.0.tar.xz https://distfiles.ariadne.space/pkgconf/pkgconf-2.3.0.tar.xz \
		3a9080ac51d03615e7c1910a0a2a8df08424892b5f13b0628a204d3fcce0ea8b
	unpack pkgconf-2.3.0.tar.xz pkgconf-2.3.0
	gnu pkgconf-2.3.0 --disable-shared --enable-static --with-system-libdir=$PREFIX/lib \
		--with-system-includedir=$PREFIX/include --with-pkg-config-dir=$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig
	tree pkgconf
	install_bin pkgconf
	cp "$T$PREFIX/bin/pkgconf" "$T$PREFIX/bin/pkg-config"
}

NAMES=${*:-unifdef m4 bison flex gperf pkgconf perl ruby cmake}
for n in $NAMES; do
	case $n in
	perl | ruby | cmake) . "$ROOT/tools/devtools/$n.sh" ;;
	*) "b_$n" ;;
	esac
	cd "$ROOT"
	find "$OUT/$n" -type f | sed "s|$OUT/$n/||" | sort
done
