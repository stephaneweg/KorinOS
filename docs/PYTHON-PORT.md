# CPython sous Korin : le portage (étapes 1 et 2)

Suite de l'étude [PYTHON.md](PYTHON.md) (§2 construction, §2.5 bis `dlopen`, §9 étapes). Commencé le 11/11/2026 :
**CPython 3.14.10** tourne sous QEMU, avec OpenSSL, zlib, SQLite, expat, libffi et libmpdec, la bibliothèque standard
dans un zip, et `python3` hôte de `dlopen` (les modules d'extension se chargent en `.so`). **Pas encore essayé sur le
Pi.**

## Ce qui est livré

| Où | Quoi |
|---|---|
| `tools/python/build.sh` | la construction (WSL), de l'archive officielle à `build/python/sdroot` |
| `tools/python/config.site` | les réponses que `configure` ne peut pas obtenir en croisé ; `epoll` coupé |
| `tools/python/korin.patch` | trois retouches de CPython (plus bas) ; `tools/python/mkpatch.sh` le refait depuis deux arbres |
| `tools/python/mkzip.py` | la bibliothèque standard en zip, sources et `.pyc` en « unchecked-hash » |
| `tools/python/testimage.py` | la carte d'essai de QEMU (`build/python/sd.img`) : mot de passe d'accès « essai », `Lib/test` et les modules d'essai |
| `tools/python/tests/step2.py` | les essais de l'étape 2 hors de `regrtest` |
| `build.ps1` | recopie `build/python/sdroot` sur la carte |
| `skel/sd0/etc/paths.txt` | `/python = SD0:/python` (8 lignes sur 16) |

Sur la carte :

| Chemin | Contenu | Taille |
|---|---|---|
| `SD0:/bin/python3` | l'interpréteur, statique, hôte de `dlopen` (`-rdynamic`) ; tous les modules de la bibliothèque standard dedans (90 modules intégrés), OpenSSL compris | 17,3 Mo (dépouillé) |
| `SD0:/python/lib/python314.zip` | la bibliothèque standard (556 modules), sources et `.pyc`, compressée | 7,5 Mo |
| `SD0:/python/lib/python3.14/lib-dynload/` | les modules en `.so` (vide sur la carte livrée) | |
| `SD0:/python/lib/python3.14/site-packages/` | les paquets | |
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
* bz2, lzma, zstd ; `preload` de `python3` et `file_pin` du zip (étape 11) ; le module `korin` (étape 12) ; pip et un
  module compilé sur la carte (étape 3).
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
