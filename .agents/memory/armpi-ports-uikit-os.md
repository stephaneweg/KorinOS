---
name: armpi-ports-uikit-os
description: "Tout programme porté depuis Onyx utilise l'interface de notre OS (UIKit assembleur, ui_*), jamais l'uikit C++ d'Onyx"
metadata:
  node_type: memory
  type: feedback
  originSessionId: d17beb06-f22c-4b5e-b6fa-50637003b332
  modified: 2026-10-30T18:35:06.666Z
---

Tout prochain programme porté depuis Onyx doit dessiner son interface avec l'API de l'OS (UIKit d'ARMPI, `ui_*`, `src/kits/uikit`) et ne doit PAS compiler ni embarquer l'uikit C++ d'Onyx.

**Why:** l'utilisateur l'a demandé le 30/10/2026 en apprenant que Koton embarquait `onyxui/` : « on a reproduit l'équivalent ». Un toolkit embarqué dessine à sa façon, lit `theme.txt` lui-même et ne connaît pas Givre, d'où le thème Peach sur le Pi.

**How to apply:** pas de pont : le dossier `armui/` a été retiré par `e79dd4f`, voir [[armpi-ports-sans-pont]] ; modèle : `src/extra/onyxports/koton` (`ui/view.h`). Ce qui manque s'ajoute à UIKit, en fin de `uikit.abi` (l'utilisateur l'autorise d'avance). Essayer avec un `theme.txt` tel que l'écrit le panneau (`theme = <nom>` seul), et sous QEMU après le préchargement de Jet (93 s) et la SoundFont, qui saturent l'accès à la carte. Voir [[armpi-ports-theme]], [[armpi-toolchain-onyx]].
