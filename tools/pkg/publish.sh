#!/bin/sh
# tools/pkg/publish.sh -- publier les paquets d'ARMPI (docs/PAQUETS.md). Depuis build/sdroot/ tel que build.ps1 l'a construit :
#
#   1. la cle privee : $ONYX_PKG_KEY_FILE (un chemin ; jamais dans un depot, jamais dans le chat), sinon $OnyxPkgKey (la cle
#      PEM elle-meme, agents du cloud ; une ligne acceptee : remise en PEM dans un fichier temporaire 600, efface a la fin),
#      sinon rien n'est publie ;
#   2. le depot ou vont les paquets : celui-ci par defaut (son dossier packages/, servi par GitHub Pages) ; un autre clone
#      de stephaneweg/KorinOS : $KORIN_REPO_DIR, mis a jour ;
#   3. build/pkgboot : ce qui va a la racine de la carte (noyaux 800x600 comme `build.ps1 sd`, modules, firmware) ;
#   4. tools/pkg/mkrepo.py --bump --db : les paquets dont les fichiers ont change prennent une nouvelle version
#      (tools/pkg/versions.ini), le dossier packages/ et son index signe, build/sdroot/var/pkg/db ;
#   5. tools/pkg/checkrepo.py : la signature verifiee avec la cle des cartes (build/sdroot/etc/pkg/onyx.pub) : une mauvaise
#      cle s'arrete ici (toutes les cartes refuseraient l'index), puis chaque archive ;
#   6. le dossier packages/ commite (et tools/pkg/versions.ini quand c'est ce depot) ;
#   7. pousse seulement avec --push (GitHub Pages sert sa branche main).
#   Les paquets release = 1 (build/pkg-release) vont, avec --push, dans la Release « packages » (gh) : GitHub Pages refuse
#   un site de plus de 1 Go.
#
#   ONYX_PKG_KEY_FILE=/chemin/cle.pem sh tools/pkg/publish.sh [--push]
#
# Rien n'a change : rien n'est publie.
#
# Le PC (ARCH=x86_64 ou BOARD=pc ; docs/PAQUETS.md, « Le PC ») : depuis build/pc/sysroot (tools/pc/sysroot*.sh), le
# demarrage de build/pc/esp (korin.efi, noyau, initrd), dans packages/pc/ (board = pc) ; les paquets de meme contenu que
# ceux du Pi sont cites (../pkgs/), les gros vont dans la Release sous le nom pc-<nom>-<version>.opk. Seul packages/pc/
# est commite (l'index du Pi n'est pas touche).
#
#   ARCH=x86_64 KORIN_REPO_DIR=../clone-de-main ONYX_PKG_KEY_FILE=/chemin/cle.pem sh tools/pkg/publish.sh [--push]
set -e
cd "$(dirname "$0")/../.."
PUSH=0
while [ $# -gt 0 ]; do
	case "$1" in
	--push) PUSH=1 ;;
	*) echo "usage : publish.sh [--push]" >&2; exit 1 ;;
	esac
	shift
done
REPO=${KORIN_REPO_DIR:-.}
PY=python3; command -v python3 >/dev/null 2>&1 && python3 -c "import cryptography" 2>/dev/null || PY=python
PC=0; { [ "${ARCH:-}" = x86_64 ] || [ "${BOARD:-}" = pc ]; } && PC=1
export ARCH BOARD
SD=build/sdroot; OUT=packages; REL=build/pkg-release; SYS=armpi
[ $PC = 0 ] || { SD=build/pc/sysroot; OUT=packages/pc; REL=build/pkg-release-pc; SYS=pc; }
[ -d $SD/sys ] || { echo "publish : pas de $SD construit (build.ps1 ; PC : tools/pc/sysroot*.sh)" >&2; exit 1; }
[ -f $SD/etc/pkg/onyx.pub ] || { echo "publish : pas de cle publique des cartes ($SD/etc/pkg/onyx.pub)" >&2; exit 2; }

# ---- la cle ----
if { [ -z "$ONYX_PKG_KEY_FILE" ] || [ ! -f "$ONYX_PKG_KEY_FILE" ]; } && [ -n "$OnyxPkgKey" ]; then
	ONYX_PKG_KEY_FILE=$(mktemp); chmod 600 "$ONYX_PKG_KEY_FILE"; trap 'rm -f "$ONYX_PKG_KEY_FILE"' EXIT
	# (la variable peut tenir sur une ligne : on refait l'en-tete, le base64 en lignes de 64, la fin ; rien n'est affiche)
	$PY -I -c 'import os,re,textwrap,sys
m = re.search (r"-+BEGIN ([A-Z ]+?)-+\s*(.*?)\s*-+END", os.environ["OnyxPkgKey"], re.S)
if not m: sys.exit ("publish : OnyxPkgKey n est pas une cle PEM")
h, b = m.group (1), re.sub (r"\s", "", m.group (2))
open (sys.argv[1], "w").write ("-----BEGIN %s-----\n%s\n-----END %s-----\n" % (h, "\n".join (textwrap.wrap (b, 64)), h))' "$ONYX_PKG_KEY_FILE"
fi
if [ -z "$ONYX_PKG_KEY_FILE" ] || [ ! -f "$ONYX_PKG_KEY_FILE" ]; then
	echo "publish : pas de cle de signature (ONYX_PKG_KEY_FILE ou OnyxPkgKey) : rien n'est publie" >&2
	exit 2
fi

# ---- le depot ----
ICI=0; [ "$(cd "$REPO" && pwd -P)" = "$(pwd -P)" ] && ICI=1	# (les paquets vont dans ce depot-ci)
if [ $ICI = 0 ]; then
	if [ ! -e "$REPO/.git" ]; then git clone -q https://github.com/stephaneweg/KorinOS "$REPO"; fi
	git -C "$REPO" fetch -q origin 2>/dev/null || true
	if git -C "$REPO" rev-parse -q --verify origin/main >/dev/null; then
		git -C "$REPO" checkout -q main 2>/dev/null || git -C "$REPO" checkout -q -b main origin/main
		git -C "$REPO" merge -q --ff-only origin/main
	fi
fi
git -C "$REPO" config core.autocrlf false		# (Windows : l'index signe ne doit pas changer d'un octet)

# ---- ce que lit le firmware : noyaux 800x600 (comme build.ps1 sd), modules, firmware, config.txt ----
# (PC : le chargeur, le noyau et l'initrd de build/pc/esp, aux places de korin.ps1 install ; jamais boot.cfg)
if [ $PC = 1 ]; then
BOOT=build/pkgboot-pc
rm -rf "$BOOT"; mkdir -p "$BOOT/EFI/Korin" "$BOOT/korin"
cp build/pc/esp/EFI/BOOT/BOOTX64.EFI "$BOOT/EFI/Korin/korin.efi"
cp build/pc/esp/korin/kernel.elf build/pc/esp/korin/initrd.tar "$BOOT/korin/"
else
BOOT=build/pkgboot
rm -rf "$BOOT"; mkdir -p "$BOOT"
ABS=$(pwd)
( cd src/kernel
  "$ABS/tools/fasmarm/fasmarm.exe" -d ECRAN_W=800 -d ECRAN_H=600 kernel.asm ../../$BOOT/kernel8.img | tail -1
  "$ABS/tools/fasmarm/fasmarm.exe" -d ARCH=pi5 -d ECRAN_W=800 -d ECRAN_H=600 kernel.asm ../../$BOOT/kernel_2712.img | tail -1 )
cp build/modules/*.mod "$BOOT/"
cp firmware/start4.elf firmware/fixup4.dat firmware/*.dtb boot/config.txt "$BOOT/"
mkdir -p "$BOOT/overlays"; cp firmware/overlays/* "$BOOT/overlays/"	# Pi 5 (firmware de l'EEPROM)
fi

# ---- les paquets ----
mkdir -p "$REPO/$OUT"
ANY=; [ $PC = 0 ] || ANY="--any $REPO/packages/index.txt"	# (PC : les paquets du Pi de meme contenu, cites)
$PY tools/pkg/mkrepo.py --boot "$BOOT" --out "$REPO/$OUT" --key "$ONYX_PKG_KEY_FILE" --db --bump $ANY

# ---- la signature avec la cle des cartes, puis l'essai des archives ----
$PY tools/pkg/checkrepo.py "$REPO/$OUT" $SD/etc/pkg/onyx.pub

# ---- les gros paquets (release = 1) : dans la Release « packages », avant l'index qui les nomme ----
if [ $PUSH = 1 ] && ls "$REL"/*.opk >/dev/null 2>&1; then
	gh release view packages -R stephaneweg/KorinOS >/dev/null 2>&1 || 		gh release create packages -R stephaneweg/KorinOS --title "Paquets" --notes "Les gros paquets de Korin OS (packages.ini : release = 1), nommés par l'index signé de packages/." >/dev/null
	DEJA=$(gh release view packages -R stephaneweg/KorinOS --json assets -q '.assets[].name')
	for f in "$REL"/*.opk; do
		echo "$DEJA" | grep -qx "$(basename "$f")" || gh release upload packages -R stephaneweg/KorinOS "$f"
	done
	echo "publish : gros paquets dans la Release packages"
fi

# ---- la sortie ----
HEAD=$(git rev-parse --short HEAD)
ME=$(git config user.name || echo ARMPI); MAIL=$(git config user.email || echo armpi@localhost)
cd "$REPO"
if [ -n "$(git status --porcelain -- $OUT)" ]; then
	QUOI=$OUT; [ $ICI = 1 ] && QUOI="$OUT tools/pkg/versions.ini"
	git add -A -- $QUOI
	V=$(grep -A2 "^\[$SYS\]" $OUT/index.txt | sed -n 's/^version = //p')
	T="Paquets"; [ $PC = 0 ] || T="Paquets PC"
	git -c user.name="$ME" -c user.email="$MAIL" commit -q -m "$T : $SYS $V ($(grep -c '^file = ' $OUT/index.txt) paquets), depuis $HEAD" -- $QUOI
	if [ $PUSH = 1 ]; then git push -q origin HEAD:main && echo "publish : pousse sur stephaneweg/KorinOS (GitHub Pages : une minute ou deux)"
	else echo "publish : commite dans $REPO, PAS pousse (--push pour pousser)"; fi
else
	echo "publish : rien n'a change dans le depot"
fi
