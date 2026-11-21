# Passation : win16 (Windows 3.x et DOS sur Korin PC) — 20/11/2026

Pour reprendre dans une session du cloud. Lire d'abord `docs/agents/CLOUD.md` (mise en place), `AGENTS.md`,
`.agents/memory/armpi-win16.md` (décisions et pièges), puis `docs/etude/WIN16.md` (l'étude, §0 l'état, §9 le DOS) et
`src/apps/win16/README.md` (fichiers, licences, essais).

## Où on en est

`src/apps/win16` (app « Windows 3.x », `win16.app`, GPL-2 d'après winevdm) fait tourner :

- **Windows 3.x (NE)** : chargeur, KERNEL/USER/GDI sur UIKit, contrôles natifs de UIKit, COMMDLG, MMSYSTEM (MIDI vers
  le synthé d'AudioKit). Art Song 2.0 (Delphi 1, copie de l'utilisateur) tourne une fois décompressé.
- **DOS** (étapes D1 à D6 faites, le PC seulement ; D7 « Pi » retirée par l'utilisateur) : machine en mode réel
  (`dos.c`, `bios.c`, `vga.c`, `svga.c`, `xms.c`, `mouse.c`, `sound.c`), FreeCOM 0.86 sur Z:, QuickBASIC 4.5,
  CGA/EGA/VGA/Mode X, VESA 2.0, XMS/EMS (FutureLib 3.5), haut-parleur, OPL3 (Nuked OPL3, `nuked/`), Sound Blaster 16.
- **Installations** (`lz.c`) : SZDD, LZEXPAND, VerFindFile/VerInstallFile, WinExec et LoadModule (un autre win16
  par `posix_spawn`, avec `--drive D=` du parent).
- **Paquet PC `win16` 1.0.0 publié** (7733638d) ; ce qui est venu depuis (installations, VB3) **n'est pas publié**.

## Mise à jour du 20/11 (session du cloud) : l'installation va au bout sur l'hôte

Avec les archives de l'utilisateur (`artsng21.zip`, `VBRUN300.zip`, jamais dans le dépôt), SETUP.EXE puis SETUP1
(VB3) installent Art Song dans `C:\ARTSNG20` (« ART SONG Installation is Complete! »), et l'ARTSNG20.EXE installé
démarre. Détail des causes et des correctifs : `docs/etude/WIN16.md`, « L'installation d'Art Song va au bout ». Ce
qui reste :

- essayer la même chose **sous QEMU PC** (`tools/win16/test_pc.sh`) puis dans Korin sur le ZBook ;
- pas de serveur DDE : le groupe du Gestionnaire de programmes n'est pas créé (des raccourcis Korin à la place ?) ;
- les questions ouvertes plus bas (paquet « win16-support », publication de win16 1.0.1 : `versions.ini` est monté,
  rien n'est publié).

Le script de l'hôte qui valide la boîte « Install To » (le clic sur Continue ; sans lui, `W16_IDLE` ferme la boîte
et VB la recharge vide, d'où « Error While Attempting to Create Directories ») :

```sh
cd $HOME/win16/c/WINDOWS && W16_IDLE=15 W16_SCRIPT="wait:60 win:3 click:140,122 win:0" \
  …/build/win16/host/win16 --drive D=/tmp/ainst setup1.exe D:
```

## Le chantier d'avant : installer Art Song 2.0 (`SETUP.EXE`)

L'utilisateur a choisi « faire marcher VBRUN300 » (plutôt qu'une commande `expand`).

1. `SETUP.EXE` (amorce du kit Visual Basic, module GVBSETUP) : marche ; il décompresse SETUP1.EXE, VER.DLL,
   SETUPKIT.DLL dans `C:\WINDOWS` et lance `C:\WINDOWS\SETUP1.exe D:` par WinExec.
2. `SETUP1.EXE` est un programme **Visual Basic 3** : il charge **VBRUN300.DLL** (copie de l'utilisateur, voir plus
   bas) ; le moteur de VB démarre, enregistre ses classes, ouvre sa fenêtre.
3. **Où ça bloque** : une boîte « DDE requires ddeml.dll » (titre ARTSNG20), puis fin. Le setup veut créer un groupe
   dans le Gestionnaire de programmes (`ProgMan|Progman`, LinkTopic d'une étiquette de SETUP1). La chaîne
   `ddeml.dll` est dans VBRUN300.DLL (près de `LinkMode`, `LinkTopic`) : VB charge sans doute DDEML par son nom de
   fichier (`LoadLibrary ("ddeml.dll")`, en minuscules ?) ou par `GetModuleHandle`. Le module intégré **DDEML** vient
   d'être ajouté (`dde.c`, `spec/ddeml.dll16.spec`, entrée dans `tools/win16/spec2c.py`, `MOD_DDEML`) mais le message
   reste : **à suivre** :
   - tracer (`W16_TRACE=1`) ce que VB appelle pour trouver DDEML : `LoadLibrary`, `GetModuleHandle`,
     `GetProcAddress` (aucun « LoadLibrary » n'apparaissait dans la trace : regarder si KERNEL les trace, et si la
     recherche des modules intégrés par nom de fichier `ddeml.dll` en minuscules marche dans `kernel.c` / `ne.c`) ;
   - `USER.GetWindowTask` n'est pas écrite (rend 0) et VB s'en sert ; `GDI.SetBrushOrg` non plus ;
   - ensuite, il n'y aura pas de serveur DDE (PROGMAN) : `dde.c` répond « pas de conversation » ; le setup de VB
     devrait l'accepter (sinon, envisager un petit PROGMAN DDE qui accepte les commandes `[CreateGroup]`,
     `[AddItem]` et les ignore, ou crée des raccourcis Korin).
4. Puis vérifier que SETUP1 copie tout (VerInstallFile) dans le dossier choisi et que `ARTSNG20.EXE` installé démarre.

### Correctifs faits en route (dans le dernier commit)

- **Tas local** (`kernel.c`) : le segment grandit jusqu'à 64 Ko quand le tas en occupe la fin (DGROUP d'une DLL) ;
  **poignées locales à la Windows** pour LMEM_MOVEABLE (la poignée, `h & 3 == 2`, est l'adresse d'une case
  `[donnée, drapeaux, verrous]` : VB3 lit la case lui-même) ; LocalHandle, LocalFlags, LocalCompact, LocalCountFree,
  LocalHeapSize, LocalShrink.
- **Recherche des DLL** (`ne.c`) : dossier du programme, dossier courant, `C:\WINDOWS`, `C:\WINDOWS\SYSTEM`.

## Se mettre en place pour ce chantier (cloud)

Ce qui sert aux essais et **n'est pas dans le dépôt** (propriétaire, ou téléchargé hors du dépôt) :

| Quoi | Sur la machine de l'utilisateur | Dans le cloud |
|---|---|---|
| Open Watcom v2 (programmes d'essai) | WSL `~/win16/watcom` | à installer (`ow-snapshot.tar.xz`, version « Current-build » de github.com/open-watcom/open-watcom-v2) |
| winevdm (référence) | WSL `~/win16/winevdm` | `git clone https://github.com/otya128/winevdm` |
| FreeCOM 0.86 | WSL `~/korin-win16/freecom` | `sh tools/win16/build-freecom.sh` (le clone et nasm) |
| Art Song 2.0 (`artsng21.zip`) | `build/win16/artsong` | **à demander à l'utilisateur** (shareware, jamais au dépôt) |
| VBRUN300.DLL (`VBRUN300.EXE`, PKSFX) | WSL `~/korin-win16/vbrun`, `C:\Users\swe\Downloads\VBRUN300.EXE` | **à demander à l'utilisateur** |
| QuickBASIC 4.5, FutureLib 3.5 | WSL `~/korin-win16/qb45`, `fl35`, copies `qbtest`, `fltest` | à demander (règle : jamais au dépôt ni en paquet, seulement dans une image d'essai effacée) |
| FM-Song (redistribuable) | WSL `~/korin-win16/asher256-legacy`, `fmtest` | `git clone https://github.com/Asher256/asher256-legacy` |

Essai de l'installation sur l'hôte (Linux, `tools/win16/host.sh` construit `build/win16/host/win16`) : copier les
seuls fichiers d'origine de l'archive dans `/tmp/ainst`, un `HOME` neuf, puis

```sh
sh tools/win16/host.sh
export HOME=/tmp/w16home W16_DOSDIR=$PWD/build/win16/dos       # (C: = $HOME/win16/c ; Z: = FreeCOM)
cd /tmp/ainst && W16_TRACE=1 W16_IDLE=15 W16_SCRIPT="wait:40" ../path/to/build/win16/host/win16 SETUP.EXE 2>log1
cp VBRUN300.DLL $HOME/win16/c/WINDOWS/SYSTEM/
cd $HOME/win16/c/WINDOWS && W16_TRACE=1 W16_IDLE=15 W16_SCRIPT="wait:40" .../win16 --drive D=/tmp/ainst setup1.exe D: 2>log2
```

`tools/win16/test_pc.sh CHEMIN/PROG.EXE` l'essaie sous QEMU PC (le dossier du programme est copié dans l'image,
effacée à la fin). Rappels : le C se construit dans WSL ou Linux, pas dans Git Bash ; `sh tools/pc/apps.sh win16`
pour l'app du PC ; publier un nouveau paquet : `python tools/pkg/pub.py NOM --new --tree DOSSIER` (option ajoutée ce
jour), une mise à jour : `pub.py win16` (seulement si l'utilisateur le demande).

## Questions ouvertes de l'utilisateur

- **Un paquet séparé « win16-support »** pour les composants propriétaires (VBRUN300.DLL…) : demandé le 20/11, pas
  fait. Avant de le publier sur le dépôt public (GitHub Pages, donc aussi l'historique git), lui redire que
  VBRUN300.DLL est de Microsoft (redistribuable avec une application VB, pas seul en libre service) et faire
  confirmer ; alternative à proposer : comme **winetricks** pour Wine (qui ne livre aucun composant de Microsoft : un
  script télécharge à la demande de l'utilisateur depuis une source qu'il choisit et installe dans son préfixe), un
  paquet qui ne contient qu'un installateur.
- Après Art Song : tests de l'utilisateur sur le ZBook (son, FM-Song, vitesse du dessin EGA/VGA), puis Tomb Raider
  (DPMI, CD-ROM, vitesse : voir WIN16.md « Après D6 »).

## État du dépôt au départ de la session

- Tout le travail de win16 est poussé sur `origin/main`. Le dossier de travail local
  (`C:\Projects\KorinOS`) avait une autre session en cours (`tools/craft/patches/craft-libs.patch` non commité) qui
  empêchait `git pull` : les deux derniers commits ont été poussés depuis un worktree temporaire (même contenu,
  autre empreinte que dans le dossier local).
