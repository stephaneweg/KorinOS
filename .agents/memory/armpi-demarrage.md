---
name: armpi-demarrage
description: "Volet Démarrage de Réglages (17/11) : types Automatique/Manuel/Désactivé façon services.msc, listes .off/.manual, services.deps, INIT_OP_SVC_STOP/START"
metadata:
  type: project
---

Demandé le 17/11/2026 (« une app pour décider quels services lancer au démarrage », puis « comme services.msc » :
automatique, manuel si un autre service en a besoin, désactivé ; démarrer, arrêter, redémarrer à chaud). Fait comme
volet de Réglages (`src/apps/control/applets/startup.cpp`, `control startup`), doc `docs/DEMARRAGE.md`, sur
claude/x86-64-pc (79416d3a), versions armpi 2026.11.41 et pc 2026.11.46 (pas publiés).

- Types dans des listes, jamais dans services.txt/session (fichiers `config =` : modifiés, ils ne sont plus mis à
  jour) : /etc/services.off et /etc/services.manual (élévation), HOME:/etc/session.off et session.manual.
- /etc/services.deps (skel/sd0/etc et skel/pc/etc, hors config) : init2 lance les dépendances d'abord, même
  manuelles, jamais une désactivée.
- init2 : INIT_OP_SVC_STOP (4, plus surveillé puis tué) et INIT_OP_SVC_START (5) ; appelant en uid 0.
- Essayé sous QEMU Pi (arrêt/démarrage à chaud, netd manuel lancé comme dépendance de ftpd, gpio désactivé) et PC
  (élévation par authd). Pas encore sur le vrai Pi ni le ZBook.
- Pièges d'essai : QEMU n'est que dans WSL (qemu_drive.py par python3, captures par `hmp screendump` .ppm : pas de
  PIL dans WSL) ; PC : `qemu_nvme.py --esp` avec un boot.cfg sans `cmdline = monitor` ; heredocs : les `\n` des
  sources C passés par python dans un heredoc sont mangés.
- Les constantes ajoutées à src/inc/armpi.inc arrivent aussi dans uikit_abi.h (macros) : `#ifndef` dans le C++.
