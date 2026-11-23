---
name: armpi-vue-ensemble
description: "Vue d'ensemble à la GNOME (23/11/2026) : Elegant overview.c, panel devenu dash WF_OVERVIEW, Korin OS bascule la vue, zone d'état -> /sys/quickmenu, molette horizontale du pavé"
metadata:
  type: project
---

Demande de l'utilisateur (23/11/2026, capture de GNOME/Fedora Workstation) : « retravailler le shell graphique comme
GNOME ». Vue normale = barre de menus + fenêtres ; vue d'ensemble = bureaux en carrousel horizontal (~80 % de
l'écran chacun, marge autour), molette pour passer d'un bureau à l'autre (défilement animé), clic pour activer ;
le panel n'est plus pleine largeur et ne montre que épinglées + ouvertes + « Applications » (plus de sélecteur de
bureaux, corbeille, réglages, éteindre). « Korin OS » bascule la vue ; clic sur la zone d'état (heure, batterie…)
= popup de réglages rapides (réseau, son, éteindre). Puis : deux doigts de gauche à droite sur le pavé = défiler.

Puis (même jour) : dash « flat » à coins plus petits ; « Applications » = grille à la GNOME avec catégories
(WF_APPGRID 0x20000 : les bureaux montent en rangée en haut, la grille monte du bas, sur le fond gris de la vue ;
launcher.c la_grid). L'utilisateur demande si « le tout animé grâce au GPU » est possible : tout est animé sur le
processeur ; le GPU (GpuKit) reste l'étape suivante sur le Pi.

Fait (branche claude/fervent-maxwell-aqm4e7) : `src/sys/elegant/overview.c` (WS_OP_OVERVIEW 42, WF_OVERVIEW
0x10000, MINI_OVERVIEW 32, korin.shell.overview publié, Super seul dans desktop.c) ; `src/sys/panel/panel.c` réécrit
en dash ; `src/sys/menubar/menubar.c` ; nouveau `src/sys/quickmenu` ; `EV_HWHEEL` = 5 (i2chid gesture.c, banc
test/run.sh). Spécification : docs/DESIGN-ELEGANT.md § 6.0, 8.9 (encadré), 19.

**Why:** l'utilisateur pensait qu'il faudrait le GPU pour composer la vue ; choix fait : processeur avec vignettes
gardées (à l'arrêt et en défilement : des copies), mise à l'échelle au plus proche seulement pendant le zoom ;
GpuKit = étape suivante si le zoom est lent sur le Pi (rien n'a été mesuré sur le vrai Pi).

**How to apply:** essayé sous QEMU q35 (Korin PC, disque NVMe d'essai) seulement : build.ps1 du Pi est cassé sur
main par l'asm (uikit.asm sans ui_menu_states/ui_frame_glyphs, WS_MENU_MAX ; svc/vfs)  ; reste à mesurer sur le Pi
(1920x1080) et sur le ZBook (pavé tactile, deux doigts). Le menu Korin d'avant ne sert plus qu'en repli. L'asm
d'Elegant/panel/menubar (selfhost) n'a pas la vue d'ensemble.

**23/11 (suite)** : choix du mode dans Réglages > Écran (Bureau classique / Vue d'ensemble / Poche), clé
`desktop = overview | classic` de /etc/system.ini ; **la vue d'ensemble est le défaut** (demande de l'utilisateur) ;
classique <-> vue d'ensemble sans redémarrer (WS_OP_OVERVIEW 3/4, korin.shell.desktop : le panel se relance) ;
panel_classic.c = le panel d'avant. /etc/ca-bundle toujours posé par sysroot.sh (pkg sur la clé live). Elegant
INI_MAX 8 Kio. La clé live : installkey.py build --extra-mb 1024 (place pour Jet) ; l'utilisateur la teste sur le ZBook
et sous QEMU+KVM (fluide).

**23/11 (fin)** : dash réorganisable en glissant (panel.c dg_*, rang panel.ini par le nom) ; commande `umount`
(src/bin/umount, C) ; panneau de Fichiers qui défile ; icône `files` = classeur à tiroirs FERMÉS (l'utilisateur
préfère fermés) ; Elegant envoie un survol (-1,-1) à la fenêtre quittée (hov_win) ; quickmenu en WF_GRAB (clic
ailleurs = fermé) et à la teinte du dash (dash_palette, jetons du seul process) ; pilules de catégories : voile
glissé sous le texte (la_pill_under). Icônes Adwaita (LGPL3 / CC-BY-SA 3.0, « GNOME Project ») : le dépôt n'a PAS
d'icônes d'apps, seulement dossiers, périphériques, types de fichiers, avatar, symboliques. Piège : un remplacement
Python avec un motif vide (`s.index` d'une déclaration anticipée) insère le texte entre chaque caractère.
Icônes (23/11, sur main) : iconlib STYLE = 'adwaita' (plaque à épaisseur, aplats ; 'tile' = avant), art.obj() +
@icon(free=True) pour les objets libres (files, control, calculator, clock, calendar, corbeille). Suite proposée :
redessiner en objets ce qui s'y prête, icônes Adwaita de dossiers/types dans Fichiers. Écran de connexion façon
Fedora (login.c plein écran, couche normale ; quickmenu « L… » sans Réglages).
