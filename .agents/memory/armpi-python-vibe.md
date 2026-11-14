---
name: armpi-python-vibe
description: "CPython 3.14 et Mistral Vibe sous Korin — étapes 0 à 5 dans main, étape 6 aussi (12/11/2026, poussé), QEMU seulement"
metadata:
  node_type: memory
  type: project
  originSessionId: 3ef69eb7-fc5d-48c1-a0c5-ac8373a7bb2c
  modified: 2026-11-12T12:08:22.099Z
---

Chantier Python + Vibe (étude docs/PYTHON.md, §9 étapes). Au 12/11/2026, dans main et poussé (a20e9a20) :
étape 0 (Vibe 3.1.2 sans V8 sur le PC, tools/vibe), 1-2 (CPython 3.14.10, tools/python), 2 bis (dlopen, voir
[[armpi-dlopen]]), 3 (tree_sitter/cffi en .so, pip), 4 (cible Rust aarch64-unknown-korin, tools/rust, docs/RUST.md),
5 (pydantic-core, rpds-py, cryptography en cdylib). Tout sous QEMU seulement, rien sur le Pi.

Étape 6 faite en local (WSL) le 12/11, fusionnée dans main et poussée (f1e632ec) : harness en cdylib
(tools/vibe/build-korin.sh), site-packages de Vibe, vibe-acp/vibe-app-server répondent à initialize ; arbre POSIX des
chemins (/sd0, getcwd POSIX, HOME=/sd0) dans vfs et libposix. Suite : étape 7 (connexion au compte, séance complète),
point de reprise docs/PYTHON-SUITE.md.

**Why:** une session cloud n'a ni ce contexte ni WSL ; la carte se fait par build.ps1 + fasmarm.exe (Windows).
**How to apply:** pour reprendre ou vérifier, partir de docs/PYTHON-SUITE.md et PYTHON-PORT.md ; Vibe se connecte au
compte par navigateur (clé rangée dans $VIBE_HOME/.env), jamais de clé dans le dépôt.
