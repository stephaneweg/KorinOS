---
name: korin-posix
description: Ce que la couche POSIX de Korin OS (newlib + libposix) fait, fait autrement ou ne fait pas — fichiers, chemins à volume, process (fork, exec, posix_spawn), threads, mémoire, signaux, sockets, ptys, poll. À charger avant d'écrire ou de porter du code C/C++ qui touche au système, ou quand un programme Unix se comporte mal sous Korin.
---

# La couche POSIX de Korin

**Référence :** le guide `SD0:/apps/help.app/api/libc_fr.md` (dans le dépôt : `src/apps/help/data/api/libc_fr.md`),
`docs/POSIX.md` pour le détail et les essais, `docs/TERMINAL-ET-FORK.md` pour `fork`, `exec` et les ptys. Un guide
copié sur une carte d'avant le 11/11/2026 dit encore que `fork` rend `ENOSYS` : c'est faux depuis le 10/11.

POSIX sert à porter des apps et des bibliothèques, pas à être conforme : ce qui manque s'ajoute quand un port le
demande.

## Fichiers et chemins

- **Tout ce qui est courant marche :** `open`, `read`, `write`, `lseek`, `pread`, `pwrite`, `stat`, `opendir`,
  `nftw`, `fnmatch`, et tout `stdio.h`.
- **Chemins :** `SD0:/a/b`, les chemins relatifs, les chemins virtuels (`/gcc`, `/bin`, `/apps`, `/lib`, `/etc`,
  `/dotnet`, `/tmp` = `RAM0:/tmp`) et les pseudo-fichiers de `/dev` (`null`, `zero`, `urandom`, `random`, `stdin`,
  `stdout`, `stderr`, `tty`).
  - Un autre `/x` désigne la racine du volume courant.
  - `realpath` rend `SD0:/…`, et garde tels quels les chemins virtuels.
- **Une seule date par fichier**, précise à 2 s sur FAT. Ni droits ni propriétaires (uid 0), sauf le bit x.
- **Pas de liens :** `link` et `symlink` rendent `ENOSYS`, `readlink` rend `EINVAL` (sauf `/proc/self/exe`).
- **`rename`** est immédiat sur un même volume. D'un volume à l'autre, un fichier est recopié, mais un dossier rend
  `EXDEV`.
- **Limites :** 64 fichiers ouverts par programme, chemins de 254 octets au plus, `flock` sans effet, `statvfs` aux
  valeurs fixes.

## Process

- **`fork` :** une copie sur écriture. Tous les descripteurs sont hérités, la position d'un fichier est partagée, et
  `pthread_atfork` est appelé. `vfork` est un `fork`.
- **`execve` :** remplace le programme **dans le même process** (même pid). Les descripteurs sans `FD_CLOEXEC`
  restent ouverts, à leur numéro.
  - Repli sur l'ancienne façon (lancer, attendre, puis finir avec son code ; le pid change) : un programme de
    `SD0:/sys`, ou une DLL .NET.
  - Un programme de `SD0:/sys` (exec, `posix_spawn`, `system`) est lancé par init2, comme depuis `sh` : sa
    sortie va au journal, et `waitpid` rend toujours 0.
- **Scripts :** les scripts `#!` marchent ; sans `#!`, c'est `/bin/sh` qui les lance.
- **`execvp` et `posix_spawnp`** cherchent dans `PATH`, puis `SD0:/bin/<nom>` et `SD0:/apps/<nom>.app/main`.
- **`posix_spawn`** et ses actions (`adddup2`, `addopen`, `addclose`, `addchdir_np`) marchent aussi.
- **`system()` et `popen()`** lancent le premier mot **sans shell** : pas de tubes ni de redirections. Utilisez
  `bash -c "…"`.
- **`waitpid`** et **`SIGCHLD`** (seulement si un gestionnaire est installé) marchent.

## Threads et mémoire

- **Threads :** pthreads complets (mutex, conditions, rwlocks, clés, sémaphores), et en C++ `std::thread` et
  `thread_local`.
  - Une pile de thread fait 256 Ko par défaut, 64 Mo au plus ; le thread principal a 64 Mo.
- **`mmap` anonyme :** plusieurs Go, `PROT_EXEC` compris (un JIT est possible), avec `mprotect` et `madvise`.
  - `MAP_SHARED` en écriture sur un fichier rend `ENOTSUP`.
  - La mémoire partagée passe par `memfd_create` ou `shm_open`.
- **Propre à Korin :** `file_pin(chemin)` garde un fichier en mémoire.

## Signaux et temps

- **Signaux :** `sigaction` (avec `SA_SIGINFO`), les masques par thread et `sigaltstack` marchent, et une faute se
  rattrape.
  - `SA_RESTART` est sans effet : un appel interrompu rend `EINTR`, il faut le relancer.
  - `SIGPIPE` n'est jamais levé.
  - `timer_create` rend `ENOSYS`.
- **Temps :** `clock_gettime` (`REALTIME` et `MONOTONIC`) et `nanosleep` marchent. Le **fuseau est UTC** :
  `localtime` rend la même heure que `gmtime`.

## Réseau et entrées-sorties

- **Sockets BSD IPv4**, sur NetKit (voir `korin-kit-netkit`) :
  - `connect` bloque toujours, même en non bloquant ;
  - `bind` sur le port 0 ne choisit pas de port ;
  - pas d'IPv6, et pas de socket `AF_UNIX` nommé : seul `socketpair` marche.
- **Tubes :** `pipe` et `O_NONBLOCK`.
- **Ptys :** `posix_openpt`, `/dev/ptmx`, `/dev/pts/N`, `termios` et `TIOCGWINSZ` marchent. `isatty` n'est vrai que
  sur un pty.
- **`poll` et `select`** ne dorment dans le noyau que si tous les descripteurs sont des canaux locaux. Sinon, ils
  tournent en boucle de 1 ms. Le bout d'écriture d'un tube est toujours « prêt ».

## Ce qui manque

- **Fonctions :** `dlopen` (les programmes sont statiques : il échoue proprement), `getifaddrs`, `timer_create`,
  `mkfifo`, `sendfile`, `eventfd`, `inotify`, `prctl`, `getloadavg`, `sysinfo`, `mremap` et `regcomp`.
- **En-têtes absents :** `sys/prctl.h`, `sys/eventfd.h`, `sys/sysctl.h`, `pty.h` et `threads.h`.
- **Pas de `/proc`**, sauf `/proc/self/exe`.
- **`syscall()`** ne connaît que `gettid`, `getrandom` et `membarrier`.
- **`uname`** rend « ARMPI » ; un programme portable peut tester `__armpi__`.

## Porter du code Unix

1. Compiler avec `/gcc/bin/gcc` ; les erreurs disent ce qui manque.
2. Remplacer ce qui manque : `fork` + `exec` marchent ; pour `inotify`, sonder avec `stat` ; pour `getifaddrs`,
   passer par NetKit (`net_info`).
3. Traiter les chemins : un chemin `SD0:/…` est absolu même sans `/` en tête.
4. Vérifier avec les essais de `posixtest <groupe>` et `posixtest-cxx` (sur la carte) : ils montrent ce qui est
   couvert.
