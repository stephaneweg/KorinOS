---
name: armpi-apps-asm-onyx
description: "Passage des apps asm en C ou C++ : celles venues d'Onyx repartent de la version Onyx, en appelant les kits Korin en direct (aucun pont)"
metadata:
  node_type: memory
  type: feedback
  originSessionId: f37cb635-d720-4a7d-a203-55460bb5fb2c
  modified: 2026-11-12T21:37:18.844Z
---

Le 12/11/2026, l'utilisateur a précisé comment porter les apps asm en C ou C++, pour qu'elles servent aussi au PC. Certaines viennent d'Onyx et avaient été converties en asm : pour celles-là, on **repart de la version Onyx** (C++ d'origine), et non de l'asm. On **appelle les kits de Korin en direct**, sans pont, sans couche onyxcompat et sans l'uikit C++ d'Onyx. Ce qui manque s'ajoute au kit.

Relevé du 12/11, selon l'en-tête de leur .asm :
- **Apps asm qui citent Onyx** : 2048, clock, control, fmtracker, freecell, gamelib, invaders, mandelbrot, pipes, same, snake, solitaire, taskman, tetris.
- **Tous les émulateurs viennent aussi d'Onyx** (précision de l'utilisateur, 12/11) : nesemu, gbemu, gbaemu, snesemu, n64emu, gcemu, et n3dsemu ; on repart donc aussi de leur version Onyx. **Ceux qui ont un JIT** (il produit du code AArch64) sont à réécrire pour le PC : un second générateur x86-64. C'est le cas de gcemu (gc_jit.cpp, gxes_rec.cpp), n3dsemu (n3ds_jit.cpp, n3ds_shjit.cpp pour les shaders) et n64emu (n64_jit.inc, en asm). Première étape : l'interpréteur seul sur PC ; ensuite, séparer décodage et génération pour avoir deux générateurs (AArch64 et x86-64). Le JIT AArch64 reste propre au Pi. **Exigence de l'utilisateur (12/11) : chaque émulateur à JIT fournit les DEUX versions, ARM (AArch64) et x86-64** ; l'interpréteur seul n'est qu'une étape.
- **Apps asm sans mention d'Onyx** : arkanoid, bubbles, builder, calculator, editor, files, mediaplayer, mixer, music, piano, recorder, targets, terminal, uidemo, viewer, welcome. Elles se portent depuis l'asm, comme les commandes de /bin, sauf si leur historique montre une origine Onyx.

**Where :** les sources d'Onyx sont dans le dépôt https://github.com/stephaneweg/onyx (autrefois C:\Projects\Onyx, absent de cette machine le 12/11). Il faudra le cloner, avec l'accord de l'utilisateur. **Accord donné le 13/11 : cloné (--depth 1) dans C:\Projects\Onyx** (lecture seule ; user/Apps, user/Kits, user/Emulators).

**Why:** la version Onyx est le vrai source. L'asm n'en est qu'une traduction, et les règles de port de Korin interdisent les ponts.

**How to apply:** avant de porter une app asm, regarder son en-tête et l'historique git pour savoir si elle vient d'Onyx.

Voir [[armpi-ports-sans-pont]], [[armpi-ports-uikit-os]], [[armpi-pas-de-pont-onyx]], [[armpi-ports-theme]].
**13/11** : les 14 apps asm d'Onyx refaites en C++ depuis Onyx (Pi et PC), dans main de la branche claude/x86-64-pc (c7c0ec79..223c3eb8) ; réserves : Invaders perd ses vies plus vite sous QEMU (à voir sur le Pi), volet Région affiche « Français » au lieu de « FR » : gardé, choix de l'utilisateur (13/11).
