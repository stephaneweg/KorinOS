---
name: armpi-widevine
description: "Widevine dans Jet : hôte Linux, jalon 1 fait (liblinux + linuxhost, QEMU PC) ; décisions du 19/11 prises ; module réel mesuré : 791 syscall directs, personnalité du noyau obligatoire ; docs/etude/WIDEVINE.md"
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

Décisions de l'utilisateur (19/11) : glibc de compatibilité ; module par le canal de Google (component
updater, .crx3), archive à la main en secours ; ClearKey d'abord dans Jet. Module 4.10.3050.0 téléchargé
(build/pc/linux/widevine, SHA-512 de Mozilla vérifiée ; versions.txt de Google = 404) : interface et hôte 10
seulement, 177 imports dont 108 non servis, TLS par __tls_get_addr, **791 syscall directs obscurcis** : la
personnalité Linux du noyau est obligatoire (étape 2b). La sonde native seccomp qui exécute le module a été
refusée par le contrôle des permissions : à faire trancher par l'utilisateur.

Pièges : `linux` est une macro prédéfinie de gcc sous Linux (pas comme nom de champ) ; le canari de pile du code
Linux (%fs:0x28) tombe dans le TCB de 64 octets de libposix, stable. Le module ne va jamais dans nos paquets.

**How to apply:** suite = étapes 2 à 7 de docs/etude/WIDEVINE.md § 5 ; les `syscall` directs du module
demanderaient la personnalité Linux du noyau, commune avec [[armpi-linux-compat]]. Voir [[armpi-webkit]],
[[armpi-musique]].
