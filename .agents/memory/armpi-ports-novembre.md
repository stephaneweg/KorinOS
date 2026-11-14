---
name: armpi-ports-novembre
description: "Ports d'Onyx du 1/11/2026 sur claude/onyx-apps : 2048, invaders, arkanoid, freecell, solitaire, clipd, agenda, stickies, calendar, notes, pdf, photos, paint C++, courier C++"
metadata:
  node_type: memory
  type: project
  originSessionId: 7fc97ae2-2e97-4ea4-870b-1c00d2b2f6f2
  modified: 2026-11-01T14:10:03.176Z
---

Branche `claude/onyx-apps` (worktree .claude/worktrees/onyx-apps). En asm : 2048, invaders, arkanoid (neuf facon LBreakout, pas un port), freecell, solitaire, `src/sys/clipd`, `clipboard`, `agenda`, `stickies`. En C++ : calendar, notes, pdf (MuPDF, AGPL, `tools/pdf/build.sh`), photos, paint (remplace paint.asm), courier (remplace la version C#, sources gardees dans tools/dotnet/courier, non empaquetees). Partages : `src/inc/gamefb.inc`, `gamesnd.inc`, `cards.inc`. Une seule categorie « Développement » (Programmation et Developpement fondues).

**Why:** liste demandee par l'utilisateur ; Courier en C# mettait trop de temps a demarrer.

**How to apply:** tout essaye sous QEMU seulement, rien sur le Pi ni au vrai toucher ; reste Photos : copier une image (`ui_clip_set_image` existe maintenant). Pieges : un ajout a uikit.abi sans reconstruire uikit.so fait mourir les apps (code 127) ; `build\dotnet` est copie apres `build\office` ; apres fusion : kits.sh, office, pkg, build.ps1. Voir [[armpi-paquets]], [[armpi-courier]].
