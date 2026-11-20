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

**Lecteurs** (réglages de chaque utilisateur) : C: = `HOME:/win16/c` (avec `C:\WINDOWS\WIN.INI`), D: = le dossier
du programme, E: = tout l'arbre de Korin ; `HOME:/win16/drives.ini` (section `[drives]`, `C=/dossier`) et l'option
`--drive C=/dossier` les changent.

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

## Sources (consultées le 20/11/2026)

* winevdm : https://github.com/otya128/winevdm (README, `LICENSE`, versions, commits, `CMakeLists.txt`,
  `appveyor.yml`, `vm86/mame/emu/cpu/i386/i386.c`, `krnl386/ne_module.c`, `krnl386/wowthunk.c`, `user/window.c`,
  `user/message.c`, `gdi/gdi.c`).
* Wine et le 16 bits en WoW64 : bogues 54670 et 58760 (list.winehq.org, wine-bugs) ; CodeWeavers, « Cannot create
  16-bit LDT segments » (support.codeweavers.com/en_US/brokenldt16).
