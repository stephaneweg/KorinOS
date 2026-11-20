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

**How to apply:** code dans src/apps/win16 (cœur de MAME copié de vm86/mame, mémoire émulée à part, sélecteur réservé
aux points d'entrée de l'API) ; sources de winevdm dans WSL ~/win16/winevdm. Même code pour le Pi. Voir
[[armpi-emulateurs]], [[armpi-uikit-fenetres]].
