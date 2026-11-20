# Étude : les programmes de Windows 3.x (Win16) sur Korin OS

Demandée le 20/11/2026 (« ça m'intéresserait », à propos de winevdm). Chantier de plaisir, pas urgent : faire
revivre les vieux logiciels de Windows 3.1 (`.exe` au format **NE**, API **Win16**). Vérifications en ligne du
20/11/2026 (sources en fin de document).

## 0. État (20/11/2026)

**Premier jalon fait (étapes 0 à 2, QEMU PC)** : `src/apps/win16` (app « Windows 3.x », `win16.app`) charge un
`.exe` NE compilé par Open Watcom v2 et affiche sa fenêtre dans le bureau de Korin : `tools/win16/tests/hello.c`
(classe, `CreateWindow`, boucle des messages, `WM_PAINT` avec `Rectangle` et `TextOut`) s'ouvre, se dessine et se
ferme par le ✕ d'Elegant (`WM_SYSCOMMAND` → `WM_CLOSE` → `WM_DESTROY` → `PostQuitMessage`, code 0) ; `msgbox.c`
montre une boîte de message de Korin. Choix de l'utilisateur au § 7 ; fichiers et licences dans
`src/apps/win16/README.md`.

**Étape 3 faite, et plus (20/11, QEMU PC)** : menus (dans la barre globale de Korin, états grisé et coché),
accélérateurs, minuteries, dialogues des ressources, contrôles (BUTTON, STATIC, EDIT, LISTBOX, COMBOBOX, SCROLLBAR)
dessinés aux couleurs du thème, DLL des programmes (LibMain), ressources par nom, segments itérés, tas local,
mémoire globale et sélecteurs, fichiers et INI (lecteurs ci-dessous), presse-papiers (tous les formats), COMMDLG
(Ouvrir, Enregistrer, Couleur sur les dialogues de Korin), GDI (polices, bitmaps et DC mémoire, BitBlt et ses
opérations raster, DIB, polygones, régions, projections, palettes, icônes), MMSYSTEM (sortie MIDI sans son,
minuteries multimédia). Essais : `tools/win16/tests/dialogs.c` sous QEMU ; **Art Song 2.0** (shareware Delphi 1
de 1996, fourni par l'utilisateur, jamais dans le dépôt) démarre dans Korin, sa fenêtre principale et sa boîte
« About » s'affichent. **Son (20/11)** : la sortie MIDI va au synthé General MIDI d'AudioKit avec la SoundFont du
système (`/res/soundfonts/GeneralUser-GS.sf2`), joué par un fil comme dans Arabesque (choix de l'utilisateur, § 8) ;
Art Song compose sous QEMU (plus de mille messages MIDI, la mesure avance), son pas encore écouté sur le ZBook.

**Mode natif (20/11, demande de l'utilisateur)** : les contrôles standard (boutons, cases, boutons radio, cadres de
groupe, textes, champs, zones de texte, listes, listes déroulantes, barres de défilement) sont des **widgets de
UIKit** posés à leur place : dessin, survol, focus, clavier et thème de Korin, comme une app native ; les dialogues
des ressources prennent alors la police de UIKit pour leurs unités (comme les « grandes polices » de Windows).
Restent dessinés par win16 : les contrôles owner-draw (les `TBitBtn` de Delphi), les listes à choix multiple, tout
sur l'hôte, et tout avec `W16_NATIVE=0`. UIKit traduit de lui-même les mots courants (« Cancel » → « Annuler »).

**Comparaison avec winevdm (20/11)** : Art Song sous OTVDM 0.9.0 (Windows 11) commence par trois « Cannot Find IFS
Codes... » et ses listes de motifs et de styles restent vides ; dans win16 il les trouve (D: = son dossier). Mise en
page proche ; écarts : menus dans la barre de Korin, polices de Korin (un peu plus larges : quelques contrôles
rognés à droite), boîte « About » sans le mot « UNREGISTERED » (texte plus large que son étiquette).

**Lecteurs** (réglages de chaque utilisateur) : C: = `HOME:/win16/c` (avec `C:\WINDOWS\WIN.INI`), D: = le dossier
du programme, E: = tout l'arbre de Korin ; `HOME:/win16/drives.ini` (section `[drives]`, `C=/dossier`) et l'option
`--drive C=/dossier` les changent.

**Programmes DOS, étape D1 (20/11, § 9)** : un `.exe` sans en-tête NE ou un `.COM` part dans la machine DOS de
win16 (`dos.c`, `bios.c`, `vga.c`) : mode réel du cœur de MAME, MCB, PSP, environnement, chargeur MZ, EXEC avec
retour et code (4Dh), console du DOS dans l'écran texte, INT 10h texte (polices de la ROM, 25/28/50 lignes), INT
16h et clavier par le port 60h et l'INT 9 (codes de balayage, caractère de Korin), PIC, PIT et INT 8/1Ch, CMOS,
A20. `tools/win16/tests/dos/dostest.c` (Open Watcom) : texte et couleurs en B800, mémoire, fichiers, horloge,
`spawnl` d'un enfant qui rend 42, touche lue.

Pas encore : le Pi (même code, à construire), MDI, métafichiers, impression, WinHelp, crochets, les barres de
défilement des fenêtres (seulement les contrôles), le son ; pas de paquet.

## 1. Ce qu'il faut faire tourner

Un programme de Windows 3.x est un exécutable **NE** (« New Executable ») : un en-tête MZ (le bout DOS qui dit
« This program requires Microsoft Windows »), puis l'en-tête NE, la table des **segments** (code et données de 64 Ko
au plus), les **relocations** de chaque segment (appels vers KERNEL, USER, GDI… par numéro d'ordre ou par nom), les
**ressources** (menus, dialogues, icônes, bitmaps, chaînes) et la table des noms exportés.

Il tourne en **mode protégé 16 bits** du 286 (sélecteurs dans la LDT, adresses `segment:décalage`), parfois avec des
instructions du 386 (registres 32 bits, `WINMEM32`, Win32s mis à part). Il appelle l'API Win16 en convention
**Pascal** (arguments empilés de gauche à droite, nettoyés par l'appelé, `RETF n`). Il partage tout avec les autres
programmes Win16 : un seul espace d'adresses, multitâche **coopératif** (la main passe dans `GetMessage`,
`PeekMessage`, `Yield`).

Les DLL du système dont il dépend : **KERNEL** (mémoire globale et locale, sélecteurs, modules, ressources, fichiers
par `_lopen`/`OpenFile`, fichiers INI, atomes, tâches), **USER** (fenêtres, classes, messages, menus, dialogues,
contrôles, presse-papiers, minuteries, curseurs), **GDI** (contextes de dessin, plumes, brosses, polices, bitmaps,
palettes, régions, métafichiers), puis selon l'app **COMMDLG** (dialogues Ouvrir, Couleur, Police), **SHELL**,
**MMSYSTEM** (son), **WIN87EM** (calcul flottant), **TOOLHELP**, **DDEML**, **LZEXPAND**, **VER**, **WINSOCK**…

## 2. winevdm (OTVDM), vérifié le 20/11/2026

* **Dépôt** : github.com/otya128/winevdm. **Vivant** : dernière version **0.9.0** (15 septembre), derniers commits
  le **24 octobre 2026** (correctifs reçus de contributeurs), 4 082 commits, 3 700 étoiles, environ 350 tickets ouverts.
* **Licences** :
  - le dépôt entier est sous **GPL-2.0** (`LICENSE`, et `vm86/COPYING.txt`), sans « ou ultérieure » ;
  - le cœur du processeur, `vm86/mame/emu/cpu/i386/` (tiré de MAME), garde son en-tête **BSD-3-Clause** par fichier
    (Ville Linde, Barry Rodewald, Carl, Phil Bennett) ;
  - les DLL reprises de Wine (`krnl386/ne_module.c`, `user/message.c`, `user/window.c`, `gdi/gdi.c`,
    `krnl386/wowthunk.c`…) gardent leur en-tête **LGPL-2.1 ou ultérieure** (Alexandre Julliard, Ulrich Weigand…) ;
  - les ajouts propres à winevdm sont donc sous GPL-2 : un programme qui les reprend devient GPL-2.
* **Plateforme** : un programme **Win32 x86 (i686)**, construit par Visual Studio 2017 (ou, « pas recommandé », par
  CMake et le GCC de MSYS2 MINGW32). Rien pour ARM64 ni pour un autre système que Windows.
* **Architecture** :

| Partie | Rôle | Dépend de Win32 ? |
|---|---|---|
| `otvdm` | le lanceur : ouvre le `.exe`, prépare la machine, lance la tâche | oui (process, registre) |
| `vm86/mame` | **émulateur 286/386/486 de MAME** (`i386.c`, `i386op16.c`, `i86`, x87, `vtlb`, softfloat) | non : C pur |
| `vm86/msdos.cpp` | appels DOS (INT 21h…) émulés pour Win16 (repris de MS-DOS Player) | en partie (fichiers) |
| `wine/` | aides de Wine : LDT **émulée** (`ldt.c`), chaînes, débogage, configuration | peu |
| `krnl386` | KERNEL 16 bits : chargeur **NE**, sélecteurs, tas global et local, ressources, tâches, thunks | oui (kernel32, ntdll) |
| `user` | USER 16 bits : **enveloppes de user32** (`ShowWindow16` → `ShowWindow (WIN_Handle32 (hwnd))`) | **entièrement** |
| `gdi` | GDI 16 bits : **enveloppes de gdi32** (`LineTo16` → `LineTo (HDC_32 (hdc))`, `TextOut16` → `TextOutA`) | **entièrement** |
| `wow32` | conversion des poignées 16 ↔ 32 bits, thunks génériques | oui |
| `convspec` | outil : produit le code de relais des `.spec` (assembleur i386, `as --32`) | produit du code x86 32 bits |
| `commdlg`, `shell`, `mmsystem`, `winsock`, `ole2`… | ~70 DLL 16 bits, toutes posées sur leur pendant Win32 | oui |
| `whpxvm`, `haxmvm` | variantes qui exécutent le code 16 bits sur l'hyperviseur de Windows ou HAXM | oui |

* **Pourquoi un émulateur** : Windows 64 bits ne laisse pas un programme modifier sa LDT, donc pas de segments
  16 bits natifs ; winevdm exécute le code 16 bits dans le cœur de MAME, avec une LDT tenue par lui, et passe dans
  le code natif (32 bits) à chaque appel de l'API.
* **Le point clé pour nous** : winevdm n'est pas un « Windows 3.1 » complet ; c'est un **traducteur Win16 → Win32**.
  Tout ce qui se voit (fenêtres, messages, contrôles, dessin, polices, dialogues) est fait par user32 et gdi32 de
  Windows. Sans Win32 en dessous, il ne reste de winevdm que l'émulateur, le chargeur NE et la tenue de la mémoire.

## 3. Ce qui se porte tel quel, ce qui doit passer par Korin

| Partie | Sur Korin |
|---|---|
| Cœur i386 de MAME | **se porte tel quel** (C, BSD-3 compatible avec la licence MIT de Korin) ; marche aussi sur le Pi (pas d'asm) |
| LDT émulée, sélecteurs | se porte (C), à détacher des types de Win32 |
| Chargeur NE, ressources, tas global et local, atomes, INI | logique reprenable (LGPL) mais écrite sur kernel32 et ntdll : à refaire sur POSIX (`open`, `malloc`…) |
| Thunks (`convspec`, `relay16.s`, `wowthunk.c`) | **pas portables** : assembleur i386, contexte x86 32 bits, registre `fs` ; à remplacer par une table d'appels en C (§ 5.2) |
| USER (fenêtres, messages, contrôles, dialogues, menus) | **rien à reprendre directement** : c'est user32 qui fait tout ; à écrire sur UIKit |
| GDI | idem (gdi32) ; mais le **dibdrv** de Wine (gdi32, LGPL, C pur) dessine tout dans un tampon de pixels : il servirait |
| Fichiers, lecteurs `C:` | POSIX : un dossier de `HOME:` vu comme `C:\` |
| Son (MMSYSTEM) | AudioKit (wave) ; MIDI plus tard (le synthé de FM Tracker ou de Koton ?) |
| Réseau (WINSOCK) | POSIX (sockets), plus tard |

## 4. Les voies possibles

### 4.1 Wine complet par la couche Linux (liblinux, linuxhost)

La couche de `docs/etude/WIDEVINE.md` charge des `.so` de Linux **x86-64**. Wine en Win16 demanderait bien plus :

* le Wine de Linux exécute le code 16 bits **en natif**, par des segments 16 bits dans la LDT (`modify_ldt`) d'un
  process **32 bits**. Korin PC n'a ni process 32 bits (mode compatibilité), ni LDT par process ; il faudrait les
  ajouter au noyau, avec les pièges de sécurité qui ont fait couper ces segments par Linux (« espfix ») ;
* le **nouveau WoW64** de Wine (sans bibliothèques 32 bits) **ne lance pas les programmes 16 bits** : bogues 54670
  (Wine 9.7) et 58760 (Wine 10, encore ouvert), le 16 bits ne marche qu'avec un Wine 32 bits classique ;
* Wine a besoin de `wineserver`, d'un pilote d'affichage **X11 ou Wayland** (Korin n'a ni l'un ni l'autre), de
  fontconfig, freetype… : des semaines de couche Linux avant la première fenêtre ;
* le PC seulement (le Pi est ARM : il faudrait en plus un émulateur x86).

**Écartée** : c'est le plus gros chemin, pour un résultat moins intégré que les autres.

### 4.2 DOSBox avec Windows 3.1 dedans

Porter DOSBox (Staging ou DOSBox-X, GPL-2) sur SDL, déjà porté (`armpi-sdl`), puis installer Windows 3.1 dedans.

* **Le plus rapide** pour une fidélité totale (c'est le vrai Windows) : quelques jours, sur le PC et le Pi.
* Mais **Windows 3.1 est propriétaire** : ni nous ni Korin ne pouvons le fournir ; l'utilisateur doit installer sa
  propre copie. Les programmes vivent dans une fenêtre 640×480 à part, pas comme des fenêtres de Korin ; les fichiers
  passent par un dossier monté ; pas de presse-papiers commun, pas d'association de fichiers.
* Utile en **plan B** ou en complément (jeux DOS, programmes qui écrivent dans le matériel), pas comme réponse.

### 4.3 winevdm presque tel quel, sur un Win32 à nous

Garder `krnl386`, `user`, `gdi`… de winevdm et écrire en dessous un **sous-ensemble de user32 et gdi32** sur UIKit.
Le Win32 visé serait celui que winevdm appelle : des centaines de fonctions, avec la sémantique fine de Windows
(messages imbriqués, `DefWindowProc`, contrôles…). Autant écrire la même chose directement en Win16, sans la
conversion des poignées 16 ↔ 32 ni les thunks de `wow32`. Et tout le programme serait **GPL-2**.

**Écartée** comme base, gardée comme **référence** : son code dit ce que les programmes de l'époque attendent
vraiment (les correctifs de compatibilité de ses 4 000 commits).

### 4.4 Une couche Win16 maison (recommandée)

Un programme Korin, **`win16`**, qui contient :

* le **cœur i386 de MAME** (BSD-3), en mode protégé 16 bits, LDT tenue par nous ;
* un **chargeur NE** et un **KERNEL** écrits pour Korin (POSIX), en s'aidant du code de Wine (LGPL) ;
* **USER et GDI écrits directement sur UIKit** : chaque fenêtre principale d'un programme Win16 devient une
  **fenêtre de Korin** (`ui_window_ex`) avec une **surface** (`ui_surface`, tampon de pixels) ; les fenêtres filles et
  les contrôles sont dessinés par nous dans cette surface, comme le faisait Windows 3.1 ;
* le reste (COMMDLG sur les dialogues de Korin, MMSYSTEM sur AudioKit…) au fil des programmes qui le demandent.

Avantages : les programmes Win16 sont de **vraies fenêtres de Korin** (barre, déplacement, Alt+Tab, thème) ;
**le même code sur le PC et le Pi** (C portable, pas de LDT ni de mode 32 bits dans le noyau) ; aucun logiciel
propriétaire à fournir ; licence maîtrisée. Inconvénient : USER et GDI sont à écrire, c'est l'essentiel du travail.

Pourquoi un émulateur même sur le PC : les programmes de Windows 3.1 visaient un 286 à 12 MHz ou un 386 à 33 MHz ;
un interpréteur sur un i7 ou un Cortex-A72 les fait tourner largement plus vite qu'à l'époque, sans toucher au noyau.

## 5. Architecture proposée (voie 4.4)

### 5.1 Où

| Chemin | Contenu |
|---|---|
| `src/sys/win16/cpu/` | cœur i386 de MAME (BSD-3, en-têtes gardés), réduit au 286/386 et au x87 |
| `src/sys/win16/ne.c` | chargeur NE : segments, relocations, ressources, DLL du programme |
| `src/sys/win16/ldt.c`, `mem.c` | sélecteurs, tas global (`GlobalAlloc`) et local (`LocalAlloc` dans le segment de données) |
| `src/sys/win16/kernel.c`, `user*.c`, `gdi*.c`… | une DLL Win16 « intégrée » par fichier ou dossier |
| `src/sys/win16/spec/*.spec` | les tables d'ordinaux (format des `.spec` de Wine), traduites en C par `tools/win16/spec2c.py` |
| `src/sys/win16/lgpl/` | ce qui serait repris de Wine (en-têtes LGPL gardés), si l'utilisateur l'accepte (§ 7) |
| `src/apps/win16/` | l'app « Windows 3.x » : app.txt, icône, association des `.exe` NE |

Un seul process par programme Win16 au début (plus simple et isolé). Les programmes qui se parlent (DDE, OLE,
fenêtres d'un autre programme) demanderont plus tard un process commun à plusieurs tâches, comme Windows 3.1.

### 5.2 Appels de l'API

Chaque fonction importée est un **point d'entrée fictif** dans un segment de code réservé (un sélecteur par DLL
intégrée, l'ordinal comme décalage). Quand le processeur émulé saute dans ce segment, la boucle de l'émulateur
appelle la fonction C correspondante, qui lit ses arguments sur la pile 16 bits (convention Pascal, décrite dans le
`.spec` : `word`, `long`, `ptr`, `str`, `segptr`), fait le travail, met le résultat dans `AX` (et `DX`), dépile
(`RETF n`) et reprend. Les rappels (procédure de fenêtre, `EnumWindows`, minuteries) font l'inverse : le C pousse
les arguments, appelle l'adresse 16 bits et fait tourner l'émulateur jusqu'au retour.

### 5.3 Fenêtres, messages, dessin

* **Messages** : une file par tâche ; `GetMessage` prend d'abord les messages postés, puis traduit les événements de
  UIKit (`ui_event`, `EV_WIN` dit la fenêtre) en `WM_MOUSEMOVE`, `WM_LBUTTONDOWN`, `WM_KEYDOWN`/`WM_CHAR`,
  `WM_SIZE`, `WM_CLOSE`… ; `WM_PAINT` naît des zones invalides ; `SendMessage` appelle directement la procédure.
* **Fenêtres** : arbre des fenêtres Win16 tenu par nous ; une fenêtre sans parent devient une fenêtre de Korin
  (`ui_window_ex`), avec sa surface ; le cadre et la barre de titre sont ceux de Korin (le programme ne voit que la
  zone cliente, `WM_NCPAINT` réduit au strict).
* **GDI** : un contexte (DC) dessine dans la surface de sa fenêtre principale, décalé et découpé à la fenêtre fille ;
  primitives (lignes, rectangles, ellipses, polygones, `BitBlt` et ses 256 opérations raster, `StretchBlt`, DIB et
  DDB, palettes en 8 bits converties à l'affichage), texte par les polices de UIKit (correspondance « System »,
  « MS Sans Serif », « Courier », « Times », « Arial » → polices de Korin, tailles en points), régions.
* **Contrôles** (`BUTTON`, `EDIT`, `STATIC`, `LISTBOX`, `COMBOBOX`, `SCROLLBAR`) et **dialogues** (ressource
  `DIALOG`, unités de dialogue) : dessinés par nous dans l'esprit de Windows 3.1, aux couleurs du thème (jetons de
  `ui_tokens()`, jamais de couleurs en dur), et non par les widgets de UIKit (les programmes les sous-classent,
  leur envoient des messages et les dessinent eux-mêmes : `WM_CTLCOLOR`, `BS_OWNERDRAW`).
* **Menus** : la barre de menus Win16 est dans la fenêtre (comme sous Windows 3.1) ; plus tard, option pour la passer
  dans la barre du haut de Korin (`ui_menu`).

### 5.4 Fichiers

`C:\` = `HOME:/win16/c` (créé au premier lancement, avec `WINDOWS\WIN.INI` et `SYSTEM.INI` minimaux) ; le dossier du
programme lancé devient le dossier courant (vu comme `D:\` s'il est hors de `C:`). Noms courts 8.3 en majuscules
côté programme, noms vrais côté Korin (table de correspondance si besoin). Pas d'écriture hors de ces dossiers.

## 6. Étapes chiffrées

Durées en jours de travail d'une session, à la louche.

| Étape | Contenu | Essai | Durée |
|---|---|---|---|
| 0 | **Programmes d'essai** : Hello en Win16 (fenêtre, `TextOut`, menu, `MessageBox`), compilés par nous (§ 7, choix 3) ; script d'essai QEMU PC | les `.exe` s'ouvrent dans winevdm ou DOSBox+Win3.1 sous Windows, si l'utilisateur veut comparer | 1 |
| 1 | **Cœur i386** de MAME isolé, LDT, boucle d'exécution, table d'appels ; **chargeur NE** ; KERNEL minimal (`InitTask`, `GetModuleHandle`, `LocalInit`, `GlobalAlloc`, `LockResource`…) ; `MessageBox` sur `ui_message` | « Hello » en boîte de message sous QEMU PC | 2 à 3 |
| 2 | **Premier jalon** : USER et GDI minimaux (`RegisterClass`, `CreateWindow`, `ShowWindow`, `GetMessage`/`DispatchMessage`, `DefWindowProc`, `BeginPaint`, `TextOut`, `Rectangle`, `WM_DESTROY` → `PostQuitMessage`) | la fenêtre Hello s'affiche dans Elegant, se ferme | 3 à 4 |
| 3 | Menus, accélérateurs, minuteries, curseurs, contrôles de base, dialogues depuis les ressources, `DialogBox`, `GetDlgItem` | un petit programme à dialogues (calculatrice, bloc-notes libres) | 10 |
| 4 | GDI complet : plumes, brosses, polices et métriques, bitmaps (DIB/DDB), `BitBlt`/`StretchBlt`, palettes, régions, métafichiers ; éventuellement le **dibdrv** de Wine | jeux simples, programmes de dessin | 10 |
| 5 | COMMDLG (dialogues de Korin), INI, `REG.DAT`, presse-papiers (`ui_clip_*`), SHELL, MMSYSTEM (wave sur AudioKit), WIN87EM, TOOLHELP, LZEXPAND, VER | installeurs, programmes avec son | 10 à 15 |
| 6 | Plusieurs tâches dans un process (DDE, programmes qui se voient), WinHelp (`.hlp`), DLL fournies par les programmes (`VBRUN300.DLL` et autres runtimes, que l'utilisateur apporte) | Visual Basic 3, aide | 10 |
| 7 | Le Pi (même code), association des `.exe` NE, paquet (sur demande), aide de l'app | | 2 |

Le premier jalon (étapes 0 à 2) tient en **une semaine** ; une couche qui fait tourner la plupart des petits
programmes de l'époque (étapes 3 à 5), **deux mois** environ. Le reste se fait au fil des programmes essayés.

## 7. Choix de l'utilisateur (20/11/2026)

* **Voie : la couche maison** (4.4), sur UIKit, le même code sur le PC et le Pi.
* **Code repris : winevdm entier accepté comme base** : le programme `win16` est sous **GPL-2** (le cœur de MAME garde
  son en-tête BSD-3, les fichiers de Wine leur en-tête LGPL). On reprend de winevdm tout ce qui se porte ; USER et
  GDI restent à écrire sur UIKit.
* **Programmes d'essai : Open Watcom v2** (dans WSL, `~/win16/watcom`, version « Current-build » de GitHub).

Ce que la lecture du code de winevdm a appris (20/11) : `vm86/msdos.cpp` exécute une instruction à la fois
(`CPU_EXECUTE`) dans le cœur de MAME, avec une adresse linéaire **égale au pointeur de l'hôte** (`mem` = 0, LDT de
Wine dont les bases sont des pointeurs Win32) ; il reconnaît un appel de l'API quand `EIP` tombe sur l'adresse native
de `__wine_call_from_16`. Tout `krnl386` repose sur ce modèle (blocs de `GlobalAlloc` = mémoire de l'hôte). Sur
Korin, en 64 bits, on garde le cœur et sa façon d'être piloté, mais avec **une mémoire émulée à part** (un tableau,
adresse linéaire = décalage) et **notre boucle** : un sélecteur réservé aux points d'entrée, reconnu avant chaque
instruction (§ 5.2). Le code de `krnl386` (NE, sélecteurs, tas local) se reprend en le passant à ce modèle.

Le dossier est `src/apps/win16/` (construit par la règle générique de `tools/pc/apps.sh`), pas `src/sys/win16/`.

## 7 bis. Les choix qui étaient proposés

1. **La voie** : couche maison (4.4, recommandée) ; DOSBox + Windows 3.1 apporté par l'utilisateur (4.2) ;
   les deux (DOSBox pour le DOS et ce que la couche ne sait pas encore).
2. **Combien de Wine reprendre** :
   * **(a)** seulement le cœur i386 de MAME (BSD-3) ; tout le reste écrit par nous, sous la licence de Korin (MIT),
     winevdm et Wine lus comme référence seulement ;
   * **(b)** (a) plus des fichiers de Wine **LGPL-2.1+** gardés tels quels dans `src/sys/win16/lgpl/` (chargeur NE,
     tas local, dibdrv, contrôles…) : moins de travail, mais `win16` devient en partie LGPL (sources des fichiers
     LGPL à fournir, ce que le dépôt public fait déjà) ;
   * **(c)** winevdm entier comme base (`krnl386`, thunks, contournements) : le programme devient **GPL-2** ; écarté
     plus haut parce que USER et GDI sont à refaire de toute façon.
3. **Comment compiler les programmes d'essai Win16** : **Open Watcom v2** (libre, Sybase Open Watcom Public
   License) installé dans WSL depuis ses versions sur GitHub ; ou un petit **éditeur de liens NE maison** en Python
   (code assemblé par `nasm` en 16 bits) : plus long, mais aucun outil à télécharger. Des programmes libres de
   l'époque (avec leur source) viendront ensuite ; jamais de logiciel propriétaire fourni par nous.

## 8. Le son MIDI

Les programmes musicaux de l'époque (Art Song, et tout ce qui joue des `.MID`) envoient des messages MIDI à
`midiOutShortMsg`. **Choix de l'utilisateur (20/11) : SoundFont General MIDI**, et en fait celui qui existe déjà :
AudioKit a un synthé General MIDI (`ak_synth_*`) avec la SoundFont du système (GeneralUser GS), celui d'Arabesque
(port d'Art Song) et de Koton. win16 le tient dans un fil (`be_uikit.c`), blocs de 256 trames à 44,1 kHz, comme
`src/apps/arabesque/audio.h`.

## 9. Les programmes DOS (étude du 20/11/2026, rien d'engagé)

Demande de l'utilisateur : « dans la couche win16, ça doit faire fonctionner quickbasic 4, les modes graphiques
cga, vga et svga (futurelib) », à la manière de NTVDM : un programme MZ (`.EXE`, `.COM`, `.BAT`) se lance depuis le
Terminal ou Fichiers et s'ouvre dans une fenêtre de UIKit. Essais prévus : **QuickBASIC 4.5, copie de
l'utilisateur** (éditeur, compilation, exécution, `SHELL`), **FutureLib 3.5** (copie de l'utilisateur, essai
principal du SVGA), **FM-Song 1.3.2** d'Asher256 (essai principal de l'OPL), et nos propres programmes compilés
par Open Watcom (`wcl -bt=dos`).

### 9.1 Ce que le processeur de winevdm couvre déjà

* Le cœur i486 de MAME (`mame/`, BSD-3) fait le **mode réel**, le **mode V86** et le mode protégé 16/32 bits, avec
  le x87 : tout ce que demandent QuickBASIC, FutureLib (instructions du 386) et FM-Song. Un vieux programme DOS
  peut tourner en mode réel pur, sans V86 : plus simple, rien à piéger par le moniteur.
* Les accès à la mémoire et aux ports passent par des fonctions de `cpu.cpp` (`read_byte`, `write_byte`,
  `read_io_byte`…) : aujourd'hui la mémoire est un tableau plat et les ports rendent `FF`. C'est là que se
  branchent la VGA (plage `A0000-BFFFF`), la page EMS (`D0000-DFFFF`) et tous les ports.
* winevdm a aussi, dans `krnl386`, la partie DOS de Wine (**LGPL-2.1**, compatible avec la GPL-2 de win16) :
  `dosexe.c` (chargeur MZ), `dosvm.c`, `dosmem.c` (MCB), `int21.c` (dont EXEC 4B00h), `int10.c` (BIOS vidéo et
  VESA), `vga.c`/`vga.h` (VGA), `int09.c`/`int16.c` (clavier), `int33.c` (souris), `int67.c` (EMS), `int2f.c`
  (XMS « pas tout à fait fait »), `ioports.c`, `timer.c`, `dma.c`, `soundblaster.c`, `int31.c` (DPMI). Mais ce
  code s'appuie sur Win32 (un fil par machine virtuelle, `DOSVM_*` rangés autour des API de Windows, DirectSound,
  écran par DirectDraw) : il **sert de référence** et on en reprend des morceaux, on ne le compile pas tel quel,
  comme pour USER et GDI.
* Le KERNEL de win16 a déjà une bonne part de l'INT 21h (fichiers, dossiers, recherche, date) et les lecteurs
  (`drives.ini`) : le DOS les partage. Noms courts : les fichiers de Korin ont des noms longs ; DOS ne voit que du
  8.3, il faut donc un nom court par fichier (`LONGNO~1.TXT`, stable dans un dossier), comme Wine.

### 9.2 La machine DOS

* **Un programme DOS = une instance de win16 en mode DOS** (comme une NTVDM par console) : mémoire du mode réel au
  bas de l'arène (640 Ko + zone haute + HMA), XMS au-dessus de 1 Mo dans l'arène. Les enfants (`SHELL`, `EXEC`)
  tournent dans la même machine, comme sous DOS. `win16 PROG.EXE` regarde l'en-tête : MZ seul → DOS, NE → Windows.
* **DOS** : PSP, bloc d'environnement (`COMSPEC`, `PATH`, `BLASTER`), chaîne de MCB (48h/49h/4Ah), EXEC 4B00h/4B03h
  avec retour et code (4Dh), fin et reste résident (31h), INT 20h/22h/23h/24h, IOCTL (4400h : la console est un
  périphérique), FCB minimaux, `CON`/`NUL`/`PRN`. La console DOS écrit dans l'écran texte (comme un vrai PC) : la
  sortie est dans la fenêtre, pas dans le Terminal.
* **BIOS** : zone de données `40:xx` (compteur `40:6C`, tampon du clavier, mode, curseur : QuickBASIC la lit),
  INT 10h, 11h, 12h, 15h (A20, mémoire étendue), 16h, 1Ah, polices 8×8/8×14/8×16 dans la « ROM » (INT 43h, 1130h).
  Il faut une police VGA libre (point à trancher à la construction).
* **Interruptions matérielles** : un PIC 8259 émulé (20h/21h, A0h/A1h) ; la boucle du processeur injecte IRQ 0
  (minuterie), IRQ 1 (clavier), IRQ 5/7 (Sound Blaster), IRQ 12 non (souris par INT 33h) entre deux instructions
  quand IF est mis. Les programmes qui détournent l'INT 8 ou l'INT 9 (FM-Lib joue en tâche de fond par l'INT 8)
  marchent alors comme sur un PC.
* **Temps** : le PIT 8253 (40h-43h) donne l'INT 8 à la fréquence que le programme programme, d'après l'horloge de
  Korin (pas de comptage de cycles) ; port 61h (haut-parleur, bit de rafraîchissement que des boucles d'attente
  lisent) ; CMOS 70h/71h (date).
* **Clavier** : les touches de UIKit deviennent des codes de balayage (jeu 1, positions du clavier US) au port 60h
  avec l'IRQ 1, et notre INT 9 remplit le tampon du BIOS avec le caractère que donne Korin (AZERTY compris).
* **Souris** : un pilote INT 33h maison (fonctions 0 à 0Ch, 0Fh, 14h, 1Ah, gestionnaire d'événements 0Ch appelé
  par rappel 16 bits), coordonnées virtuelles du mode en cours, curseur dessiné par-dessus l'image en texte et
  dans les modes standard (en SVGA, FutureLib dessine le sien).
* **XMS** (INT 2Fh 4300h/4310h, point d'entrée lointain : versions, allocation, copie 0Bh, verrou) et **EMS**
  (INT 67h, cadre de pages en `D000`) : FutureLib s'en sert. DPMI (jeux DOS/4GW) : plus tard, si l'on veut.

### 9.3 La VGA émulée

Le cœur : une VGA de 256 Ko de mémoire vidéo (4 plans) plus une SVGA de 4 Mo, rendue dans la fenêtre.

| Mode | Ce qu'il faut |
|---|---|
| texte 0-3, 7 | caractère + attribut à `B8000`/`B0000`, police de la ROM, clignotement, curseur (3D4h 0Ah-0Fh), 40/80 colonnes, 25/43/50 lignes |
| CGA 4, 5, 6 | 320×200×4 et 640×200×2, mémoire entrelacée (lignes paires à `B8000`, impaires à `BA000`), palettes CGA (3D9h) |
| EGA 0Dh, 0Eh, 10h, VGA 11h, 12h | **mode planaire** : registres du séquenceur (3C4h, masque des plans) et du contrôleur graphique (3CEh : modes d'écriture 0-3, verrous, set/reset, masque de bits, rotation, lecture par plan et comparaison de couleurs) |
| 13h | 320×200×256 chaîné, direct |
| Mode X | 13h « déchaîné » (séquenceur 04h), 320×240 et autres par le CRTC, pages, défilement (début d'affichage) |
| VESA VBE 1.2/2.0 | INT 10h 4F00h-4F0Ah ; fenêtre de 64 Ko à `A0000` déplacée par 4F05h (et par un appel lointain) ; **tampon linéaire** placé dans l'arène (son adresse « physique » est son décalage) ; 640×480 à 1280×1024 en 8, 15, 16, 24 et 32 bits |

* Ports : 3C0h (attributs), 3C2h/3CCh, 3C4h/3C5h, 3C6h-3C9h (**DAC**, palette 6 bits), 3CEh/3CFh, 3D4h/3D5h
  (CRTC), 3DAh (**retour de balayage** : beaucoup de programmes l'attendent ; il bat au rythme de 60/70 Hz réels).
* Affichage : à chaque image (60 Hz, ou dès qu'il y a eu des écritures), le mode en cours est converti en pixels
  32 bits dans la surface de la fenêtre, **agrandi en nombre entier** (320×200 → 640×400, ou 640×480 avec le
  rapport 4:3) avec la palette du DAC ; plein écran par F9 comme les émulateurs. La fenêtre prend la taille du mode.
* Coût : chaque accès dans `A0000-BFFFF` passe par une fonction (les modes planaires l'exigent) ; le mode 13h et le
  tampon linéaire peuvent rester des écritures directes dans l'arène.

### 9.4 L'interpréteur de commandes : FreeCOM

**FreeCOM** (COMMAND.COM de FreeDOS, github.com/FDOS/freecom, **GPL-2**, version 0.86 du 31/12/2024, encore
entretenu) : `DIR`, `COPY`, `SET`, `.BAT` complets, `COMMAND /C`. Il se construit avec **Open Watcom** (déjà dans
WSL) et NASM ; on prend la variante sans échange XMS. Il demande à notre DOS : EXEC avec retour, environnement et
`COMSPEC`, INT 2Eh, INT 2Fh AE00h/AE01h (commandes installables : répondre « non »), et `BLASTER` dans son
environnement. `SHELL "dir"`, `SHELL` seul et « Sortir sous DOS » de QuickBASIC passent alors par lui. Un
COMMAND maison serait plus petit mais devrait refaire les `.BAT` : FreeCOM est recommandé, livré avec sa licence
et le lien vers ses sources.

### 9.5 Le son

Tout se mélange dans un seul flux d'AudioKit (`ak_open (44100, 2, 16, 0)`, `ak_write_all`), dans un fil comme le
synthé MIDI. Les écritures du programme dans les ports sont datées par l'horloge de Korin et rejouées par le fil du
son avec 20 à 40 ms de retard : le tempo suit l'INT 8 du programme.

* **Haut-parleur** : canal 2 du PIT + bits 0-1 du port 61h → onde carrée (`SOUND`, `PLAY`, `BEEP` de QuickBASIC).
* **OPL (AdLib, 388h-38Bh ; Sound Blaster 220h-223h et 228h-229h)** : un émulateur de l'OPL au niveau des
  registres. Les **minuteries** de l'OPL (registres 02h-04h, état en 388h) sont indispensables : tous les
  programmes s'en servent pour détecter la carte. **FM Tracker** de Korin et le `ak_fm.c` d'AudioKit ne sont pas
  réutilisables : c'est une FM à deux opérateurs « à la OPL2 », pas une puce émulée registre par registre.
  Candidats :
  * **Nuked OPL3** (github.com/nukeykt/Nuked-OPL3, **LGPL-2.1**, C, un fichier) : le plus fidèle (OPL2 et OPL3),
    le plus coûteux en calcul ; à mesurer sur le Pi 4.
  * **ymfm** (Aaron Giles, **BSD-3**, C++) : très fidèle aussi, plus rapide, couvre OPL, OPL2, OPL3.
  * **DBOPL** de DOSBox (**GPL-2+**) : rapide, un peu moins fidèle.
* **Sound Blaster** à 220h, IRQ 5 (ou 7), DMA 1 (8 bits) / 5 (16 bits) : DSP (reset 226h, lecture 22Ah, écriture
  22Ch, état 22Eh), d'abord **SB 2.0** (DSP 2.01, 8 bits mono, transferts simples et auto-init, haute vitesse),
  puis **SB Pro** (DSP 3.02, stéréo, mélangeur 224h/225h, OPL3) et **SB16** (DSP 4.05, 16 bits, commandes Bxh/Cxh)
  si possible. Contrôleur DMA 8237 émulé (00h-0Fh, 81h-8Fh, C0h-DFh) : le DSP lit les octets dans l'arène et lève
  son IRQ en fin de bloc. `BLASTER=A220 I5 D1 H5 T6` (T4 tant qu'il n'y a pas le SB16).
* **FM-Song** (github.com/Asher256/asher256-legacy, dossier `fmsong` : `FMSONG.EXE` 1.3.2 compilé et la source
  de FM-Lib 1.5) : licence de l'auteur « distribué gratuitement, peut être utilisé, copié et distribué librement,
  avec cette licence » (vérifié le 20/11) ; on peut donc s'en servir comme essai et même le garder dans les essais
  avec sa licence. Le dépôt n'a pas de fichier de licence global : seul `fmsong` est vérifié.

### 9.6 Licences des essais

* **QuickBASIC 4.5** : logiciel propriétaire de Microsoft, copie de l'utilisateur : hors du dépôt, extrait dans
  WSL `~/korin-win16/qb45`, copié seulement dans l'image de test de QEMU, effacée après l'essai.
* **FutureLib 3.5** : « Copyright © 1999-2000, Future Software » ; sa documentation ne dit rien de la
  redistribution (vérifié le 20/11) : même règle que QuickBASIC, jamais dans un commit ni un paquet.
* **FM-Song** : redistribuable (9.5). **FreeCOM** : GPL-2. Programmes d'essai : les nôtres (Open Watcom, QBasic
  écrits par nous).

### 9.7 Les voies

1. **Intégrée à win16 (recommandée)** : la machine DOS ci-dessus dans `src/apps/win16` (GPL-2), même processeur,
   mêmes lecteurs, fenêtre et clavier de UIKit ; un programme Windows peut lancer un programme DOS et
   inversement. On écrit la VGA, le DOS, le BIOS, le Sound Blaster (références : `krnl386` de winevdm en LGPL), on
   reprend un émulateur d'OPL tel quel. Plus de travail, tout est à nous et à la manière de Korin.
2. **Porter DOSBox (Staging)** comme application à part (GPL-2+, SDL 2, que Korin a par `sdl2-compat`) : VGA,
   VESA, Sound Blaster et OPL complets d'un coup, cœur dynamique bien plus rapide (et pour l'ARM64). Mais c'est
   un autre monde que win16 (ses propres lecteurs montés, son propre DOS, fenêtre SDL et non UIKit), et la demande
   était « dans la couche win16 ».
3. **Hybride** : la voie 1, mais en copiant le code VGA et Sound Blaster de DOSBox (GPL-2+, compatible) ; ce code
   est tissé dans les structures de DOSBox, l'adapter coûte presque autant que l'écrire.

Le point faible de la voie 1 : la vitesse. Le cœur de MAME est un interpréteur ; il suffit pour l'éditeur de
QuickBASIC et les programmes QBasic, à mesurer sur le Pi 4 pour les démos SVGA.

### 9.8 Étapes (voie 1)

| Étape | Contenu | Essai | Durée |
|---|---|---|---|
| D1 | chargeur MZ/COM, PSP, environnement, MCB, INT 21h de base, INT 10h texte dans la fenêtre, police, clavier (INT 9/16h), BDA, PIT et INT 8 | programmes Open Watcom | 2-3 j |
| D2 | FreeCOM construit par Open Watcom, EXEC avec retour, `.BAT`, noms courts | `DIR`, un `.BAT` | 1-2 j |
| D3 | souris INT 33h, ports et retour de balayage, ce qui manque à QuickBASIC | QB 4.5 : édition, compilation (BC et LINK par EXEC), exécution, `SHELL` | 2-3 j |
| D4 | CGA 4/5/6, EGA/VGA planaire, 13h, Mode X, DAC | `SCREEN 1, 2, 7, 9, 12, 13` | 3-4 j |
| D5 | VESA VBE (fenêtre et tampon linéaire), XMS, EMS | FutureLib 3.5 | 2-3 j |
| D6 | haut-parleur, OPL et ses minuteries, Sound Blaster (DSP, DMA, IRQ, mélangeur) | FM-Song, `PLAY` | 3-4 j |
| D7 | construction pour le Pi, mesure de la vitesse | les mêmes | 1-2 j |

**Choix de l'utilisateur (20/11/2026)** : la voie **intégrée à win16** (9.7, voie 1), **Nuked OPL3** pour l'OPL,
**FreeCOM 0.86** pour COMMAND.COM.

## Sources (consultées le 20/11/2026)

* winevdm : https://github.com/otya128/winevdm (README, `LICENSE`, versions, commits, `CMakeLists.txt`,
  `appveyor.yml`, `vm86/mame/emu/cpu/i386/i386.c`, `krnl386/ne_module.c`, `krnl386/wowthunk.c`, `user/window.c`,
  `user/message.c`, `gdi/gdi.c`).
* Wine et le 16 bits en WoW64 : bogues 54670 et 58760 (list.winehq.org, wine-bugs) ; CodeWeavers, « Cannot create
  16-bit LDT segments » (support.codeweavers.com/en_US/brokenldt16).
* DOS (§9) : `krnl386` de winevdm (dosexe, dosvm, int10, vga, int21, int2f, int33, int67, soundblaster, dma) ;
  FreeCOM (github.com/FDOS/freecom, licence et versions) ; Nuked OPL3 (github.com/nukeykt/Nuked-OPL3, licence) ;
  asher256-legacy (github.com/Asher256/asher256-legacy, `README.md`, `fmsong/README.MD`) ; documentation de
  FutureLib 3.5 (copie de l'utilisateur).
