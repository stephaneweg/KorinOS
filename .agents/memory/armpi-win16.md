---
name: armpi-win16
description: Programmes Windows 3.x (Win16) sur Korin d'après winevdm : étude docs/etude/WIN16.md, choix du 20/11 (couche maison, winevdm GPL-2 accepté, Open Watcom), app src/apps/win16
metadata:
  type: project
---
Chantier de plaisir lancé le 20/11/2026 : faire tourner les `.exe` NE de Windows 3.x sur Korin, en partant de
winevdm (OTVDM, github.com/otya128/winevdm, GPL-2, cœur i386 de MAME en BSD-3, DLL de Wine en LGPL ; vivant :
0.9.0, commits d'octobre 2026). Étude : docs/etude/WIN16.md.

Choix de l'utilisateur (20/11) :
- **couche maison** sur UIKit (pas Wine par la couche Linux, pas DOSBox + Windows 3.1) ;
- **winevdm entier accepté comme base** : le programme `win16` est sous GPL-2 ;
- programmes d'essai compilés par **Open Watcom v2** (WSL, `~/win16/watcom`) ; jamais de logiciel Windows propriétaire
  fourni ou téléchargé par nous.

**Why:** winevdm n'est qu'un traducteur Win16 → Win32 (user/gdi appellent user32/gdi32) et suppose un hôte Win32
32 bits (adresse linéaire = pointeur) : USER et GDI sont à écrire sur UIKit de toute façon.

**Premier jalon fait le 20/11 (QEMU PC)** : hello.exe (Open Watcom) s'affiche dans Elegant et se ferme par le ✕
(code 0). Essais : `tools/win16/host.sh` (hôte WSL, host_be.c, W16_TRACE=1/2, W16_SHOT), `tools/win16/test_pc.sh`
(QEMU, CLICKS="click:X,Y …"). Pièges : depuis PowerShell, passer à `wsl` un script écrit dans le scratchpad (les
guillemets et `$` se perdent) ; depuis Git Bash, `wsl sh /mnt/...` est réécrit en chemin Windows.

**Étape 3 et plus faites le 20/11 (QEMU PC)** : menus dans la barre globale, dialogues, contrôles au thème, DLL des
programmes, GDI large, COMMDLG, MMSYSTEM sans son. **Art Song 2.0** (shareware Delphi 1 de l'utilisateur, dans
build/win16/artsong, jamais commité) démarre dans Korin. Suite : le son MIDI (choix à présenter : SoundFont, FM, ou
moteur de Koton), le Pi. Lecteurs réglables par utilisateur : HOME:/win16/drives.ini, --drive (demande du 20/11).
Pièges : wrc d'Open Watcom ignore AUTOCHECKBOX/AUTORADIOBUTTON (écrire CONTROL) ; segments NE itérés (0x0008) ;
Delphi se sert du presse-papiers (format privé) pour cloner : tous les formats doivent être gardés.

**Son fait le 20/11** : MIDI -> synthé GM d'AudioKit (SoundFont du système), un fil comme Arabesque (l'utilisateur :
« regarde Arabesque, c'est un portage d'Art Song »). Art Song compose sous QEMU ; écoute sur le ZBook à faire.
Pièges : BeginPaint doit découper à la zone invalide et WS_CLIPCHILDREN exclure les filles (sinon le fond d'un
parent efface ses filles) ; lire les événements de Korin à chaque GetMessage (double-clics sous émulation lente) ;
pctest.py a « dblclick:X,Y ». Fait le 20/11 : comparaison avec winevdm (OTVDM 0.9.0 sous Windows, captures dans
build/win16/compare) ; **mode natif** : contrôles standard = widgets de UIKit (ctrl.c, native_*), W16_NATIVE=0 pour
l'ancien dessin.

**DOS demandé le 20/11** (« quickbasic 4, modes cga, vga et svga (futurelib) », FreeCOM, Sound Blaster/OPL,
essai OPL = FM-Song d'Asher256, redistribuable) : architecture dans WIN16.md §9. **Choix de l'utilisateur (20/11)** :
voie intégrée à win16 (pas DOSBox), Nuked OPL3, FreeCOM 0.86. Étapes D1-D6 du §9.8 ; **le PC seulement** (l'utilisateur, 20/11 : « pas besoin le pi pour l'instant » : rien pour le Pi). **D1 faite le 20/11
(20430286, hôte et QEMU PC)** : dos.c/bios.c/vga.c, tests/dos/dostest.c. Pièges : en mode réel, CS garde la base
FFF00000 tant que m_performed_intersegment_jump est 0 ; sel_base rend seg×16 quand w16_realmode ; un gestionnaire
qui attend le clavier rend WAIT (CS:IP reste sur le point d'arrêt F000:vecteur, IF mis, les IRQ passent). **D2 faite le 20/11** : FreeCOM sur Z: (build-freecom.sh,
sources dans WSL ~/korin-win16/freecom), noms courts, redirection. Piège : FreeCOM ferme la poignée 1 et attend que
le fichier créé la reçoive (la plus basse libre) ; host_be.c sautait les mots après un « type: ». QB 4.5 et FutureLib 3.5 = copies de
l'utilisateur, jamais au dépôt ni en paquet (FutureLib : licence muette), extraites dans WSL ~/korin-win16, image
QEMU effacée après l'essai. Dans les commits : « QuickBASIC 4.5, copie de l'utilisateur ».

**How to apply:** code dans src/apps/win16 (cœur de MAME copié de vm86/mame, mémoire émulée à part, sélecteur réservé
aux points d'entrée de l'API) ; sources de winevdm dans WSL ~/win16/winevdm. Même code pour le Pi. Voir
[[armpi-emulateurs]], [[armpi-uikit-fenetres]].
