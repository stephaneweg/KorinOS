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

## Fichiers

| Fichier | Rôle |
|---|---|
| `cpu.cpp`, `cpu.h` | le processeur : colle de `vm86/msdos.cpp`, mémoire émulée de 32 Mo à part |
| `mem.c` | LDT émulée, sélecteurs, tas global |
| `run.c` | boucle du processeur, points d'entrée des DLL intégrées, interruptions, rappels 16 bits |
| `ne.c` | chargeur NE : segments, relocations, points d'entrée, ressources |
| `kernel.c` | KERNEL, INT 21h, chaînes CP1252 → UTF-8 |
| `user.c`, `user.h` | USER : classes, fenêtres, messages |
| `gdi.c` | GDI : contextes de dessin, plumes, brosses, formes, texte |
| `be.h`, `be_uikit.c` | l'affichage sur UIKit (`tools/win16/host_be.c` : l'hôte, pour les essais) |

## Construire et essayer

* Korin PC : `sh tools/pc/apps.sh win16` (WSL) → `build/pc/apps/sdroot/apps/win16.app`.
* Programmes d'essai : `sh tools/win16/build-tests.sh` (Open Watcom v2 dans `~/win16/watcom`).
* Sur l'hôte, sans Korin : `W16_TRACE=1 sh tools/win16/host.sh build/win16/tests/hello.exe`.
* Sous QEMU PC : `sh tools/win16/test_pc.sh hello` (après `. ~/korin-cloud/env.sh`).
* Ce qu'un `.exe` importe : `python3 -I tools/win16/nedump.py PROG.EXE`.
