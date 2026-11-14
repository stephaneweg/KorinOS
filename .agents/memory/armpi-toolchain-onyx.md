---
name: armpi-toolchain-onyx
description: "Pour porter du C/C++ d'Onyx (Jet Browser/WebKit…) vers ARMPI : reprendre la chaîne aarch64-onyx-elf d'Onyx (WSL), ne porter que newlib + glue"
metadata:
  node_type: memory
  type: reference
  originSessionId: 5720a561-b887-4de4-aad5-6b04aff7366c
  modified: 2026-10-29T02:07:57.619Z
---

Porter Jet Browser (WebKit, Onyx `docs/08-WEBKIT-PORT.md`) ou un autre programme C/C++ d'Onyx : l'utilisateur veut qu'on **reprenne la chaîne d'outils d'Onyx** plutôt que d'en refaire une (29/10/2026).

- Chaîne `aarch64-onyx-elf`, dans WSL : `sh tools/toolchain/fetch.sh` (→ `/opt/toolchains`), dans C:\Projects\Onyx.
- Sysroot POSIX : `make -C user/Runtime/libc/posix install PREFIX=aarch64-onyx-elf-` (libonyxposix sur newlib 4.4).
- Bibliothèques tierces : `tools/ports/build-all.sh` (ICU, Skia…).
- Selon l'utilisateur, il n'y a vraiment que **newlib et sa glue** à porter (appels système → AppKit/vfs/NetKit d'ARMPI, tas, threads), plus l'affichage vers UIKit/Elegant.
- POSIX minimal : reprendre libonyxposix (`user/Runtime/libc/posix`, ~8 900 lignes C, 25 fichiers), en remplaçant ses `kapi_*` par les appels/services d'ARMPI ; crt0posix.S + onyx-posix.ld au format ARMPI.
- Licence : Jet est LGPL-2.1 ; ARMPI n'a pas encore de fichier de licence.

Voir [[armpi-projet]], [[armpi-feuille-de-route]].
