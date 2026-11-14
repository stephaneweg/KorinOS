---
name: armpi-cosmozoom
description: "Cosmo Zoom! porté d'Onyx (31/10/2026) : jeu en C++, gk_draw en asm dans GpuKit ; QEMU seulement, GPU jamais vu sur le Pi"
metadata:
  node_type: memory
  type: project
  originSessionId: 37ad952f-a4b2-482f-b808-afcd71063ec2
  modified: 2026-10-31T22:54:09.536Z
---

Dans `main` depuis le 01/11/2026 (abf04b1). Jeu en C++ dans `src/apps/cosmozoom` (nom d'Onyx, sans pont) ; transformation, découpe et rastériseur CPU en asm dans GpuKit (`draw.asm`, `gk_draw`). Limites : `src/apps/cosmozoom/README.md`.

**Why:** l'asm de toute la logique (6 700 lignes de C++ flottant) n'apporte rien ; l'utilisateur a demandé « asm si possible », verdict donné et assumé. Il peut encore vouloir pousser l'asm plus loin, comme SNES et GBA ([[armpi-emulateurs]]).

**How to apply:** `GK_MAX_VERTS` vaut 196 608 (Skyline envoie 180 978 sommets). Le `/gcc` de `build/` peut manquer de `make` : `tools/gcc/make.sh`. Reste à voir : rendu GPU et vitesse sur le Pi, manettes quand `WEV_PAD` sera dans `main`. Voir [[armpi-ports-sans-pont]], [[armpi-ports-theme]].
