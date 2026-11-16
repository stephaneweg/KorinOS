#!/bin/sh
# git : git 2.51.0 pour Korin (docs/GIT.md), statique sur newlib + libposix, par la chaine d'Onyx,
# avec libcurl (HTTP, HTTPS), OpenSSL et zlib construits ici. Hors du selfhost, comme les autres
# ports C. A lancer dans WSL depuis la racine du depot, apres sh tools/posix/build.sh :
#     sh tools/git/build.sh
#
# Les sources sont des archives locales (pas de telechargement), dans $DEPS :
#   git_2.51.0.orig.tar.xz, zlib_1.3.dfsg+really1.3.1.orig.tar.gz (zlib 1.3.1),
#   openssl_3.5.5.orig.tar.gz, curl_8.18.0.orig.tar.gz (archives .orig d'Ubuntu, sources amont).
#
# Produit :
#   build/git/zlib, build/git/openssl, build/git/curl   include/ et lib/ (statiques)
#   build/git/sdroot/                                   l'arbre pour la carte (/), recopie par
#                                                       build.ps1 et tools/ci/carte_linux.py :
#     bin/git                    le programme (commandes internes)
#     bin/git-core/              gitexecdir : git-remote-http(s), git-http-fetch, scripts
#     etc/gitconfig              la configuration du systeme (tools/git/gitconfig)
#     etc/git-core/templates/    modele de git init
# Arbres de construction : $WORK (hors de git). REBUILD=1 : tout reconstruire.
#
# ARCH=x86_64 : pour Korin sur PC (chaine x86_64-onyx-elf, libposix de build/pc/posix, apres
# ARCH=x86_64 sh tools/posix/build.sh) ; OpenSSL est celui du PC (build/pc/thirdparty/openssl,
# ARCH=x86_64 sh tools/openssl/build.sh, lance ici s'il manque) ; produit build/pc/git (zlib, curl,
# sdroot/), pose dans l'arbre du PC par tools/pc/sysroot_git.sh. Les archives sont aussi cherchees
# dans ~/.cache/korin-src/git-deps.
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCH=${ARCH:-aarch64}
case $ARCH in
aarch64) TARGET=aarch64-onyx-elf; WORK=${WORK:-$HOME/.cache/armpi-git}
	SPECS=${SPECS:-$ROOT/build/posix/lib/posix.specs}; B=$ROOT/build/git; CPU="-mcpu=cortex-a72 " ;;
x86_64) TARGET=x86_64-onyx-elf; WORK=${WORK:-$HOME/.cache/korin-git-x86_64}
	SPECS=${SPECS:-$ROOT/build/pc/posix/lib/posix.specs}; B=$ROOT/build/pc/git; CPU="" ;;
*) echo "git : ARCH=aarch64 ou x86_64" >&2; exit 1 ;;
esac
TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/$TARGET-14.2}
TC=$TARGET-
DEPS=${DEPS:-/opt/src/git-deps}
[ -d "$DEPS" ] || [ ! -d "$HOME/.cache/korin-src/git-deps" ] || DEPS=$HOME/.cache/korin-src/git-deps
JOBS=${JOBS:-$(nproc)}
OUT=$B/sdroot
P=$ROOT/tools/git
export PATH="$TOOL/bin:$PATH"
[ -f "$SPECS" ] || { echo "libposix absente : ARCH=$ARCH sh tools/posix/build.sh" >&2; exit 1; }
[ "${REBUILD:-0}" = 1 ] && rm -rf "$B/zlib" "$B/openssl" "$B/curl" "$WORK"
mkdir -p "$WORK/src" "$B"

CF="${CPU}-O2 -g0 -fno-stack-protector -ffunction-sections -fdata-sections"
CC="${TC}gcc -specs=$SPECS"

# --- zlib 1.3.1 : les .c dans libz.a (comme tools/webkit/ports/common.sh)
if [ ! -f "$B/zlib/lib/libz.a" ]; then
	echo "zlib"
	rm -rf "$WORK/src/zlib-1.3.1" "$WORK/zlib"
	tar xzf "$DEPS/zlib_1.3.dfsg+really1.3.1.orig.tar.gz" -C "$WORK/src"
	Z=$WORK/src/zlib-1.3.1
	mkdir -p "$WORK/zlib" "$B/zlib/lib" "$B/zlib/include"
	for s in "$Z"/*.c; do
		$CC $CF -w -DHAVE_UNISTD_H -DZ_HAVE_UNISTD_H -DHAVE_STDARG_H -c "$s" -o "$WORK/zlib/$(basename "$s" .c).o"
	done
	${TC}ar rcs "$B/zlib/lib/libz.a" "$WORK/zlib"/*.o
	cp "$Z/zlib.h" "$Z/zconf.h" "$B/zlib/include/"
fi

# --- OpenSSL 3.5.5 : tools/openssl/build.sh avec l'archive locale ; armpi.patch est fait pour
# 3.5.9, patches/openssl-3.5.5.patch est le meme pour 3.5.5 (une ligne de contexte change)
# Le PC : l'OpenSSL du PC (3.5.9, cible korin-x86_64 d'armpi.patch), celui des apps et de Jet
SSL=$B/openssl
if [ "$ARCH" = x86_64 ]; then
	SSL=$ROOT/build/pc/thirdparty/openssl
	if [ ! -f "$SSL/lib/libssl.a" ]; then
		echo "openssl"
		ARCH=x86_64 JOBS=$JOBS sh "$ROOT/tools/openssl/build.sh"
	fi
elif [ ! -f "$B/openssl/lib/libssl.a" ]; then
	echo "openssl"
	OPENSSL_TAR="$DEPS/openssl_3.5.5.orig.tar.gz" OPENSSL_VER=3.5.5 \
	OPENSSL_PATCH="$P/patches/openssl-3.5.5.patch" WORK="$WORK/openssl" OUT="$B/openssl" \
	NO_TESTS=1 JOBS=$JOBS SPECS=$SPECS sh "$ROOT/tools/openssl/build.sh"
fi

# --- curl 8.18.0 : libcurl.a seule, par configure. HTTP/1.1 seulement (pas de nghttp2 ici),
# IPv4, resolveur dans un thread, pas de brotli / zstd / psl / idn / ldap ; le reveil du multi
# par un tube (pas de socketpair, comme tools/webkit/ports/curl) ; certificats /etc/ca-bundle
if [ ! -f "$B/curl/lib/libcurl.a" ]; then
	echo "curl"
	rm -rf "$WORK/src/curl-8.18.0" "$WORK/curl"
	tar xzf "$DEPS/curl_8.18.0.orig.tar.gz" -C "$WORK/src"
	mkdir -p "$WORK/curl"
	cd "$WORK/curl"
	ac_cv_func_socketpair=no curl_cv_func_socketpair=no \
	"$WORK/src/curl-8.18.0/configure" --build=x86_64-pc-linux-gnu --host=$TARGET \
		--prefix="$B/curl" --disable-shared --enable-static --disable-docs --disable-manual \
		--with-openssl="$SSL" --with-zlib="$B/zlib" --without-nghttp2 --without-nghttp3 \
		--without-ngtcp2 --without-brotli --without-zstd --without-libpsl --without-libidn2 \
		--without-libssh2 --without-librtmp --disable-ldap --disable-ldaps --disable-ipv6 \
		--disable-unix-sockets --enable-threaded-resolver --disable-socketpair --disable-rtsp \
		--disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp \
		--disable-gopher --disable-mqtt --disable-websockets --disable-ntlm --disable-kerberos-auth \
		--with-ca-bundle=/etc/ca-bundle --without-ca-path --without-ca-fallback \
		CC="$CC" AR=${TC}ar RANLIB=${TC}ranlib CFLAGS="$CF" LIBS="-lz" \
		CPPFLAGS="-I$B/zlib/include" LDFLAGS="-L$B/zlib/lib" > configure.log 2>&1 \
		|| { tail -30 configure.log; exit 1; }
	grep -E "Protocols:|Features:|SSL:" configure.log || true
	make -j"$JOBS" -C lib > make.log 2>&1 || { grep -n -B2 -A8 "error" make.log | head -60; exit 1; }
	make -C lib install > install.log 2>&1 && make -C include install >> install.log 2>&1 \
		|| { tail -20 install.log; exit 1; }
	${TC}strip -g "$B/curl/lib/libcurl.a"
	cd "$ROOT"
fi

# --- git 2.51.0 : config.mak (tools/git/config.mak) et les patchs de tools/git/patches
G=$WORK/src/git-2.51.0
if [ ! -f "$G/.armpi-patched" ]; then
	rm -rf "$G"
	tar xJf "$DEPS/git_2.51.0.orig.tar.xz" -C "$WORK/src"
	for p in "$P"/patches/git-*.patch; do
		[ -f "$p" ] && patch -d "$G" -p1 < "$p"
	done
	touch "$G/.armpi-patched"
fi
sed -e "s|@SPECS@|$SPECS|g" -e "s|@DEP@|$B|g" -e "s|@SSL@|$SSL|g" -e "s|@TC@|$TC|g" \
	-e "s|@CPU@|$CPU|g" "$P/config.mak" > "$G/config.mak"
# uname_* : pas les reglages de l'hote (config.mak.uname) ; tout est dans config.mak
UN="uname_S=Korin uname_M=$ARCH uname_O=Korin uname_R=1 uname_P=$ARCH uname_V=1"
make -C "$G" -j"$JOBS" $UN all > "$WORK/git-make.log" 2>&1 \
	|| { grep -n -B2 -A6 "error\|undefined" "$WORK/git-make.log" | head -80; exit 1; }

# --- l'arbre de la carte
rm -rf "$OUT"
mkdir -p "$OUT/bin/git-core" "$OUT/etc/git-core"
${TC}strip "$G/git" -o "$OUT/bin/git"
# git-remote-http, -https : le meme programme (pas de liens sur FAT) ; il fait aussi le HTTP
# « simple » (git-http-fetch, 5,7 Mo, n'est pas copie : il ne sert qu'appele a la main)
${TC}strip "$G/git-remote-http" -o "$OUT/bin/git-core/git-remote-http"
cp "$OUT/bin/git-core/git-remote-http" "$OUT/bin/git-core/git-remote-https"
# les scripts (git submodule, mergetool...) : il leur faut un sh POSIX (bash, etape 5)
for s in git-sh-setup git-sh-i18n git-submodule git-merge-octopus git-merge-one-file \
	git-merge-resolve git-mergetool git-mergetool--lib git-difftool--helper git-request-pull \
	git-quiltimport git-web--browse git-filter-branch; do
	[ -f "$G/$s" ] && cp "$G/$s" "$OUT/bin/git-core/"
done
cp "$P/gitconfig" "$OUT/etc/gitconfig"
# le modele de git init : sans les exemples de crochets
make -C "$G/templates" $UN > /dev/null
mkdir -p "$OUT/etc/git-core/templates"
cp -r "$G/templates/blt/." "$OUT/etc/git-core/templates/"
rm -rf "$OUT/etc/git-core/templates/hooks"
ls -l "$OUT/bin/git" "$OUT/bin/git-core"
