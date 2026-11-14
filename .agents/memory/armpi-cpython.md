---
name: armpi-cpython
description: "CPython 3.14.10 porté sous Korin (étapes 1-2 dans main 59c119e8 ; étape 3 tree-sitter/cffi/pip sur claude/python-step3-fd7473) ; QEMU seulement"
metadata:
  node_type: memory
  type: project
  originSessionId: b91c21c1-20c1-4bdd-a790-686c14303c3d
  modified: 2026-11-12T00:36:01.515Z
---

Port de CPython 3.14.10 (12/11/2026) : tools/python/build.sh (WSL, ~8 min), korin.patch (3 retouches), docs/PYTHON-PORT.md. Étapes 1 et 2 faites **sous QEMU seulement** : step2.py 17/17, REPL dans le Terminal, regrtest résumé dans PYTHON-PORT.md. Le port est dans main depuis le 12/11 (59c119e8, avec l étude PYTHON.md) ; les correctifs de libposix et vfs (openpty, waitid WNOWAIT, poll des tubes par V_PIPE_ROOM, bind port 0, setenv, signal par défaut = WIFSIGNALED...) **sont dans main** (7bf4fc18, f0293f88, 57cf62a5), poussés à la demande de l'utilisateur, car une autre session porte kits/noyau/services en C pour x86-64 ([[armpi-x86-pc]]).

Choix non évidents : stdlib statique dans python3 + modules d'essai en .so (Setup.local *shared*) ; libffi avec -DFFI_MMAP_EXEC_WRIT=1 (sinon fermetures dans le tas non exécutable) ; --without-mimalloc ; sysconfig de la carte réécrit (CC=gcc).

Pièges d'essai : telnetd et ftpd exigent SD0:/etc/remote.txt (tools/python/testimage.py met « essai ») ; QEMU s'arrête sans message sous fort trafic TCP sur 127.0.0.1 (test_socket, asyncio sendfile/ssl) ; le wait de qemu_drive.py borne la vie de QEMU.

**Why:** Python langage général de Korin, puis Vibe ([[armpi-agent-ia-korin]], [[armpi-dlopen]]).
Étape 3 (12/11) : branche claude/python-step3-fd7473 (1e530429), pas dans main : build-ext.sh (roues Korin croisées), site-packages.sh (voie A), pip.conf (HOME=SD0:/), korin.patch get_platform linux-aarch64, /gcc STAGES=posix ; step3.py 10/10 QEMU. Pièges : RAM0 = 256 nœuds en tout (pip avec isolation : TMPDIR sur la carte) ; le build/ d'un autre worktree peut se recopier, mais refaire libposix (posix.specs a des chemins absolus).

**How to apply:** prochaines étapes de PYTHON.md §9 : 5 (Rust), 12 (module korin) ; essayer sur le Pi d'abord.
