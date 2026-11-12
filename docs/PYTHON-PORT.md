# CPython sous Korin : le portage (étapes 1 à 3, 5 et 6)

Suite de l'étude [PYTHON.md](PYTHON.md) (§2 construction, §2.5 bis `dlopen`, §9 étapes). Commencé le 11/11/2026 :
**CPython 3.14.10** tourne sous QEMU, avec OpenSSL, zlib, SQLite, expat, libffi et libmpdec, la bibliothèque standard
dans un zip, et `python3` hôte de `dlopen` (les modules d'extension se chargent en `.so`). Étape 3 le 12/11/2026 :
`tree_sitter`, `tree_sitter_bash` et `_cffi_backend` en `.so`, pip sur la carte, un paquet en C compilé sur la carte
par `/gcc` ([section « Étape 3 »](#étape-3--modules-natifs-et-pip-12112026)). Étape 5 le 12/11/2026 : les modules Rust
de Vibe, `pydantic-core`, `rpds-py` et `cryptography`, chacun sa `cdylib` en `.so`, chargés ensemble dans `python3`
([section « Étape 5 »](#étape-5--les-modules-rust-12112026)). Étape 6 le 12/11/2026 : le harness de Vibe en `cdylib`,
le `site-packages` de Vibe sur la carte (`vibe-acp` et `vibe-app-server` répondent à `initialize`) et l'arbre POSIX
des chemins (`os.getcwd()` → `/sd0/...`) ([section « Étape 6 »](#étape-6--le-harness-le-site-packages-de-vibe-et-larbre-posix-des-chemins-12112026)).
**Pas encore essayé sur le Pi.**

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
| `tools/python/site-packages.sh` | étape 3 : `site-packages` de la carte préparé sur le PC (voie A), et `/python/pip.conf` ; étape 6 : Vibe, `vibe-acp`, `vibe-app-server` |
| `tools/vibe/build-korin.sh` | étape 6 : la roue `mistral-vibe` pour Korin (harness en `cdylib`, sans code mode) et la liste des paquets purs du verrou |
| `tools/python/tests/step6.py` | les essais de l'étape 6 |
| `tools/python/tests/step3.py` | les essais de l'étape 3 |
| `tools/python/build-rust-ext.sh` | étape 5 : un paquet pyo3 de PyPI (ou un dossier) en roue pour Korin, une `cdylib` par paquet, sans maturin |
| `tools/python/tests/step5.py`, `step5-pytest.sh`, `panic/` | les essais de l'étape 5 ; `panic/` : une `cdylib` pyo3 qui panique (`run_pytest.py`, retiré à l'étape 6) |
| `tools/gcc/build.sh` | `STAGES=posix` : libposix, `dlmod.o`, `dlhost*.ld` et les specs de `/gcc` refaits seuls (`gcc -shared`, `-rdynamic` sur la carte) |
| `build.ps1` | recopie `build/python/sdroot` sur la carte |
| `skel/sd0/etc/paths.txt` | `/python = SD0:/python` ; étape 6 : `/sd0`, `/sd1`, `/ram0`, `/usb0` (12 lignes sur 16) |

Sur la carte :

| Chemin | Contenu | Taille |
|---|---|---|
| `SD0:/bin/python3` | l'interpréteur, statique, hôte de `dlopen` (`-rdynamic`) ; tous les modules de la bibliothèque standard dedans (90 modules intégrés), OpenSSL **entier** exporté (étape 5) | 17,9 Mo (dépouillé) |
| `SD0:/python/lib/python314.zip` | la bibliothèque standard (556 modules), sources et `.pyc`, compressée | 7,5 Mo |
| `SD0:/python/lib/python3.14/lib-dynload/` | les modules en `.so` (vide sur la carte livrée) | |
| `SD0:/python/lib/python3.14/site-packages/` | les paquets : pip, setuptools, pycparser, cffi, tree-sitter, tree-sitter-bash (étape 3) ; pydantic, pydantic-core, rpds-py, referencing, jsonschema, cryptography, joserfc et leurs dépendances pures (étape 5) ; Mistral Vibe 3.1.2, son harness et tout son verrou (étape 6, 86 paquets) | 147 Mo |
| `SD0:/bin/vibe-acp`, `SD0:/bin/vibe-app-server` | les commandes de Vibe (étape 6), des scripts `#!/bin/python3` | |
| `SD0:/python/pip.conf` | la configuration de pip pour tout le système (pas de question de version, `root`) ; plus de `cache-dir` depuis l'étape 6 | |
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
| `test_os` | 385 | 7 (16) | 116 | pas de liens durs ni symboliques (`ENOSYS`), pas de FIFO (`mkfifo`), modes de fichier sans effet (FAT), chemins longs (≈ 254 octets), 256 nœuds au plus dans RAM0 (`ENOSPC`, `ScandirThreadingTest` ; 4096 depuis l'étape 3, pas relancé), `read`/`lseek` sur un `memfd` non faits (il faut le projeter), `setpriority` sans effet, `scandir ("")` ne lève pas, `pathconf`/`tcsetpgrp` sur un mauvais descripteur (`EINVAL` au lieu d'`EBADF`), avertissement de `fork` avec un thread non Python (pas de `/proc`) |
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

`HOME` valait `SD0:/` sous Korin : `os.path.expanduser("~")` rendait `SD0:`, et pip refuse un cache qui n'est pas un
chemin absolu (`assert os.path.isabs(path)` dans `check_path_owner`) : `pip list` et `pip install` échouaient sans ce
fichier (`pip --version` passait). C'était le problème des chemins à volume (`getcwd`), réglé ici pour pip seulement ;
depuis l'étape 6 (arbre POSIX, `HOME` = `/sd0`), `cache-dir` n'est plus dans `pip.conf`.

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

* dans `/tmp` (RAM0) : **`ENOSPC`** en posant setuptools. ramfs n'avait que **256 nœuds** pour tout le volume
  (`NOEUDS` dans `src/svc/ramfs/ramfs.asm`), setuptools a plus de 1000 fichiers. Avec `--no-binary=:all:`, pip
  compile en plus setuptools depuis ses sources : même fin ;
* avec **`TMPDIR=/python/cache/tmp`** (sur la carte SD) : **réussi**. Les commandes de setuptools sur la carte :
  `gcc -fno-strict-overflow ... -mcpu=cortex-a72 -O2 -g0 -fno-stack-protector -fPIC -I/python/include/python3.14 -c
  ./src/mmh3/mmh3module.c ...`, puis `gcc -shared ... -o build/lib.linux-aarch64-cpython-314/mmh3.cpython-314.so` ;
  même valeur de `mmh3.hash`.

Puis **ramfs passe à 4096 nœuds** ([FICHIERS.md](FICHIERS.md) §4.2, « ramfs, limites ») : `pip install
--force-reinstall --no-deps mmh3==5.2.0`, isolation par défaut dans `/tmp`, sans `TMPDIR` : **réussi**, 216 s sous QEMU,
même valeur de `mmh3.hash`, `/tmp` vide ensuite. `--no-build-isolation` reste plus rapide (setuptools est déjà sur la
carte : 98 s).

Un arrêt de QEMU sans message, comme à l'étape 2, une fois, après les premiers essais (rien ne tournait alors) ; la
seconde carte a passé tous les essais sans arrêt.

## Étape 5 : les modules Rust (12/11/2026)

Étape 5 de [PYTHON.md](PYTHON.md) §9 : les trois paquets en Rust dont Vibe 3.1.2 dépend, aux versions de son
`uv.lock`, construits sur le PC par la cible `aarch64-unknown-korin` ([RUST.md](RUST.md)) et chargés par `dlopen`.
**QEMU seulement.** La « caisse parapluie » de l'étude (§2.5, `korin-pyext` en `staticlib`) n'a pas lieu d'être depuis
`dlopen` (§2.5 bis) : **chaque paquet a sa `cdylib`**, un `.so` dans son paquet, avec sa copie de la `std`.

| Roue | Module | Taille (dépouillé) | Symboles pris à `python3` | TLS |
|---|---|---|---|---|
| `pydantic_core-2.46.4-cp314-cp314-linux_aarch64.whl` (pyo3 0.28) | `pydantic_core/_pydantic_core.cpython-314.so` | 4,6 Mo | 234 | 0x80 |
| `rpds_py-0.30.0-cp314-cp314-linux_aarch64.whl` (pyo3 0.27) | `rpds/rpds.cpython-314.so` | 0,8 Mo | 167 | 0x70 |
| `cryptography-50.0.0-cp314-abi3-linux_aarch64.whl` (pyo3 0.29, `abi3`) | `cryptography/hazmat/bindings/_rust.abi3.so` | 4,5 Mo | 790 (OpenSSL compris) | 0x70 |

Avec les paquets purs de Vibe, aux mêmes versions : `pydantic` 2.13.4, `annotated-types` 0.7.0, `typing-extensions`
4.16.0, `typing-inspection` 0.4.2, `referencing` 0.37.0, `jsonschema` 4.26.0, `jsonschema-specifications` 2025.9.1,
`attrs` 26.1.0, `joserfc` 1.7.7 (`tools/python/site-packages.sh`). `cffi` 2.0.0 (étape 3) sert à `cryptography`.

### Construire

```sh
sh tools/posix/build.sh                 # (si besoin)
sh tools/python/build.sh                # l'interpréteur (OpenSSL entier exporté)
SKIP_TESTS=1 bash tools/rust/build.sh   # la cible Rust, avec target-lexicon et getrandom patchées
sh tools/python/site-packages.sh        # les roues C et Rust, puis site-packages (et la carte d'essai)
```

**`tools/python/build-rust-ext.sh <paquet>==<version> | <dossier>...`**, sans maturin : le sdist de PyPI (sha256 de
PyPI, et celui écrit dans le script pour les trois versions de Vibe), un patch `tools/python/rust/<nom>-<version>.patch`
s'il existe (aucun n'a été nécessaire), la lecture de `[tool.maturin]` (`manifest-path`, `module-name`,
`python-source`, `features`), puis `cargo korin build --release --lib`, et la roue faite par le script : le Python du
paquet, le `.so` à la place que dit `module-name` (un module Rust seul, `rpds`, devient un paquet avec un `__init__.py`,
comme le fait maturin), la `METADATA` du sdist, `RECORD`. Puis la vérification de `build-ext.sh` (tout symbole non
défini doit être exporté par `python3`) et la taille du TLS.

* **pyo3 en croisé** : `PYO3_CONFIG_FILE` (CPython 3.14, `shared=false`, `suppress_build_script_link_lines=true` : pas
  de `libpython` à lier, l'API C vient de l'hôte), `PYO3_CROSS_LIB_DIR` = `build/python/stage/python/lib/python3.14`
  (le `sysconfig` et, à côté, les en-têtes de la carte : le `build.rs` de `cryptography-cffi` les y cherche),
  `PYO3_BUILD_EXTENSION_MODULE=1`. Le même fichier sert aux trois versions de pyo3.
* **Caisses patchées** ([RUST.md](RUST.md), « Caisses patchées ») : avant de construire, `cargo update --precise`
  ramène `libc` à 0.2.185, `target-lexicon` à 0.13.5 et `getrandom` 0.3 à 0.3.3, les versions que `tools/rust` patche
  pour `korin` (`cryptography` avait `libc` 0.2.189, `rpds-py` 0.2.177). Sans `target-lexicon` patché,
  `pyo3-build-config` panique sur `TARGET=aarch64-unknown-korin` ; sans `getrandom` patché, `pydantic-core` (par
  `ahash`) ne compile pas (« target is not supported »).
* **Une seule OpenSSL par process.** `cryptography` (`openssl-sys`, et le C que cffi génère pour `_openssl`, compilé
  dans la `cdylib`) ne porte **pas** OpenSSL : `OPENSSL_LIB_DIR` désigne des `libssl.a` et `libcrypto.a` vides, ses
  références restent non définies et `dlopen` les résout dans `python3`. `tools/python/build.sh` relie donc `python3`
  avec un script `EXTERN (...)` de tous les symboles globaux de `libssl.a` et `libcrypto.a` (10 787), passé par
  `make LINKFORSHARED="-rdynamic openssl-export.ld"` (sur la ligne de `make` : le `sysconfig` de la carte n'en garde
  rien). `python3` passe de 17,3 à 17,9 Mo. Deux OpenSSL dans le process (l'une dans le module, l'autre dans l'hôte,
  les symboles cherchés d'abord dans l'hôte) se seraient mélangées ; et `build/openssl` est construit sans `-fPIC`.
* **TLS** : 0x80 + 0x70 + 0x70 octets pour les trois modules (0x58 de plus pour le module d'essai) : la réserve de
  32 Ko de l'hôte suffit largement, pas besoin de `-tls-reserve`.
* **Le dérouleur** : `python3` est relié avec la libposix de main (`dlunwind.S`, un seul dérouleur par process) ; une
  panique Rust dans un module remonte en `PanicException` (essai plus bas).

### Essais (QEMU, 12/11/2026) : `tools/python/tests/step5.py`

**12 réussis sur 12**, dans un seul process :

| Essai | Résultat |
|---|---|
| `import pydantic_core` | `_pydantic_core.cpython-314.so` par `dlopen`, 4 à 5 s sous QEMU (premier import) |
| `pydantic.TypeAdapter(list[int]).validate_json("[1,2]")` | `[1, 2]` |
| un modèle pydantic | `User(name='Korin', age=7, tags=['os'])` depuis du JSON (`"7"` converti), aller-retour JSON ; `age=-1` → `ValidationError` `greater_than_equal` sur `('age',)` |
| `import rpds` | `HashTrieMap`, `HashTrieSet`, `List` |
| `jsonschema.validate` | un schéma avec `$ref` (`referencing`, donc `rpds`) : valide, puis refus (`1 is not of type 'string'`) |
| `import cryptography` | `_rust.abi3.so`, `OpenSSL 3.5.9` (celle de `python3`) |
| SHA-256 | le même condensé que `hashlib` |
| Ed25519 | signature de 64 octets vérifiée, fausse signature refusée (`InvalidSignature`) |
| AES-GCM, X.509 | chiffré puis déchiffré ; un certificat auto-signé (ECDSA P-256) écrit en PEM et relu |
| JWT par `joserfc` | ES256 (vérifié, mauvaise clé refusée), EdDSA (à permettre : joserfc ne le recommande pas d'office), HS256 |
| les trois modules dans le même process | les trois `.so` chargés ; 8 threads Python qui appellent pydantic, rpds et cryptography en même temps |
| **une panique Rust** | `korin_panic_probe.panic()` (`tools/python/tests/panic`, une `cdylib` pyo3 faite par `build-rust-ext.sh`, sur la carte d'essai seulement) : `PanicException("panique voulue : korin")`, le process continue ; `catch_unwind` dans le module ; une exception Python levée dans un validateur traverse pydantic-core (`ValidationError`) et rpds (`RuntimeError`) |

### Essais : `pytest` des paquets (`tools/python/tests/step5-pytest.sh`)

Les dossiers `tests` des trois sdist, avec pytest 9.2.0 et ce que demandent ces suites, en paquets purs, sur la
carte d'essai seulement (`hypothesis`, `dirty-equals`, `inline-snapshot`, `pytest-mock`, `pytest-timeout`, `pretend`,
`cryptography-vectors` 50.0.0...). `run_pytest.py` contourne le chemin courant : `getcwd` rend `SD0:/home/...`, que
pathlib ne tient pas pour absolu (pytest recollait le dossier devant chaque chemin : `SD0:/x/SD0:/x/tests`) ; il rend
`/home/...`. Et comme **`/x` vise la racine du volume courant** (après un `chdir` dans `/tmp`, `/home` est
`RAM0:/home`), son `chdir` remet `SD0:` devant. Les deux tiennent à l'arbre POSIX des chemins (étape 6).

| Suite | Réussis | Échecs (erreurs) | Sautés | Ce qui échoue, et pourquoi |
|---|---|---|---|---|
| `rpds-py` : `tests/` (tout) | **132** | 0 | 0 | — (47 s) |
| `pydantic-core` : `tests/` sans `benchmarks` ni `test_docstrings.py` (pytest-benchmark, pytest-examples) | **5609** | 11 (8) | 125 (+ 10 xfail) | 5 : pas de fuseaux horaires (`tzdata` absent, `ZoneInfoNotFoundError` : UTC seulement, comme à l'étape 2) ; 6 : `print` → `OSError: [Errno 22]` en écrivant dans le fichier de capture de pytest ou dans la sortie, **seulement au fil de la suite entière** (deux lancements, les mêmes essais) : lancés seuls, ces fichiers passent (sortie dans un fichier de `/tmp`, de la carte ou dans un tube) ; non expliqué ; 7 erreurs de `test_union.py` : pytest 9 refuse une fixture de classe écrite en méthode (`PytestRemovedIn10Warning`), rien de Korin ; 1 : `test_function_positional_only` (`FileNotFoundError` à la préparation, passe seul). 7 min 38 |
| `cryptography` : `tests/hazmat/primitives`, `tests/hazmat/bindings`, `tests/test_fernet.py` (vecteurs de `cryptography-vectors`) | **2860** | 1 | 36 | `test_update_auto_chunking` : 513 Mo projetés, puis chiffrés et déchiffrés (1,5 Go en tout) : plus que la mémoire de QEMU. Python rend `MemoryError`, que pyo3 change en `PanicException` (« PyObject pointer is null ») : le process continue. Mais **le noyau a tué ramfs** (« plus de mémoire pour une page »), le process qui demandait une page à ce moment, et `/tmp` a disparu jusqu'au redémarrage (une première fois, la sortie de pytest y était). 14 min 20 |

Non lancés : `tests/x509` et `tests/wycheproof` de cryptography (plus longs, rien de propre à Korin au-delà de ce que
`primitives` couvre), les bancs de pydantic-core.

## Étape 6 : le harness, le `site-packages` de Vibe et l'arbre POSIX des chemins (12/11/2026)

Étape 6 de [PYTHON.md](PYTHON.md) §9. **QEMU seulement, pas essayé sur le Pi.** Trois parties : le cœur Rust de Vibe
3.1.2 en `cdylib` pour Korin, tout le `site-packages` de Vibe sur la carte (voie A) avec `vibe-acp` et
`vibe-app-server`, et l'arbre POSIX des chemins dans vfs et libposix ([POSIX.md](POSIX.md), « L'arbre POSIX des
chemins »), qui retire les contournements des étapes 3 et 5.

### Construire

```sh
sh tools/posix/build.sh                 # libposix (l'arbre POSIX)
SKIP_TESTS=1 bash tools/rust/build.sh   # getrandom 0.3.4 patchée (plus bas)
sh tools/python/build.sh                # python3 relié à la nouvelle libposix
sh tools/python/site-packages.sh        # étapes 3 et 5, pyyaml, puis Vibe (tools/vibe/build-korin.sh)
STAGES=posix sh tools/gcc/build.sh      # /gcc : libposix, et libgcc_mod.a (plus bas)
```

Puis `.\build.ps1` et `python -I tools/python/testimage.py`. Il faut `uv` dans WSL (`~/.local/bin/uv`, comme à
l'étape 0) pour lire le verrou de Vibe.

**`tools/vibe/build-korin.sh`** : l'archive de l'étiquette `v3.1.2` de GitHub (sha256 noté dans le script, celui de la
première prise), `tools/vibe/korin.patch`, puis un dossier préparé pour `tools/python/build-rust-ext.sh` : le harness
(`harness/core`, son `Cargo.lock`), les paquets `vibe` (sans `cli-rust`, l'interface en Rust) et
`mistralai_vibe_local_harness`, et un `pyproject.toml` à nous (`manifest-path = harness/core/Cargo.toml`,
`module-name = mistralai_vibe_local_harness._native`, `python-packages = [vibe, mistralai_vibe_local_harness]`,
`no-default-features = true`, `features = [extension-module]` : **sans code mode**, ni `deno_*` ni V8, ce que le script
vérifie par `cargo tree`). `build-rust-ext.sh` sait maintenant lire `no-default-features` et `python-packages`. Le
résultat est **une vraie roue** `mistral_vibe-3.1.2-cp314-abi3-linux_aarch64.whl` (5,9 Mo) avec son
`mistral_vibe-3.1.2.dist-info` minimal (`Name`, `Version`, `Requires-Python`), que pip installe comme les autres.
Le script écrit aussi `~/.cache/korin-vibe/requirements-korin.txt` : le verrou de Vibe (`uv export`) sans les natifs
faits ici (étapes 3 et 5), ni `uuid-utils`, `watchfiles`, `setproctitle`, `miniaudio`, `secretstorage`.

| | |
|---|---|
| `_native.abi3.so` | 9,3 Mo dépouillé (10,3 Mo sur le PC) ; 154 symboles pris à `python3`, tous exportés ; **TLS 0x118 octets** (avec les trois modules de l'étape 5, 0x2E0 sur les 32 Ko de la réserve : pas de `-tls-reserve`) ; compilé en 2 min 22 |
| caisses à patcher | **aucune de plus** que `getrandom` : `jsonschema` 0.49.1 exige `getrandom` ≥ 0.3.4, le patch de `tools/rust` passe de 0.3.3 à **0.3.4** (le même correctif, rejoué ; `build-rust-ext.sh` y ramène toutes les 0.3.x) ; `libc` 0.2.189 du verrou redescend à 0.2.185 sans plainte ; `tokio`, `parking_lot`, `chrono`, `uuid`, `rmcp`, `pyo3` 0.29 compilent tels quels pour `korin` |
| `pyyaml` 6.0.3 | le seul paquet du verrou sans roue pure : construit par `build-ext.sh` ; sans libyaml, son `setup.py` ne compile pas `_yaml` : du Python pur dans une roue `cp314-linux_aarch64` |

**`site-packages` de la carte** (`tools/python/site-packages.sh`) : les roues des étapes 3 et 5, `pyyaml`, la roue
`mistral-vibe` et `-r requirements-korin.txt`, puis `tools/vibe/watchfiles` (le remplaçant par sondage).
`secretstorage`, que pip tire par `keyring`, est retiré (pas de D-Bus sous Korin), comme sur le PC. **147 Mo, 86
paquets**, dans `SD0:/python/lib/python3.14/site-packages`. Les `.pyc` des modules de `mistralai` aux noms très longs
(jusqu'à 156 caractères sous `site-packages`, 197 sur la carte, sous la limite de 255) sont retirés : sous Windows,
`build.ps1` et `testimage.py` les recopient depuis un dossier dont le chemin dépasse alors 260 caractères
(`LongPathsEnabled` n'est pas mis) ; Python les compile sur la carte au premier import.

**`vibe-acp` et `vibe-app-server`** : deux scripts `#!/bin/python3` dans `SD0:/bin` (exécutables par la règle de
vfs), comme ceux de `build-pc.sh`.

**`/python/pip.conf`** garde `disable-pip-version-check` et `root-user-action` mais plus `cache-dir` : `HOME` vaut
`/sd0`, pip met son cache dans `/sd0/.cache/pip`.

### Ce que l'arbre POSIX a changé pour ce chantier

* `os.getcwd()` rend `/sd0/home/pytest`, `HOME` vaut `/sd0`, `sys.executable` `/bin/python3`,
  `tempfile.gettempdir()` `/tmp` : `pathlib`, pip et pytest les tiennent pour absolus.
* **`tools/python/tests/run_pytest.py` est retiré** : `step5-pytest.sh` lance `python3 -m pytest` tel quel.
* Dans la `std` de Rust, `current_dir` et `current_exe` rendent `/sd0/...` : l'essai `env::test_self_exe_path`, le
  seul échec de l'étape 4, passe ([RUST.md](RUST.md)). Le contournement de la `std` pour RamFS (`remove_dir_all`)
  n'avait rien à voir avec les chemins : il reste, RamFS n'est pas corrigé.
* Deux défauts trouvés par les essais et corrigés dans libposix : `st_ino` et `st_dev` étaient des hachages du chemin
  **tel qu'écrit** (`/bin/x` et `SD0:/bin/x` étaient deux fichiers pour `os.path.samefile`, déjà avant l'arbre) :
  ils hachent maintenant la forme du système ; et `realpath` remplissait `resolved` de zéros jusqu'à `PATH_MAX`
  (`strncpy`), ce qui écrasait la pile d'un appelant au tampon plus petit.

### Essais (QEMU, 12/11/2026) : `tools/python/tests/step6.py`

**12 réussis sur 12** :

| Essai | Résultat |
|---|---|
| `os.getcwd()` | `/sd0/home/pytest` |
| `pathlib` | `Path.cwd()` absolu ; `Path("x/../step6.py").resolve()` = `/sd0/home/pytest/step6.py` ; `relative_to` |
| les formes d'un même fichier | `os.path.samefile` : `/sd0/etc/paths.txt` = `SD0:/etc/paths.txt` = `/etc/paths.txt` ; `/home` = `/sd0/home` ; `/bin/python3` = `SD0:/bin/python3` ; `/tmp` = `/ram0/tmp`. (`os.path.realpath` est en Python : il garde `/sd0/bin` tel quel ; le `realpath` C, qui rend une forme unique, est essayé par `posixtest tree`) |
| « `/x` » et le volume courant | après `chdir ("/tmp")`, `/home/pytest` est toujours `SD0:/home/pytest` ; `chdir ("/ram0")` → `/ram0` ; `chdir ("/home")` → `/sd0/home` |
| `HOME`, `sys.executable`, `tempfile` | `/sd0`, `/bin/python3`, `/tmp` |
| `python3 -m pip cache dir` | `/sd0/.cache/pip`, sans `cache-dir` dans `pip.conf` |
| **`import mistralai_vibe_local_harness`** | `_native.abi3.so` par `dlopen`, `CODE_MODE` faux ; 28 à 32 s sous QEMU (premier import, avec pydantic) |
| `importlib.metadata.version ("mistral-vibe")` | `3.1.2`, le `__version__` de `vibe` |
| remplaçants | `watchfiles` `1.2.0+korin.poll`, `uuid.uuid7()` ; ni `uuid_utils`, ni `setproctitle`, `miniaudio`, `secretstorage` |
| `import vibe.acp.entrypoint, vibe.app_server.stdio` | 17 à 27 s sous QEMU |
| **`vibe-acp` répond à `initialize`** (`tools/vibe/acp_client.py --stop-after initialize`) | `@mistralai/mistral-vibe 3.1.2`, protocole 1, méthodes `browser-auth` et `browser-auth-delegated` ; 195 s au premier lancement sous QEMU (les `.pyc` de `mistralai` se compilent), 65 à 119 s ensuite |
| **`vibe-app-server` répond à `initialize`** (`acp_client.py --app-server`, nouvelle option : `initialize` puis `initialized`) | `vibe-app-server 3.1.2`, 80 s sous QEMU |

Puis, à la main, **`session/new`** sur la carte (`acp_client.py --stop-after new`, `VIBE_HOME` neuf) :
`-32000 Missing API key for mistral provider`, comme sur le PC : il demande la connexion au compte. La connexion
(`--sign-in`, un lien que l'utilisateur ouvre lui-même) est laissée à l'étape 7.

**Non-régression** (même carte, `python3` relié à la nouvelle libposix) :

| Essai | Résultat |
|---|---|
| `step2.py --net` | 17 réussis sur 17 |
| `step3.py --net` | 9 réussis, 1 sauté (`--gcc`) |
| `step3.py --gcc` | **échec, sans rapport avec l'arbre** : `/gcc` n'avait pas `libgcc_mod.a` (la libgcc des modules, étape 4) : `gcc -shared` ne reliait plus de module sur la carte depuis l'étape 4. `tools/gcc/build.sh` la recopie maintenant ; puis **9 réussis**, 1 sauté : mmh3 compilé sur la carte, la valeur du PC, 165 s |
| `step5.py` | 12 réussis sur 12 |
| `step5-pytest.sh rpds` (sans `run_pytest.py`) | **132 réussis** sur 132, 54 s |

La non-régression du système (libposix, bash, git, Rust) est dans [POSIX.md](POSIX.md), « L'arbre POSIX des chemins ».

## Étape 7 : Vibe de bout en bout sur la carte (12/11/2026)

Étape 7 de [PYTHON.md](PYTHON.md) §9 : une séance complète de Vibe 3.1.2 **sur la carte**, avec le compte Mistral de
l'utilisateur, par le client ACP de l'étape 0 lancé **sur la carte** (`python3 acp_client.py`). **QEMU seulement, pas
essayé sur le Pi.** Rien n'a eu à changer dans le système ni dans Vibe : seul le client a reçu le scénario de l'étape.

**`tools/vibe/acp_client.py --step7 --sign-in --cwd DIR`** (bibliothèque standard seulement) : prépare `DIR` en dépôt
git avec un `notes.txt` (un « mot secret »), ouvre la séance (connexion au compte si `session/new` répond `-32000`),
passe en mode `ask`, puis cinq tours, chacun chronométré et vérifié :

1. « Lis le fichier notes.txt et dis-moi quel est le mot secret » : un outil, et le mot dans la réponse ;
2. « Ajoute à la fin de notes.txt une ligne… » : **une approbation demandée et accordée** (`allow_once`), le fichier
   changé sur la carte ;
3. « Lance dans le shell `git status --short`, puis `rg -n Korin` » : deux outils `bash`, leurs sorties dans la
   réponse ;
4. un long poème, puis **Échap** (`session/cancel` deux secondes après le premier morceau de texte) : `stopReason`
   `cancelled` ;
5. un **second process** `vibe-acp` : `initialize`, **`session/load`** de la même séance (l'historique rejoué), puis
   « Quel était le mot secret que tu as lu tout à l'heure ? Réponds sans relire le fichier ».

Lancement sur la carte d'essai par telnet (`tools/python/testimage.py`, QEMU `--fwd 2371:23`) :

```sh
cd /home/pytest && python3 acp_client.py --step7 --sign-in --cwd /home/pytest/work7
```

**La connexion** : `session/new` répond `-32000 Missing API key for mistral provider`, le client demande
`authenticate` `browser-auth-delegated` (`action: start`) et affiche le lien (`console.mistral.ai/codestral/cli/
authenticate?...`, valable 10 minutes) ; **l'utilisateur l'a ouvert lui-même** sur le PC. `complete` répond d'abord
`-32602 Browser sign-in status could not be retrieved` (attendu, repris 5 s plus tard), puis `completed`. Vibe range la
clé dans `/sd0/.vibe/.env` (`HOME` = `/sd0`, pas de trousseau) ; le client ne la voit ni ne l'affiche. Sur FAT, le
« mode 600 » que Vibe demande n'a pas d'effet (`ls -l` montre `-rw-r--r--`) : sans conséquence ici (pas de sécurité
contre un attaquant), à savoir.

### Essais (QEMU, 12/11/2026)

**8 réussis sur 9, et le neuvième vérifié à la main** : le seul « NO » était le contrôle du client, trop strict (il
cherchait la ligne avec son point final ; le modèle l'a écrite sans) ; le fichier de la carte avait bien la ligne
(`cat notes.txt`, `git diff --stat` : `notes.txt | 1 +`). Le contrôle accepte maintenant la ligne sans le point (pas
relancé : la séance entière prend une demi-heure sous QEMU).

| Essai | Résultat | Temps (QEMU) |
|---|---|---|
| `initialize` (premier lancement après la carte neuve) | `@mistralai/mistral-vibe 3.1.2` | **139,9 s** |
| connexion au compte | lien ouvert par l'utilisateur, `completed` | (le temps de l'utilisateur) |
| `session/new` | séance ouverte, mode `accept-edits`, puis `ask` | 13,7 s |
| lire `notes.txt` | outil `Reading notes.txt`, « **LUNE-2604** » | 28,3 s |
| le modifier en `ask` | outil `Editing notes.txt`, **approbation demandée et accordée**, la ligne ajoutée sur la carte | 48,0 s |
| `git status` et `rg` par le shell | `bash: cd /home/pytest/work7 && git status --short` (sans approbation : commande en lecture) et `bash: … && rg -n Korin` (approbation accordée) ; ` M notes.txt`, `notes.txt:1:Korin…`, `notes.txt:3:Modifié par Vibe sous Korin` | 19,9 s |
| Échap | `session/cancel` à 52 s ; réponse `cancelled` à 78 s : **26 s** entre Échap et la fin du tour sous QEMU | 78,3 s |
| second `initialize` | | 51,1 s |
| **`session/load`** | l'historique rejoué : 4 `user_message_chunk`, 4 `agent_message_chunk`, 8 `agent_thought_chunk`, 5 `tool_call`, `available_commands_update`, `usage_update`, `session_info_update` | 28,0 s |
| après la reprise | « Le mot secret était **LUNE-2604**. » | 71,7 s |

**Temps de démarrage** : sous QEMU, `initialize` 140 s la première fois (les `.pyc` de `mistralai` se compilent), 51 à
63 s ensuite ; `session/new` 14 s. Sur le PC : 3 s et 0,7 s. Les tours prennent 20 à 70 s sous QEMU contre 4 à 7 s sur le
PC (le même compte, le même modèle) : le temps passe dans Python et le harness, pas dans le réseau. À mesurer sur le Pi
(étape 11).

Le même scénario a d'abord été passé sur le PC (Vibe de l'étape 0, `VIBE_HOME` du compte) : tout réussi sauf `rg`,
absent du PC (le modèle s'est rabattu sur `grep`).

## Étape 8 : `libacp` et `agent`, le client terminal (12/11/2026)

Étape 8 de [PYTHON.md](PYTHON.md) §9 et la forme du §5.3 : une bibliothèque C, **`libacp`**, et un client terminal en
C, **`agent`**, qui parle ACP à `vibe-acp` par deux tubes. **QEMU seulement, pas essayé sur le Pi.**

| Où | Quoi |
|---|---|
| `src/lib/acp/json.h`, `json.c` | le JSON : le lecteur en arbre de PocketQI (`src/apps/pocketqi/json.h`) refait en C, et un tampon d'écriture (`jbuf`, `jb_str` échappe et remplace l'UTF-8 invalide par U+FFFD) |
| `src/lib/acp/acp.h`, `acp.c` | **libacp** : le process de l'agent (`fork`, deux tubes, stderr dans un journal, son propre groupe de process : Ctrl+C du terminal ne le vise pas) ; le JSON-RPC (requêtes en vol et leurs réponses, notifications, requêtes de l'agent) ; **la séance** rendue en événements |
| `tools/agent/agent.c` | **agent**, le client terminal |
| `tools/agent/build.sh` | `build/agent/libacp.a`, `build/agent/sdroot/bin/agent` (pour la carte, recopié par `build.ps1`), `build/agent/host/agent` (le même pour le PC) |
| `tools/agent/ptytest.py` | l'essai, dans un pseudo-terminal (bibliothèque standard : sur le PC et sur la carte) |

### `libacp`

Un seul fil et rien de bloquant sauf l'écriture vers l'agent : le client appelle `acp_pump` quand `acp_fd` est lisible
(ou avec un délai), et tout part de là en rappels (`acp_events`). Ce que la façade doit savoir de Vibe (étape 0) est
dedans, une fois pour les deux clients :

* `acp_start` : `initialize` (sans capacités `fs` ni `terminal` : l'agent fait tout lui-même ; `_meta`
  `browser-auth-delegated`), puis `session/new` ou `session/load` (`load = "last"` : `session/list` du dossier, la plus
  récente) ;
* `session/new` → `-32000` : **la connexion** : `authenticate` (`action: start` dans `_meta`), l'URL donnée au client
  (`sign_in`), puis `complete`, repris toutes les 5 s sur erreur (la première `-32602` est attendue) pendant 15 min,
  puis la séance ;
* le **mode voulu** (`ask` pour les deux clients) posé par `session/set_mode` dès l'ouverture ;
* les mises à jour : texte de l'agent, pensées, texte de l'utilisateur rejoué (`session/load`), **outils** fusionnés par
  `toolCallId` (titre, genre, état, sortie, diff `path`/`oldText`/`newText`), plan, mode, modèle (`configOptions`),
  jetons du contexte (`usage_update`) ;
* `session/request_permission` → `permission` (l'appel d'outil connu, les options) ; réponse par `acp_permit` ;
  `acp_cancel` répond d'abord `cancelled` aux approbations en attente, puis `session/cancel` ;
* `acp_open` : une autre séance dans le même agent (Nouvelle séance, Reprendre), sans attendre un nouveau démarrage.

Les requêtes que l'agent n'a pas à faire (`fs/*`, `terminal/*`) reçoivent `-32601`.

### `agent`

```
agent [--cwd DIR] [--mode ask|accept-edits|auto-approve] [--load ID | --continue] [--agent CMD] [--log FICHIER]
```

* Le terminal en mode brut (sans écho, **sans signaux** : Ctrl+C est une touche) ; une invite d'une ligne (`›`), qui
  passe à la ligne suivante quand elle est plus longue que l'écran ; flèches, Début, Fin, Suppr, Ctrl+A/E/U/L,
  historique (haut, bas) ; **Maj+Tab** change de mode ; `/mode`, `/session`, `/help`, `/quit`.
* Le texte du modèle au fil de l'eau, avec un peu de Markdown (gras, code, blocs ```, titres) ; les pensées en gris ;
  chaque outil sur une ligne (`» Editing notes.txt`), la sortie d'une commande (12 lignes), un **diff en couleur**
  (lignes communes du début et de la fin retirées, deux de contexte) ; le plan.
* **L'approbation** : `? Vibe demande votre accord : <outil>`, le diff s'il y en a un, puis les options de Vibe
  numérotées avec une lettre : `[1/o] oui, une fois  [2/s] oui, pour cette séance  [3/t] toujours  [4/n] non` ; Échap
  refuse et interrompt le tour.
* **Échap** ou **Ctrl+C** pendant un tour : `session/cancel` ; à l'invite, Ctrl+C efface la ligne, deux fois de suite
  quitte ; **Ctrl+D** sur une ligne vide quitte.
* Pendant un tour, une ligne d'état (`| Vibe travaille… 12 s · Échap : interrompre`), effacée dès que du texte arrive.
* **Le redimensionnement** : `SIGWINCH`, la taille relue (`TIOCGWINSZ`), l'invite redessinée.
* **Aucune séquence en clair** : une séquence reçue (CSI, SS3, Alt+touche) est lue en entier et avalée si elle n'est
  pas comprise ; Échap seul se reconnaît à 50 ms de silence derrière lui ; un caractère UTF-8 coupé entre deux lectures
  attend sa suite (le pty de Korin livre « é » en deux fois : avant ce correctif, `agent` insérait l'octet de tête seul
  et la demande partait en UTF-8 invalide, que Vibe rejetait sans réponse) ; le texte de l'agent est filtré (ni ESC ni
  caractère de commande C0 ou C1 n'en sort tel quel). `agent` n'écrit que SGR, `K`, `J`, `H`, `nA`, `nC`.
* La connexion : l'URL est affichée ; la clé reste l'affaire de Vibe (`$VIBE_HOME/.env`).
* Le texte de l'utilisateur que Vibe renvoie (`user_message_chunk`) n'est montré qu'au rejeu de `session/load` :
  pendant un tour, c'est la demande déjà affichée (elle sortait deux fois).

### Essais (QEMU, 12/11/2026)

**`tools/agent/ptytest.py`** lance `agent` dans un pseudo-terminal (`pty.fork`, 100 × 30), tape au clavier et lit
l'écran. La séance de l'étape 7 : lire `notes.txt`, le modifier (la question d'approbation, réponse `o` ; le diff
affiché), `git status` et `rg` (réponse `1`), le terminal **redimensionné** pendant ce tour (`TIOCSWINSZ`, donc
`SIGWINCH`, 72 × 24), **Échap** pendant un long poème, **Ctrl+C** pendant un autre tour, des touches spéciales à
l'invite (flèche, Suppr, Ctrl+→, F1), Ctrl+U, **Ctrl+D** ; puis `agent --continue` (la séance la plus récente du
dossier), qui doit se souvenir du premier tour. Toute la sortie est passée au crible : chaque séquence doit être de
celles qu'`agent` écrit exprès, et aucun caractère de commande ne sort seul.

```sh
cd /home/pytest && python3 ptytest.py --cwd /home/pytest/work8 --slow 4     # sur la carte (pty de vterm)
python3 tools/agent/ptytest.py --agent-bin build/agent/host/agent            # sur le PC, Vibe de l'étape 0
```

**Sur la carte : 17 réussis sur 17**, après le correctif de l'UTF-8 coupé (la première passe s'était arrêtée à la
modification : la demande, tapée avec ses accents, partait en UTF-8 invalide). Sur le PC aussi, 17 sur 17.

| Essai | Carte (QEMU) | PC |
|---|---|---|
| démarrage jusqu'à la séance ouverte en `ask` | 88 s | 3 s |
| lire `notes.txt` | outil affiché, « LUNE-2604 », 18 s | 5 s |
| modifier : approbation demandée (`[1/o]`…), diff affiché (`+ Modifié par Vibe sous Korin.`), fichier changé | 14 s | 6 s |
| `git status`, `rg`, redimensionnement pendant le tour | les deux commandes et leurs sorties ; `agent` continue après `SIGWINCH` | |
| Échap, Ctrl+C | `(interrompu)` les deux fois, l'agent toujours là | |
| Ctrl+D | `agent` quitte, le terminal rendu à son état | |
| aucune séquence en clair | oui (les deux process) | oui |
| `--continue` | la séance reprise (96 s), le mot secret retrouvé sans relire le fichier | 3 s |

**Dans l'app Terminal de Korin** (QEMU, clavier AZERTY par VNC) : `agent --cwd /home/pytest/work8 --continue` reprend
la séance en 89 s et rejoue l'historique (pensées en gris, réponse en clair, gras rendu) ; « Ajoute à la fin de
notes.txt la ligne : essai depuis le Terminal » : `» Editing notes.txt`, la question d'approbation avec le diff en
couleur et les quatre options, `o` au clavier, `→ allow_once`, « Fait. », 88 s ; la fenêtre agrandie : l'invite
redessinée à la nouvelle largeur (le Terminal ne recoupe pas les lignes déjà écrites) ; Ctrl+D rend l'invite de
bash. Aucune séquence affichée en clair. Le doublon de la demande, vu là, est corrigé depuis (vérifié sur le PC).

Mesure en passant : au repos, `agent`, `python3` et `vibe-acp` font chacun ~400 appels système par seconde sous
QEMU ; les trois ne font qu'attendre (`poll`, `select`) : sans doute la façon dont libposix attend, à vérifier à
l'étape 11 (charge au repos).



* **Pas essayé sur le Pi.**
* Étape 5 : les `print` qui échouent en `EINVAL` au fil de la suite de pydantic-core (passent seuls) ; `tzdata` pour
  les fuseaux (`zoneinfo`) ; le noyau tue le process qui demande une page quand la mémoire manque (ramfs l'a été, et
  `/tmp` avec lui) : un service ne devrait pas être la victime d'un `python3` gourmand.
* (fait à l'étape 6) `getcwd` → `/sd0/...`, « `/x` » indépendant du volume courant : l'arbre POSIX des chemins,
  commun avec Node ([NODEJS.md](NODEJS.md) §6.3) ; `run_pytest.py` retiré.
* Étape 6 : la connexion au compte Mistral sur la carte (`acp_client.py --sign-in`), puis une séance complète :
  l'étape 7. Le démarrage de Vibe sous QEMU (65 à 195 s pour `initialize`) : l'étape 11.
* `socket (AF_UNIX)` qui rend un socket (même sans `bind` nommé) : `test_asyncio` en charge quatre modules de plus.
* `sigwait` dans un thread, `sigtimedwait`, `sigwaitinfo`, `ITIMER_PROF`/`ITIMER_VIRTUAL`, `read`/`lseek` sur un
  `memfd`, un `accept` bloquant pendant qu'un autre thread du process se connecte (`test_socket`).
* Les arrêts de QEMU sous charge réseau : à suivre sur le Pi.
* `ctypes.util.find_library("c")` rend `None` (pas de `libc.so`, choix de l'étude) : un correctif de deux lignes dans
  `ctypes/util.py` peut rendre `None` exprès, ou un nom que `CDLL` comprendrait comme l'hôte.
* `python3` n'exporte pas libstdc++ (`dlhost-cxx.ld`) : à ajouter avant un module C++ (numpy, étape 14).
* bz2, lzma, zstd ; `preload` de `python3` et `file_pin` du zip (étape 11) ; le module `korin` (étape 12).
* RAM0 (`/tmp`) : 4096 nœuds depuis l'étape 3 (256 avant) ; un paquet qui en pose plus dans `/tmp` (une grosse
  construction) : `TMPDIR` sur la carte.
* `platform.system()` dit « ARMPI » (le `uname` de libposix), pas « Korin ».

## Étape 0 : le moteur de Vibe sans V8, sur le PC (12/11/2026)

Étape 0 de [PYTHON.md](PYTHON.md) §9 : **Mistral Vibe 3.1.2** (dernière 3.1, épinglée) construit depuis ses sources
dans WSL (Ubuntu, Python 3.14.4, Rust 1.97.1 de `harness/rust-toolchain.toml`), sans Korin. Sources téléchargées dans
`~/vibe-pc`, hors du dépôt.

**Verdict : le moteur 3.1.2 tourne sans V8.** Le cœur Rust se construit sans `deno_core` ni `deno_ast`, et
`vibe-acp` passe la séance complète avec le compte Mistral de l'utilisateur : `initialize`, connexion par navigateur,
`session/new`, « liste les fichiers » (un appel d'outil), l'écriture d'un fichier (une permission demandée et
accordée), les réponses, et l'appel d'un outil MCP. Le risque 1 du §8 est levé ; le repli sur 2.24.5 n'a pas lieu
d'être. Avec la clé d'API de l'utilisateur, la séance s'arrête au premier tour sur un 429 de Mistral (quota de la clé,
plus bas).

### Ce qui est livré

| Où | Quoi |
|---|---|
| `tools/vibe/korin.patch` | le patch de Vibe 3.1.2 (8 fichiers), détaillé plus bas |
| `tools/vibe/build-pc.sh` | télécharge l'archive de l'étiquette `v3.1.2`, applique le patch, construit `_native` par cargo (sans maturin), fait un venv Python 3.14 depuis `uv.lock` **sans** `uuid-utils`, `watchfiles`, `setproctitle`, `miniaudio`, `cryptography`, `cffi`, `pycparser`, `secretstorage`, recopie Vibe dans `site-packages`, écrit `vibe-acp` et `vibe-app-server`, vérifie le tout |
| `tools/vibe/watchfiles/` | `watchfiles` en Python, par sondage (`awatch`, `watch`, `Change`) : `os.scandir` toutes les secondes (`WATCHFILES_POLL_DELAY_MS`) |
| `tools/vibe/acp_client.py` | le client ACP de l'essai, bibliothèque standard seulement (il resservira sur la carte à l'étape 7) : `initialize`, connexion par navigateur au besoin (`--sign-in`), `session/new`, `--mode`, un `session/prompt` par `--prompt`, chaque permission accordée une fois (`allow_once`), bilan et code de sortie |
| `tools/vibe/mcp_test_server.py` | un serveur MCP minimal sur stdio (un outil, `korin_word`), pour `acp_client.py --mcp-test` |

```bash
tools/vibe/build-pc.sh
```

```bash
VIBE_HOME=~/vibe-pc/home PATH=~/vibe-pc/venv/bin:$PATH python3 tools/vibe/acp_client.py --cwd DIR --mode ask
```

### `korin.patch`

1. `harness/core/Cargo.toml` : `deno_ast` et `deno_core` deviennent `optional`, derrière la fonction **`code-mode`**,
   gardée dans `default` (une construction de Mistral ne change pas) ; Korin construit avec `--no-default-features
   --features extension-module`. `cargo tree` ne montre alors ni `deno_*` ni `v8` (`build-pc.sh` le vérifie).
2. `code_mode/mod.rs` : sans `code-mode`, `mod v8` n'est pas compilé, `evaluate()` rend `Err("code mode is not available
   on this platform")`, `MAX_SOURCE_BYTES` garde sa valeur (16 Mio). Le cœur se compile sans un avertissement.
3. `core/config.rs` : sans `code-mode`, Core **refuse** une configuration qui active `programmatic` : l'outil ne peut
   pas atteindre le modèle, même si le Python l'oubliait.
4. `python_binding.rs` et `_native.pyi` : `_native.CODE_MODE` (`False` ici).
5. `vibe/app_server/_runtime.py` : `programmatic.mode = "disabled"`, et **sous-agents et process en arrière-plan
   coupés** quand `CODE_MODE` est faux. Core ne les offre que depuis un programme et refuse la configuration sinon
   (« subagents require programmatic tool calling », `harness/core/src/core/config.rs:398-414`).
6. `mistralai_vibe_local_harness/protocol.py` : sans code mode, les outils « programmatic » seulement deviennent des
   outils **directs** nommés `<groupe>_<outil>` (le `direct_name` de Core), et les groupes de connecteurs sont
   retirés. Voir « MCP » plus bas.
7. `vibe/app_server/_session_artifacts.py` : `uuid_utils.uuid7` → `uuid.uuid7` (bibliothèque standard de 3.14).

Le réglage n'est **pas** dans `config.toml` : la clé que l'étude cherchait (§4.1, point 3) n'existe pas. Les limites
du code mode sont écrites en dur dans `_runtime.py` (`RustProgrammaticToolSettings`), d'où le point 5.

### Essais

| Essai | Résultat |
|---|---|
| `cargo build --release --no-default-features --features extension-module` | passe, 2 min 20 à froid, sans avertissement ; `lib_native.so` 13,9 Mo (10,3 Mo dépouillé) |
| imports sans `uuid_utils`, `watchfiles` natif, `setproctitle`, `miniaudio`, `cryptography`, `cffi` | passe (`build-pc.sh` le vérifie) ; `import vibe.acp.entrypoint, vibe.app_server.server` : 1,5 s, 81 Mo |
| `initialize` | passe en 2 s : `@mistralai/mistral-vibe 3.1.2`, protocole 1, méthodes d'authentification `browser-auth` et `browser-auth-delegated` |
| `session/new` sans clé | refusé proprement : `-32000 Missing API key for mistral provider` (une clé est exigée dès `session/new`) |
| `session/new` avec une fausse clé (`not-a-key`) | passe en 3 s ; mode `accept-edits` parmi `ask`, `accept-edits`, `auto-approve` |
| `session/prompt` avec la fausse clé | le tour traverse le harness et part vers l'API : `-31002 Invalid API key` |
| outils offerts au modèle (requête du journal de session) | `todo`, `unified_harness_scratchpad`, `cron`, `read_file`, `write_file`, `edit`, `bash`, `skill` : ni `run_typescript` ni `search_tool_functions` ; V8 n'est ni lié ni initialisé |
| serveur MCP (`--mcp-test`), sans `cryptography` | le serveur ne se connecte pas (`mcp_transport_failed`), la session continue |
| serveur MCP, avec `cryptography` | sans le point 6 du patch, le tour échoue (`programmatic-only tool "mcp_korin.korin_word" requires programmatic tool calling`) ; avec, l'outil arrive au modèle sous le nom `mcp_korin_korin_word` |
| séance complète avec le **compte** (`--sign-in --mode ask`) | **passe** : connexion déléguée (lien ouvert par l'utilisateur), `session/new` en 59 s la première fois (amorçage des connecteurs et de la configuration gérée), 2,6 s ensuite ; « liste les fichiers » : outil `Reading .`, réponse en 4,3 s ; « crée le fichier bonjour.txt » : outil `Writing bonjour.txt`, **permission demandée et accordée** (`allow_once`), fichier écrit, réponse en 2,9 s ; `end_turn` aux deux tours |
| la même avec le compte et le serveur MCP (`--mcp-test`) | **passe** : le modèle appelle `mcp_korin.korin_word` (permission demandée et accordée) et répond `GIVRE-7431`, en 2,8 s |
| la même avec la **clé d'API** de l'utilisateur | `session/new` passe, le tour finit en `-31001 Rate limit exceeded` après 4 min 40 de reprises ; à la main, `/v1/chat/completions` rend 429 pour `mistral-vibe-cli-latest`, `devstral-medium-latest` et `mistral-small-latest`, `/v1/models` rend 200 : clé valide, quota épuisé. Rien qui tienne à Vibe ; à refaire avec une clé qui a du quota |
| essais Rust du cœur (`cargo test`) | impossibles depuis l'archive de l'étiquette : les fixtures de `harness/core/tests/fixtures/` n'y sont pas |

### Ce que coûte l'absence du code mode

* **Pas de sous-agents ni de process en arrière-plan** (ni l'outil `wait` qui va avec) : Core ne les offre que dans
  `run_typescript`. L'agent travaille seul ; les commandes vont au bout au premier plan.
* **Pas de `search_tool_functions`** : tous les outils sont dans la liste plate du modèle.
* **MCP** : en 3.1.2, les outils des serveurs MCP, des connecteurs et des greffons sont « programmatic » seulement
  (`mistralai_vibe_local_harness/vibe/_host.py:5340-5388`, `vibe/core/plugins/_catalog.py:508-511`). Sans le point 6,
  un serveur MCP configuré **casse** la session. Avec, ses outils deviennent directs et le modèle les appelle (essai
  plus haut). La réécriture se fait à la sérialisation (`model_serializer`), car les ensembles de capacités sont
  aussi faits par `model_copy(update=...)`, qui saute les validateurs.
* **Connecteurs de Le Chat** : retirés, Core les refuse sans programmes. À revoir si l'utilisateur s'en sert.
* La description de l'outil `todo` parle encore de `run_typescript` (texte seulement, laissé tel quel).

### Constats pour la suite

* **Risque 9 (`cryptography`)** : le moteur, ACP et un tour avec une clé d'API marchent **sans** `cryptography`. Mais
  `mcp` 2.3.0 l'importe au chargement (`mcp/server/request_state.py:22`, `from cryptography.exceptions import
  InvalidTag`) : **pas de MCP sans `cryptography`**. Elle reste à compiler à l'étape 5 (ou un module
  `cryptography.exceptions` de remplacement, si MCP ne s'en sert pas plus loin : à vérifier).
* **`dist-info`** : sans maturin, il faut un `mistral_vibe-3.1.2.dist-info` minimal. Le harness lit la version de
  `mistral-vibe` par `importlib.metadata` (User-Agent MCP, `_mcp_transport.py:56-60`), sinon MCP échoue. Même chose
  sur la carte (`build-pc.sh` l'écrit).
* **Connexion (§4.3 de l'étude à corriger)** : en ACP, le compte se connecte par `authenticate`
  `browser-auth-delegated` (une URL à ouvrir, puis `complete`), et **le résultat est une clé d'API**, rangée dans le
  trousseau ou à défaut dans `$VIBE_HOME/.env` (`vibe/setup/auth/api_key_persistence.py`). Les jetons OIDC du compte
  (interface `vibe-rs`) vont, eux, **seulement** dans le trousseau (`vibe/setup/auth/_oidc_credentials.py:72`) : rien
  d'autre à recopier d'un PC vers la carte que la clé. Sous Korin, la façade fera la connexion déléguée (le lien
  s'ouvre dans Jet ou sur un autre appareil).
* Les arguments de `authenticate` (`action`, `attemptId`) vont **dans `_meta`** : la bibliothèque ACP passe `_meta`
  en arguments nommés à Vibe. `complete` attend côté agent que la connexion soit faite (jusqu'à l'expiration du
  lien, une dizaine de minutes) ; une première erreur `-32602 Browser sign-in status could not be retrieved` est à
  reprendre. La clé obtenue va dans `$VIBE_HOME/.env` (mode 600) quand il n'y a pas de trousseau, ce qui sera le cas
  sous Korin.
* `session/new` exige une clé ou une connexion : la façade doit traiter `-32000` en proposant la connexion.
* Mode par défaut `accept-edits` : les écritures ne demandent pas d'approbation ; la façade devra proposer `ask`.
* Démarrage sur le PC : `initialize` en 2 s, `session/new` 1 s de plus. `site-packages` : 115 Mo, 84 paquets.
