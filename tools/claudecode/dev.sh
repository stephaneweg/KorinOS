#!/bin/bash
# dev.sh -- l'hôte de développement Linux de Claude Code (src/apps/claudecode, fonction cargo « dev ») : la même
# interface egui que sous Korin, dans une fenêtre eframe, sur le `claude` installé (PATH, ou CLAUDECODE_CLI), avec les
# réglages de ~/.claude (CLAUDE_CONFIG_DIR pour un autre dossier) et les données de l'app dans ~/.config/claudecode
# (CLAUDECODE_DATA pour un autre dossier). Depuis la racine du dépôt :
#     bash tools/claudecode/dev.sh                    la fenêtre
#     bash tools/claudecode/dev.sh --route overview   sur une page (new, overview, worktrees, background, extensions,
#                                                     settings, session:<id>)
#     bash tools/claudecode/dev.sh --shot f.png [--route …] [--size 1440x900] [--theme light] [--wait S]
#          [--click X,Y] [--rclick X,Y] [--type TEXTE] [--key ctrl+k] [--sleep S] [--palette] [--shot-also g.png]
#                                                     sans fenêtre : l'image faite par le rendu CPU de Korin (src/dev.rs)
# RELEASE=1 : construit en --release. Le dossier target : ~/korin-craft/target-claudecode-dev (KORIN_CRAFT).
set -e
cd "$(dirname "$0")/../../src/apps/claudecode"
W=${KORIN_CRAFT:-$HOME/korin-craft}
export CARGO_TARGET_DIR=$W/target-claudecode-dev
P=debug
[ -n "${RELEASE:-}" ] && P=release
cargo build --quiet --features dev ${RELEASE:+--release}
exec "$CARGO_TARGET_DIR/$P/claudecode" "$@"
