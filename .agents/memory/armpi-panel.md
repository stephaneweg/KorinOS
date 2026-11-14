---
name: armpi-panel
description: "Panel (31/10/2026) : barre du bas par défaut à la place du dock, lanceur, rideau WF_CURTAIN ; pièges d'essai QEMU"
metadata:
  type: project
---

Le Panel (`src/sys/panel`, `launcher.inc`, `src/inc/panelconf.inc`, applet `applets/panel.inc`) est **le choix par défaut** depuis le 31/10/2026 (décision de l'utilisateur : « on pourra le conserver par défaut »). L'ancien dock et son applet restent dans les sources, masqués : ne pas les supprimer sans qu'il le demande. L'applet Panel réutilise des aides de `applets/dock.inc`.

Choix à garder : le lanceur est une fenêtre WF_GRAB du même programme (boucle imbriquée, comme un menu : un clic hors du cadre le ferme, le bouton « apps » compris) ; `WF_CURTAIN` lie le masquage à la vie de la fenêtre (MINI_CURTAIN, retombe si le programme meurt) ; `panel.ini` absent : repli sur `dock.ini` ; le panel suit WEV_SCREEN et a le bouton shutdown, comme le dock.

**Why:** demandé par l'utilisateur pour moderniser le dock ; il aime ce type de dock et la fenêtre des applications.

**How to apply:** essais QEMU : le préchargement de Jet (95 Mo) occupe vfs, menubar et panel (applis_load) n'apparaissent qu'après 40 à 80 s ; ce n'est pas un blocage. Pour tester vite, image sans la ligne jet de preload.txt, attendre 42 s, `move` avant le premier clic. Après un merge dans main, refaire un build complet : un pilote absent (usbgamepad -> -2) laisse la souris QEMU sans pilote. Après « Appliquer » d'un thème, le panel met plus de 7 s à revenir sous QEMU. Thèmes sombre et clair, 800x600 et 1024x768 essayés ; PocketTerm pas encore. Voir [[armpi-espaces-travail]], [[armpi-uikit-fenetres]].
