#!/bin/bash
# build.sh -- mai-acp pour la carte (docs/MAI-ACP.md) : l'agent de code sur une API compatible OpenAI (oai_acp, profil
# MAI de Microsoft Foundry), que le client terminal lance par « agent --mai » et l'app MAI. Dans WSL, depuis la racine
# du depot, apres tools/claude/build.sh (oai_acp reprend claude_acp) :
#     bash tools/oai/build.sh                      le Pi (build/python/sdroot)
#     ARCH=x86_64 bash tools/oai/build.sh          Korin PC (build/pc/python/sdroot)
#     DEST=dossier bash tools/oai/build.sh         un autre arbre (sa racine : bin/, python/, .mai-acp/)
# Pose dans l'arbre :
#   python/lib/python3.14/site-packages/oai_acp    tools/oai/oai_acp (rien d'autre : la requete passe par urllib)
#   bin/mai-acp                                    la commande (#!/bin/python3, profil mai)
#   .mai-acp/                                      AGENTS.md et les competences, comme .claude-acp (tools/claude/home.py) ;
#                                                  ni .env (le point d'acces et la cle) ni config.toml, ni sessions/
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
. "$ROOT/tools/python/arch.sh"
BUILD_PYTHON=${BUILD_PYTHON:-/usr/bin/python3.14}
D=${DEST:-$OUT/sdroot}
SP=$D/python/lib/python3.14/site-packages
if [ -z "${DEST:-}" ] && [ ! -d "$SP/claude_acp" ]; then
	echo "mai : lancer d'abord tools/claude/build.sh (pas de claude_acp dans $SP)" >&2; exit 1
fi
mkdir -p "$SP" "$D/bin"
rm -rf "$SP/oai_acp"
cp -r "$ROOT/tools/oai/oai_acp" "$SP/oai_acp"
find "$SP/oai_acp" -name __pycache__ -prune -exec rm -rf {} +
"$BUILD_PYTHON" -I -m compileall -q -j0 --invalidation-mode checked-hash "$SP/oai_acp" >/dev/null
printf '#!/bin/python3\nimport os, sys\n# (unixtools : /devtools/bin devant /bin pour les commandes de l agent, BusyBox)\nif os.path.isfile("/devtools/bin/busybox") and "/devtools/bin" not in os.environ.get("PATH", "").split(":"):\n    os.environ["PATH"] = "/devtools/bin:" + os.environ.get("PATH", "/bin:/sbin")\nfrom oai_acp.main import main\nsys.exit(main(["--profile", "mai"] + sys.argv[1:]))\n' \
	> "$D/bin/mai-acp"
chmod +x "$D/bin/mai-acp"
"$BUILD_PYTHON" -I "$ROOT/tools/claude/home.py" "$D" .mai-acp
du -sh "$SP/oai_acp" "$D/.mai-acp"
