# env.sh : versions, sommes et dossiers de la CAO (docs/DESIGN-CAO.md, « Construction »). Tout se construit dans
# WSL (ou une machine Linux), hors du dépôt, sous ~/korin-cao. Inclus par les autres scripts de tools/cao.
#
#   ARCH=x86_64 (défaut, le PC) ou ARCH=aarch64 (le Pi)
#   KORIN_CAO (défaut ~/korin-cao), JOBS (défaut : nproc, au plus 4 : -j12 a fait tomber un WSL de 7,8 Go, partagé
#   avec d'autres constructions ; une longue construction : setsid nohup … & dans WSL)
OCCT_VER=8.0.1
OCCT_TAG=V8_0_1
OCCT_SHA256=0d6913eae4bcc09a3653ceced6dda1aec11c35a1513d4c06762c9b002092c68a
EIGEN_VER=5.0.1
EIGEN_SHA256=e9c326dc8c05cd1e044c71f30f1b2e34a6161a3b6ecf445d56b53ff1669e3dec
# PlaneGCS : src/Mod/Sketcher/App/planegcs de FreeCAD, seul dossier gardé de l'archive
FREECAD_VER=1.1.4
FREECAD_SHA256=b95270a74a26dbdde331c6ff8e764289da6c77713e2b2d4e0c0041dfe1950f3f

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CAO=$ROOT/tools/cao
ARCH=${ARCH:-x86_64}
K=${KORIN_CAO:-$HOME/korin-cao}
DL=$K/dl
SRC=$K/src
OCCT_SRC=$SRC/OCCT-${OCCT_TAG#V}		# (l'archive de GitHub : OCCT-8_0_1)
EIGEN_SRC=$SRC/eigen-$EIGEN_VER
PGCS_SRC=$SRC/FreeCAD-$FREECAD_VER/src/Mod/Sketcher/App/planegcs
OUT=$K/$ARCH			# out/occt (en-têtes, lib*.a), out/planegcs, obj/…
TOOL=${KORIN_TOOLCHAINS:-/opt/toolchains}/$ARCH-onyx-elf-14.2
TC=$TOOL/bin/$ARCH-onyx-elf-
if [ "$ARCH" = x86_64 ]; then
	POSIX=${POSIX:-$ROOT/build/pc/posix}
	MCPU=""
else
	POSIX=${POSIX:-$ROOT/build/posix}
	MCPU="-mcpu=cortex-a72"
fi
n=$(nproc 2>/dev/null || echo 4)
[ "$n" -le 4 ] || n=4
JOBS=${JOBS:-$n}
mkdir -p "$DL" "$SRC" "$OUT"
