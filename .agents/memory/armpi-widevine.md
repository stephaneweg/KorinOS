---
name: armpi-widevine
description: "Widevine dans Jet : hôte Linux, jalon 1 fait (liblinux + linuxhost, QEMU PC) ; décisions du 19/11 prises ; module mesuré ; étapes 2b (capture des syscall) et 2c (requête de licence dans Korin) faites, QEMU ; docs/etude/WIDEVINE.md"
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
personnalité Linux du noyau est obligatoire (étape 2b). Sonde sous Linux autorisée et faite (tools/linux/cdmprobe/run.sh) : avec un Host_10 minimal le module
produit une vraie requête de licence (1702 octets) ; ses syscall directs = openat/read/close de /proc/self/maps
(×11) et /proc/net/unix, rt_sigaction en lecture (SIGILL, SIGTRAP, SIGABRT). À fabriquer dans liblinux.

**Décision de forme (19/11)** : le noyau ne connaît pas « Linux » ; il offre une **capture générique** : l'hôte
déclare les syscall qu'il veut capturer (plages d'adresses de l'instruction, numéros), le noyau les lui renvoie
(même thread, comme un signal) ; porte ouverte à d'autres personnalités. Étude § 3.2. Livraison dans le
thread appelant choisie par l'utilisateur (pas de thread hôte séparé).
**Étape 2b faite le 19/11** : SYS_SYSCALL_CAPTURE 0x96 (noyau PC : core/signal.c, arch/x86_64/sigframe.c ;
trame de signal SIGSYS, retour par SIG_RETURN), liblinux capture.c ; QEMU : 4 syscall directs traduits, ptest
22/0. Noyau d'essai construit à part : `PC_OUT=build/pc/linux/kbuild PC_QUICK=1 PC_SVC=pcid sh tools/pc/build.sh`,
puis `test_pc.py --esp-src build/pc/linux/kbuild/esp`. **Étape 2c faite le 19/11 (QEMU)** : le vrai module produit une requête de licence (1702 octets) DANS Korin :
glibc_thread.c, glibc_misc.c (177 imports servis), TLS (__tls_get_addr), .eh_frame déclaré à libgcc, /proc/self/maps
fabriqué (hôte nommé libc.so.6), futex ; noyau PC : **arène haute** 0x1000_0000_0000-0x7000_0000_0000 (PartitionAlloc
réserve 32 Go alignés sur 32 Go ; docs/ABI.md). Sonde Korin : `WIDEVINE_SO=… sh tools/linux/build.sh` puis
`test_pc.py --probe --esp-src build/pc/linux/kbuild/esp` (module embarqué, build/ seulement). Suite : étape 3
(UpdateSession avec une vraie licence, Decrypt, CreateFileIO, cdmhost et IPC), puis ZBook.

Pièges : `linux` est une macro prédéfinie de gcc sous Linux (pas comme nom de champ) ; le canari de pile du code
Linux (%fs:0x28) tombe dans le TCB de 64 octets de libposix, stable. Le module ne va jamais dans nos paquets.

**How to apply:** suite = étapes 2 à 7 de docs/etude/WIDEVINE.md § 5 ; la capture générique des syscall dans le
noyau sert aussi [[armpi-linux-compat]]. Voir [[armpi-webkit]],
[[armpi-musique]].
