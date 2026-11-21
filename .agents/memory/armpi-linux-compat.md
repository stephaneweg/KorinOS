---
name: armpi-linux-compat
description: Décision 13/11 : couche de compatibilité Linux (personnalité par process, liblinux préchargée, TuxKit) pour faire tourner le binaire officiel de Claude Code par abonnement
metadata:
  type: project
---

Le 13/11/2026, l'utilisateur choisit la **couche de compatibilité Linux** pour avoir Claude Code dans Korin, en mode chat, avec son abonnement (pas de clé d'API) : « la moins intrusive et qui demande le moins de travail (je veux être autonome) ». Revient sur l'écart du 11/11 (« sans couche de compatibilité », docs/NODEJS.md §10, docs/etude/ETUDE-CLAUDE-NATIF.md).

Forme validée en discussion : personnalité Linux par process ; le noyau renvoie les `syscall` du code de l'app vers **liblinux.so** (même process, façon Syscall User Dispatch ; les appels venant de la plage de liblinux partent comme appels Korin) ; liblinux **préchargée comme un kit** et liée au process quand la personnalité est activée (ELF Linux à l'exec, `linux prog`, app.txt) ; traduit et exécute par libposix. Kit **TuxKit** (.abi) pour les programmes de Korin (nom choisi pour éviter « LinuxKit » de Docker). Cible : version **musl** officielle non modifiée de Claude Code (x86-64 d'abord, arm64 ensuite). Dépendances relevées : epoll, eventfd, signalfd, inotify, pidfd_spawnp, posix_spawn, futex, clone, sched_affinity, recvmmsg/sendmmsg, splice, sendfile, getifaddrs (netlink), /proc/self/exe.

**Why:** Claude Code n'existe plus qu'en binaire Linux sur une fourche interne de Bun ; l'extraction (voie A) est bloquée par les conditions ; le binaire officiel non modifié est la voie propre.

**How to apply:** d'abord l'étude docs/etude/LINUX-COMPAT.md ; ordre : busybox statique → Alpine + apk → Node/Bun → Claude Code (xdg-open → Jet). Pièges : FS/TPIDR_EL0 à l'app, adresse de liblinux. Lire les conditions d'Anthropic ; couper la mise à jour automatique. Remplace la partie Claude Code de [[armpi-agent-ia-korin]] ; Vibe reste l'agent intégré ([[armpi-vibe-facade]]). Voir [[armpi-x86-pc]].

**Précision de l'utilisateur (19/11)** : pas de « personnalité Linux » dans le noyau ; une **capture générique des
syscall** : l'hôte du process déclare ce qu'il capture (plages d'adresses de l'instruction, numéros), le noyau les
lui renvoie sans savoir quelle personnalité il imite (d'autres personnalités possibles). Voir docs/etude/WIDEVINE.md § 3.2,
[[armpi-widevine]].

**21/11, chantier « hôte Linux » lancé** (docs/etude/LINUX-HOST.md) : décisions de l'utilisateur : pas de Claude Desktop (pas publié pour Linux) ; jalons 1 à 3 d'abord (exécutable glibc, Node.js, Claude Code dans le Terminal) ; le but est la CLI officielle par l'abonnement, jamais une clé d'API (le frontal, jalon 4, pilotera cette CLI en stream-json). Claude Code 2.1.330 : ET_EXEC non PIE à 0x200000 (365 Mo), glibc, 519 imports, TLS 0x5d30. Conception du jalon 1 : hôte lié à 0x40_0000_0000, zone basse VM_FIXED dans le noyau, chargeur d'exécutables dans liblinux, TLS de l'exe dans une zone vide de 64 Ko en haut du TLS de l'hôte.
