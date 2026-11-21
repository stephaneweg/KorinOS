# L'hôte Linux : Claude Code officiel sur Korin PC

Chantier ouvert le 21/11/2026 à la demande de l'utilisateur : **faire tourner la CLI officielle de Claude Code, non
modifiée, avec son abonnement** (pas de clé d'API), sur Korin PC (x86-64), par l'hôte Linux construit pour Widevine
(`src/lib/linux`, `src/sys/linuxhost`, docs/etude/WIDEVINE.md). Il prolonge la décision du 13/11
(`.agents/memory/armpi-linux-compat.md`) et l'étude du binaire (docs/etude/ETUDE-CLAUDE-NATIF.md).

## 1. Décisions de l'utilisateur (21/11)

| Question | Réponse |
|---|---|
| Claude Desktop | **rien pour l'instant** : Anthropic ne le publie que pour macOS et Windows, l'hôte Linux ne peut pas le lancer |
| Par quoi commencer | **jalons 1 à 3** : un exécutable glibc, puis Node.js, puis Claude Code dans le Terminal ; le frontal après |
| Le but | la **CLI officielle par l'abonnement** ; un frontal Korin (jalon 4) pilotera cette même CLI (`--input-format` / `--output-format stream-json`, connectée au compte de la même façon), jamais une clé d'API |

## 2. Ce qu'il y a (21/11)

* `liblinux` (`src/lib/linux`, 2 700 lignes) : charge une **bibliothèque** `.so` ELF64 x86-64 (`ET_DYN`), la reloge,
  lie ses imports à une glibc de compatibilité servie par libposix (environ 45 des fonctions dont Claude Code a
  besoin), traduit les `syscall ()` de glibc ; `capture.c` : les instructions `syscall` du code Linux renvoyées par le
  noyau (SYS_SYSCALL_CAPTURE, trame SIGSYS ; docs/etude/WIDEVINE.md § 3.2) ; `/proc/self/maps` fabriqué ; TLS
  dynamique (`__tls_get_addr`) ; threads, futex.
* `linuxhost` (`src/sys/linuxhost`) : `--check`, `--self-test`, `FICHIER.so FONCTION`.
* Le noyau du PC : la capture des syscall, l'arène haute `0x1000_0000_0000`–`0x7000_0000_0000` (VM_FIXED permis).

## 3. Ce que demande Claude Code 2.1.330

Binaire officiel de ce conteneur (`/opt/claude-code/bin/claude`, 381 Mo), lu sans l'exécuter sous Korin :

* **exécutable** ELF64 `ET_EXEC` non PIE, interpréteur `ld-linux-x86-64.so.2` ; segments `LOAD` de `0x200000` à
  environ `0x16D6_C000` (365 Mo à adresse fixe) ; `PT_TLS` de 0x5d30 octets (0x25f0 initialisés) ; pile demandée :
  12,2 Mo (`GNU_STACK`) ;
* liaison dynamique : `libc.so.6`, `libpthread.so.0`, `libdl.so.2`, `librt.so.1`, `libm.so.6` ; **521 symboles**
  importés, dont **170 servis** le 21/11 et 341 absents (`python3 -I tools/linux/imports.py BINAIRE --missing`) :
  stdio et ses variantes `_chk`, locale,
  maths, pthread, `dlopen`, sockets, `getaddrinfo`, `getifaddrs`, **epoll**, **eventfd**, **inotify**, `pidfd_spawnp`,
  `posix_spawn*`, `fork`, `execve`, termios, `mount`, xattr, `iconv`, `backtrace`…
* c'est **Bun** : JavaScriptCore avec son compilateur à la volée (pages RWX ou W^X par `mprotect`), beaucoup de
  threads, la boucle d'événements sur epoll, des signaux (à vérifier : JSC suspend des threads par signal pour son
  ramasse-miettes sous Linux), le terminal en mode brut (TUI Ink), BoringSSL intégré (TLS par sockets ordinaires),
  `/proc/self/exe` ; la connexion au compte ouvre une page (xdg-open : Jet) et attend le retour sur `localhost`.

* il **définit lui-même** `malloc`, `free`, `realloc`, `calloc`, `strdup`… (mimalloc) et `sigaction`, `signal`,
  `sigprocmask`, `pthread_sigmask` (ses enveloppes, qui appellent les vraies par `dlsym (RTLD_NEXT)`) : sous Linux,
  la glibc elle-même appelle alors son malloc. La glibc de compatibilité alloue par libposix : tout ce qu'elle rend
  à libérer par l'appelant (`strdup` interne, `getaddrinfo`, `realpath (p, NULL)`, `getline`…) devra passer par le
  malloc de l'exécutable quand il en a un (jalon 3).

## 4. Jalon 1 : un exécutable glibc (fait le 21/11)

`linuxhost run PROGRAMME [arguments]` : un exécutable Linux x86-64 lié à glibc démarre dans le process de l'hôte.
Essai : `sh tools/linux/exectest/run.sh` (QEMU PC, disque d'essai, telnet) : `hello` et `tlstest` compilés par la
gcc d'Ubuntu, chacun en PIE et en ET_EXEC (`-no-pie`, à `0x400000`) : argc, argv, environnement, `getauxval`,
constructeur, `atexit`, code de sortie, variables `__thread` dans le fil principal et 4 threads, `errno` ; tout
**réussi** du premier coup, et `linuxhost --self-test` (les modules, Widevine) aussi.

### 4.1 La carte mémoire

Un exécutable non PIE se charge **à ses adresses** : Claude Code à `0x200000`, le cas habituel des exécutables
`x86-64` (`0x400000` par défaut de ld). Or les programmes du PC sont liés à `0x400000` (`posix.ld`), l'hôte aussi.

* **L'hôte lié à `0x4000_0000`** (1 Go) : pas plus haut, car libposix et la newlib du PC sont compilées dans le
  modèle « small » de x86-64 (des symboles sous 2 Go). `tools/linux/build.sh` fait une copie de `posix.ld` et de
  `posix.specs` du PC (`build/pc/linux/posix-lx.ld`, `posix-lx.specs`) avec cette base ; le reste du système ne
  change pas. Sous l'hôte : `[0x10000, 0x4000_0000)` pour l'exécutable de Linux (Claude Code finit à 0x16D6_C000,
  Node.js vers 0x0800_0000). Le tas de `SBRK` (après l'hôte, jusqu'à `0x50_0000_0000`), l'arène, les tampons et
  l'arène haute ne bougent pas.
* **Le noyau : une zone basse pour `VM_FIXED`** (`core/mm.c`, `in_fixed`) : `[VM_LOW_BASE = 0x10000, image)`, sous
  le plus bas segment de l'image du process (`mm_note_image`, par `elf_load` et `image_map` ; copié au fork) ;
  `VM_MAP`, `VM_UNMAP` et `SHM_MAP` fixes y sont permis, `find_gap` n'y cherche jamais (les adresses habituelles ne
  changent pas). Pour un programme ordinaire (lié à `0x400000`), la zone est `[0x10000, 0x400000)`.
* le `brk` du programme Linux : pas servi (sa glibc de compatibilité alloue par libposix) ; à voir avec Bun.

### 4.2 Le chargeur d'exécutables (`elf.c`)

`linux_load_file` (elf.c) : les en-têtes lus, puis chaque `PT_LOAD` lu par `pread` à sa place dans une réservation
`MAP_FIXED_NOREPLACE` (ET_EXEC) ou quelconque (PIE), puis les droits (`mprotect`) ; `PT_INTERP` ignoré (l'hôte est
l'éditeur de liens) ; les imports (`JUMP_SLOT`, `GLOB_DAT`, `64`) liés à la glibc de compatibilité, `COPY` (les
données de la libc copiées dans l'exécutable : `stdout`, `stdin`, `stderr` ; `environ` posé au démarrage),
`TPOFF64` de ses propres variables. `linux_exec` (exec.c) : la pile du programme (`PT_GNU_STACK`, 8 Mo au moins ;
argc, argv, envp, auxv : `AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_PAGESZ`, `AT_BASE`, `AT_ENTRY`, `AT_*ID`,
`AT_PLATFORM`, `AT_HWCAP`, `AT_CLKTCK`, `AT_SECURE`, `AT_RANDOM`, `AT_EXECFN` ; `getauxval` les lit), puis un saut à
`_start` (rsp sur argc, rdx = 0), qui appelle `__libc_start_main` (elf.c : `environ`, les constructeurs
`DT_PREINIT_ARRAY`, `DT_INIT`, `DT_INIT_ARRAY` quand `init` est NULL comme depuis glibc 2.34, `main`, `exit` ; à la
fin, ses `__cxa_atexit`, `DT_FINI_ARRAY`, `DT_FINI`). Le process finit avec le programme. L'exécutable est déclaré
à la capture de syscall comme un module.

### 4.3 Le TLS de l'exécutable

Un exécutable a son bloc TLS **juste sous `%fs`** (variante II ; décalages fixés à l'édition des liens : Claude Code
lit `%fs:-0x5d30…`), et l'hôte aussi (libposix, liblinux). Les deux se recouvriraient. Solution retenue : **réserver
dans le TLS de l'hôte, en haut, une zone vide** : `linux_exe_tls_pad` (exectls.S), 64 Ko dans une section
`.lxexe_tls` que `posix-lx.ld` place en dernier dans `.tbss` ; exec.c vérifie qu'elle finit à `%fs`. Les variables
de l'hôte sont plus bas, le bloc de l'exécutable (au plus 64 Ko, alignement ≤ 64) se pose dans
`[%fs - taille, %fs)`. Chaque thread : le `.tdata` de l'exécutable recopié là (le fil principal par `linux_exec` ;
les autres par `linux_thread_hook`, appelé par un tremplin du `pthread_create` de compatibilité). exec.o (et donc
la réserve) n'est lié que dans un hôte qui appelle `linux_exec` : pas dans cdmhost. Le TCB à `%fs` est celui de libposix : `%fs:0` (soi-même), `%fs:0x28` (canari),
`%fs:0x30` (garde des pointeurs) sont lus par le code Linux (déjà vrai pour Widevine).

## 4bis. Claude Code démarre (21/11)

Le même jour : **`claude --version` rend `2.1.330 (Claude Code)` et `claude --help` l'aide complète, code 0**, sous
QEMU PC (`EXTRA=/opt/claude-code/bin/claude MEM=4G ONLY='sleep:10|@200 /bin/linuxhost run /home/lx/claude --version'
sh tools/linux/exectest/run.sh` ; 4 s et 10 s). Les 521 imports sont servis (`tools/linux/imports.py` : 512, et 9
faibles) :

* `glibc_libc.c` : maths, ctype, caractères larges, locale « C » (`__newlocale`…, `nl_langinfo` avec les nl_item de
  glibc), stdio, `getline`, `realpath`, `asprintf`, gettext, `quick_exit`, `sysinfo`, `program_invocation_name`…
  Ce qui est rendu à libérer par l'appelant vient du malloc de l'exécutable quand il en définit un
  (`lx_umalloc`, posé par `linux_exe_start` : Bun a mimalloc).
* `glibc_sys.c` : les enveloppes d'appels système (une forme `SC (nom, numéro)` pour toutes), et les structures de
  glibc : `sigaction` et `sigset_t` de 128 octets, `signal` et ses formes BSD et System V (Bun les enveloppe et
  appelle les vraies par `dlsym (RTLD_NEXT)`), DIR et `readdir` sur `getdents64`, `__xstat`…, `statvfs`,
  `posix_spawn` (sur celui de libposix ; un programme de Linux passe par `linuxhost run`), `passwd` et `group`
  (uid sur 32 bits), `getcwd (NULL, 0)`.
* `syscall2.c` : la traduction des appels de Linux, pour les enveloppes et pour les `syscall` directs de Bun :
  fichiers (`openat`, `newfstatat`, `statx`, `getdents64`, `fcntl`, `ioctl` du terminal, `*at`, `utimensat`,
  `statfs`, `/proc/self/exe`), `select`/`poll`/`ppoll`, epoll (`epoll_event` de 12 octets), **eventfd** (une paire
  de sockets locales et un compteur), process (`fork`, `execve` d'un programme de Linux par l'hôte, `wait4`,
  `waitid`, `kill`, `tgkill`), **signaux réels** (`rt_sigaction` par un tremplin : numéros traduits, le siginfo et
  l'ucontext du noyau du PC ont déjà la disposition de Linux ; le gestionnaire de plantage de Bun marche),
  `rt_sigprocmask`, `sigaltstack`, sockets (mêmes constantes), `mremap`, `close_range`… Les modules (Widevine)
  gardent leur `rt_sigaction` « gardé, pas installé ».
* `elf.c` : `dlopen` (libc, libpthread… : la compatibilité ; un chemin : un module), `dlsym` (RTLD_DEFAULT,
  RTLD_NEXT, l'exécutable), `dladdr`, `dl_iterate_phdr`, `dlerror`.
* traces : `LINUX_TRACE_SIG=1` (chaque signal, `=2` les fautes : adresse, rip, chaîne des rbp), `LINUX_TRACE_DL=1`
  (les `dlsym` et `dlopen` manqués) ; un appel non servi est dit une fois.

Pas encore : inotify, timerfd, signalfd, pidfd, `timer_create`, clone3 (ENOSYS ; Bun a des replis, à vérifier quand
il s'en sert), `brk`.

## 4ter. Le réseau, `-p` et l'interface dans le Terminal (21/11, suite)

* **`claude -p bonjour`** sans compte : `Not logged in · Please run /login`, code 1 — après un vrai aller-retour
  HTTPS (DNS, TCP, TLS de BoringSSL par les sockets de netd). Dans ce conteneur, le réseau sortant passe par un
  proxy qui intercepte TLS avec sa propre autorité : pour les essais, `NODE_EXTRA_CA_CERTS=` son certificat (rien
  de tel sur le PC de l'utilisateur).
* **L'interface interactive** (Ink) **s'affiche dans le Terminal de Korin** : accueil, choix du thème avec l'aperçu
  de diff en couleurs, puis l'écran de connexion (« Claude account with subscription » / Console / tiers), l'URL
  d'autorisation et « Paste code here ». Essai : `APPS="terminal jet" QEMU_EXTRA="-device usb-tablet,bus=xhci.0"`
  et `click:`/`type:`/`key:`/`shot:` dans `ONLY` (tools/linux/exectest/run.sh ; le Terminal du PC :
  `sh tools/pc/apps.sh terminal`).
* **La connexion par l'abonnement ouvre Jet** sur `https://claude.com/cai/oauth/authorize?…` : Claude Code ne lance
  un navigateur sous Linux que si `DISPLAY`, `WAYLAND_DISPLAY` ou `BROWSER` est posé ; `linuxhost run` pose
  `BROWSER=xdg-open`, et `src/sys/linuxhost/xdg-open` (un script : une adresse web → `/apps/jet.app/main ADRESSE`,
  détaché) l'ouvre dans Jet. La page elle-même ne charge pas dans le conteneur (même proxy) : à essayer sur le PC.
* corrigé en route : `madvise (MADV_DONTNEED)` rend vraiment les pages (libpas, mimalloc comptent sur des zéros) et,
  sur l'image d'un exécutable lu d'un fichier, **les relit depuis le fichier** comme une projection privée de Linux
  (Bun rend ainsi des pages de son code embarqué `.bun` : sans relecture, `SyntaxError: Invalid character '\0'`) ;
  `/proc/self/{statm,stat,status}`, `/proc/{meminfo,cpuinfo,stat,uptime,loadavg,version}` fabriqués ;
  `readlink ("/proc/self/fd/N")` d'un dossier (Bun : le vrai chemin du dossier courant) ; `preadv2`, `pwritev2`,
  `close_range`, `quick_exit`, `execl*`.
* traces : `LINUX_TRACE_SYS=fichier` (chaque appel de Linux, chemins et événements d'epoll ; dans `/tmp`, la trace
  sur un disque ralentit trop), `LINUX_TRACE_EXIT=1` (qui termine le process). Le disque d'essai est en snapshot :
  un fichier se lit par telnet (un petit script dans `EXTRA`, le `|` sépare les commandes de `ONLY`).
* reste vu : `claude auth status` sort en silence, code 0 (sa boucle d'événements se croit vide avant d'écrire ;
  `-p` et l'interface vont plus loin) ; `link` rend ENOSYS (FAT : à rendre EPERM).

## 5. Jalons suivants

**La suite du jalon 1, puis le 2** : les 341 imports absents de Claude Code. La plupart sont des enveloppes
d'appels système ; Bun fait aussi des `syscall` directs (la capture les envoie à `linux_syscall`). D'où la règle :
**la traduction vit dans syscall.c** (structures de Linux : `stat`, `statx`, `dirent64`, `termios`, `sigaction`,
`sigset_t` de 128 bits, `epoll_event`, `sockaddr`, `msghdr`…), et l'import de glibc qui n'est qu'une enveloppe
appelle `linux_syscall` (une table nom → numéro) ; le reste (stdio, locale, maths, `getaddrinfo`, `iconv`,
`posix_spawn`, `dlopen`) dans glibc*.c, par libposix. Les essais grandissent dans `tools/linux/exectest` (fichiers,
sockets, epoll, eventfd, signaux, fork et exec, termios) avant Node.js.

* **2. Node.js officiel** (linux-x64, glibc) : `node -e`, puis un petit serveur ; valide libuv (epoll, eventfd,
  threads, signaux) avant Bun.
* **3. Claude Code** : `claude --version`, `claude -p` (connexion par l'abonnement : la page d'Anthropic dans Jet, le
  retour sur la boucle locale), puis l'interface dans le Terminal de Korin (xterm-256color). Les outils qu'il lance
  (`bash`, `git`, `rg`) : ceux de Korin. Mise à jour automatique coupée.
* **4. Le frontal Korin** : une app UIKit qui pilote la CLI en `stream-json`.
* **5. L'installation** : comme Widevine (cdmupdate), le binaire n'entre jamais dans nos paquets ; un installateur le
  télécharge chez Anthropic, vérifie son empreinte, et le pose dans le dossier de l'utilisateur.

## 6. Risques

* JSC et les signaux (suspension des threads pour le ramasse-miettes) : la capture et la livraison des signaux
  Linux à un thread précis ;
* les `syscall` directs de Bun (au-delà de glibc) : la capture les attrape, il faut les traduire ;
* la taille : 381 Mo d'image, une pile de 12 Mo, un tas de plusieurs centaines de Mo ;
* les conditions d'Anthropic : binaire officiel non modifié, utilisé avec son propre compte.
