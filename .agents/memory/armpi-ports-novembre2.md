---
name: armpi-ports-novembre2
description: "Deuxieme vague de ports d'Onyx du 1/11/2026 sur claude/onyx-apps2 : jeux, taskman, archiver, iconedit, fractales, disks, Manette, impression, effets Koton, icones"
metadata:
  node_type: memory
  type: project
  originSessionId: 7fc97ae2-2e97-4ea4-870b-1c00d2b2f6f2
  modified: 2026-11-01T23:47:33.777Z
---

Dans main depuis le 2/11/2026 (branche `claude/onyx-apps2`). Asm : tetris, same, snake, pipes, taskman (remplace `src/apps/monitor`, retire), mandelbrot (Fractales, 13 types, NEON, 4 coeurs), applet Manette (padconf). C++ : pinball, critters, archiver, iconedit, disks. Commun des jeux : `src/inc/gameart.inc`, `gameart.h`, `gamekv.h`. Fichiers lit maintenant `opens` des manifestes (`find_opener`), les `.txt` restent a l'Editeur.

**Why:** liste demandee par l'utilisateur, avec des jeux plus attrayants et toutes les icones refaites, puis paquets deployes une fois les ports finis.

**How to apply:** tout essaye sous QEMU seulement. Manette reelle, son, batteurs de Pinball et vitesse sur le Pi restent a verifier. Ajouts hors apps qui demandent un `build.ps1` complet : noyau (`SYS_PROC_RIGHTS` 0x1D), fatfs et ramfs (`FS_INFO`), netd, NetKit, HIDManager, Elegant. Tetris a fantome, reserve et pieces a venir, absents d'Onyx. Icones : images RGBA par `tools/icons` (`art.py`, `build.py`, rendus commites), `icon.rgba` dans chaque app, `SD0:/res/icons` pour applets et glyphes (`ui_glyph`). Confirmees sur le Pi par l'utilisateur le 2/11 (son signalement venait d'une carte d'avant la refonte). Multimedia = son, video, images vues ; Creation = Paint, Icon Editor, Fractales. Voir [[armpi-ports-novembre]], [[armpi-paquets]].
