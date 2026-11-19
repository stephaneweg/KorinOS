---
name: armpi-build-world-pc
description: Prochain « build world » du PC (18/11) : nettoyer, tout reconstruire, publier, nouvelle clé ; puis intégrer les apps de FromClaude et faire des widgets (Météo, Agenda, Notes) un choix de l'utilisateur
metadata:
  type: project
---

Plan de l'utilisateur (18/11) : quand le diagnostic de Jet (connexion Facebook) sera fini, 1) nettoyer build/ (énorme),
2) tout reconstruire, 3) publier tous les paquets, 4) nouvelle image de la clé d'installation.

À y faire partir (committé sur claude/x86-64-pc, pas publié) :
- le fantôme du glisser de Fichiers (110b1598 : Elegant, UIKit ui_drag_image, Fichiers ; essayé sur le Pi seulement,
  à vérifier sur le PC avant publication) ; le coffre à secrets (keyvault, 1f076b73..7a1316c1) ; l'ABI monte (616 avant).
- libposix sans limite d'atexit (3b25cdfd, 77883a44 ; docs/POSIX.md « Fonctions de sortie sans limite ») : libposix
  est statique, tout doit être relié de nouveau. Ordre (WSL, racine du dépôt) :
  1. `ARCH=x86_64 sh tools/posix/build.sh`
  2. GCC de /devtools : `ARCH=x86_64 STAGES=posix sh tools/gcc/build.sh` (facultatif : `RELINK=1 ARCH=x86_64 sh
     tools/gcc/build.sh` pour cc1, as, ld…), puis `sh tools/pc/sysroot.sh && sh tools/pc/sysroot_gcc.sh` ; gcc 1.0.11 -> 1.0.12.
  3. relier les autres programmes (apps.sh, sysroot_apps.sh…), monter leurs versions ; Jet déjà en 1.3.5 (PC), 1.3.3 (Pi).
  4. le Pi : `sh tools/posix/build.sh` puis `STAGES=posix sh tools/gcc/build.sh`.
Pièges : WSL a 7 Go, WebCore à 12 travaux le fait planter (JOBS=4) ; /tmp/korin-ffmpeg-jet-x86_64 disparaît quand WSL
s'arrête (`FLAVOR=jet ARCH=x86_64 sh tools/mediakit/ffmpeg.sh`) ; ninja refait tout WebCore (en-tête précompilé).
Pas de worktree ni d'image de test superflus (consigne du 18/11). Voir [[armpi-batterie]], [[armpi-installateur]].

**Après le build world, la clé et la publication (décisions du 18/11)** :
- Intégrer les trois apps de `FromClaude/` (zips faits par Claude dans Korin) aux sources officielles et les revoir
  (règles, style, ergonomie) : `weather` (app + widget), `asteroids` (C), `parkingjam` (Godot).
- **Widgets du bureau = choix de l'utilisateur, pas du système** : un widget n'est plus dans `/etc/session` ni dans
  `src/sys` ; c'est un rôle de son app (`main --widget`), que l'utilisateur ajoute depuis l'app (« Afficher sur le
  bureau » : une ligne dans `HOME:/etc/startup.txt`). Vaut pour Météo, et aussi pour **agenda** (Calendrier) et
  **stickies** (Notes) : les sortir de `src/sys` et de `/etc/session` (aujourd'hui sys/agenda, sys/stickies sur
  korinrt, dans les paquets calendar et notes). Compétence `korin-widget` à mettre à jour en même temps.

**Fait le 18/11 au soir** : build world complet, 113 paquets PC publiés (pc 2026.11.54, Godot 4.7.2-2.5 avec Vulkan,
gcc 1.0.12, make 1.0.2), Release à jour, main poussé ; clé `build/pc/installkey.img` (302 Mo, keyroot.py --repo
packages/pc puis installkey.py build), pas essayée. Pièges vus : RELINK=1 de tools/gcc/build.sh efface `make` de
build/pc/gccroot (refaire tools/gcc/make.sh) ; anv/VkKit sont maintenant dans l'étape mesa de rebuild-ports.sh (sans
eux, Godot sans Vulkan et pc sans vkkit.so) ; l'installateur n'est que sur la clé (sysroot_apps.sh le retire) ;
`mkrepo --drop NOM` retire un paquet que --keep-unbuilt garderait. Reste : widgets agenda et stickies (ci-dessus).
