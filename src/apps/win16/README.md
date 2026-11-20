# win16 : les programmes de Windows 3.x sur Korin OS

Étude, choix et étapes : `docs/etude/WIN16.md`.

## Licences

* **win16 est sous GPL-2** (`COPYING`) : il reprend winevdm (github.com/otya128/winevdm, GPL-2), choix de
  l'utilisateur du 20/11/2026. Le reste de Korin OS garde sa licence MIT.
* `mame/` : le cœur i486 de MAME tel que winevdm et MS-DOS Player le livrent (`vm86/mame`), en-têtes
  **BSD-3-Clause** gardés ; SoftFloat (`mame/lib/softfloat`) garde sa licence. Seule retouche :
  `mame/emu/cpu/i386/x87ops.c`, le bloc des fonctions du x87 que winevdm exporte à ses DLL Win32 est mis sous
  `#ifdef WINEVDM_X87_EXPORTS` (pointeurs 32 bits de l'hôte).
* `spec/` : les tables des DLL 16 bits (format de Wine, **LGPL-2.1+**), copiées de winevdm ; `api_table.c` en est
  tiré par `tools/win16/spec2c.py` (ne pas le modifier à la main).

* `dos/COMMAND.COM` de l'app (lecteur Z: de la machine DOS) : **FreeCOM 0.86** (FreeDOS, GPL-2,
  github.com/FDOS/freecom), construit par `tools/win16/build-freecom.sh`, livré avec sa licence (`COPYING.TXT`) et
  le lien de ses sources (`FREECOM.TXT`).
* Les polices de la « ROM » vidéo de la machine DOS (code page 437, 8×8, 8×14, 8×16) viennent de
  `src/lib/basic/basfont.h` (polices VGA de console-setup de Debian : MIT et domaine public).

## Fichiers

| Fichier | Rôle |
|---|---|
| `cpu.cpp`, `cpu.h` | le processeur : colle de `vm86/msdos.cpp`, mémoire émulée de 32 Mo à part |
| `mem.c` | LDT émulée, sélecteurs, tas global |
| `run.c` | boucle du processeur, points d'entrée des DLL intégrées, interruptions, rappels 16 bits |
| `ne.c` | chargeur NE : segments, relocations, points d'entrée, ressources |
| `kernel.c` | KERNEL : mémoire, sélecteurs, tas local, ressources, modules, fichiers, INI, lecteurs ; INT 21h |
| `user.c`, `user.h` | USER : classes, fenêtres, messages, minuteries, focus, rectangles, atomes, presse-papiers |
| `menu.c` | menus (barre globale de Korin) et accélérateurs |
| `dialog.c` | dialogues des ressources, DefDlgProc, IsDialogMessage |
| `ctrl.c` | contrôles BUTTON, STATIC, EDIT, LISTBOX, COMBOBOX, SCROLLBAR : widgets de UIKit (mode natif), sinon dessinés au thème |
| `gdi.c`, `gdi.h` | GDI : DC, objets, polices, bitmaps, BitBlt, DIB, formes, régions, palettes, icônes |
| `commdlg.c` | Ouvrir, Enregistrer, Couleur (dialogues de Korin) |
| `mmsystem.c` | sortie MIDI (sans son), minuteries multimédia |
| `misc.c` | WIN87EM, KEYBOARD, TOOLHELP |
| `be.h`, `be_uikit.c` | l'affichage sur UIKit (`tools/win16/host_be.c` : l'hôte, pour les essais) |
| `dos.h`, `dos.c` | la machine DOS (un `.exe` sans en-tête NE, un `.COM`) : MCB, PSP, chargeur MZ, EXEC, console, INT 21h, la boucle |
| `bios.c` | son BIOS : zone 40:00, INT 10h-1Ah, PIC, PIT, clavier (port 60h, INT 9), CMOS, ports, fenêtre |
| `mouse.c` | son pilote de souris (INT 33h, IRQ 12 vers le gestionnaire du programme) |
| `svga.c` | sa SVGA : VESA BIOS Extension 2.0 (banques, tampon linéaire, 8 à 32 bits) |
| `xms.c` | sa mémoire XMS 3.0 et EMS 4.0 |
| `vga.c` | sa VGA : modes texte, CGA, EGA, VGA en plans, 13h, Mode X, registres, DAC, INT 10h, rendu dans la fenêtre |

## Construire et essayer

* Korin PC : `sh tools/win16/build-freecom.sh` (une fois), puis `sh tools/pc/apps.sh win16` (WSL) →
  `build/pc/apps/sdroot/apps/win16.app` (avec `dos/COMMAND.COM`).
* DOS : `win16 PROG.EXE` (un `.exe` sans en-tête NE ou un `.COM`), `win16 FICHIER.BAT`, `win16 --dos` (l'invite) ;
  sur l'hôte, `W16_DOSDIR=build/win16/dos` donne le lecteur Z:.
* Programmes d'essai : `sh tools/win16/build-tests.sh` (Open Watcom v2 dans `~/win16/watcom`) ; ceux du DOS
  (`tools/win16/tests/dos`) : `dostest.exe` (console, écran B800, BIOS, mémoire, fichiers, EXEC de `dosecho.exe`).
* Sur l'hôte, sans Korin : `W16_TRACE=1 sh tools/win16/host.sh build/win16/tests/hello.exe`.
* Sous QEMU PC : `sh tools/win16/test_pc.sh hello` (après `. ~/korin-cloud/env.sh`).
* Ce qu'un `.exe` importe : `python3 -I tools/win16/nedump.py PROG.EXE` ; ce qui n'est pas encore écrit :
  `python3 -I tools/win16/checkimpl.py PROG.EXE`.
* Fichiers compressés des disquettes (`FICHIER.EX_`) : `python3 -I tools/win16/expand.py *_`.
* Sur l'hôte : `W16_SCRIPT="menu:101 click:20,48 type:abc key:40,13 wait:3 shot"` joue des événements,
  `W16_SHOT=dossier` fait les images, `W16_WATCH=sel:off` signale qui change une adresse.
* Sous QEMU, un programme à soi : `sh tools/win16/test_pc.sh chemin/PROG.EXE` (son dossier est copié ;
  `WAIT=40`, `W16T=0` sans trace, `CLICKS="click:X,Y sleep:S shot:nom"`).
