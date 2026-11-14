---
name: armpi-circuits
description: "Port de circuits d'Onyx dans src/apps/circuits (31/10/2026) : dans main, essayé sous QEMU seulement ; pièges d'essai"
metadata:
  node_type: memory
  type: project
  originSessionId: 86a8c994-83b2-45e7-82c9-1c83a1e2faa5
  modified: 2026-10-31T22:05:40.243Z
---

Circuits d'Onyx est porté sans pont dans `src/apps/circuits` (dans main depuis le 31/10/2026, commit 92d20d7 ; jamais essayé sur le Pi). Les différences durables sont dans son `README.md` : tout le dessin suit les jetons, leçon et résultat sont des dialogues, `kv.h` remplace FileKit qu'ARMPI n'a pas.

**Why:** demande de l'utilisateur, suivant [[armpi-ports-sans-pont]] et [[armpi-ports-theme]].

**How to apply:** essais parallèles : `qemu_drive.py` quitte sans message utile (`'Qmp' object has no attribute 's'`) si l'écran `--vnc` est déjà pris par une autre session ; regarder les QEMU en cours avant de choisir un numéro. Un worktree n'a pas de `build/` : jonctions vers `build/gcc`, `build/posix`, `build/modules` du dépôt principal, à retirer par `rmdir` avant de nettoyer. Le dock : `scr[2]` vaut 0 quand il flotte (retirer sa hauteur), non nul quand il réserve sa place (Onyx Noir). Écrans 800x600 et 640x480 : assembler `src/kernel/kernel.asm` avec `-d ECRAN_W=... -d ECRAN_H=...`, puis `--kernel` et `--ecran`.
