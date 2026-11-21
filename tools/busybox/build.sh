#!/bin/sh
# busybox : BusyBox 1.37.0 pour Korin OS PC (les « unixtools » : awk, xargs, which, env, sort -k, sed -E...),
# construit dans WSL ou le cloud sur libposix et newlib, comme bash (tools/bash/build.sh). Les commandes :
# tools/busybox/applets.txt ; les retouches : tools/busybox/patches/*.patch. A lancer depuis la racine du depot,
# apres ARCH=x86_64 sh tools/posix/build.sh :
#     sh tools/busybox/build.sh [archive]
# L'archive : busybox-1.37.0.tar.bz2 de busybox.net (BUSYBOX_TAR, par defaut ~/.cache/korin-src/busybox/).
# Produit build/pc/busybox/devtools/bin : busybox et un lanceur par commande (pas de liens sous Korin : un
# script « exec /devtools/bin/busybox <commande> "$@" »), la liste dans build/pc/busybox/applets.list.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TARGET=x86_64-onyx-elf
WORK=${WORK:-$HOME/.cache/korin-busybox-x86_64}
SPECS=$ROOT/build/pc/posix/lib/posix.specs
OUT=$ROOT/build/pc/busybox
TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/$TARGET-14.2}
TC=$TOOL/bin/$TARGET-
TAR=${1:-${BUSYBOX_TAR:-$HOME/.cache/korin-src/busybox/busybox-1.37.0.tar.bz2}}
V=busybox-1.37.0
export PATH="$TOOL/bin:$PATH"
[ -f "$SPECS" ] || { echo "busybox : lancer d'abord ARCH=x86_64 sh tools/posix/build.sh" >&2; exit 1; }
[ -f "$TAR" ] || { echo "busybox : archive absente : $TAR (https://busybox.net/downloads/$V.tar.bz2)" >&2; exit 1; }
rm -rf "${WORK:?}/src" "$WORK/build"
mkdir -p "$WORK/src" "$WORK/build" "$WORK/bin"
tar -C "$WORK/src" -xf "$TAR"
for p in "$ROOT"/tools/busybox/patches/*.patch; do
	[ -f "$p" ] || continue
	patch -d "$WORK/src/$V" -p1 -s < "$p"
done
cat > "$WORK/bin/korin-cc" <<CC
#!/bin/sh
exec ${TC}gcc -specs=$SPECS "\$@"
CC
chmod +x "$WORK/bin/korin-cc"
S=$WORK/src/$V
B=$WORK/build
# la configuration : rien, puis les commandes de applets.txt et le cadre (busybox <commande>, options longues)
make -C "$S" O="$B" allnoconfig > "$B/config.log" 2>&1
{
	for o in BUSYBOX LFS LONG_OPTS SHOW_USAGE FEATURE_VERBOSE_USAGE FEATURE_COMPRESS_USAGE FEATURE_HUMAN_READABLE USE_BB_PWD_GRP \
		PLATFORM_LINUX; do echo "CONFIG_$o=y"; done
	grep -v '^#' "$ROOT/tools/busybox/applets.txt" | grep . | sed 's/^/CONFIG_/; s/$/=y/'
	echo "CONFIG_EXTRA_CFLAGS=\"-O2 -g0 -fcommon -Wno-error=implicit-function-declaration -I$ROOT/tools/busybox/include\""
} > "$B/korin.config"
# (les lignes de allnoconfig pour ces options retirees d'abord : la premiere l'emporte)
sed 's/=.*//' "$B/korin.config" > "$B/korin.keys"
grep -v -F -w -f "$B/korin.keys" "$B/.config" > "$B/.config.base" || true
cat "$B/.config.base" "$B/korin.config" > "$B/.config"
yes "" | make -C "$S" O="$B" oldconfig >> "$B/config.log" 2>&1
# (pas de shell dans BusyBox : bash est celui de Korin ; le choix « sh » de oldconfig prend ash par défaut)
sed -i 's/^CONFIG_SH_IS_ASH=y/# CONFIG_SH_IS_ASH is not set/; s/^# CONFIG_SH_IS_NONE is not set/CONFIG_SH_IS_NONE=y/; s/^CONFIG_SHELL_ASH=y/# CONFIG_SHELL_ASH is not set/' "$B/.config"
yes "" | make -C "$S" O="$B" oldconfig >> "$B/config.log" 2>&1
make -C "$S" O="$B" CC="$WORK/bin/korin-cc" LD=${TC}ld AR=${TC}ar STRIP=${TC}strip HOSTCC="gcc" -j8 busybox > "$B/make.log" 2>&1 || {
	grep -n 'error:\|undefined reference' "$B/make.log" | head -40; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT/devtools/bin"
${TC}strip "$B/busybox" -o "$OUT/devtools/bin/busybox"
# les commandes construites (busybox --list), un lanceur chacune
"$ROOT/tools/busybox/list.sh" "$B" > "$OUT/applets.list"
while read -r a; do
	case $a in busybox|"["|"[[") continue ;; esac	# ([ et [[ : ceux de bash)
	printf '#!/bin/bash\nexec /devtools/bin/busybox %s "$@"\n' "$a" > "$OUT/devtools/bin/$a"
	chmod +x "$OUT/devtools/bin/$a"
done < "$OUT/applets.list"
ls -l "$OUT/devtools/bin/busybox"
echo "busybox : $(wc -l < "$OUT/applets.list") commandes dans $OUT/devtools/bin"
