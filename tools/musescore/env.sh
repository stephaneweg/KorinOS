# env.sh : versions et dossiers du port de MuseScore Studio (docs/etude/MUSESCORE.md). Tout se construit sous Linux
# (WSL ou une machine du cloud), hors du dépôt, sous ~/korin-mscore. Inclus par les autres scripts de tools/musescore.
# Il faut le Qt de Korin (tools/qt : build.sh, avec qt5compat et qtnetworkauth) et celui de l'hôte (tools/qt/host.sh).
MS_TAG=v4.7.7
MS_GIT=https://github.com/musescore/MuseScore.git

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MST=$ROOT/tools/musescore
. "$ROOT/tools/qt/env.sh"		# (Qt : PREFIX, HOSTQT, DEVSYS, TC, POSIX, KORIN_CPU, JOBS…)
KM=${KORIN_MSCORE:-$HOME/korin-mscore}
MSRC=$KM/src/musescore			# la source, tenue sous git (le tag cloné)
MOBJ=$KM/$ARCH/obj			# les objets
MINST=$KM/$ARCH/install			# l'arbre installé (bin/mscore, share/…)
mkdir -p "$KM/src" "$MOBJ"
