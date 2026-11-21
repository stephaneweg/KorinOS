# Les premiers programmes Win32 sur Korin (étude du 20/11/2026, rien d'engagé)

Demande de l'utilisateur (20/11/2026), après win16 (Windows 3.x et DOS, [WIN16.md](WIN16.md)) : « à terme, exécuter
aussi les premiers programmes Win32 ». Cette étude dit ce que cela recouvre, compare les voies possibles, propose
une architecture qui prolonge win16 et des étapes chiffrées, et liste les questions à trancher.

## 0. Ce que sont « les premiers programmes Win32 »

| Époque | Système | Programmes typiques |
|---|---|---|
| 1992-1995 | **Win32s** sur Windows 3.1 (un sous-ensemble de Win32 renvoyé vers USER et GDI de 16 bits), Windows NT 3.1-3.51 | FreeCell 32 bits de Win32s, premiers outils 32 bits, Office 95 en partie |
| 1995-1997 | **Windows 95**, NT 4.0 | les accessoires de Windows 95 (Bloc-notes, WordPad, Paint, Calculatrice, Démineur…), Delphi 2-3, VB 4-5 en 32 bits, MFC 4 (Visual C++ 4-5), shareware et jeux de bureau, Netscape 3, mIRC 5, Winamp 1 |
| après 1997 | Windows 98, NT 4 SP, 2000 | COM/OLE partout, IE 4 et le shell intégré, DirectX 5+ : hors de cette étude |

Tous sont des exécutables **PE** (Portable Executable) 32 bits x86, liés à KERNEL32, USER32, GDI32 et, selon le
cas, ADVAPI32, COMCTL32, COMDLG32, SHELL32, WINMM, WSOCK32, plus le moteur de leur langage (MFC4x.DLL, VB40032.DLL,
le runtime de Delphi lié dans l'exécutable). Les plus anciens sont en **ANSI** (fonctions …A) ; les programmes
pensés pour NT appellent les fonctions …W (Unicode).

## 1. Ce que win16 apporte déjà

* **Le processeur** : le cœur i486 de MAME (`cpu.cpp`) fait déjà le mode protégé 32 bits, le x87 et la
  pagination ; win16 le tient aujourd'hui en mode protégé 16 bits avec sa LDT (`mem.c`).
* **USER et GDI** (`user.c` 2 900 lignes, `gdi.c` 2 700, `ctrl.c` 2 300, `dialog.c`, `menu.c`) : fenêtres de
  Korin, file des messages, minuteries, menus dans la barre globale, dialogues des ressources, **contrôles natifs
  de UIKit**, DC, polices, bitmaps, régions, palettes, presse-papiers. Win32 USER/GDI en est un sur-ensemble :
  Windows 95 lui-même faisait passer une grande partie de USER32 et GDI32 vers USER.EXE et GDI.EXE de 16 bits.
* **Le reste** : COMMDLG sur les dialogues de Korin, MMSYSTEM (MIDI sur AudioKit), WINSOCK (`winsock.c`), le
  registre (vide pour l'instant), les lecteurs (`C:` = `HOME:/win16/c`, `drives.ini`), DDEML, les fichiers
  compressés et les installateurs (LZEXPAND, VER), la machine DOS.
* **Les essais** : Open Watcom v2 produit aussi des programmes Win32 (`wcl386 -l=nt` et `-l=nt_win`, bibliothèques
  `lib386/nt` : kernel32, user32, gdi32, advapi32…) ; les programmes d'essai s'écrivent comme ceux de
  `tools/win16/tests`.

## 2. Les voies possibles

| Voie | Principe | Pour | Contre |
|---|---|---|---|
| **A. Prolonger win16 (« à la Win32s »)** | un chargeur PE et KERNEL32 dans win16 ; USER32 et GDI32 comme enveloppes 32 bits autour du USER et du GDI existants ; le code x86 exécuté par le cœur de MAME en mode 32 bits à plat | même programme sur le PC et sur le Pi ; réutilise 15 000 lignes ; fenêtres et contrôles natifs de Korin ; programmes Win16 et Win32 mélangés possibles | il faut écrire KERNEL32 (fils, synchronisation, mémoire virtuelle, exceptions) ; vitesse d'un interpréteur |
| **B. Porter Wine** | Wine (LGPL) avec un pilote d'affichage pour Korin à la place de `winex11.drv` | Win32 très complet | la partie Unix de Wine (`ntdll` côté Unix, mmap à des adresses fixes, signaux, `wineserver`, fils) suppose un vrai Unix ; sur le Pi (ARM), il faut en plus un émulateur x86 (projet Hangover : Wine + box64/FEX) ; des mois, et un rendu de « Windows dans Korin » loin des contrôles natifs |
| **C. Code x86 natif sur le PC** | exécuter le code 32 bits directement (mode compatibilité de l'x86-64), API comme en A | rapide | seulement le PC (rien pour le Pi) ; il faut des segments 32 bits et un passage des appels système dans le noyau de Korin ; l'API reste à écrire comme en A |

**Recommandation : A**, avec une porte ouverte vers C plus tard : l'API (KERNEL32, USER32, GDI32…) écrite pour A
servirait telle quelle à C, seul l'exécuteur changerait (interpréteur partout, natif sur le PC si la vitesse
l'exige).

## 3. L'architecture de A

* **Un seul programme** : `win16.app` apprend le PE (renommé plus tard, `winrt` ou `windows.app`, à trancher). Il
  choisit déjà entre Windows (NE) et DOS (MZ seul) ; le PE est un troisième chemin.
* **La mémoire** : un espace d'adresses 32 bits à plat. Le PE se charge à son `ImageBase` (0x400000 le plus
  souvent), les DLL à leur base ou relogées ; la mémoire émulée passe de 32 Mo à 256 Mo ou 512 Mo (réglable),
  sans pagination au début (adresse linéaire = adresse de la mémoire émulée) ; `VirtualAlloc` réserve et engage
  par pages de 4 Ko dans cet espace ; les tas (`HeapAlloc`, `LocalAlloc`, `GlobalAlloc` en 32 bits) au-dessus.
* **Les segments** : CS, DS, ES, SS à plat (base 0, limite 4 Go) dans la GDT ou la LDT déjà tenue par `mem.c` ;
  **FS** pointe sur le TIB du fil (bloc d'information du fil : `FS:[0]` la chaîne des gestionnaires
  d'exceptions SEH, `FS:[18h]` le TIB lui-même, la pile, les TLS) : le code compilé par MSVC, Delphi et Watcom
  s'en sert dès le démarrage.
* **Les appels de l'API** : comme dans win16, les points d'entrée des DLL intégrées sont des adresses piégées
  (un sélecteur ou une page réservée) ; la convention est `stdcall` (arguments 32 bits, nettoyés par l'appelé) ;
  les tables de fonctions viennent des `.spec` de Wine (LGPL-2.1) par un `spec2c.py` étendu, chaque fonction pas
  encore écrite étant signalée au premier appel (comme aujourd'hui).
* **USER32 et GDI32** : des enveloppes qui convertissent les structures 32 bits (MSG, WNDCLASS, RECT de 32 bits,
  LOGFONT, BITMAPINFO…) et appellent le cœur commun de `user.c` et `gdi.c`, qui passe d'entiers 16 bits à des
  entiers 32 bits là où il le faut (coordonnées, handles). Les procédures de fenêtre 32 bits sont rappelées en
  `stdcall` ; une fenêtre d'un programme 16 bits et d'un programme 32 bits ne se mélangent pas dans un même
  process au début.
* **Les chaînes** : les fonctions …A en ANSI (page 1252, comme win16) ; les …W convertissent vers l'ANSI ou vers
  l'UTF-8 de UIKit ; un programme Unicode de NT 4 passe donc, avec les caractères hors 1252 en plus.
* **Les fils** : `CreateThread` donne un second contexte du processeur (registres, pile, TIB) ; un ordonnanceur
  à tour de rôle dans win16 (tranches d'instructions, et changement à chaque attente : `WaitForSingleObject`,
  `GetMessage`, `Sleep`) ; événements, mutex, sémaphores, sections critiques en espace émulé ; les programmes de
  1995-1997 en ont peu, mais MFC, Delphi et VB en créent.
* **Les exceptions** : une faute du processeur (division par zéro, accès hors segment, `INT 3`) déroule la chaîne
  SEH de `FS:[0]`, comme `KiUserExceptionDispatcher` ; le C++ de MSVC et le `try` de Delphi en dépendent.
* **Les fichiers** : les chemins Windows passent par les lecteurs de win16 (`dos_to_host`) ; `CreateFile`,
  `ReadFile`, `FindFirstFile`, `GetFileAttributes`, fichiers en mémoire (`CreateFileMapping`) par copie.
* **Le registre** : ADVAPI32 sur un fichier `HOME:/win16/registry.ini` (ou le format `.reg`), partagé avec le
  SHELL.DLL de 16 bits (le registre vide d'aujourd'hui).
* **Les contrôles communs** (COMCTL32 : barre d'outils, barre d'état, liste, arbre, onglets, barre de
  progression, curseur) : vers les widgets de UIKit, comme les contrôles standard en « mode natif ».

## 4. Les points durs

* **La vitesse** : un interpréteur ; les programmes de bureau de 1995-1997 visaient un 486 ou un Pentium de 66 à
  133 MHz ; à vérifier sur le Pi 4 (l'ordre de grandeur de MAME sur un cœur moderne : quelques dizaines de MIPS
  émulés). Les jeux DirectX ou en rendu logiciel sont hors de portée sans recompilateur.
* **COM et OLE** (OLE32, OLEAUT32) : le glisser-déposer, les contrôles ActiveX de VB, le shell de Windows 95 ;
  beaucoup de programmes chargent OLE32 sans s'en servir vraiment : des tables qui répondent « pas disponible »
  suffisent d'abord.
* **Les installateurs** (InstallShield 3-5) : 16 et 32 bits mêlés, des DLL de Windows copiées dans
  `C:\\WINDOWS\\SYSTEM` (MFC, VB, msvcrt) qui sont des DLL ordinaires que le chargeur PE prend telles quelles.
* **Les DLL de Microsoft** que les programmes apportent (MSVCRT.DLL, MFC42.DLL, VB40032.DLL) : chargées comme
  n'importe quelle DLL, jamais fournies par nous (comme VBRUN300.DLL).
* **Le mélange 16 / 32 bits** : un programme 32 bits qui lance un programme 16 bits (`WinExec`, `CreateProcess`)
  lance un autre win16, comme aujourd'hui ; les « thunks » plats 16/32 de Windows 95 ne sont pas prévus.

## 5. Étapes

| Étape | Contenu | Essai | Durée |
|---|---|---|---|
| **W0** | mémoire à plat de 256 Mo, segments 32 bits, TIB et FS, appels `stdcall` piégés, `spec2c.py` pour les `.spec` de Wine de KERNEL32, USER32, GDI32 | — | 2-3 j |
| **W1** | chargeur PE (sections, relocations, imports, exports, ressources), KERNEL32 de base (processus, modules, mémoire, tas, fichiers, environnement, console sur la fenêtre DOS de win16) | un programme console d'Open Watcom (`wcl386 -l=nt`) : « bonjour », fichiers, `argv` | 1 sem. |
| **W2** | USER32 et GDI32 par-dessus `user.c` et `gdi.c` (fenêtre, messages, peinture, minuteries, menus, ressources, dialogues) | un programme fenêtre d'Open Watcom (`-l=nt_win`) comme `hello.c` et `dialogs.c` de win16 | 1-2 sem. |
| **W3** | COMDLG32, COMCTL32 de base, SHELL32 (quelques fonctions), ADVAPI32 (registre), WINMM, WSOCK32 | les accessoires de Windows 95 ou NT 4 (copie de l'utilisateur), des freewares de l'époque | 2 sem. |
| **W4** | fils, synchronisation, exceptions SEH, TLS, `CreateFileMapping` | un programme MFC 4, un programme Delphi 2, un programme VB 5 | 2 sem. |
| **W5** (plus tard) | OLE32 minimal, InstallShield, vitesse (voie C sur le PC ?) | à choisir | — |

## 6. Licences

* win16 est sous **GPL-2** (il part de winevdm) ; les `.spec` et, au besoin, des morceaux de Wine sont en
  **LGPL-2.1**, compatibles avec la GPL-2 d'un programme qui les inclut.
* Jamais de DLL ni de programme de Microsoft dans le dépôt ni dans un paquet : l'utilisateur apporte les siens
  (comme VBRUN300.DLL et Windows for Workgroups 3.11), dans une image d'essai effacée après l'essai.

## 7. Questions à trancher

1. **Un programme ou deux** : le PE dans `win16.app` (même USER et GDI, même process) ou un `win32.app` à part
   qui partage le code par une bibliothèque ?
2. **Le nom** : garder « Windows 3.x » (`win16`) ou renommer l'app (« Windows », `windows.app`) quand le Win32
   arrive ?
3. **La mémoire émulée** : 256 Mo par défaut, réglable ? (les programmes de 1995 tenaient dans 8 à 16 Mo)
4. **Le premier vrai programme visé** pour juger W3 : lequel de la collection de l'utilisateur ?
5. **La voie C** (le code natif sur le PC) : à garder comme option de vitesse, ou à écarter ?

## Sources

* [WIN16.md](WIN16.md) (winevdm, MAME, structure de win16) ; le code de `src/apps/win16`.
* Wine : `dlls/kernel32`, `dlls/user32`, `dlls/gdi32`, `dlls/ntdll` (structures PE, TIB, SEH) et leurs `.spec`
  (LGPL-2.1).
* Open Watcom v2 : bibliothèques `lib386/nt`, en-têtes `h/nt`.
