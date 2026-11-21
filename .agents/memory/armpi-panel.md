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

**17/11/2026 : panel en verre** (demande « fait encore un peu rétro », icônes de droite « vieillottes », « semi-transparent, comme du verre ») : `panel.c` (commun Pi/PC ; `panel.asm` n'est plus copié) : fond `draw_glass` (TJ_DOCK translucide, GLASS_T_*), `glass_rbox` pour tout ce qui est translucide (ui_rbox ne lisse que l'opaque), boutons de droite tous à TJ_TEXT2 ; glyphes trash/clipboard/gear/power redessinés au trait dans `tools/icons/art.py` (partagés avec dock, accueil, quick, login). Pas de flou : Elegant compose par petites zones (curseur). Détail : DESIGN-ELEGANT.md §8.9. Captures sous QEMU : QEMU de Windows absent le 17/11, celui de WSL marche (`ARMPI_QEMU=qemu-system-aarch64`, python3 sans -I) ; image privée = copie de build/sd.img patchée (imgpatch.py de docs/design-proposal/outils) ; une autre session peut réécrire build/sd.img pendant la copie.
**17/11 (suite) : flou et volume** (demande de l'utilisateur) : rôle `WF_BLUR` (0x8000, ws_ext.inc, uk_consts.h) ; Elegant (`glass_under`, `glass_extend` dans core.c) floute le dessous et pose une ombre de 10 px au-dessus ; toute zone sale qui touche le verre est agrandie à tout le verre (sinon marques). Coût sur le vrai Pi pas mesuré (barre entière recomposée au survol du curseur).
**17/11 : barre de menus en verre** (demande de l'utilisateur) : même verre par `src/sys/common/glass.c` (panel et menubar), ombre d'Elegant sous une fenêtre WF_BLUR collée en haut. Arrivé dans le commit 31df755a d'une autre session (index git partagé : une session qui commite tout l'index emporte ce que les autres ont préparé ; préparer et commiter d'un coup).
**21/11 : une app « ouverte » = un process de son nom** (apprun.c : nom du process == nom du catalogue ; pkglib.h `running` fait de même et refuse la mise à jour). Un widget rôle d'app (`main --widget`) doit donc changer de nom : Météo se relance par execv en `weather-widget` (korin_exec_name), compétence korin-widget à jour. Sur claude/busy-knuth-qcktwy, pas essayé sous QEMU ni sur le Pi (syntaxe vérifiée seulement).
