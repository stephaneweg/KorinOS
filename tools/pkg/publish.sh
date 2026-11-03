#!/bin/sh
# tools/pkg/publish.sh -- publier les paquets d'ARMPI (docs/PAQUETS.md). Depuis build/sdroot/ tel que build.ps1 l'a construit :
#
#   1. la cle privee : $ONYX_PKG_KEY_FILE (un chemin ; jamais dans un depot, jamais dans le chat), sinon rien n'est publie ;
#   2. le clone de stephaneweg/KorinOS : $KORIN_REPO_DIR, ../KorinOS par defaut, mis a jour ;
#   3. build/pkgboot : ce qui va a la racine de la carte (noyaux 800x600 comme `build.ps1 sd`, modules, firmware) ;
#   4. tools/pkg/mkrepo.py --bump --db : les paquets dont les fichiers ont change prennent une nouvelle version
#      (tools/pkg/versions.ini), le dossier packages/ et son index signe, build/sdroot/var/pkg/db ;
#   5. tools/pkg/checkrepo.py : la signature verifiee avec la cle des cartes (build/sdroot/etc/pkg/onyx.pub) : une mauvaise
#      cle s'arrete ici (toutes les cartes refuseraient l'index), puis chaque archive ;
#   6. le dossier packages/ commite dans le clone ;
#   7. pousse seulement avec --push (GitHub Pages sert sa branche main).
#
#   ONYX_PKG_KEY_FILE=/chemin/cle.pem sh tools/pkg/publish.sh [--push]     puis commiter tools/pkg/versions.ini ici
#
# Rien n'a change : rien n'est publie.
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
REPO=${KORIN_REPO_DIR:-../KorinOS}
PY=python3; command -v python3 >/dev/null 2>&1 && python3 -c "import cryptography" 2>/dev/null || PY=python
[ -d build/sdroot/sys ] || { echo "publish : pas de build/sdroot construit (build.ps1)" >&2; exit 1; }
[ -f build/sdroot/etc/pkg/onyx.pub ] || { echo "publish : pas de cle publique des cartes (build/sdroot/etc/pkg/onyx.pub)" >&2; exit 2; }

# ---- la cle ----
if [ -z "$ONYX_PKG_KEY_FILE" ] || [ ! -f "$ONYX_PKG_KEY_FILE" ]; then
	echo "publish : pas de cle de signature (ONYX_PKG_KEY_FILE) : rien n'est publie" >&2
	exit 2
fi

# ---- le depot ----
if [ ! -d "$REPO/.git" ]; then git clone -q https://github.com/stephaneweg/KorinOS "$REPO"; fi
git -C "$REPO" config core.autocrlf false		# (Windows : l'index signe ne doit pas changer d'un octet)
git -C "$REPO" fetch -q origin 2>/dev/null || true
if git -C "$REPO" rev-parse -q --verify origin/main >/dev/null; then
	git -C "$REPO" checkout -q main 2>/dev/null || git -C "$REPO" checkout -q -b main origin/main
	git -C "$REPO" merge -q --ff-only origin/main
fi

# ---- ce que lit le firmware : noyaux 800x600 (comme build.ps1 sd), modules, firmware, config.txt ----
BOOT=build/pkgboot
rm -rf "$BOOT"; mkdir -p "$BOOT"
ABS=$(pwd)
( cd src/kernel
  "$ABS/tools/fasmarm/fasmarm.exe" -d ECRAN_W=800 -d ECRAN_H=600 kernel.asm ../../$BOOT/kernel8.img | tail -1
  "$ABS/tools/fasmarm/fasmarm.exe" -d ARCH=pi5 -d ECRAN_W=800 -d ECRAN_H=600 kernel.asm ../../$BOOT/kernel_2712.img | tail -1 )
cp build/modules/*.mod "$BOOT/"
cp firmware/start4.elf firmware/fixup4.dat firmware/bcm2711-rpi-4-b.dtb boot/config.txt "$BOOT/"

# ---- les paquets ----
mkdir -p "$REPO/packages"
$PY tools/pkg/mkrepo.py --boot "$BOOT" --out "$REPO/packages" --key "$ONYX_PKG_KEY_FILE" --db --bump

# ---- la signature avec la cle des cartes, puis l'essai des archives ----
$PY tools/pkg/checkrepo.py "$REPO/packages" build/sdroot/etc/pkg/onyx.pub

# ---- la sortie ----
HEAD=$(git rev-parse --short HEAD)
ME=$(git config user.name || echo ARMPI); MAIL=$(git config user.email || echo armpi@localhost)
cd "$REPO"
if [ -n "$(git status --porcelain -- packages)" ]; then
	git add -A -- packages
	V=$(grep -A2 '^\[armpi\]' packages/index.txt | sed -n 's/^version = //p')
	git -c user.name="$ME" -c user.email="$MAIL" commit -q -m "ARMPI : armpi $V ($(grep -c '^file = ' packages/index.txt) paquets), depuis ARMPI $HEAD" -- armpi
	if [ $PUSH = 1 ]; then git push -q origin HEAD:main && echo "publish : pousse sur stephaneweg/KorinOS (GitHub Pages : une minute ou deux)"
	else echo "publish : commite dans $REPO, PAS pousse (--push pour pousser)"; fi
else
	echo "publish : rien n'a change dans le depot"
fi
