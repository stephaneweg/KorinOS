---
name: armpi-webkit
description: "Portage de WebKit (Jet d'Onyx) sur ARMPI : état au 30/10/2026, scripts, IPC du noyau (0x70-0x7D), Jet commencé"
metadata:
  node_type: memory
  type: project
  originSessionId: 5720a561-b887-4de4-aad5-6b04aff7366c
  modified: 2026-10-30T00:47:01.910Z
---

- Scripts WSL : tools/webkit (env.sh, build-deps, build-jsc, build-webcore, build-wctest, build-wk2test, build-jet, build-media, build-ui, stage.sh) ; sorties ~/armpi-webkit-build ; build.ps1 recopie build/webkit/sdroot (dans un worktree : copier build/webkit du depot principal).
- Fait et vérifié QEMU : deps, jsc C_LOOP, WebCore (wctest), son -> AudioKit, uikit.h généré, chargeur d'images.
- 30/10/2026 (branche worktree-agent-a12527a311bc7a638) : IPC entre process dans le noyau (src/kernel/ipc.asm, appels 0x70-0x7D : poignees par process, canaux SEQPACKET/STREAM/DGRAM avec anneau 64 Ko, SCM_RIGHTS, HANDLE_WAIT = poll noyau, memfd/shm_open + SHM_MAP en regions VMK_SHARED, heritage HANDLE_INHERIT/HANDLE_LIST) + libposix (ipc.c...). posixtest ipc 27 PASS. WebKit2 tourne : wk2test (UI+web+reseau) peint page1.html. libWebKit.a n'a pas eu besoin d'etre recompile (en-tetes posix inchanges).
- Jet commence : src/extra/onyxports/jet (engine_webkit.cpp d'Onyx, main.cpp habillage UIKit ecrit pour ARMPI, present.c sur la surface attachee ui_surface) ; build-jet.sh -> SD0:/apps/jet.app. Verifie QEMU (pages locales, molette). Reste : menus, redimensionnement, telechargements/console, reseau reel https sur le PocketTerm.
- Adresses WebKit : /chemin (file://SD0:/x invalide). Essai QEMU de Jet : mettre SD0:/apps/jet.app/main dans sdroot/etc/startup.txt (sh n'a pas de &), puis le remettre.
- armpi_remote telnet : "@600 commande" pour une attente > 60 s (preload de 95 Mo).
