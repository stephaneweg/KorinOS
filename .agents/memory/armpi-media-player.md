---
name: armpi-media-player
description: "Port du Media Player d'Onyx dans src/apps/media (media.app, à côté de mediaplayer en asm) ; essayé sous QEMU, jamais sur le Pi (31/10/2026)"
metadata:
  node_type: memory
  type: project
  originSessionId: 4c90101c-1819-4503-89c8-c0534b79742a
  modified: 2026-10-31T21:00:19.554Z
---

Le Media Player d'Onyx est porté sans pont dans `src/apps/media` (`SD0:/apps/media.app`, nom d'origine d'Onyx, fusionné dans `main` le 31/10/2026, commit 882d030). Le lecteur en assembleur `mediaplayer` reste : selfhost.

**Why:** docs/MEDIAKIT.md prévoyait les deux applications (29/10) ; `media` suit [[armpi-noms-apps-onyx]] et [[armpi-ports-sans-pont]].

**How to apply:** les limites durables sont dans `src/apps/media/README.md` : contrôles vidéo autour de la surface (Elegant la compose par-dessus le contenu), mini-lecteur sans déplacement de fenêtre. Mesure sur le Pi : `SD0:/home/media/lastvideo.txt`. Essais : `tools/office/tryimg.py` + `qemu_drive.py`, sous Git Bash les chemins de carte sont réécrits (passer par PowerShell).
