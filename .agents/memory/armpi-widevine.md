---
name: armpi-widevine
description: "Widevine dans Jet (Netflix, Disney+, Spotify web) : hôte Linux lancé le 19/11, jalon 1 fait (liblinux + linuxhost, QEMU PC) ; décisions glibc et source du module en attente ; docs/etude/WIDEVINE.md"
metadata:
  type: project
---
Demande de l'utilisateur (18/11/2026), à partir de LibreWolf (une case télécharge Widevine) et de l'envie d'avoir
Netflix et Disney+. Le 19/11, il lance le chantier par un **hôte qui exécute du code Linux** (sans attendre la
couche Linux complète de Claude Code). Autre motif : la lecture de librespot (app Spotify) n'est pas au point,
le lecteur web de Spotify a besoin de Widevine.

**Jalon 1 fait le 19/11 (fce63664, main)** : `src/lib/linux` (chargeur ELF de .so Linux x86-64, glibc de
compatibilité sur libposix, appels de Linux traduits) et `src/sys/linuxhost` (`--check`, `--self-test`,
`FICHIER.so FONCTION`). Module d'essai lxtest.so construit pour Linux par la gcc de WSL : 16/16 sous QEMU PC.
Construire : `wsl sh tools/linux/build.sh` ; essayer : dans WSL, `. ~/korin-cloud/env.sh` puis
`python3 -I tools/linux/test_pc.py` (QEMU n'est plus installé côté Windows).

Décisions présentées le 19/11, en attente : glibc de compatibilité (recommandée, en place) ou vraie glibc ;
source du module (component updater de Google recommandé, Balrog de Mozilla, ou archive posée à la main) ;
téléchargement de développement du module pour `linuxhost --check` ; ClearKey d'abord dans Jet.

Pièges : `linux` est une macro prédéfinie de gcc sous Linux (pas comme nom de champ) ; le canari de pile du code
Linux (%fs:0x28) tombe dans le TCB de 64 octets de libposix, stable. Le module ne va jamais dans nos paquets.

**How to apply:** suite = étapes 2 à 7 de docs/etude/WIDEVINE.md § 5 ; les `syscall` directs du module
demanderaient la personnalité Linux du noyau, commune avec [[armpi-linux-compat]]. Voir [[armpi-webkit]],
[[armpi-musique]].
