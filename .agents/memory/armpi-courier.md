---
name: armpi-courier
description: "Courier, client HTTP d'Onyx réécrit en C# sur WinForms (tools/dotnet/courier) ; pièges d'essai QEMU des apps .NET"
metadata:
  node_type: memory
  type: project
  originSessionId: 94f2ffd1-b0ba-41c2-804f-d21c3c9e48f8
  modified: 2026-11-01T14:10:07.325Z
---

**Remplacé le 1/11/2026 par le port C++ de `src/apps/courier` : l'utilisateur trouvait le démarrage en C# trop long. Ce qui suit concerne les sources gardées dans tools/dotnet/courier, plus empaquetées.**

Courier porté d'Onyx en C# (1er/11/2026), dans `main` depuis d2ce4b4. Essayé sous QEMU ; pas essayé sur le Pi. L'utilisateur avait accepté un retour au C++ si .NET bloquait trop : ça n'a pas été nécessaire.

**Why:** il voulait voir si une vraie app tenait en .NET. Elle a révélé des bugs du système : `epoll` gardait les fd fermés, `recv` en course avec `poll` réordonnait le flux TCP (`libposix`), `ui_rbox` bouclait sur une taille négative, `win_restore` d'Elegant jamais remis à zéro.

**How to apply:** pour essayer une app .NET sous QEMU, la lancer **depuis le terminal du bureau**, jamais par telnet ni `startup.txt` (dit par l'utilisateur) ; clavier AZERTY dans l'OS (`qemu_drive.py type` : `>` donne `/`, `<` donne `.`, `q` donne `a`, `;` donne `m`, chiffres par `key shift-1`) ; vider `etc/preload.txt` dans l'image d'essai (Jet occupe la carte 100 à 140 s). `libposix` changée : relancer `tools/posix/build.sh` puis `tools/dotnet/build.sh`, l'hôte `/dotnet/dotnet` la lie en statique. Voir [[armpi-but-dotnet]], docs/DOTNET.md.
