#!/bin/sh
# build-freecom.sh -- COMMAND.COM de la machine DOS de win16 : FreeCOM 0.86 (FreeDOS, GPL-2,
# github.com/FDOS/freecom, étiquette com086), en français, sans échange XMS, construit par Open Watcom dans WSL
# (docs/etude/WIN16.md §9.4 ; choix de l'utilisateur du 20/11/2026).
#
#   sh tools/win16/build-freecom.sh      -> build/win16/dos/COMMAND.COM (copié par apps.sh dans win16.app/dos)
#
# Les sources sont tirées dans ~/korin-win16/freecom (hors du dépôt) ; il faut nasm (installé par apt s'il manque).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WATCOM=${WATCOM:-$HOME/win16/watcom}
[ -x "$WATCOM/binl64/wcl" ] || { echo "build-freecom : Open Watcom absent ($WATCOM)" >&2; exit 1; }
command -v nasm >/dev/null || sudo apt-get install -y nasm >/dev/null
SRC=$HOME/korin-win16/freecom
[ -d "$SRC" ] || git clone -q --depth 1 --branch com086 https://github.com/FDOS/freecom.git "$SRC"
export WATCOM
export PATH="$WATCOM/binl64:$WATCOM/binl:$PATH"
export INCLUDE="$WATCOM/h"
cd "$SRC"
./build.sh -r no-xms-swap wc french >build.log 2>&1 || { tail -40 build.log >&2; exit 1; }
mkdir -p "$ROOT/build/win16/dos"
cp command.com "$ROOT/build/win16/dos/COMMAND.COM"
cp license "$ROOT/build/win16/dos/COPYING.TXT"
printf 'COMMAND.COM : FreeCOM 0.86 (FreeDOS), GPL-2 (COPYING.TXT).\r\nSources : https://github.com/FDOS/freecom (com086).\r\n' >"$ROOT/build/win16/dos/FREECOM.TXT"
echo "COMMAND.COM : $(stat -c %s command.com) octets (FreeCOM $(git describe --tags 2>/dev/null || echo com086))"
