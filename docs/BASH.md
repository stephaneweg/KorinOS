# GNU bash sur Korin OS

Étape 5 de [TERMINAL-ET-FORK.md](TERMINAL-ET-FORK.md) (10/11/2026) : **GNU bash 5.2.37**, avec le contrôle des tâches depuis l'étape 6 (11/11/2026)
(étape 6), sur libposix et newlib, lié en statique. `SD0:/bin/bash` ; le **Terminal l'ouvre s'il est sur la carte**,
sinon le `sh` natif ([SHELL.md](SHELL.md)). Paquet `bash` (`tools/pkg/packages.ini`).

## Construire

```sh
sh tools/posix/build.sh            # libposix d'abord
sh tools/bash/build.sh [archive]   # -> prebuilt/bash (copié sur SD0:/bin/bash par build.ps1, carte_linux.py)
TESTS=1 sh tools/bash/build.sh     # et build/bash/tests : la suite d'essais, avec ses aides
```

* **Source** : l'archive telle quelle de bash 5.2.37 (`BASH_TAR`, par défaut `/opt/src/bash/bash_5.2.37.orig.tar.xz`,
  celle de Debian : ftp.gnu.org n'est pas joignable d'ici ; SHA-256 de l'archive xz :
  `370704c9c859f4060b7df19055e43bb9b5fa09d887699cf6ba87885c5485d36a`), dépaquetée hors du dépôt (`WORK`, par défaut
  `~/.cache/korin-bash`).
* **Compilateur** : `aarch64-onyx-elf-gcc -specs=build/posix/lib/posix.specs` (un relais `korin-cc`), `-O2`.
  `-fcommon` (readline et termcap définissent tous deux `PC`, `UP`, `BC`) et `-Wno-error=implicit-function-declaration`
  (`parse.y` appelle `count_all_jobs` sans `jobs.h` quand le contrôle des tâches est absent ; GCC 14 en fait une erreur).
* **configure** : `--host=aarch64-onyx-elf --build=x86_64-pc-linux-gnu --without-bash-malloc --disable-nls
  --enable-job-control --enable-static-link --without-curses` (readline et termcap de bash). Ce qu'il ne peut pas
  essayer en compilation croisée est donné : `ac_cv_func_fork=yes` (et `vfork`), `bash_cv_func_sigsetjmp=present`,
  `bash_cv_getcwd_malloc=yes`, `bash_cv_getenv_redef=no` (le `setenv` de bash, avant `main`, avalerait le
  `TERM=xterm-256color` que libposix pose sur un pty), `bash_cv_dev_fd=absent` et `bash_cv_sys_named_pipes=missing` (pas de `/dev/fd` ni de
  FIFO : pas de substitution de process `<(...)`), `bash_cv_termcap_lib=gnutermcap`, `bash_cv_wexitstatus_offset=8`,
  etc. (`tools/bash/build.sh`).
* **Retouches** (`tools/bash/patches/`, petites et commentées en tête) :
  * `01-chemins-volume.patch` : un chemin qui commence par un volume (`SD0:/a`, `RAM0:`) est absolu (`ABSPATH` de
    `general.h`) ; `pathcanon.c` et `pathphys.c` gardent ce préfixe comme racine : `cd SD0:/home`, `cd ..`, `$PWD`,
    `\w` de l'invite. Calqué sur le cas des lecteurs `C:` de Cygwin ;
  * `02-config-korin.patch` : `PATH` par défaut `/bin:/gcc/bin:.` (chemins virtuels, sans « `:` » de volume) ;
    `SYS_BASHRC` = `/etc/bash.bashrc`.
* **Binaire** : 1,1 Mo une fois dépouillé (1 117 328 octets).

Ce qui a été ajouté à **libposix** pour bash : `sigsetjmp` / `siglongjmp` (`<setjmp.h>`), `tcflow`, `getgrnam`,
`getgrent` / `setgrent` / `endgrent`, `siginterrupt` déclaré, `pw_shell` = `/bin/bash` ; et l'étape 5 elle-même :
bit d'exécution (`stat`, `access(X_OK)`, `chmod`), scripts `#!`, `alarm` / `setitimer` (`SIGALRM`), lecture de tube
interrompue par un signal ([POSIX.md](POSIX.md)). `getpwnam`, `wait3`/`waitpid` (`WNOHANG`), `getrusage`, `times`,
`strsignal`, `getrlimit` (pour `ulimit`), `select` (pour `read -t`) existaient déjà.

## La carte

| Fichier | Rôle |
|---|---|
| `SD0:/bin/bash` | le shell |
| `SD0:/etc/bash.bashrc` | lu par tout bash interactif : l'invite `korin:\w\$` en couleur, l'historique |
| `SD0:/etc/termcap` | l'entrée `xterm-256color` du Terminal (et `dumb`) pour la bibliothèque termcap de bash : déplacements, effacements, touches |
| `SD0:/etc/exec.txt` | les dossiers d'exécutables hors des règles ([FICHIERS.md](FICHIERS.md)) |
| `~/.bashrc`, `~/.bash_history` | ceux de l'utilisateur (`HOME`, par défaut `SD0:/`) |

Le **Terminal** lance `SD0:/bin/bash` (sinon `SD0:/bin/sh`) sur l'esclave du pty, en fait un groupe à lui et le met
**au premier plan du pty** (`setpgid`, `VT_SPGRP`, comme `login` sous Unix) : sans contrôle des tâches, bash ne le
fait pas lui-même, et Ctrl+C n'atteindrait personne. `exit` (ou Ctrl+D) ferme le Terminal.

## Ce qui marche (QEMU, 10/11/2026)

Dans le Terminal (captures de l'essai) : l'invite avec `\w`, readline (flèches, retour arrière, historique par la
flèche du haut, complétion d'une commande et d'un chemin par Tab), tubes, `$(...)`, documents en ligne, fonctions,
`for` / `while` / `case`, arithmétique, motifs, redirections (`> f`, `2>&1`), arrière-plan `&` puis `wait` (sans
contrôle des tâches), un script `#!` rendu exécutable par `chmod +x` puis lancé par `./script`, `read -t 1` (fin du
délai : 142), Ctrl+C qui arrête la commande en cours sans tuer bash, `exit` qui ferme le Terminal. Les mêmes choses
dans un script lancé par `bash script` (`SD0:/t5/t1.sh` de l'essai), tableaux, tableaux associatifs, `${v^^}`,
`trap`, sous-shells.

Le `sh` natif reste le shell du Terminal quand bash manque (même essai, Ctrl+C compris). `gcc` du paquet 1.0.8,
lancé par bash, compile `hello.c` (le pilote trouve `cc1`, `as`, `ld` par les dossiers de `exec.txt`) ; le
résultat demande `chmod +x` (voir plus bas).

## Ce qui ne marche pas (encore)

* **Une sortie redirigée vers un fichier, partagée avec un programme en assembleur** : `{ echo a; cat f; echo b; } > out`
  perd une partie de la sortie. Le programme en assembleur rouvre le fichier lui-même (bout-fichier de vfs, en ajout) :
  il ne partage pas la position du descripteur de bash, qui récrit par-dessus (limite de l'étape 4,
  [FICHIERS.md](FICHIERS.md) « Redirections vers un fichier » ; un programme C la partage). C'est l'échec de
  `run-herestr`.
* **Contrôle des tâches** (`jobs`, `fg`, `bg`, Ctrl+Z, `kill %n`) : en place depuis l'étape 6, vu sous QEMU le
  11/11/2026 dans le Terminal (`sleep 30`, Ctrl+Z, `jobs`, `bg`, `fg`, Ctrl+C, `sleep 20 &`, `kill %1`).
* **Substitution de process** `<(...)`, `>(...)` : ni `/dev/fd` ni FIFO.
* **`[[ x =~ regex ]]`** : newlib n'a pas `regcomp` (bash le compile sans).
* **Locale** : `--disable-nls`, `LANG=C.UTF-8` ; les caractères multi-octets passent, sans traduction des messages.
* **`ulimit`** : ce que `getrlimit` de libposix rend (valeurs fixes).
* **`/usr/bin/env`** n'existe pas : un script doit commencer par `#!/bin/bash` (ou n'avoir pas de `#!` : `/bin/sh`,
  le `sh` natif, le lance).
* **Le bit d'exécution** : seuls les fichiers des règles de chemin et ceux marqués par `chmod +x` (ou créés avec un
  bit `x` par un programme lié à la nouvelle libposix) s'exécutent. Un programme lié par le paquet `gcc` 1.0.8
  (l'ancienne libposix dans `ld`) n'est pas marqué : `chmod +x` ([GCC.md](GCC.md)).
* `ls -l` ne montre pas le bit d'exécution.
* Une lecture de terminal ou de tube n'est interrompue par un signal qu'au bout d'une seconde au plus (vterm et vfs
  rendent la main toutes les secondes) ; `SA_RESTART` n'a d'effet que pour les tubes.

## Essais

`TESTS=1 sh tools/bash/build.sh` met `tests/` de bash et ses aides (`recho`, `zecho`, `printenv`, `xcase`, compilés
pour Korin OS) dans `build/bash/tests` ; copiés sur la carte (`SD0:/t5/tests`), un script les lance un par un par
`bash ./run-<nom>` (`THIS_SH=/bin/bash`, `BASH_TSTOUT` dans `RAM0:/tmp`) : un essai réussit quand `diff` ne trouve
aucun écart avec `<nom>.right` (le fichier de sortie est alors effacé).

Sous QEMU (10/11/2026), 40 essais : **15 réussis** (`arith`, `func`, `quote`, `braces`, `dollars`, `precedence`,
`strip`, `invert`, `parser`, `posixpat`, `rhs-exp`, `comsub`, `getopts`, `mapfile`, `nameref`), 25 en échec, pour
l'essentiel à cause de l'environnement et non de bash :

| Cause | Essais |
|---|---|
| commandes absentes de `SD0:/bin` : `od`, `awk`, `expr`, `egrep`, `chgrp`, `mkfifo`, `locale` | `case`, `nquote`, `more-exp`, `iquote`, `set-e`, `array`, `test`, `glob-test`, `read` |
| commandes natives plus pauvres : `tr` sans `\012` octal, `cat` sans `-v` | `exp-tests`, `printf` |
| sortie redirigée partagée avec un programme en assembleur (position non partagée) | `herestr`, `heredoc`, `type`, `redir` |
| substitution de process `<(...)`, `/dev/fd` | `varenv`, `new-exp` |
| `[[ =~ ]]` (pas de `regcomp`) | `cond` |
| contrôle des tâches (`set -o monitor`, `checkjobs`) | `shopt`, `trap` |
| essais lancés en `root`, pas de `/usr`, pas de locale `zh_HK` | `test`, `tilde`, `glob-test` |
| `cp` ne garde pas le bit d'exécution (§5.4 de l'étude ; `cp` d'Unix le garde, sans `-p`) : `cp bash $TMPDIR/sh` puis `$TMPDIR/sh` | `posixexp` |
| autres (non analysés) | `assoc`, `ifs`, `lastpipe` |

`dstack` et `histexp` n'ont pas de `run-*` de ce nom dans 5.2.37. Ce qui s'y voit côté système : la réserve de 32
tubes de vfs s'épuisait (`pipe error`) ; elle est passée à 128.
