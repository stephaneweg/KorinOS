# Licences

Le code de Korin OS est sous licence **MIT** ([LICENSE](../LICENSE)), sauf ce qu'une autre licence impose, listé ici.

## Dans ce dépôt

| Quoi | Où | Licence |
|---|---|---|
| Lecteur PDF | `src/apps/pdf`, `tools/pdf` | AGPL-3.0 à cause de MuPDF (`src/apps/pdf/COPYING`) |
| Patchs de WebKit | `tools/extra/patches`, `tools/webkit/patches` | LGPL-2.1 / BSD, comme WebKit |
| Patchs et pilote de FreeBASIC | `tools/gcc/fbc` | GPL-2.0+ pour le compilateur, LGPL-2.0+ avec exception pour `rtlib` et `gfxlib2` |
| Scripts de GCC et binutils | `tools/gcc` | MIT pour les scripts ; GCC et binutils restent GPL-3.0+ |
| Patchs de Mesa, .NET, Avalonia, Skia | `tools/mesa`, `tools/dotnet`, `tools/avalonia` | MIT ou BSD, comme le projet patché |
| Patch d'OpenSSL | `tools/openssl` | Apache-2.0 |
| Patch de SuperTux | `tools/sdl/supertux.patch` | GPL-3.0+ |
| stb_image | `src/apps/media` | domaine public ou MIT |
| Accès mémoire repris d'ares | `tools/emu/n64ref/n64/n64_bus.cpp` | ISC, notice gardée dans le fichier |
| Firmware du Pi | `firmware/`, `sdcard/` | Broadcom, `firmware/LICENCE.broadcom` : redistribution binaire |
| Firmware Wi-Fi CYW43455 | `firmware/wifi/`, `sdcard/firmware` | Cypress, `LICENCE.cypress` ; nvram GPL-2.0 ; origine dans `SOURCE.txt` |
| FASMARM, fasm | `tools/fasmarm`, `ref/fasm*`, `src/bin/fasmarm/fasmarm.asm` | licence de fasm (`LICENSE.TXT`), le code traduit compris |
| Polices DejaVu, Inter, Spleen | `ref/fonts`, `ref/spleen` | leurs fichiers `LICENSE` |
| Sintel (extraits) | `skel/sd0/home/video` | CC BY 3.0, Blender Foundation |

Les émulateurs sont réécrits sous MIT : leurs commentaires citent le comportement décrit par d'autres projets, sans
reprendre leur code. Le code de Jet (`src/extra/onyxports/jet`) est sous MIT ; le programme lié à WebKit est soumis à
la LGPL, voir plus bas.

## Récupéré à la construction, jamais dans le dépôt

WebKit (LGPL-2.1, BSD), GCC et binutils (GPL-3), newlib, Mesa (MIT), .NET et Avalonia (MIT), Skia (BSD), ICU, curl,
OpenSSL (Apache-2.0), SDL (zlib), SuperTux et SuperTuxKart (GPL-3, données CC et autres), FreeBASIC (GPL-2, rtlib
LGPL-2), MuPDF (AGPL-3.0), SoundFont GeneralUser GS (sa licence l'accompagne).

## À respecter pour distribuer des binaires

Vaut pour les images, les paquets et `sdcard/`.

* **GPL et AGPL** : qui donne GCC, SuperTux, SuperTuxKart ou le lecteur PDF doit offrir leurs sources, patchs
  compris. Ce dépôt et `tools/extra/fetch.sh` les fournissent, aux versions fixées.
* **LGPL (WebKit)** : Jet est lié en statique, donc les objets doivent permettre de le relier à une autre version de
  WebKit. La construction de `docs/CONSTRUIRE.md` le permet.
* **Firmware** : redistribution binaire sans modification, licence jointe.
* **Contenu des images** : `korin_full` contient tout ce qui précède ; `korin_lite` et `sdcard/` contiennent le firmware,
  les polices et le code MIT.

Les ROM, les BIOS et les sauvegardes de jeux ne sont jamais dans le dépôt : les émulateurs n'embarquent aucune image de
BIOS (`gba_bios.inc`, `nds_bios.cpp` sont des réécritures).
