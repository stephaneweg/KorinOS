---
name: armpi-aide
description: "Aide de Korin OS (4/11/2026) : menu Aide de menubar, lecteur src/apps/help, help_fr/en.md des apps, guide API en C ; dans main (1d34c8bd, e28ad605)"
metadata:
  node_type: memory
  type: project
  originSessionId: 85ca319f-572b-451f-84ef-3b04fcb98f84
  modified: 2026-11-04T09:10:26.425Z
---

Fait le 4/11/2026 dans le worktree `claude/vigilant-mccarthy-05a710`, fusionné dans main le 4/11 (1d34c8bd, correctif e28ad605). Doc : `docs/AIDE.md`.

Choix à garder : la barre de menus trouve l'app active par `WL_PID` puis le nom du process (`SYS_PROCLIST`) = dossier de l'app ; menu Aide après les menus de l'app, ou deux lignes en tête de son propre menu Aide/Help ; lecteur C++ `help.app` (Markdown maison : titres, listes, code avec Copier, tableaux, images, liens `help:`/`app:`) ; guide dans `src/apps/help/data/api` (data/ jamais compilé : filtre ajouté à pibuild.mk et office/build.sh) ; `tools/help/check.sh` compile chaque bloc avec `main`.

**How to apply:** essais QEMU sur une carte faite depuis `build/sdroot` de main (script mkcard du scratchpad) ; port QMP 4593 (4471 est pris par une autre session, qui tue les QEMU au redémarrage). Restent : icône de l'app Aide, fusion avec un menu Aide d'app (Arabesque, QBasic) pas essayée, exemples jamais exécutés, prototypes faux d'abi2c.py (`ak_write_all`, `ak_enc_open_io`), FASMARM natif sur menubar pas essayé. Voir [[armpi-pocketqi]], [[armpi-panel]].
