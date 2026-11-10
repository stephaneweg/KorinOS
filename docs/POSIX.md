# La couche POSIX d'ARMPI (`posix`)

`libposix` est la couche POSIX des programmes **C et C++** d'ARMPI : une **bibliothèque
statique** liée dans chaque programme (avec newlib 4.4 de la chaîne d'Onyx), pas un kit. Elle
est reprise de **libonyxposix** d'Onyx (`user/Runtime/libc/posix`, ≈ 9 000 lignes de C, licence
MIT, code de l'utilisateur), ses appels `kapi_*` remplacés par ce qu'ARMPI offre : appels système
(`src/inc/syscalls.inc`), AppKit (fichiers, tubes, lancement), vfs, NetKit (réseau).

| Où | Quoi |
|---|---|
| `src/lib/posix/*.c`, `crt0posix.S` | la bibliothèque, le point d'entrée |
| `src/lib/posix/armpi.h`, `armpi.c` | le côté ARMPI : appels système, erreurs, kits, fichier_sys d'AppKit, vfs |
| `src/lib/posix/include/` | en-têtes (sur ceux de newlib : `pthread.h`, `semaphore.h`, `poll.h`, `sys/socket.h`, `sys/mman.h`...) |
| `src/lib/posix/posix.ld` | script d'édition de liens : ELF64 `ET_EXEC` à `0x40_0000_0000`, comme les programmes en assembleur |
| `src/lib/posix/test/` | `posixtest.c`, `posixtest-cxx.cpp` : les essais d'Onyx, portés |
| `tools/posix/build.sh` | construction (WSL) |
| `build/posix/` | le résultat : `include/`, `lib/libposix.a`, `lib/crt0posix.o`, `lib/posix.ld`, `lib/posix.specs` |
| `prebuilt/posixtest`, `prebuilt/posixtest-cxx` | les essais, copiés sur `SD0:/bin` par `build.ps1` |

Rangement : `src/lib/posix` (décision de l'utilisateur) — une bibliothèque, pas un programme
(`src/bin`) ni un kit (`src/kits` : chargé une fois pour tout le système, sans adresse absolue,
en assembleur). Comme MediaKit, c'est du C compilé par la chaîne d'Onyx : **hors du selfhost**
(décision du 28/10/2026 : seuls WebKit et le code C porté y échappent ; tout le reste s'assemble
sous ARMPI).

## Construire et compiler un programme C

Dans WSL, la chaîne d'Onyx (`/opt/toolchains/aarch64-onyx-elf-14.2`, `tools/toolchain/fetch.sh`
d'Onyx : GCC 14.2, newlib 4.4 `--enable-newlib-reent-thread-local`, libstdc++ avec threads POSIX) :

```sh
sh tools/posix/build.sh             # libposix.a, crt0posix.o, posix.ld, posix.specs, posixtest(-cxx)
TC=/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
${TC}gcc -O2 -specs=build/posix/lib/posix.specs hello.c -o hello       # C
${TC}g++ -O2 -specs=build/posix/lib/posix.specs prog.cpp -o prog       # C++ (std::thread...)
```

`posix.specs` ajoute `crt0posix.o`, `libposix.a`, le script `posix.ld`, `-isystem
build/posix/include`, `-DFD_SETSIZE=1024`, `__unix__` et `__armpi__`. Le programme se copie dans
`SD0:/bin` (sans extension) et se lance du terminal comme les autres. `armpi_sys.h` (les numéros
d'appels) est **généré** depuis `src/inc/syscalls.inc` par le script : une seule source.

Démarrage (`crt0posix.S`, `start.c`) : `x0` = ligne d'arguments du noyau ; `TPIDR_EL0` du thread
principal (bloc TLS statique), tables de déroulement (exceptions C++), descripteurs 0–2,
environnement, `argv` (`argv[0]` = nom du process, puis la ligne coupée aux espaces ; guillemets
et `\"`, `\\`), constructeurs, `exit (main (...))`.

## Les kits depuis le C : `<armpi/kit.h>`

`armpi_kit_open (nom, version minimale)` mappe un kit (`SD0:/lib/<nom>.so`, lu par AppKit la première
fois, comme `kits_open` des programmes en assembleur) et rend sa table ; `armpi_kit_fn (kit, rang)`
une fonction (rang = ligne `kit_fn` du `.abi`, à partir de 0 ; AAPCS64) ; `armpi_kit_version` le
nombre de fonctions. S'en servent : la sortie son du moteur média de WebKit sur AudioKit
(`tools/webkit/glue/av_sound_armpi.c`) et l'en-tête C d'UIKit (docs/WEBKIT.md).

## Correspondance avec ARMPI

| Domaine | Onyx (`kapi_*`) | ARMPI |
|---|---|---|
| Threads | `thread_create_ex`, `join`, `self`, `priority`, futex | `SYS_THREAD_CREATE_EX` (pile `malloc`, `TPIDR_EL0` = TCB), `SYS_THREAD_JOIN`, `SYS_THREAD_SELF`, `SYS_THREAD_SET_PRIORITY` (priorité > 0 : haute), `SYS_FUTEX_WAIT` (délai) / `WAKE` |
| TLS, errno | TPIDR_EL0 par tâche | TPIDR_EL0 sauvé par le noyau à chaque commutation ; errno de newlib en `__thread` |
| Fichiers, répertoires | `file_open`, `dir_read`, `path_stat`... | client de fichiers d'AppKit (`fichier_sys` : `SYS_OPEN`, `SYS_READ`, `SYS_WRITE`, `SYS_SEEK`, `SYS_CLOSE`, `SYS_STAT`, `SYS_READDIR`, `SYS_MKDIR`, `SYS_UNLINK`, `SYS_CHDIR`, `SYS_GETCWD`) |
| Entrée / sortie standard | `stdin_read`, `stdout_write` | descripteurs 0, 1, 2 d'AppKit (tube donné par le père, sinon la console) ; un bout de pseudo-terminal de vterm : descripteur TTY de `tty.c` |
| Terminaux (10/11/2026) | | service vterm ([VTERM.md](VTERM.md)) : `isatty`, `tcgetattr`, `tcsetattr`, `tcflush`, `tcgetpgrp`, `tcsetpgrp`, `ioctl` (`TIOCGWINSZ`, `TIOCSWINSZ`, `TIOCGPGRP`, `TIOCSPGRP`, `FIONREAD`, `TCGETS`, `TCSETS*`, `TIOCGPTN`, `TIOCSCTTY`, `TIOCNOTTY`), `tcgetsid`, `posix_openpt`, `grantpt`, `unlockpt`, `ptsname`, `/dev/ptmx`, `/dev/pts/N`, `/dev/tty` |
| Groupes de process, sessions, contrôle des tâches (10/11/2026) | | `SYS_SETPGID`, `SYS_GETPGID` : `setpgid`, `getpgid`, `getpgrp`, `setsid` (`SETPGID (-1)`), `getsid` (`GETPGID` + `GETPGID_SID_MASK`) ; `kill (-g)`, `killpg`, `kill (0)` : `SIG_SEND` au groupe ; `waitpid`, `wait3`, `wait4`, `waitid` : `SYS_WAIT` étendu (section « Contrôle des tâches ») |
| Tubes | `pipe`, `stream_*` | vfs : `V_PIPE`, `V_PIPE_READ` (sans attente : `O_NONBLOCK`, poll), `V_PIPE_WRITE`, `V_PIPE_CLOSE` |
| Sockets locaux (v76) | `sock_pair`, `sock_sendmsg` / `recvmsg` | noyau : `CHAN_CREATE`, `CHAN_SEND`, `CHAN_RECV` (poignées portées : `SCM_RIGHTS`), `CHAN_SHUTDOWN`, `HANDLE_WAIT` (poll), `HANDLE_INFO`, `HANDLE_CLOSE` (ABI.md `0x70`–`0x7D`) |
| Mémoire partagée (v76) | `shm_create`, `shm_open`, `shm_ctl`, `shm_map` | noyau : `SHM_CREATE`, `SHM_OPEN`, `SHM_UNLINK`, `SHM_CTL` (`ftruncate`, sceaux), `SHM_MAP` (`mmap MAP_SHARED`) |
| Descripteurs du fils (v76) | `spawn_ex2`, `get_handles` | noyau : `HANDLE_INHERIT` (le père, juste avant le lancement), `HANDLE_LIST` (le fils, au démarrage) |
| Process | `spawn_ex2`, `proc_wait`, `kill_pid` | vfs `V_EXEC` + AppKit `f_spawn_file` (→ `SYS_SPAWN_IMAGE`), `SYS_WAIT`, `SYS_PROCLIST`, `SYS_KILL`, `SYS_GETPID` |
| Mémoire | `sbrk`, `vm_map`... | `SYS_SBRK` (malloc de newlib) ; `mmap`, `munmap`, `mprotect`, `madvise`, `mincore` : `SYS_VM_MAP`, `VM_UNMAP`, `VM_PROTECT`, `VM_ADVISE`, `VM_PAGEINFO` (pagination à la demande) ; `MAP_PRIVATE` d'un memfd : `SHM_MAP` + `VM_COW` ; d'un fichier : plus bas (Mémoire, `file_pin`) |
| `epoll` | | `epoll.c`, sur `poll` : tranches de 5 ms, `EPOLLET` rappelé toutes les 50 ms ; fermer un descripteur le retire des tables ; assez pour les sockets de .NET |
| Temps | `clock_info`, `sleep_us` | compteur `CNTPCT_EL0` (lisible en EL0), `SYS_TIME_MS`, horloge du noyau (`SYS_CLOCK_GET` : UTC et fuseau), `SYS_SLEEP` |
| Aléa | `random` | `SYS_GETRANDOM` (`getrandom`, `/dev/urandom`) |
| Réseau | `sock_*`, `net_resolve` | NetKit : `net_socket`, `net_connect`, `net_bind`, `net_listen`, `net_accept`, `net_send`, `net_recv`, `net_sendto`, `net_recvfrom`, `net_close`, `net_resolve`, `net_info` |
| Système | `meminfo`, `abi_version` | `SYS_SYSINFO` (cœurs, mémoire), `uname` = « ARMPI » |

Les codes d'erreur d'ARMPI (numéros de Linux) sont traduits en `errno` de newlib (`armpi.c` :
ENOSYS 38 → 88, ENOTEMPTY 39 → 90, ETIMEDOUT 110 → 116...).

## Ce qui marche (QEMU, 29-30/10/2026)

`posixtest`, groupe par groupe (`posixtest mem`, `thread`... : une commande par groupe, le
telnet d'essai coupe au bout de 60 s) :

| Groupe | Réussis | Échecs | Sautés (ce qu'ARMPI n'a pas) |
|---|---|---|---|
| mem | 16 | 0 | 0 (30/10/2026 ; avant `VM_MAP` : 8 réussis, 5 sautés) |
| thread | 19 | 0 | 0 |
| file (`RAM0:/posixtest` et `/tmp`) | 40 | 0 | 4 (noms de 200 octets sur RamFS, `mtime`) |
| io | 9 | 0 | 0 |
| time | 7 | 0 | 0 |
| proc | **14** | 0 | 0 (30/10/2026 : environnement transmis, 8 Ko d'`argv`, variable de 9 Ko, 48 Mo de pile, redirections vers un fichier, héritage par un petit-fils) |
| ipc (31/10/2026) | **28** | 0 | 0 : `socketpair` SEQPACKET (limites gardées), STREAM, DGRAM, `poll` (délai, `POLLIN`), `SO_PEERCRED`, `memfd_create` + `ftruncate` + `fstat`, deux `MAP_SHARED` (mêmes pages), `MAP_PRIVATE` (copie, et `mprotect` n'atteint pas l'objet), sceaux, `SCM_RIGHTS` d'un memfd et d'un socket (3 iovecs, `MSG_CTRUNC`, `MSG_CMSG_CLOEXEC`), `shm_open` / `shm_unlink`, `posix_spawn` (le socket à son numéro chez le fils, `FD_CLOEXEC` gardé chez le père, `adddup2` sur 30), le fils écrit dans la mémoire du père, charge (3 000 paquets de 2 à 4 000 octets, 1 Mo en STREAM), un fils tué (bloqué dans `recv`) : `POLLHUP`, 8 Mo de mémoire partagée en vol rendus |
| net | 10 | 0 | 1 (`getsockname` / `getpeername` du chemin « anciens sockets ») |
| signal (30/10/2026) | 18 | 0 | 0 : fautes, `ucontext`, `sigaltstack`, `pthread_kill`, débordement de pile sur la page de garde, `SIGRTMIN`, `membarrier` |
| misc | 24 | 0 | 0 |
| cxx (`posixtest-cxx`) | **82** | 0 | 0 : `std::thread`, mutex, `condition_variable`, `thread_local`, statiques, `std::filesystem`, `steady_clock`, `async` / `future`, exceptions entre threads |

`posixtest file SD0:/tmp` (FAT32, service `fatfs`) et `posixtest file SD1:/tmp` (exFAT) : 20
réussis, 0 échec, 1 sauté (`mtime`) — avant les corrections de `fatfs` du 29/10/2026 : 16 réussis,
4 échecs (deux descripteurs sur un fichier, `O_APPEND` à deux, nom de 200 caractères, `rmdir`).

## fork (10/11/2026, étape 3 de [TERMINAL-ET-FORK.md](TERMINAL-ET-FORK.md))

`fork` (`proc.c`) passe par `f_fork` d'AppKit (`SYS_FORK`, [KITS.md](KITS.md)) :

* **Avant** : les fonctions `prepare` de `pthread_atfork` (des dernières inscrites aux premières, 64
  au plus), puis les verrous internes, toujours dans cet ordre : lancement (`s_spawnLock`), liste
  des `FILE` et environnement de newlib, table des fils, `alive`, table des descripteurs, clés,
  liste des threads, régions de `mmap`, `malloc` en dernier.
* **Après** : le père les rend ; le fils (un seul thread : l'appelant) les remet à zéro, ainsi que
  les verrous de `stdin`, `stdout`, `stderr` (pas pris : un autre thread pouvait attendre sur un
  tube plein en les tenant), reprend son tid (`THREAD_SELF`), vide sa liste de threads et sa
  table des fils, fait oublier à NetKit le tampon du père ; puis les fonctions `parent` / `child`
  dans l'ordre d'inscription. Les autres threads du père n'existent pas dans le fils ; un verrou du
  programme qu'ils tenaient y reste fermé (comme partout).
* **Descripteurs** : tous hérités, aux mêmes numéros (fichiers, répertoires, tubes, sockets de
  NetKit, canaux et mémoire partagée du noyau). La **position d'un fichier est partagée** entre
  père et fils (gardée par fatfs ou ramfs après le `fork`) : `{ cmd1; cmd2; } > f` marche ;
  `pread` / `pwrite` passent par `f_lire_a` / `f_ecrire_a` et n'y touchent pas.
* `vfork` est `fork` : le père n'est pas suspendu ; ce que POSIX permet au fils d'un `vfork`
  (`_exit`, `exec*`) marche pareil. `getppid` : `SYS_GETPPID` (1 si le père est fini).
* `waitpid`, `wait` : les fils de `posix_spawn` et de `fork` ; `SYS_WAIT` attend dans le noyau
  (le père est réveillé à la fin du fils, sans sonder) ; `wait` / `waitpid (-1)` avec un seul fils
  l'attend ainsi, avec plusieurs demande à chacun sans attendre (`SYS_WAIT` étendu, `WNOHANG`)
  toutes les 5 ms.
* Essai : groupe `fork` de `posixtest` (mémoire séparée, `getpid` / `getppid`, tube hérité,
  fichier au descripteur 40, position partagée sur `RAM0:` et `SD0:` (écritures, `lseek` du fils,
  `O_APPEND`), `WNOHANG`, 50 `fork` + `wait`, ordre de `pthread_atfork`, père à plusieurs threads,
  socket TCP hérité sur 127.0.0.1).

## exec (10/11/2026, étape 4 de [TERMINAL-ET-FORK.md](TERMINAL-ET-FORK.md))

`execve` (`proc.c`) remplace le programme **dans le même process** (`SYS_EXEC`, [ABI.md](ABI.md)
`0x6B`, par `f_exec_file` d'AppKit, [KITS.md](KITS.md)) : même pid, même père, même groupe.

* **Avant** : `V_EXEC` à vfs (fichier absent : `ENOENT`, rien n'est touché). Puis les
  descripteurs `FD_CLOEXEC` et les sockets de NetKit (son état ne passe pas un exec) sont fermés.
* **Les autres descripteurs gardent leur numéro**, au-delà de 2 compris : fichiers (position
  continue, partagée après un `fork`), répertoires, tubes, bouts de pty, canaux locaux et mémoire
  partagée, `/dev/null`… Leur description (une seule pour des `dup`) va avec `argv` et `envp` dans
  l'objet de lancement, après les chaînes ; le nouveau programme les remet en place au démarrage
  (`__posix_exec_restore`), avec ses fils (`waitpid` les connaît toujours) et les signaux ignorés.
  AppKit passe ses propres descripteurs (64..127). Entrées / sorties 0 à 2 : aussi dites à vfs
  (un programme en assembleur les lit par `V_STDIO`) ; un fichier en 0 à 2 y devient un bout-fichier.
* **Signaux** (noyau) : gestionnaires remis par défaut, ignorés gardés, masque et attente gardés.
* Les tampons de `stdio` ne sont pas vidés (comme ailleurs : ce qui n'est pas écrit est perdu).
* **Repli** sur l'ancienne façon (lancer, attendre, `_exit` avec son code ; le pid change) : un
  programme de `SD0:/sys` (exec n'accorde pas les droits : `EPERM` d'AppKit), une DLL .NET, un
  AppKit d'avant exec. Un fichier non exécutable : `-1`, `EACCES` ; un script : son interprète
  (section suivante).
* `execvp`, `execvpe`, `execlp` : un nom sans `/` est cherché dans `PATH` (entrées comme `/bin`,
  `/gcc/bin`, chemins virtuels de vfs, ou `SD0:/bin`), puis par vfs comme `posix_spawn`
  (`SD0:/bin/<nom>`, `SD0:/apps/<nom>.app/main`). `fexecve` : le chemin du fichier ouvert.
* Un programme lié à l'ancienne libposix, lancé par exec, n'a que 0 à 2 (par vfs).
* Essai : groupe `exec` de `posixtest` (`getpid` / `getppid` gardés, `FD_CLOEXEC`, fichier, tube et
  canal au-delà de 2, position partagée, `envp`, code par `waitpid`, fils d'avant l'exec, signaux,
  `ENOENT`, `EACCES`, `execvp` par `PATH`) ; `exectest` côté AppKit.

## Bit d'exécution, scripts, `SIGALRM` (10/11/2026, étape 5 de [TERMINAL-ET-FORK.md](TERMINAL-ET-FORK.md))

* **`stat`, `fstat`, `lstat`** demandent `V_STATX` à vfs : un fichier exécutable (règles de chemin
  ou index des exécutables, [FICHIERS.md](FICHIERS.md) « Le bit d'exécution ») a `0755`, les autres
  `0644`, un répertoire `0755`. **`access`, `faccessat`, `eaccess`** avec `X_OK` : `EACCES` sans le
  bit (lire et écrire restent permis : un seul utilisateur).
* **`chmod`, `fchmod`, `fchmodat`** : `V_CHMOD` (seuls les bits `x` comptent : ajouter ou retirer de
  l'index). **`open(O_CREAT)` / `creat`** d'un fichier neuf avec un bit `x` (moins `umask`) : `chmod`
  juste après (l'éditeur de liens rend ainsi `a.out` exécutable).
* **Scripts** : `execve`, `posix_spawn`, `execvp` d'un script lancent son interprète (`#!`, sinon
  `/bin/sh`) avec argv = interprète, [argument de `#!`,] script, `argv[1..]` (vfs ouvre l'interprète :
  `ARMPI_ATTR_SCRIPT` ; `posix_spawn` refait alors l'objet des arguments). Un fichier non exécutable :
  `EACCES` (`E_PERM` de vfs).
* **`alarm`, `ualarm`, `setitimer`, `getitimer`** (`ITIMER_REAL`) : `SYS_SETITIMER` ([ABI.md](ABI.md)
  `0x6F`) ; le noyau envoie `SIGALRM` à l'échéance, puis à chaque intervalle. Pas de minuterie dans le
  fils d'un `fork` ; gardée par `exec`. `ITIMER_VIRTUAL`, `ITIMER_PROF` : `EINVAL` ; `timer_create` :
  `ENOSYS`.
* **Une lecture de tube bloquée** rend `EINTR` après un signal livré à un gestionnaire sans
  `SA_RESTART` (vfs attend 1 s au plus, `PIPE_COURT`, puis libposix redemande ; un terminal : 1 s
  aussi, tty.c).
* Ajouts pour bash : `sigsetjmp` / `siglongjmp` (`<setjmp.h>` : newlib ne les donne qu'à Cygwin et
  RTEMS), `tcflow` (sans effet), `getgrnam`, `getgrent` / `setgrent` / `endgrent` (le seul groupe
  `root`), `siginterrupt` déclaré ; `pw_shell` vaut `/bin/bash` s'il est sur la carte, sinon `/bin/sh`.
* Essais : groupes `xbit` et `alarm` de `posixtest`.

## Contrôle des tâches (10/11/2026, étape 6 de [TERMINAL-ET-FORK.md](TERMINAL-ET-FORK.md))

Le noyau arrête et reprend les process (`src/kernel/jobs.asm`, [ABI.md](ABI.md) « Contrôle des
tâches ») ; vterm envoie `SIGTSTP` (Ctrl+Z), `SIGTTIN` et `SIGTTOU` ([VTERM.md](VTERM.md)).

* **Actions par défaut** (`signal.c`, comme le noyau) : `SIGSTOP`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU`
  arrêtent le process (`raise (SIGTSTP)` aussi : `SIG_SEND` à soi) ; `SIGCONT` le fait repartir,
  puis est ignoré s'il n'a pas de gestionnaire.
* **`waitpid`** : `WNOHANG`, `WUNTRACED`, `WCONTINUED` ; statuts de Linux (`<sys/wait.h>` de
  libposix) : `WIFSTOPPED`, `WSTOPSIG` ((signal << 8) | 0x7f), `WIFCONTINUED` (0xffff). pid -1, 0
  ou -groupe : les fils (du groupe : `getpgid` de chacun). Un fils arrêté n'est pas relevé ; son
  arrêt est rendu une fois. `wait3`, `wait4` (rusage à zéro), `waitid` (`P_ALL`, `P_PID`,
  `P_PGID` ; `WEXITED`, `WSTOPPED`, `WCONTINUED`, `WNOHANG` ; `si_code` `CLD_*`, `si_status` ;
  `WNOWAIT` : `EINVAL`).
* **`SIGCHLD`** à l'arrêt et à la reprise d'un fils, sauf `SA_NOCLDSTOP` ; sans `siginfo`
  (`si_pid`, `si_code` à zéro) : un shell relit l'état par `waitpid (-1, WNOHANG | WUNTRACED |
  WCONTINUED)`, comme bash.
* **Sessions** : `setsid` (meneur de groupe : `EPERM`), `getsid` ; `ioctl (TIOCSCTTY)` (meneur
  de session) donne le pty à la session et son groupe au premier plan ; `TIOCNOTTY` ; `tcgetsid`
  (un pty sans session : la session de l'appelant).
* **Terminal en arrière-plan** : une lecture rend la main après `SIGTTIN` (le process s'arrête),
  puis, repris, est refaite ; de même `write` avec `TOSTOP`, `tcsetattr`, `tcsetpgrp`, `tcflush`
  après `SIGTTOU`. Si un gestionnaire a tourné : `EINTR`. `SIGTTIN` ignoré ou masqué, groupe
  orphelin : `EIO` ; `SIGTTOU` ignoré ou masqué : l'opération se fait (bash masque `SIGTTOU` pour
  `tcsetpgrp`).
* **Arrêt d'un process à plusieurs threads** : tous ; un thread dans un appel au noyau le finit
  d'abord (un `sleep` arrêté continue de compter, comme sous Linux ; un `read` de pty revient au
  plus tard au bout d'une seconde).
* Essai : `jobtest` (`src/lib/posix/test/jobtest.c`, `prebuilt/jobtest`, 40 essais, sans
  terminal : il devient meneur de session et joue le terminal sur un pty à lui) ; dans le Terminal,
  `sleep 30`, Ctrl+Z, `jobs`, `bg`, `fg` (le `sh` natif, [SHELL.md](SHELL.md)).
* **Pour bash `--enable-job-control`** : tout ce que vérifie `initialize_job_control` est là
  (`setsid`/`setpgid`, `tcgetpgrp`/`tcsetpgrp` avec `SIGTTOU` masqué, `waitpid` avec `WUNTRACED`
  et `WCONTINUED`, `SIGCHLD`, `killpg`). Simplifié : pas de `siginfo` pour `SIGCHLD`, `waitpid`
  bloquant n'est pas interrompu par un signal (`SYS_WAIT`), `waitid` sans `WNOWAIT`.

## IPC entre process (30/10/2026 : ce qu'il faut à WebKit2)

`ipc.c` (et les branches `POSIX_FD_LSOCK` / `POSIX_FD_SHM` de `socket.c`, `file.c`, `stat.c`,
`mman.c`, `poll.c`, `fd.c`, `proc.c`) sur les appels `0x70`–`0x7D` du noyau
(`src/kernel/ipc.asm`, [ABI.md](ABI.md) « 0x70 ») :

* `socketpair (AF_UNIX, SOCK_SEQPACKET | SOCK_STREAM | SOCK_DGRAM [| SOCK_NONBLOCK |
  SOCK_CLOEXEC])` : un canal du noyau, deux poignées → deux descripteurs. `send`, `recv`,
  `sendmsg`, `recvmsg` (iovecs, `SCM_RIGHTS`, `MSG_DONTWAIT`, `MSG_PEEK`, `MSG_TRUNC`,
  `MSG_CTRUNC`, `MSG_CMSG_CLOEXEC`), `read`, `write`, `shutdown`, `getsockopt` (`SO_TYPE`,
  `SO_DOMAIN`, `SO_PEERCRED` : le pid du créateur de la paire, `SO_RCVTIMEO` / `SO_SNDTIMEO`
  appliqués), `getsockname` / `getpeername` (AF_UNIX sans nom), `ioctl (FIONREAD)`, `fstat`
  (`S_IFSOCK`). Fin : `recv` → 0, `send` → `EPIPE` (pas de `SIGPIPE`).
* `memfd_create` (`MFD_CLOEXEC`, `MFD_ALLOW_SEALING`), `shm_open` (`O_CREAT`, `O_EXCL`, `O_TRUNC` ;
  `FD_CLOEXEC`) / `shm_unlink` (noms globaux du noyau), `ftruncate`, `fstat` (`S_IFREG`, taille),
  `fcntl (F_ADD_SEALS / F_GET_SEALS)`, `mmap` : `MAP_SHARED` = `SHM_MAP` (les mêmes pages dans
  chaque process qui projette l'objet), `MAP_PRIVATE` = une copie ; `munmap`, `mprotect`,
  `madvise` comme pour la mémoire anonyme.
* `poll` / `select` : attente dans le noyau (`HANDLE_WAIT`) ; un `shutdown` d'un autre thread
  réveille celui qui attend (ce que fait WebKit pour arrêter son thread de surveillance).
* `posix_spawn` : chaque descripteur LSOCK / SHM sans `FD_CLOEXEC`, après les `adddup2` (la copie
  n'a pas `FD_CLOEXEC`) et `addclose`, est donné au fils à son numéro (`HANDLE_INHERIT` juste
  avant le lancement, sous un verrou du process ; le fils les place au démarrage, `HANDLE_LIST`
  dans `__posix_fd_init`). C'est ce qu'attend `ProcessLauncherOnyx.cpp` : le socket du fils à son
  numéro, donné dans `argv`.

**Pourquoi des objets du noyau à poignées, et pas des nœuds de vfs.** Les trois besoins sont
d'abord des affaires de **mémoire et d'attente**, que seul le noyau sait faire : projeter les mêmes
pages physiques dans plusieurs process (régions `VMK_SHARED` de `vmspace.asm`, fautes de page),
dormir jusqu'à un message sans sonder (les tubes de vfs sont sondés, 1 ms dans `poll`), et faire
passer une référence d'un process à un autre sans qu'aucun service n'ait à suivre qui tient quoi.
Par vfs, chaque message aurait coûté deux `SVC_CALL` (512 octets au plus, copiés deux fois), un
descripteur passé aurait demandé à vfs de connaître le receveur avant la réception, et la
mémoire partagée serait revenue au noyau de toute façon. Les objets à poignées (comme Onyx v76,
Zircon, les *handles* de seL4) donnent au même endroit : le comptage des références (poignées,
messages en vol, régions, nom, liste de lancement), la fermeture à la mort d'un process
(`vm_destroy`), les limites par process (1 023 poignées) et globales (1 024 anneaux de 64 Ko), et
une attente précise (`HANDLE_WAIT`). Le reste du système (fichiers, tubes, services) ne change
pas ; les tubes de vfs restent les entrées / sorties standard des programmes.


## Ce qui manque (ENOSYS, ou sans effet)

* **Processus** : `exec*` garde le pid (section « exec » ; repli : lancer, attendre, finir) ;
  `posix_spawn` : un fichier peut être l'entrée / la sortie d'un fils
  (`adddup2`, `addopen`, descripteurs 0 à 2 : le fils l'ouvre lui-même, sans reprendre la position
  du père) ; `addopen` au-delà de 2 → `ENOTSUP` ; les canaux locaux et la mémoire
  partagée sans `FD_CLOEXEC` passent au fils à leur numéro, `adddup2` / `addclose` compris, mais
  pas les fichiers, tubes ou sockets de NetKit au-delà de 2 (`fork` les donne tous ; un bout de pty passe en 0 à 2,
  hérité ou par `adddup2`) ; `system` et
  `popen` lancent le premier mot (pas de shell).
* **Signaux** : asynchrones, livrés par le noyau (ABI.md, `0x2B`) : `sigaction` avec
  `sa_sigaction` et `SA_SIGINFO`, fautes rattrapables avec `si_addr`, `ucontext_t` modifiable
  (forme de Linux arm64), `pthread_kill` vers un autre thread, masques par thread, `sigaltstack`,
  `sigpending`, `sigsuspend`, `pause`, `sigwait`, `membarrier` (`<sys/membarrier.h>`). `kill`
  vers un autre process : tout signal. `SA_RESTART` n'a pas d'effet : un `sleep`, un `poll` ou un
  `recv` interrompus rendent `EINTR` ; une lecture de terminal ou de tube aussi (en 1 s au plus ;
  un tube : seulement sans `SA_RESTART`), mais une lecture de fichier ou un appel de service vont au
  bout. `SIGPIPE` n'est jamais levé. `SIGCHLD` vient du noyau, à la fin, à l'arrêt et à la
  reprise d'un fils lancé par ce process, seulement si un gestionnaire est installé : sans lui, un
  `sleep` n'est pas interrompu.
* **Mémoire** : `mmap` anonyme = région du noyau peuplée à la demande (`VM_MAP` : réservations
  de plusieurs Go, `MAP_FIXED` dans l'arène `0x58_0000_0000`–`0x68_0000_0000`, `PROT_EXEC`,
  protections appliquées) ; fichier en lecture de 64 Ko à 128 Mo : ses pages sont gardées dans un
  objet nommé d'après lui, une seule fois pour tous les process, en copie sur écriture (`VM_COW`) ;
  il part avec le dernier process qui le projette, sauf s'il est épinglé (`file_pin`, plus bas), et un
  fichier modifié prend un autre nom ; `mprotect` vers l'exécution nettoie le cache de données de
  chaque page (Cortex-A72 : IDC = 0) ;
  autre fichier : copie privée lue puis protégée ; `MAP_SHARED` en écriture → `ENOTSUP`. Sur un noyau sans `VM_MAP` (`E_NOSYS`), repli sur le tas (ni `MAP_FIXED`, ni plus de
  256 Mo, ni `PROT_EXEC`).
* **Garder un fichier en mémoire** (`<sys/mman.h>`, propres à ARMPI) : `file_pin (chemin)` lit le
  fichier une fois et le garde sans process, quelle que soit sa taille (0, ou -1 et `errno`) ;
  `file_unpin (chemin)` rend l'épingle, et l'objet part avec le dernier process qui le projette ;
  `file_pinned (chemin)` dit s'il est épinglé. `mmap` prend alors ses pages, même sous 64 Ko.
  L'objet est trouvé par nom, taille et date du fichier : l'épingle d'un fichier qui a changé
  n'est plus atteignable et reste jusqu'au redémarrage.
* **Fichiers** : une seule date par fichier (`st_mtime`, `st_atime` et `st_ctime` valent la même,
  2 s de précision sur FAT : [FICHIERS.md](FICHIERS.md)), pas de droits sauf le bit d'exécution
  (section plus haut), pas de propriétaires (`chown` sans effet), pas de liens (`link`, `symlink` → `ENOSYS`,
  `readlink` → `EINVAL`), `rename` : sans copie sur un même volume, d'un volume à l'autre un
  fichier est recopié et un répertoire rend `EXDEV` ; `ftruncate` relit et réécrit le fichier, 64 fichiers ouverts au plus par process (AppKit),
  `statvfs` donne des valeurs fixes, pas de verrous (`flock`, `F_SETLK` : 0).
* **IPC** : pas de socket `AF_UNIX` nommé (`socket (AF_UNIX)`, `bind`, `connect` : seulement
  `socketpair`) ; `SCM_RIGHTS` ne porte que des canaux locaux et de la mémoire partagée (un tube de
  vfs, un fichier, un socket de NetKit : `EOPNOTSUPP`) ; `read` / `write` d'un objet partagé :
  `EINVAL` (le projeter) ; `MSG_PEEK` ne rend pas de descripteurs ; pas de futex entre process sur
  une page partagée (`IPC::Semaphore` de WebKit n'est compilé que pour Linux) ; `SO_RCVBUF` /
  `SO_SNDBUF` fixes (64 Ko).
* **Réseau** : IPv4 seulement (AF_INET6, AF_UNIX → `EAFNOSUPPORT`) ; `connect` toujours
  bloquant (rend 0 même sur un socket non bloquant) ; l'autre bout d'un `accept` est inconnu
  (0.0.0.0:0) ; `shutdown` ferme à moitié (TCP : FIN après ce qui reste en file, on reçoit toujours ; ensuite `send` rend `EPIPE`) ;
  `getifaddrs` → `ENOSYS` ; pas de SIGPIPE.
* **poll / select** : canaux locaux et mémoire partagée seuls (64 au plus) : une attente dans le
  noyau (`HANDLE_WAIT`) ; avec d'autres descripteurs, boucle en espace utilisateur (1 ms, les
  canaux réveillent tout de suite) ; un bout d'écriture de tube est
  toujours « prêt » ; l'entrée standard aussi, sauf un pty (vterm : `VT_INFO`, prêt s'il y a des
  octets ou si l'autre côté est parti).
* **Terminaux** ([VTERM.md](VTERM.md)) : `isatty` n'est vrai que sur un bout de pty (plus sur la
  console ni sur un tube). Un `read` sur un pty attend dans vterm, qui rend la main toutes les
  secondes : si un gestionnaire de signal a tourné entre-temps (compteur de `signal.c` : les
  gestionnaires passent par `__posix_sig_tramp`), `read` rend `EINTR` ; Ctrl+C rend la main tout
  de suite. Sur un pty, `TERM=xterm-256color` et `COLORTERM=truecolor` si `TERM` manque ou vaut
  `dumb`. Contrôle des tâches : section plus haut.
* **Processeur** : `getauxval (AT_HWCAP)` rend FP et NEON, rien d'autre : le noyau ne dit pas ce que
  sait le processeur ([OPENSSL.md](OPENSSL.md)).
* **Divers** : `dlopen` échoue proprement (programmes statiques) ; `dl_iterate_phdr` voit le programme seul
  (`dl.c`, en-têtes ELF gardés par `posix.ld`, `PT_GNU_EH_FRAME` par `--eh-frame-hdr`) ; `dladdr` rend le
  programme, sans nom de symbole ; pas de `/proc`, sauf `readlink ("/proc/self/exe")`, déduit d'`argv[0]`
  et de `PATH` (le noyau ne garde pas le chemin), et `access ("/proc")`, qui réussit : SDL et PhysicsFS le regardent d'abord ; `sched_getaffinity` : tous les cœurs,
  `sched_setaffinity` sans effet ; `statfs` : les valeurs fixes de `statvfs` ; `syscall` : seulement
  `gettid`, `getrandom`, `membarrier` (numéros de Linux arm64, `ENOSYS` sinon) ; `fnmatch` (`fnmatch.c`) ; `timer_create` → `ENOSYS`, fuseau
  UTC seulement, une seule identité (uid 0).

`fatfs` (FAT32 `SD0:`, exFAT `SD1:`) : les ouvertures d'un même fichier partagent son état dans
le service (taille, clusters : docs/FICHIERS.md, « fatfs : fichiers ouverts partagés ») — `pread` /
`pwrite` croisés, `O_APPEND` à deux descripteurs, `stat` d'un fichier ouvert ailleurs ; noms
jusqu'à 255 caractères dans le système de fichiers, mais un chemin passe par AppKit et vfs
(≈ 254 octets, « `SD0:/` » compris) : un nom de plus de ≈ 249 octets n'est pas joignable, et
`readdir` rend 255 octets au plus. Sur `RAM0:` (RamFS) les noms sont limités à 63 octets.

## Notes

* **`mmap` sur les appels du noyau** (29/10/2026) : `mman.c` appelle `VM_MAP`, `VM_UNMAP`,
  `VM_PROTECT`, `VM_ADVISE`, `VM_PAGEINFO` (docs/ABI.md `0x06`–`0x0C`) ; les blocs du tas
  (`heap_map`, `heap_find`) ne servent plus que de repli sur un ancien noyau.
* **Piles** : 256 Ko par défaut par pthread (une région de `mmap`, à la demande, sous une page de garde
  `PROT_NONE` : `pthread_attr_setguardsize`, 0 : aucune), 64 Mo au plus ; le
  thread principal a la pile du noyau : **64 Mo** (`USER_STACK_SIZE`, tous les programmes),
  peuplés à la demande (région `VMK_STACK`) : elle ne coûte que ce qui est touché.
* **Arguments et environnement d'un fils** (`posix_spawn`, `exec*`, 30/10/2026) : le noyau ne
  donne qu'une ligne de 1 024 octets (`ARGS_MAX`) et pas d'environnement. `proc.c` met donc
  `argv` (`argv[0]` compris) et `envp` dans un **objet partagé** (`SHM_CREATE`), sans limite de
  longueur, donné au fils par `HANDLE_INHERIT` avec la marque `0x80000000 | pages` ; `env.c` le
  relit au démarrage, puis le rend. La ligne reste remplie pour les programmes en assembleur (vide
  si elle dépasse 1 024 octets) ; lancé par un programme en assembleur, un programme C a la ligne
  et les valeurs par défaut (`PATH=/bin:/gcc/bin`, `TMPDIR=/tmp`, `HOME=SD0:/`). Le même protocole
  existe en assembleur : `f_spawn_env` et `f_start_blob` d'AppKit (`spawnenv.inc`, utilisés par `sh`).
* **Chemins virtuels** : `realpath` garde « `/gcc/bin/gcc` » tel quel (`VFS_PATHQ`) : pas de `:`,
  qui sépare les listes de chemins de GCC et de `PATH` (docs/FICHIERS.md, « Chemins virtuels »).
  Depuis le 05/11/2026, `getcwd` suit : après `chdir ("/apps/x")`, il rend « `/apps/x` » et non
  « `SD0:/apps/x` », et un chemin relatif en part (`path.c`, `cwd_change`). Sinon un programme qui
  compare les deux formes se trompe : Irrlicht monte un dossier par `realpath`, le liste depuis
  `getcwd` et coupait chaque nom à la longueur du premier (SuperTuxKart ne trouvait plus ses modèles).
  Un dossier courant reçu d'un parent garde la forme du système.
* `warn`, `warnx`, `err`, `errx`, `vwarn`, `vwarnx`, `verr`, `verrx` sont faibles : un programme
  peut avoir les siens (binutils).
* **En-têtes de la chaîne** : `pthread.h`, `sys/_pthreadtypes.h`, `semaphore.h`, `sys/dirent.h`
  gardent les dispositions de ceux d'Onyx (libstdc++ de la chaîne a été compilé avec) ; seuls les
  noms internes (`__posix_*`) changent.
* Les commentaires repris tels quels d'Onyx sont restés en anglais ; ce qui est propre à ARMPI est
  commenté en français.

## À vérifier sur le vrai Pi (PocketTerm)

* `posixtest` groupe par groupe et `posixtest-cxx` (vitesse réelle des boucles de `poll` et des
  attentes avec délai ; quatre cœurs réels pour `std::thread`) ;
* `posixtest net` en Wi-Fi (DNS, TCP, UDP vers 8.8.8.8) ;
* `posixtest file SD0:/tmp` sur la vraie carte (20 réussis attendus, comme sous QEMU).
* `posixtest ipc` (27 réussis attendus : canaux, mémoire partagée, un fils tué ; la charge mesure la
  vitesse réelle des anneaux sur quatre cœurs).
