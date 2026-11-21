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
le fichier créé la reçoive (la plus basse libre) ; host_be.c sautait les mots après un « type: ». **D3 faite le 20/11** : QB 4.5 (éditeur, F5, BC, LINK, SHELL), souris INT 33h (mouse.c). QB et ses fichiers : WSL ~/korin-win16/qb45 (archive) et ~/korin-win16/qbtest (copie de travail). Après D6 : Tomb Raider demandé (DPMI, CD, vitesse). **D4 faite le 20/11** : graphisme (plans de la VGA par cpu_vga_rd/wr, CGA, EGA, 12h, 13h, Mode X) ; QB lit l'INT 10h 1Bh avant SCREEN 12/13 ; les modes de 200 lignes ont la palette « CGA » (bit 4 = intensité). **D5 faite le 20/11** : VESA (svga.c), XMS et EMS (xms.c) ; FutureLib 3.5 (WSL ~/korin-win16/fl35, copie de travail ~/korin-win16/fltest : FUTURE45.LIB faite par LIB.EXE). Pièges : FutureLib change de page par le CRTC (pas 4F07h) et copie l'écran vers XMS/EMS par les fonctions de copie (0Bh, 57h) : elles doivent passer par la carte. **21/11 : passation dans docs/agents/HANDOVER-WIN16.md** (installation d'Art Song par SETUP.EXE : VB3 et VBRUN300, bloqué sur « DDE requires ddeml.dll » ; paquet win16-support demandé, pas fait). **20/11 (cloud) : l'installation d'Art Song va au bout sur l'hôte** (WIN16.md « L'installation d'Art Song va au bout ») ; pas encore sous QEMU ni sur le ZBook. **win16 1.0.1 publié le 20/11** (fa15172, pub.py, depuis le cloud) avec les lecteurs dans le home : C: = <home>/win16/c, drives.ini dans <home>/win16 (<home> = $HOME s'il vaut /home/<nom>, sinon l'alias HOME: ; l'utilisateur, 20/11 : « c: doit être dans /home/<user>/win16/c »), drives.ini tolérant (« C = /x »), lecteurs écrits sur stderr. **1.0.2** : clavier du DOS sous Korin (une IRQ par attente de 20 ms : l'IRQ 0 passait toujours avant l'IRQ 1 ; pic_waiting), « *.* » prend les noms sans point. **1.0.3** : win16 sans fichier (lanceur) = invite du DOS. Essais Korin sous QEMU PC depuis le cloud : bash dans l'arbre (le sh de Korin ne lance pas « & » en arrière-plan), une image avec un compte (users.ini home = /home/essai) pour voir le vrai HOME:. **Win32 demandé à terme (20/11)** : étude docs/etude/WIN32.md (voie A : prolonger win16 à la Win32s, PE + KERNEL32 + USER32/GDI32 sur user.c/gdi.c ; questions au § 7, rien d'engagé). Publiés le 20/11 au soir : win16 1.0.5, pc 2026.11.68 (bat = win16). **WINSOCK.DLL** (winsock.c, 20/11) : passe sur l'hôte ; sous Korin, en asynchrone les octets envoyés ne partent qu'à la fermeture (netd, à suivre) ; pas publié. Étude du mode protégé : WIN16.md § 10 (recommandation : un hôte DPMI, pas Windows entier). Programmes de WfW 3.11 (copie de l'utilisateur) : Bloc-notes, Calculatrice, Horloge, Solitaire, Démineur, Table de caractères, Terminal s'ouvrent. **MDI fait le 21/11** (mdi.c, WIN16.md § 10.6) : Gestionnaire de fichiers, Gestionnaire de programmes, Sysedit passent sur l'hôte ; pièges : LB_ERR = -1L (0xFFFFFFFF), SubtractRect, hauteur fermée des listes déroulantes, GlobalDiscard (GlobalLock rend NULL), HICON = bloc global (CURSORICONINFO, DumpIcon), pas de BN_SETFOCUS, touches retargetées au focus quand elles sont prises ; host_be.c a down:/move:/up: pour glisser. Open Watcom dans le cloud : ~/win16/watcom (instantané GitHub). Même soir : Éditeur (paquet pc 2026.11.66-67) : menu Fichier Nouveau/Ouvrir…/Enregistrer/Enregistrer sous…, chemin en lecture seule, sans sh ni bat. Pièges : OpenFile(OF_EXIST) doit trouver les DLL intégrées ; lpCmdLine finit par 0, pas 0Dh ; FindFirst sans résultat = 12h ; GlobalSize arrondi à 32 ; une boîte de VB fermée par W16_IDLE n'est pas validée (cliquer). **Paquet PC win16 1.0.0 publié le 20/11** (7733638d, à la demande de l'utilisateur) par `pub.py win16 --new --tree build/pub-win16-tree` (option ajoutée : un nouveau paquet sans publish.sh) ; l'arbre du PC n'avait pas encore win16.app. **D6 faite le 20/11** : son (sound.c, Nuked OPL3 dans nuked/, SB16, DMA) ; FM-Song et FM-Lib dans WSL ~/korin-win16/asher256-legacy, copie de travail ~/korin-win16/fmtest. Pièges : les minuteries de l'OPL doivent avancer à chaque lecture du port d'état ; l'eofms de FM-Lib répond « fini » tout de suite (ne pas s'en servir) ; sur l'hôte, W16_REALTIME=1 pour que les attentes durent. QB 4.5 et FutureLib 3.5 = copies de
l'utilisateur, jamais au dépôt ni en paquet (FutureLib : licence muette), extraites dans WSL ~/korin-win16, image
QEMU effacée après l'essai. Dans les commits : « QuickBASIC 4.5, copie de l'utilisateur ».

**How to apply:** code dans src/apps/win16 (cœur de MAME copié de vm86/mame, mémoire émulée à part, sélecteur réservé
aux points d'entrée de l'API) ; sources de winevdm dans WSL ~/win16/winevdm. Même code pour le Pi. Voir
[[armpi-emulateurs]], [[armpi-uikit-fenetres]].
