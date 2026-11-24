# env.sh : versions et dossiers du port de Qt 6 (docs/etude/QT.md). Tout se construit sous Linux (WSL ou une
# machine du cloud), hors du dépôt, sous ~/korin-qt. Inclus par les autres scripts de tools/qt.
#
#   KORIN_QT (défaut ~/korin-qt)
#   JOBS (défaut : nproc, au plus 6)
QT_VER=6.10.2				# celle de MuseScore Studio 4.7
QT_MINOR=${QT_VER%.*}
QT_URL=https://download.qt.io/official_releases/qt/$QT_MINOR/$QT_VER/submodules
# les modules, dans l'ordre de construction (qtbase d'abord ; qtshadertools avant qtdeclarative)
QT_MODULES="qtbase qtshadertools qtdeclarative qtsvg"

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
QTT=$ROOT/tools/qt
ARCH=${ARCH:-x86_64}			# Korin PC d'abord
K=${KORIN_QT:-$HOME/korin-qt}
DL=$K/dl
SRC=$K/src
HOSTQT=$K/host				# Qt pour la machine de construction (moc, rcc, qsb, qmlcachegen…)
OUT=$K/$ARCH				# obj/<module>, qt/ (le préfixe installé pour Korin)
# Qt va dans /devtools/qt de Korin (paquets qtbase-dev, qtdeclarative-dev… : .a, en-têtes, CMake, outils pour
# Korin) ; ici, l'arbre de cette installation (CMAKE_STAGING_PREFIX)
QT_KORIN_PREFIX=/devtools/qt
PREFIX=$OUT/stage$QT_KORIN_PREFIX
DEVSYS=$OUT/devsys			# les paquets <nom>-dev dont Qt dépend, dépaquetés (deps.sh)
UIKIT_LIBDIR=$ROOT/build/pc/gcc/lib	# libuikit.a (tools/pc/apps.sh) ; dans Korin : /devtools/gcc/lib
OBJ=$OUT/obj
TOOL=${KORIN_TOOLCHAINS:-/opt/toolchains}/$ARCH-onyx-elf-14.2
TC=$TOOL/bin/$ARCH-onyx-elf-
POSIX=${POSIX:-$ROOT/build/pc/posix}
n=$(nproc 2>/dev/null || echo 4)
[ "$n" -le 6 ] || n=6
JOBS=${JOBS:-$n}
# Processeur : Tiger Lake sans AVX-512 de base, mais pas « -mno-avx512f » (celui de Jet et de Blender) : Qt compile
# ses chemins AVX-512 à part et les choisit à l'exécution (qCpuHasFeature) ; son test x86intrin les demande.
export KORIN_CPU=${KORIN_CPU:-"-march=x86-64-v3 -mtune=tigerlake"}
mkdir -p "$DL" "$SRC" "$OBJ"

# qt_src <module> : le dossier des sources dépaquetées
qt_src() { echo "$SRC/$1-everywhere-src-$QT_VER"; }
