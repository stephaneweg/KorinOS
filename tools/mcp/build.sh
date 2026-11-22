#!/bin/sh
# build.sh -- korin-mcp, le serveur MCP de Korin (src/sys/mcp, docs/MCP-KORIN.md), sur libposix :
#   build/pc/mcp/korin-mcp           pour le PC (ARCH=x86_64, par défaut : là où tourne Claude Code)
#   build/mcp/aarch64/korin-mcp      pour le Pi (ARCH=aarch64 ; libposix du Pi : tools/posix/build.sh)
# Avant : la libposix de la machine (ARCH=x86_64 sh tools/posix/build.sh). Les numéros des appels système, les
# dispositions (THL_, WE_, NIF_, NSK_…) et les rangs de NetKit sont tirés des .inc et de netkit.abi (la seule
# source) dans build/…/gen ; sh tools/mcp/build.sh --host : les essais du JSON et du PNG sur la machine de
# construction (tools/mcp/hosttest.c).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
ARCH=${ARCH:-x86_64}
TCS=${KORIN_TOOLCHAINS:-/opt/toolchains}
SRC=src/sys/mcp
if [ "$ARCH" = x86_64 ]; then
	TC=$TCS/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
	POSIX=build/pc/posix
	OUT=build/pc/mcp
else
	TC=$TCS/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
	POSIX=build/posix
	OUT=build/mcp/aarch64
fi
GEN=$OUT/gen
mkdir -p "$GEN" "$OUT/obj"

# les constantes : NOM = nombre des .inc, pour les préfixes dont le serveur se sert (une seule fois chacune)
{
	echo "/* Tiré de src/inc (syscalls, armpi, fs, netd) et src/kits/netkit par tools/mcp/build.sh : ne pas modifier. */"
	echo "#ifndef KORIN_MCP_CONSTS_H"
	echo "#define KORIN_MCP_CONSTS_H"
	for f in src/inc/syscalls.inc src/inc/armpi.inc src/inc/fs.inc src/inc/netd.inc src/kits/netkit/netkit.inc; do
		tr -d '\r' <"$f" | sed -n 's/^\(\(SYS\|THL\|WS_OP\|WS\|WM\|WE\|WES\|WF\|VFS\|FS\|NIF\|NSK\|NI\)_[A-Z0-9_]*\)[[:space:]]*=[[:space:]]*\(0x[0-9A-Fa-f]*\|[0-9][0-9]*\)\([[:space:]].*\)\{0,1\}$/\1 \3/p'
	done | awk '!seen[$1]++ { printf "#define %s %s\n", $1, $2 }'
	# les rangs de NetKit (ligne kit_fn, à partir de 0) ; la version demandée : le plus haut rang servi + 1
	tr -d '\r' <src/kits/netkit/netkit.abi | awk '/^kit_fn/ { printf "#define NK_%s %d\n", $2, n++ }'
	tr -d '\r' <src/kits/netkit/netkit.abi | awk '/^kit_fn/ { n++; if ($2 == "net_sockets") v = n } END { printf "#define NK_VERSION_NEEDED %d\n", v }'
	echo "#endif"
} >"$GEN/korin_consts.h"
# les noms des appels système (tableau de SYS_PROC_STATS), en minuscules comme sysstat
tr -d '\r' <src/inc/syscalls.inc | sed -n 's/^SYS_\([A-Z0-9_]*\)[[:space:]]*=[[:space:]]*\(0x[0-9A-Fa-f]*\).*/\2 \1/p' |
	awk '{ printf "{ %s, \"%s\" },\n", $1, tolower($2) }' >"$GEN/korin_sysnames.h"
for k in SYS_KLOG THL_RUNTIME WE_TITLE NSK_OWNER NI_DHCP VFS_MOUNTS FS_INFO NK_net_sockets NK_VERSION_NEEDED; do
	grep -q "define $k " "$GEN/korin_consts.h" || { echo "mcp : $k absent de korin_consts.h (sed raté)" >&2; exit 1; }
done

if [ "${1:-}" = --host ]; then # les essais sur la machine de construction : JSON et PNG (sans Korin)
	gcc -O1 -g -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$SRC" tools/mcp/hosttest.c \
		"$SRC/json.c" "$SRC/png.c" -o "$OUT/hosttest"
	"$OUT/hosttest" "$OUT"
	exit 0
fi

[ -f "$POSIX/lib/posix.specs" ] || { echo "mcp : d'abord ARCH=$ARCH sh tools/posix/build.sh" >&2; exit 1; }
CFLAGS="-specs=$POSIX/lib/posix.specs -O2 -g0 -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -I$GEN -I$SRC"
for f in main json png tools files; do
	${TC}gcc $CFLAGS -c "$SRC/$f.c" -o "$OUT/obj/$f.o"
done
${TC}gcc $CFLAGS "$OUT"/obj/main.o "$OUT"/obj/json.o "$OUT"/obj/png.o "$OUT"/obj/tools.o "$OUT"/obj/files.o -o "$OUT/korin-mcp.dbg"
${TC}strip -o "$OUT/korin-mcp" "$OUT/korin-mcp.dbg"
ls -l "$OUT/korin-mcp"
