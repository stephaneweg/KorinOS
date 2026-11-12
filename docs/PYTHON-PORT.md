# CPython sous Korin : le portage (étapes 1 à 3)

Suite de l'étude [PYTHON.md](PYTHON.md) (§2 construction, §2.5 bis `dlopen`, §9 étapes). Commencé le 11/11/2026 :
**CPython 3.14.10** tourne sous QEMU, avec OpenSSL, zlib, SQLite, expat, libffi et libmpdec, la bibliothèque standard
dans un zip, et `python3` hôte de `dlopen` (les modules d'extension se chargent en `.so`). Étape 3 le 12/11/2026 :
`tree_sitter`, `tree_sitter_bash` et `_cffi_backend` en `.so`, pip sur la carte, un paquet en C compilé sur la carte
par `/gcc` ([section « Étape 3 »](#étape-3--modules-natifs-et-pip-12112026)). **Pas encore essayé sur le Pi.**

## Ce qui est livré

| Où | Quoi |
|---|---|
| `tools/python/build.sh` | la construction (WSL), de l'archive officielle à `build/python/sdroot` |
| `tools/python/config.site` | les réponses que `configure` ne peut pas obtenir en croisé ; `epoll` coupé |
| `tools/python/korin.patch` | trois retouches de CPython (plus bas) ; `tools/python/mkpatch.sh` le refait depuis deux arbres |
| `tools/python/mkzip.py` | la bibliothèque standard en zip, sources et `.pyc` en « unchecked-hash » |
| `tools/python/testimage.py` | la carte d'essai de QEMU (`build/python/sd.img`) : mot de passe d'accès « essai », `Lib/test` et les modules d'essai |
| `tools/python/tests/step2.py` | les essais de l'étape 2 hors de `regrtest` |
| `tools/python/build-ext.sh` | étape 3 : un paquet C de PyPI (ou un dossier) en roue pour Korin, modules en `.so` |
| `tools/python/site-packages.sh` | étape 3 : `site-packages` de la carte préparé sur le PC (voie A), et `/python/pip.conf` |
| `tools/python/tests/step3.py` | les essais de l'étape 3 |
| `tools/gcc/build.sh` | `STAGES=posix` : libposix, `dlmod.o`, `dlhost*.ld` et les specs de `/gcc` refaits seuls (`gcc -shared`, `-rdynamic` sur la carte) |
| `build.ps1` | recopie `build/python/sdroot` sur la carte |
| `skel/sd0/etc/paths.txt` | `/python = SD0:/python` (8 lignes sur 16) |

Sur la carte :

| Chemin | Contenu | Taille |
|---|---|---|
| `SD0:/bin/python3` | l'interpréteur, statique, hôte de `dlopen` (`-rdynamic`) ; tous les modules de la bibliothèque standard dedans (90 modules intégrés), OpenSSL compris | 17,3 Mo (dépouillé) |
| `SD0:/python/lib/python314.zip` | la bibliothèque standard (556 modules), sources et `.pyc`, compressée | 7,5 Mo |
| `SD0:/python/lib/python3.14/lib-dynload/` | les modules en `.so` (vide sur la carte livrée) | |
| `SD0:/python/lib/python3.14/site-packages/` | les paquets : pip, setuptools, pycparser, cffi, tree-sitter, tree-sitter-bash (étape 3) | 29 Mo |
| `SD0:/python/pip.conf` | la configuration de pip pour tout le système : cache dans `/python/cache/pip` | |
| `SD0:/python/lib/python3.14/_sysconfig_vars__linux_.json` | le `sysconfig` de la carte (aussi dans le zip, en `.py`) | |
| `SD0:/python/include/python3.14/` | les en-têtes, pour compiler des modules sur la carte (`/gcc`) | |

`sys.prefix` = `/python` (chemin virtuel), `sys.executable` = `/bin/python3`, `sys.platform` = `"linux"`,
`platform.system()` = `"ARMPI"` (le `uname` de libposix).

## Construction

```sh
sh tools/posix/build.sh          # libposix (une fois, ou après un changement de libposix)
sh tools/python/build.sh         # ≈ 8 min sur 16 cœurs, OpenSSL compris la première fois
```

Puis `.\build.ps1` (la carte) ; pour les essais, `python -I tools/python/testimage.py`.

* **Sources** : `Python-3.14.10.tar.xz` de `https://www.python.org/ftp/python/3.14.10/` (sha256 `de414a57...`, le même
  que sur la page de la version), libffi 3.8.0 de `https://github.com/libffi/libffi/releases` (sha256 `7da3e2d9...`, le
  condensé publié par GitHub). Téléchargées dans `~/.cache/armpi-python/dl`, jamais dans le dépôt. zlib 1.3.1 et
  SQLite 3.50.4 viennent de `src/extra` du dépôt principal (`EXTRA=...` sinon), OpenSSL 3.5.9 de `build/openssl`
  (`tools/openssl/build.sh`, lancé s'il manque). expat et libmpdec : les copies fournies par CPython.
* **Python de construction** : le 3.14.4 de WSL (`/usr/bin/python3.14`) ; CPython n'exige que le même 3.14.
* **`configure`** : `--host=aarch64-unknown-linux-gnu`, `CC="aarch64-onyx-elf-gcc -specs=build/posix/lib/posix.specs"`
  (la spec dans `CC` : deux `-specs` se contredisent), `--prefix=/python`, `--disable-shared`, `--disable-ipv6`,
  `--without-ensurepip`, `--without-readline`, `--without-mimalloc` (pymalloc : mimalloc suppose trop de choses de
  `mmap` et divise par `CLOCKS_PER_SEC / 1000`, qui vaut 0 avec newlib), `--with-openssl=build/openssl`,
  `--enable-test-modules`. `MODULE_BUILDTYPE=static` : la bibliothèque standard dans l'interpréteur.
  `LINKFORSHARED=-rdynamic` : l'hôte de `dlopen`. `-DTHREAD_STACK_SIZE=0x400000` (4 Mo par thread Python).
  `PKG_CONFIG_LIBDIR` ne voit que nos bibliothèques (sinon celles d'Ubuntu).
* **Modules d'essai en `.so`** : `Modules/Setup.local` reprend les lignes `_testcapi`, `_testinternalcapi`,
  `_ctypes_test`, `xxlimited`... de `Setup.stdlib.in` sous `*shared*` (la première règle d'un module gagne). Ils vont
  dans `build/python/tests/lib-dynload` avec `Lib/test`, seulement sur la carte d'essai.
* **Non-régression de libposix** (QEMU) : `posixtest` ipc 28, net 24, proc 14, signal 18, dl 26, `posixtest-cxx` 82,
  `ttytest` 34, `jobtest` 40 : tous réussis.
* **libffi** : `-DFFI_MMAP_EXEC_WRIT=1` : hors de Linux et de Windows, libffi prend ses fermetures à `malloc`, dans le
  tas, qui n'est pas exécutable sous Korin (faute d'instruction au premier rappel `ctypes`) ; avec ce réglage, des pages
  `PROT_EXEC` par `mmap`, que Korin permet. Sans les trampolines statiques de Linux (`--disable-exec-static-tramp`).
* **SQLite** : `SQLITE_THREADSAFE=1`, FTS5, fonctions mathématiques, sans extensions chargeables.
* **`sysconfig` de la carte** : après `make install`, `_sysconfigdata__linux_.py` et `_sysconfig_vars__linux_.json`
  sont réécrits pour la carte : `CC=gcc`, `LDSHARED=gcc -shared`, `CCSHARED=-fPIC`, `LINKFORSHARED=-rdynamic`,
  `EXT_SUFFIX=.cpython-314.so` (pas de triplet : `configure` le lit de `__linux__`), plus aucun chemin du PC.
  `build_ext` de setuptools compilera donc un module sur la carte avec le GCC de `/gcc` (ses specs sont celles de
  Korin, [GCC.md](GCC.md)) en `-fPIC -shared`. Pas encore essayé sur la carte (étape 3).

### `korin.patch`

| Fichier | Pourquoi |
|---|---|
| `Python/fileutils.c` | newlib donne le `strerror_r` GNU (`char *`) sous `_GNU_SOURCE` ; CPython ne le sait que de la glibc et d'Android |
| `Modules/timemodule.c` | le `clockid_t` de newlib est un `unsigned long` (8 octets) ; CPython le lit en `int` (assertion de taille) |
| `Modules/getpath.py` | `python3` est dans `/bin`, la bibliothèque seulement dans `/python/lib/python314.zip` : sans retouche, `getpath` cherche `os.py` sous le préfixe compilé et avertit « Could not find platform independent libraries » à chaque lancement |
| `Lib/sysconfig/__init__.py` | (étape 3) `get_platform()` lit `uname` : `ARMPI` donnait `armpi-1-aarch64`, et les roues faites sur la carte auraient eu ce nom de plateforme ; `armpi` y vaut `linux` : `linux-aarch64`, comme `sys.platform` et les roues faites sur le PC |

Tout le reste est venu de libposix et de vfs (section « Ajouts pour CPython » de [POSIX.md](POSIX.md)) : `libdl.a`
et compagnie vides, `SSIZE_MAX`, les constantes de `syslog` et de `termios`, `timezone`/`daylight`, `openpty`/`forkpty`,
`chroot`, `waitid (WNOWAIT)`, la fin d'un esclave de pty vue par le maître ; puis, trouvés par les essais : `poll`
juste sur un tube plein (op `V_PIPE_ROOM` de vfs), `bind` sur le port 0, `setenv`/`unsetenv` qui vérifient le nom,
`_SC_TTY_NAME_MAX`, `ESHUTDOWN`, l'action par défaut d'un signal qui tue le process (`WIFSIGNALED`). **Ces correctifs
de libposix et de vfs sont dans main** (12/11/2026, `7bf4fc18`, `f0293f88`, `57cf62a5`) ; le port de Python lui-même
est sur la branche `claude/cpython-port-1b676a`.

### Modules absents

`_curses`, `_curses_panel`, `readline` (le REPL de 3.14 est en Python, `_pyrepl`, et n'en a pas besoin), `_dbm`,
`_gdbm`, `_tkinter`, `_uuid` (`uuid` marche en Python), `_bz2`, `_lzma`, `_zstd` (à ajouter quand un paquet en a
besoin : `tarfile` lit le `.tar.gz` sans eux).

## Essais (QEMU, 11/11/2026)

Carte : `build.ps1`, puis `tools/python/testimage.py` ; QEMU par `tools/qemu_drive.py --fwd 2360:23 --sd
build/python/sd.img` ; commandes par telnet (`ARMPI_REMOTE_PW=essai python -I tools/armpi_remote.py telnet
127.0.0.1:2360 "@600 commande"`).

### Étape 1

| Essai | Résultat |
|---|---|
| `python3 -c "print(6*7)"` | `42` |
| `sys.version`, `sys.platform` | `3.14.10 (main, Nov 11 2026) [GCC 14.2.0] linux` ; `sys.path` : `''`, `/python/lib/python314.zip`, `/python/lib/python3.14`, `lib-dynload`, `site-packages` |
| le REPL par telnet (pty de vterm) | invite en couleur, flèches (modes normal et application), édition dans la ligne, historique, Ctrl+C → `KeyboardInterrupt` puis la main revient |
| le REPL dans l'app Terminal (QEMU, clavier AZERTY) | `x = 5`, flèche haut, retour arrière, `7`, Entrée : `x = 7` ; `x` → `7` ; l'historique remonte |

### Étape 2 : `tools/python/tests/step2.py --net`

**17 réussis sur 17** :

| Essai | Résultat |
|---|---|
| modules natifs | `_ssl`, `_hashlib`, `_sqlite3`, `zlib`, `_ctypes`, `_decimal`, `pyexpat`, `_elementtree`, `_asyncio` dans l'interpréteur |
| **`_testcapi` en `.so`** | chargé par `dlopen` depuis `lib-dynload` (avec `_testinternalcapi`, `_testmultiphase`, `_testsinglephase`, `_ctypes_test`) |
| `sysconfig` | `CC=gcc`, `LDSHARED=gcc -shared`, `CCSHARED=-fPIC`, `EXT_SUFFIX=.cpython-314.so` |
| `hashlib`, `ssl` | SHA-256 et SHA3 d'OpenSSL ; `ssl.create_default_context()` charge 121 certificats (`/etc/ca-bundle`) |
| zlib, gzip, sqlite3 | justes (une base sur `/tmp`, rouverte) |
| `ctypes` | `CDLL(None)` : `getpid`, `snprintf` de l'hôte ; `qsort` avec un rappel Python (fermeture libffi) |
| `subprocess.run(["bash", "-c", "ls / \| cat"])` | marche ; un `python3` fils, son code de sortie (3) |
| **`os.replace` sur une cible existante** | marche sur `/tmp` (RAM0:) et sur la carte (fatfs) |
| threads, asyncio | serveur et client TCP sur 127.0.0.1, sous-process, `PollSelector` ; `bind` sur le port 0 donne un port éphémère (correctif de libposix ; avant : `EINVAL`) |
| `pty.fork` | le fils écrit dans le pty, le père lit jusqu'à `EIO` |
| `urllib.request.urlopen("https://api.mistral.ai")` | **HTTP 404** : une réponse HTTPS, certificat vérifié |

### Étape 2 : `python3 -m test <suite>` (une suite à la fois)

Chaque suite par `python3 -m test <suite> --timeout 900 -W`, une session telnet par suite. Résultats après les
correctifs de libposix et de vfs (POSIX.md, « Ajouts pour CPython ») :

| Suite | Lancés | Échecs (erreurs) | Sautés | Ce qui échoue, et pourquoi |
|---|---|---|---|---|
| `test_json` | 225 | 0 | 5 | — |
| `test_decimal` | 739 | 0 | 8 | — |
| `test_pty` | 6 | 0 | 0 | — |
| `test_sqlite3` | 514 | 1 | 5 | deux process sur la même base (`MultiprocessTests`) : **pas de verrous de fichiers** sous Korin (attendu, §2.4 de l'étude) ; sautés : mémoire (4 à 8 Go) |
| `test_ctypes` | 616 | 3 | 71 | `find_library ("c")`, `dllist` : **pas de `libc.so`** (choix de l'étude) ; sautés : Windows, macOS, Emscripten, « could not find libc » |
| `test_ssl` | 206 | 3 (1) | 16 | `TestPreHandshakeClose` (texte d'erreur d'OpenSSL attendu différent), `test_server_accept` (`getpeername` : `0.0.0.0` au lieu de l'adresse du client, NetKit ne la donne pas), `test_nonblocking_send` |
| `test_signal` (sans `test_sigwait_thread`) | 56 | 1 (4) | 11 | `ITIMER_PROF`, `ITIMER_VIRTUAL` absents (minuteries de temps processeur) ; `test_keyboard_interrupt_exit_code` : le code `128 + SIGINT`, réglé ensuite par le correctif de l'action par défaut (pas relancé) ; sautés : `sigtimedwait`, `sigwaitinfo` absents. **`test_sigwait_thread` bloque** (un thread en `sigwait` pendant qu'un signal vise le process) : écarté |
| `test_os` | 385 | 7 (16) | 116 | pas de liens durs ni symboliques (`ENOSYS`), pas de FIFO (`mkfifo`), modes de fichier sans effet (FAT), chemins longs (≈ 254 octets), 250 fichiers au plus dans un dossier de RAM0 (`ENOSPC`, `ScandirThreadingTest`), `read`/`lseek` sur un `memfd` non faits (il faut le projeter), `setpriority` sans effet, `scandir ("")` ne lève pas, `pathconf`/`tcsetpgrp` sur un mauvais descripteur (`EINVAL` au lieu d'`EBADF`), avertissement de `fork` avec un thread non Python (pas de `/proc`) |
| `test_subprocess` | 353 | 9 | 49 | **`getcwd` rend `SD0:/bin` et non `/bin`** (4 essais × 2 : l'arbre POSIX des chemins, §2.6 de l'étude) ; un environnement vide reçoit `TERM`, `COLORTERM`, `TZ` (libposix) ; `umask` sans effet. Avant les correctifs : blocage dans `test_communicate_pipe_buf` (poll des tubes), `abort` vu comme code 134 |
| `test_socket` | 749 | (661) | 339 | **avant** le correctif de `bind (port 0)`, presque tout échouait là. Après : **bloque** dans `accept` (`InterruptedSendTimeoutTest`, puis les essais `sendmsg`/`recvmsg` sur TCP de `SendrecvmsgBase`), et QEMU s'arrête pendant la suite (plus bas) : **pas de chiffre** |
| `test_asyncio` | 469 sur les modules lancés | voir à droite | | passent : `buffered_proto`, `context`, `eager_task_factory`, `free_threading`, `futures`, `futures2`, `graph`, `locks`, `pep492`, `proactor_events`, `protocols`, `queues`, `runners`, `selector_events`, `staggered`, **`subprocess`** ; ne se chargent pas : `base_events`, `events`, `server`, `sock_lowlevel` (`socket (AF_UNIX)` → `EAFNOSUPPORT` : pas de socket `AF_UNIX` nommé) ; non lancés : QEMU s'arrête dans `sendfile`, `ssl`, `taskgroups` |

**QEMU s'arrête** (le process `qemu-system-aarch64` de Windows disparaît, sans message sur sa sortie d'erreur ni sur
l'UART de Korin, sans faute du noyau) pendant les suites qui font beaucoup de trafic TCP sur `127.0.0.1`
(`test_asyncio.test_sendfile`, `test_asyncio.test_ssl`, `test_socket`), et une fois dans `test_taskgroups`. Pas
reproductible sur un module seul (`test_queues` seul passe). Non diagnostiqué : la boucle locale de netd n'étant pas dans
main, ce trafic passe par l'usb-net et le slirp de QEMU. **À refaire sur le Pi**, où ce chemin n'existe pas.

Sautés et notés, comme le prévoyait l'étude : `symlink`, IPv6 (`--disable-ipv6`), sockets `AF_UNIX` nommés, fuseaux
horaires (UTC seulement).

## Étape 3 : modules natifs et pip (12/11/2026)

Étape 3 de [PYTHON.md](PYTHON.md) §9 : les modules C dont Vibe a besoin en `.so`, chargés par `dlopen` ; pip sur la
carte ; un paquet en C compilé sur la carte. **QEMU seulement.** Aucun module n'a eu besoin du repli statique (§2.5 :
module pointé dans `Setup.local`), et libposix n'a rien eu à gagner : `endian.h` y était déjà.

### Construire

```sh
sh tools/posix/build.sh                 # (si besoin)
sh tools/python/build.sh                # l'interpréteur ; vide build/python/sdroot
sh tools/python/site-packages.sh        # les roues de Korin et site-packages, dans build/python/sdroot
STAGES=posix sh tools/gcc/build.sh      # /gcc : libposix et specs de dlopen (si build/gcc/sdroot est plus ancien)
```

Puis `.\build.ps1` et, pour les essais, `python -I tools/python/testimage.py`.

**`tools/python/build-ext.sh <paquet>==<version> | <dossier>...`** fait des roues pour Korin sur le PC, dans
`build/python/wheels`. Le Python de construction (3.14 de WSL, un venv `~/.cache/armpi-python/venv-ext` avec
setuptools 80.9 et wheel) lance `pip wheel --no-build-isolation --no-binary=:all:` avec le `sysconfig` de la carte
(`_PYTHON_SYSCONFIGDATA_NAME=_sysconfigdata__korin_`, `_PYTHON_HOST_PLATFORM=linux-aarch64`), recopié de
`build/python/stage` en changeant seulement `CC`, `CXX`, `LDSHARED` (`aarch64-onyx-elf-gcc -specs=posix.specs -shared
-s`), `LDCXXSHARED` (`-nostdlib++`) et `INCLUDEPY` (les en-têtes de `stage`). setuptools compile donc en `-fPIC` et
relie en `-shared` (la spec ajoute libgcc et `dlmod.o`, pas de libc), nomme le module `<nom>.cpython-314.so`
(`EXT_SUFFIX` de la carte) ou `<nom>.abi3.so`, et la roue `...-cp314-cp314-linux_aarch64.whl`. Puis chaque `.so` est
vérifié : tout symbole non défini (`U`) doit être exporté par `python3` (`nm -D`), sinon le script échoue en les
nommant. `EXT_CFLAGS` ajoute des options C.

* **cffi** : `_cffi_backend` ne porte pas de libffi. Le `libffi.pc` du `pkg-config` croisé n'a que des `Cflags`
  (les en-têtes de la libffi de CPython) : les `ffi_*` sont pris à `python3`, qui les exporte (`-rdynamic`, la libffi
  de `_ctypes`). Une seule libffi dans le process. cffi prend ses fermetures à lui (`malloc_closure.h`, `mmap`
  `PROT_EXEC`), que Korin permet.
* **tree-sitter** : `-DHAVE_ENDIAN_H` (son `portable/endian.h` ne connaît `<endian.h>` que sous Linux et quelques
  autres ; libposix l'a) et `--param=early-inlining-insns=9` (le contournement d'un défaut de GCC sur aarch64 que
  son `setup.py` ne pose que si la machine qui construit est aarch64).
* **tree-sitter-bash** : rien ; module `abi3` (API limitée).

| Roue | Module | Taille |
|---|---|---|
| `cffi-2.0.0-cp314-cp314-linux_aarch64.whl` | `_cffi_backend.cpython-314.so` | 234 Ko |
| `tree_sitter-0.26.0-cp314-cp314-linux_aarch64.whl` | `tree_sitter/_binding.cpython-314.so` | 314 Ko |
| `tree_sitter_bash-0.25.1-cp310-abi3-linux_aarch64.whl` | `tree_sitter_bash/_binding.abi3.so` | 1,4 Mo |

Relocations : `RELATIVE`, `ABS64`, `GLOB_DAT`, `JUMP_SLOT`, `TLSDESC`, sans `TEXTREL`. **TLS** : 4 octets pour cffi,
rien pour les deux autres (leur `TLSDESC` vise l'`errno` de l'hôte) : la réserve de 32 Ko de `python3` (§2.5 bis) est
loin d'être prise ; à remesurer avec les modules Rust (étape 5).

**`tools/python/site-packages.sh`** (voie A de [PYTHON.md](PYTHON.md) §2.8) : ces trois roues, puis `pip install
--target build/python/site-packages --platform linux_aarch64 --implementation cp --python-version 3.14 --abi cp314
--only-binary=:all: --find-links build/python/wheels` de pip 26.3, setuptools 80.9.0, pycparser 2.23, cffi,
tree-sitter et tree-sitter-bash (les paquets purs viennent de PyPI ; nos roues d'abord). Les lanceurs de `bin/` (`#!`
vers le Python du PC) sont retirés : sur la carte, `python3 -m pip`. Les `.pyc` sont faits sur le PC en
`checked-hash` (la date de FAT, à 2 s, ne les invalide pas ; une modification du `.py`, si). Le tout (29 Mo, dont pip
14 Mo et setuptools 9 Mo) va dans `build/python/sdroot/python/lib/python3.14/site-packages`, avec
`/python/pip.conf` :

```ini
[global]
cache-dir = /python/cache/pip
disable-pip-version-check = true
root-user-action = ignore
```

`HOME` vaut `SD0:/` sous Korin : `os.path.expanduser("~")` rend `SD0:`, et pip refuse un cache qui n'est pas un chemin
absolu (`assert os.path.isabs(path)` dans `check_path_owner`) : `pip list` et `pip install` échouaient sans ce fichier
(`pip --version` passait). C'est le problème des chemins à volume (`getcwd`), réglé ici pour pip seulement.

**`/gcc` et `dlopen`.** Le `/gcc` des cartes datait d'avant `dlopen` : ses specs reliaient toujours `posix.ld` et la
libc, et `dlmod.o` n'y était pas. `tools/gcc/build.sh` recopie maintenant `dlmod.o`, `dlhost.ld`, `dlhost-cxx.ld` et
les `lib*.a` vides avec libposix, et refait les specs depuis `posix.specs`, à chaque lancement ou seul par
`STAGES=posix` (sans refaire l'arbre : les kits et FreeBASIC restent). Sur la carte, `gcc -shared` fait donc un module
et `gcc -rdynamic` un hôte, comme sur le PC.

### Essais (QEMU, 12/11/2026) : `tools/python/tests/step3.py --net`, puis `--gcc`

**10 réussis sur 10** (`--net` et `--gcc` dans deux lancements) :

| Essai | Résultat |
|---|---|
| `sysconfig.get_platform()` | `linux-aarch64` (retouche de `korin.patch`) ; `EXT_SUFFIX` `.cpython-314.so` |
| `import tree_sitter` | `tree_sitter/_binding.cpython-314.so`, chargé par `dlopen` |
| **`ls -la \| grep x`** avec la grammaire bash | `(program (pipeline (command name: (command_name (word)) argument: (word)) (command name: (command_name (word)) argument: (word))))` : noms `ls`, `grep`, arguments `-la`, `x` |
| une requête tree-sitter | `(command name: (command_name) @n)` sur `cd /tmp && rm -rf x; echo $(whoami)` : `cd`, `echo`, `rm`, `whoami` |
| `import _cffi_backend` | 2.0.0, `.so` |
| **cffi en mode ABI** | `ffi.dlopen(None)` (l'hôte) : `abs`, `strlen`, `snprintf` variadique, `strtod`, `qsort` avec un rappel `ffi.callback` (fermeture), une `struct` |
| `python3 -m pip --version` | `pip 26.3 from /python/lib/python3.14/site-packages/pip (python 3.14)` (5 à 13 s sous QEMU) |
| `pip list` | les 6 paquets de la carte |
| **`pip install --only-binary=:all: six==1.17.0`** | depuis PyPI (HTTPS), importé, puis désinstallé : 44 s |
| **`pip install --no-binary=:all: --no-build-isolation mmh3==5.2.0`** | les sources de PyPI, **compilées sur la carte par `/gcc`** (setuptools de `site-packages`) : `mmh3.cpython-314.so`, `mmh3.hash("korin")` = `-1181790595`, la valeur du PC : 98 s |

Puis, à la main, `pip install mmh3==5.2.0` **avec l'isolation de construction** (le cas par défaut : pip télécharge
setuptools 84.2.1, wheel et packaging dans un dossier temporaire, puis construit) :

* dans `/tmp` (RAM0) : **`ENOSPC`** en posant setuptools. ramfs n'a que **256 nœuds** pour tout le volume
  (`NOEUDS` dans `src/svc/ramfs/ramfs.asm`), setuptools a plus de 1000 fichiers. Avec `--no-binary=:all:`, pip
  compile en plus setuptools depuis ses sources : même fin ;
* avec **`TMPDIR=/python/cache/tmp`** (sur la carte SD) : **réussi**. Les commandes de setuptools sur la carte :
  `gcc -fno-strict-overflow ... -mcpu=cortex-a72 -O2 -g0 -fno-stack-protector -fPIC -I/python/include/python3.14 -c
  ./src/mmh3/mmh3module.c ...`, puis `gcc -shared ... -o build/lib.linux-aarch64-cpython-314/mmh3.cpython-314.so` ;
  même valeur de `mmh3.hash`.

Pour l'instant, sur Korin : `pip install --no-build-isolation` (setuptools est sur la carte), ou `TMPDIR` sur la carte.
Plus de nœuds dans ramfs réglerait le cas par défaut (hors de cette étape).

Un arrêt de QEMU sans message, comme à l'étape 2, une fois, après les premiers essais (rien ne tournait alors) ; la
seconde carte a passé tous les essais sans arrêt.

## Pas fait, pour la suite

* **Pas essayé sur le Pi.**
* L'arbre POSIX des chemins (`getcwd` → `/sd0/...`), commun avec Node ([NODEJS.md](NODEJS.md) §6.3).
* `socket (AF_UNIX)` qui rend un socket (même sans `bind` nommé) : `test_asyncio` en charge quatre modules de plus.
* `sigwait` dans un thread, `sigtimedwait`, `sigwaitinfo`, `ITIMER_PROF`/`ITIMER_VIRTUAL`, `read`/`lseek` sur un
  `memfd`, un `accept` bloquant pendant qu'un autre thread du process se connecte (`test_socket`).
* Les arrêts de QEMU sous charge réseau : à suivre sur le Pi.
* `ctypes.util.find_library("c")` rend `None` (pas de `libc.so`, choix de l'étude) : un correctif de deux lignes dans
  `ctypes/util.py` peut rendre `None` exprès, ou un nom que `CDLL` comprendrait comme l'hôte.
* `python3` n'exporte pas libstdc++ (`dlhost-cxx.ld`) : à ajouter avant un module C++ (numpy, étape 14).
* bz2, lzma, zstd ; `preload` de `python3` et `file_pin` du zip (étape 11) ; le module `korin` (étape 12).
* RAM0 (`/tmp`) n'a que 256 nœuds (`NOEUDS` de `src/svc/ramfs/ramfs.asm`) : pip, avec l'isolation de construction, y
  pose setuptools (plus de 1000 fichiers) et échoue en `ENOSPC` ; aujourd'hui `TMPDIR` sur la carte (étape 3).
* `HOME` vaut `SD0:/` : un chemin à volume, que pip refuse pour son cache (réglé par `/python/pip.conf`) ; le même
  problème que `getcwd` (l'arbre POSIX des chemins).
* `platform.system()` dit « ARMPI » (le `uname` de libposix), pas « Korin ».
