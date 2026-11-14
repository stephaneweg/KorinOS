---
name: armpi-freebasic
description: "FreeBASIC dans /gcc (branche claude/freebasic-5c0d21, 31/10/2026) ; décisions de l'utilisateur et ce qui reste"
metadata:
  node_type: memory
  type: project
  originSessionId: 70161cb0-5ea7-4402-9523-f90f08b5228c
  modified: 2026-10-31T11:07:35.968Z
---

FreeBASIC 1.10.1 tourne sous ARMPI (QEMU et vrai Pi, 31/10/2026), fusionné dans `main` le 31/10/2026. Décisions de l'utilisateur : dans `/gcc`, GCC en backend par défaut, `gfxlib2` par un pilote de type fbdev dans une vue UIKit, et des `.bi` pour les kits, l'édition de liens prenant les `.a`.

**Why:** une chaîne de plus à côté de GCC et de `/dotnet`, pour coder en BASIC sur l'appareil, kits compris.

**How to apply:** construire par `tools/gcc/fbc.sh`, après `tools/gcc/kits.sh` qui génère les `.bi` (`tools/kits/abi2bi.py`). Essayé sur le vrai Pi le 31/10/2026 (fichiers neufs dans `SD0:/gcc`, kernel non touché) : tout marche, mais la libposix du Pi est plus ancienne, d'où `/gcc/test/basic/tcg.o` à passer à `fbc` (tcgetpgrp, __posix_exe_path) tant que le Pi n'a pas la libposix de main ; ne pas mêler deux libposix (`-l posix` donne des doubles définitions). `pibuild` connaît les `.bas` (commit 8a9b58c). Reste : `CLS` et `LOCATE` (le terminal n'a pas de séquences). Règle voisine : [[armpi-pas-de-pont-onyx]] ; voir [[armpi-gcc-sur-armpi]].
