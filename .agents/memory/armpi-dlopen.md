---
name: armpi-dlopen
description: "vrai dlopen dans libposix (dlopen.c), hôtes -rdynamic, modules -shared, réserve TLS ; dans main (11/11)"
metadata:
  node_type: memory
  type: project
  originSessionId: b2dce808-c35a-4cf0-ba16-66f27c7dc846
  modified: 2026-11-11T18:01:03.974Z
---

dlopen implémenté le 11/11/2026 (dans main depuis 06ef7995 ; liaison à des .so au démarrage et pages de code partagées en 5fb03d1a dans main), d'après docs/PYTHON.md §2.5 bis. Groupe `dl` de posixtest : 24/24 sous QEMU et sur le Pi (11/11).

Choix (non évidents) :
- symboles de l'hôte par `gcc -rdynamic` (.dynsym que ld fait dans l'ET_EXEC, survit à strip), pas la .symtab ni une 2e édition de liens ;
- `--whole-archive` de newlib impossible : tools/posix/dlexport.sh calcule les membres sûrs → dlhost.ld (EXTERN), dlhost-cxx.ld pour libstdc++ ;
- le dérouleur de libgcc (unwind-dw2-fde) n'utilise pas dl_iterate_phdr : dlopen appelle __register_frame_info ; dlmod.o (spec -shared) donne le mot nul final de .eh_frame et __dso_handle ;
- réserve TLS 32 Ko par thread seulement pour les hôtes ; piège trouvé : le bloc TLS du programme doit rester à TP+align_up(16, alignement du segment), seul le TP s'aligne sur 64.

**Why:** sert Python (modules natifs, pip), Node, Lua — voir [[armpi-but-libc]], [[armpi-posix-par-necessite]].
**How to apply:** un port qui charge des .so se relie en hôte (`-rdynamic`, + dlhost-cxx.ld s'il charge du C++) ; modules : `-specs=posix.specs -fPIC -shared` (C++ : `-nostdlib++`). Détail : docs/POSIX.md « dlopen ».
