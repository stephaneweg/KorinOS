# Python sous Korin OS (vers Mistral Vibe) : étude

Étude du 11/11/2026, avant tout code. Question : **porter CPython 3.12+ sous Korin vaut-il la peine, et comment ?** Deux
buts. D'abord faire tourner le **moteur de Mistral Vibe** (`mistral-vibe` 3.1.x, Apache 2.0), l'agent de code de
Mistral, couvert par l'abonnement Le Chat Pro (ou un compte gratuit avec quota), derrière **une façade native Korin à
nous** à la place de son interface en Rust. C'est la 4e voie vers un agent sur Korin, à côté de Node.js + Claude Code
2.1.112 ([NODEJS.md](NODEJS.md)) et du binaire natif de Claude Code ([etude/ETUDE-CLAUDE-NATIF.md](etude/ETUDE-CLAUDE-NATIF.md)).
Ensuite, à la demande du propriétaire, **Python comme langage général de Korin**, en particulier pour que l'agent
produise des **artefacts à la demande** (scripts, petites apps graphiques, graphiques, tableaux, documents, traitements de
fichiers) : §6. Et la question qui en découle : **un vrai `dlopen`** dans libposix, fait le 11/11/2026 (§2.5 bis).

Sources lues, sans rien cloner ni rien mettre dans le dépôt (fichiers isolés dans un dossier de travail hors du dépôt) :

* **mistral-vibe v3.1.2** (étiquette du 10/11/2026) par `raw.githubusercontent.com` et l'API GitHub : `pyproject.toml`,
  `uv.lock`, les ADR de `docs/adr/`, `docs/reference/app-server-protocol.md`, `docs/programmatic-mode.md`,
  `docs/acp-setup.md`, une vingtaine de modules de `vibe/` (`app_server/stdio.py`, `protocol.py`, `models.py`,
  `acp/agent.py`, `_experimental_harness.py`...), et **les 141 fichiers Rust de `harness/core/src`** (hors essais). Les
  références `vibe:fichier:ligne` s'y rapportent. Pour comparer : les `pyproject.toml` et `uv.lock` de v2.25.5 et de
  quelques autres étiquettes, les fiches PyPI (JSON) des paquets natifs et de `mistralai-vibe-local-harness`.
* **CPython v3.14.0** : `configure.ac`, `Modules/Setup.stdlib.in`, `Lib/importlib/_bootstrap*.py`,
  `Lib/ctypes/__init__.py`, `Lib/asyncio/unix_events.py` (références `cpython:fichier:ligne`).
* Le code et les documents de Korin (références `fichier:ligne` du dépôt).

**Rien de lourd n'a été téléchargé** : ni l'archive de CPython (≈ 23 Mo en `.tar.xz`), ni les caisses Rust, ni les roues
de PyPI. Ils le seront à l'étape 1 et à l'étape 4 (§9). Les décisions à prendre sont rassemblées à la fin (§10).

> **Constat principal.** Depuis la **v2.25.7** (22/09/2026), le moteur de Vibe n'est plus du Python seul : il exige le
> « **Unified Harness** », un cœur en **Rust** (`harness/core`, ≈ 49 900 lignes) chargé comme module d'extension
> `mistralai_vibe_local_harness._native`, **sans repli** depuis la 3.x (`vibe:vibe/_experimental_harness.py:11-18`). Ce
> cœur embarque **V8** par `deno_core` pour un seul usage, le « code mode » (le modèle écrit du TypeScript qui appelle
> les outils), isolé derrière une fonction (`vibe:harness/core/src/core/features/programmatic_tool_calling/code_mode/mod.rs:86-88`)
> et qu'un réglage coupe. Et **toutes** les versions de Vibe dépendent de `pydantic-core`, écrit en Rust. Donc :
> **pas de Vibe sur Korin sans une cible Rust pour Korin** ; V8, lui, s'évite par un petit patch (§4.1).

---

## 1. Ce que demande le moteur de Vibe

### 1.1 L'architecture

| Pièce | Langage | Rôle (ADR) | Sur Korin |
|---|---|---|---|
| `vibe-rs` (`vibe/cli-rust`, 678 fichiers) | Rust : ratatui, crossterm, tokio | l'interface en terminal, « client mince » de l'app-server par JSON-RPC 2.0 en lignes sur stdio (`vibe:docs/adr/0016-rust-cli-delivery-surface.md:5-9`) ; même `vibe -p` (mode programme) passe par lui (`vibe:vibe/cli/launcher.py:44,74`) | **remplacé par notre façade** (§5) |
| `vibe-app-server` (`vibe.app_server.stdio:main`) | Python | capacités du produit, configuration, intégrations, sessions, projections publiques (`vibe:docs/adr/0002-core-engine-and-delivery-surfaces.md:7-12`) | à faire tourner |
| `vibe-acp` (`vibe.acp.entrypoint:main`) | Python | le même moteur derrière l'**Agent Client Protocol** (Zed, JetBrains, Xcode) ; adaptateur en process sur l'app-server (`vibe:vibe/acp/agent.py:137-182`) | à faire tourner (protocole de la façade, §5.1) |
| Runtime du harness (`mistralai_vibe_local_harness`) | Python | exécution, ordre, annulation, approbations, persistance d'une session | à faire tourner |
| Cœur du harness (`_native`, `harness/core`) | **Rust** (pyo3) | contexte, transitions, planification des actions ; `HarnessSession.create/restore/apply/inspect/checkpoint` (`vibe:.../mistralai_vibe_local_harness/_native.pyi:13-31`) : du calcul pur, JSON en entrée et en sortie | **à compiler** (§3, §4.1) |

Les trois points d'entrée sont déclarés dans `vibe:pyproject.toml:174-177`. Le paquet se construit par **maturin**
(`vibe:pyproject.toml:145-172`) : la roue contient le Python, le module `_native` et le binaire `vibe-rs`.

### 1.2 L'histoire récente : où le Rust est entré

| Version (date) | Moteur | Cœur Rust | Python demandé |
|---|---|---|---|
| ≤ **2.24.5** (27/08/2026) | Python seul ; app-server présent depuis 2.23 ; interface Textual (Python) | aucun | ≥ 3.12 |
| 2.25.0 – **2.25.5** (18/09) | Python par défaut ; harness **facultatif** (`--experimental-harness`, paquet PyPI `mistralai-vibe-local-harness` 0.3.6 à 0.5.1, roues abi3 pour macOS, manylinux et Windows seulement) | facultatif | ≥ 3.12 |
| 2.25.7 – 2.26.1 | harness dans l'arbre, construit par maturin | dans le paquet | ≥ 3.12 |
| 3.0.0 (19/10) | interface Rust `vibe-rs`, harness exigé | exigé, avec `deno_core` | **≥ 3.14** |
| **3.1.2** (10/11) | idem | exigé, **sans repli** (« silently running a second engine would reintroduce the fallback », `vibe:vibe/_experimental_harness.py:11-18`) | ≥ 3.12 (`vibe:pyproject.toml:6`) ; `.python-version` : 3.12 |

Rythme : **8 versions entre le 19/10 et le 10/11/2026**, dépendances épinglées à la version près
(`vibe:pyproject.toml:30-124`, 93 dépendances).

### 1.3 Le cœur Rust

`vibe:harness/core/Cargo.toml:28-41` : `deno_ast` 0.53.3 (analyseur swc, transpilation TypeScript), **`deno_core` 0.409.0**
(V8 par `rusty_v8`, réacteur tokio), `glob`, `jsonschema` 0.49.1 (sans fonctions par défaut), `pyo3` 0.29 (`abi3-py312`),
`rmcp` 3.1.2 (types du protocole MCP seulement, `default-features = false`), `schemars`, `serde`, `serde_json`, `sha256`,
`uuid`. Chaîne demandée : **Rust 1.97.1**, édition 2024 (`vibe:harness/rust-toolchain.toml`).

Relevé sur les 141 fichiers (hors essais) :

* `deno_core`, `deno_ast` et `v8::` n'apparaissent **que dans deux fichiers** : `code_mode/mod.rs` et `code_mode/v8.rs`
  (4 505 lignes). `mod.rs` le dit (« The rest of Core depends on this evaluator-neutral interface. Deno and V8 types
  [...] stay inside `v8` », lignes 1-5) et ne présente au reste qu'`evaluate()` et `MAX_SOURCE_BYTES` (lignes 16-18,
  86-88).
* Le code mode est un **réglage** : `mode` et `sleep` sont des interrupteurs dont l'absence vaut « actif »
  (`programmatic_tool_calling/settings.rs:5-16, 26-45`).
* Hors code mode et bancs d'essai : `std::sync` (`Mutex`, `RwLock`, `LazyLock`, atomiques), `std::collections` ; **ni
  fichiers, ni réseau, ni process** (`std::fs` ne sert qu'aux fixtures d'essai, `std::thread` qu'au code mode et aux
  bancs). Le Runtime Python fait toutes les entrées et sorties.

Conséquence : sans le code mode, le cœur est du **Rust de calcul pur** ; la partie système de `std` dont il a besoin est
minime (allocation, verrous, `LazyLock`).

### 1.4 Les dépendances à extension native

Relevé sur `vibe:uv.lock` (paquets dont aucune roue n'est `none-any`), et sur qui les importe (`requires_dist` de PyPI,
dépendances inverses du verrou, recherche de code sur GitHub) :

| Paquet | Langage | Qui en a besoin | Obligatoire pour le moteur ? | Voie |
|---|---|---|---|---|
| **`_native`** (harness, dans le paquet) | Rust | tout le moteur (`vibe:.../mistralai_vibe_local_harness/__init__.py:3`, `runtime.py:13`) | **oui** (3.x) | **compiler** sans `deno_*` (§4.1) |
| **`pydantic-core`** 2.46.4 | Rust (pyo3, jiter, speedate, regex) | `pydantic` 2.13, partout (modèles du protocole, config, outils) | **oui** | **compiler** : pas de remplaçant en Python |
| **`rpds-py`** 0.30.0 | Rust | `referencing` → `jsonschema` 4.26, importé par le runtime du harness (`.../protocol.py`, `_mcp_models.py`...) et par `mcp` | oui | compiler ; à défaut, un `rpds` en Python (trois classes immuables) |
| **`cryptography`** 50.0.0 | Rust + OpenSSL (+ `cffi` demandé par `requires_dist`) | `authlib`, `joserfc`, `google-auth`, `pyjwt[crypto]`, `secretstorage` : connexion OIDC, fournisseurs Google | probablement pas avec une clé d'API, **à vérifier** (imports paresseux ?) | compiler contre l'OpenSSL de Korin |
| **`watchfiles`** 1.2.0 | Rust (`notify`) | importé au chargement par `vibe:vibe/app_server/_skill_watch.py:14` et `_pull_request_cache.py` | oui (import) | **remplacer** par un `watchfiles` en Python à sondage (`awatch`, `watch`, `Change`) |
| **`uuid-utils`** 1.0.0 | Rust | `vibe:vibe/app_server/_session_artifacts.py` | oui (import) | **remplacer** : `uuid.uuid7` existe dans la bibliothèque de Python 3.14 |
| **`tree-sitter`** 0.26.0 + **`tree-sitter-bash`** 0.25.1 | C | analyse des commandes bash pour les permissions (`vibe:vibe/core/utils/bash_parser.py:6-7`, `vibe/core/tools/builtins/_shell_permission_analysis.py`) | oui | **compiler** (C pur, sans dépendance) |
| `cffi` 2.0.0 | C + libffi | `cryptography` ; dépendance directe (pour `miniaudio`) | avec `cryptography` | compiler (libffi, §2.4) |
| `pyyaml` 6.0.3 | C (libyaml) facultatif | compétences (`vibe/core/skills/parser.py`) | oui | **pur Python** : sans `_yaml`, PyYAML se replie sur son analyseur en Python |
| `charset-normalizer` 3.4.7 | mypyc facultatif | `requests` | oui | roue pure (`py3-none-any`) |
| `websockets` 16.0 | C facultatif | — | — | roue pure |
| `protobuf` 6.33.6 | C (upb) facultatif | `opentelemetry-proto` (télémétrie) | non | roue pure ; `PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python` |
| `setproctitle` 1.3.7 | C | `vibe:vibe/app_server/stdio.py:177-184`, dans un `try` (« Cosmetic, so it must never block startup ») | non | omettre |
| `miniaudio` 1.71 | C | la voix ; le marqueur l'exclut déjà d'aarch64 Linux (`vibe:pyproject.toml:71, 126-137`) | non | omettre |

Sans extension native mais à surveiller : `keyring` (aucun magasin de secrets sous Korin : `NoKeyringError` est
rattrapé, `vibe:vibe/utils/keyring.py:11, 215`, et les secrets vont dans `$VIBE_HOME/.env`), `pexpect`/`ptyprocess`
(ptys), `gitpython` (lance `git`), `uvicorn`/`starlette` (MCP en HTTP, rappel OAuth), `truststore` (magasin de
certificats). Le bilan : **six paquets à compiler** (harness, pydantic-core, rpds-py, cryptography, cffi, tree-sitter ×2),
dont quatre en Rust, **deux à remplacer** par quelques dizaines de lignes de Python, le reste en Python pur.

---

## 2. Construire CPython pour Korin

### 2.1 Quelle version

**CPython 3.14** (3.14.10 est la dernière ; 3.15.2 existe aussi). Pourquoi pas 3.15 : Vibe déclare 3.12 à 3.14
(`vibe:pyproject.toml:23-25`), `pydantic-core` 2.46 et `rpds-py` 0.30 ne publient de roues que jusqu'à cp314
(`uv.lock`), et une compilation croisée de CPython demande **un Python de construction de même version**
(`--with-build-python`, `cpython:configure.ac:149-155`) : la machine de construction (WSL) a **Python 3.14.4**. 3.14
apporte aussi `uuid.uuid7` (remplace `uuid-utils`) et `compression.zstd`.

### 2.2 Le principe : « configure voit Linux, le compilateur ne voit pas `__linux__` »

`configure` refuse en croisé tout système qu'il ne connaît pas (`cpython:configure.ac:318-349` : Linux, Android,
Cygwin, iOS, Darwin, VxWorks, Emscripten, WASI ; sinon « cross build not supported for $host »). Deux façons :

1. comme Node et .NET : `--host=aarch64-unknown-linux-gnu` avec `CC=aarch64-onyx-elf-gcc` : `sys.platform` vaut
   `"linux"`, mais le C, compilé sans `__linux__` (`posix.specs` ne définit que `__unix__` et `__armpi__`,
   [POSIX.md](POSIX.md)), prend ses chemins POSIX génériques ; `configure` essaie chaque fonction en compilant et liant
   contre newlib + libposix : ce qui manque est vu comme manquant ;
2. un patch de `configure.ac` (une branche `*-*-onyx*` → `ac_sys_system=Korin`, `MACHDEP=korin`) et `sys.platform ==
   "korin"`, inconnu de tout le monde.

Recommandation : la 1 (§10, décision 3), pour la même raison qu'en [NODEJS.md](NODEJS.md) §10 décision 5. Un fichier
`tools/python/config.site` fixe les réponses que `configure` ne peut pas obtenir en croisé (`ac_cv_file__dev_ptmx=yes`,
`ac_cv_file__dev_ptc=no`, `ac_cv_buggy_getaddrinfo=no`...) et **coupe `epoll`** (`ac_cv_func_epoll_create=no`,
`ac_cv_func_epoll_create1=no`) : l'`epoll` de libposix est une émulation sur `poll` par tranches de 5 ms
([POSIX.md](POSIX.md):73), plus lente que `poll` lui-même ; sans lui, `selectors.DefaultSelector` et donc `asyncio`
prennent `PollSelector`.

### 2.3 Options de `configure`

```sh
CONFIG_SITE=tools/python/config.site \
CC=aarch64-onyx-elf-gcc CXX=aarch64-onyx-elf-g++ AR=aarch64-onyx-elf-ar \
CFLAGS="-O2 -specs=build/posix/lib/posix.specs" LDFLAGS="-specs=build/posix/lib/posix.specs -static" \
./configure --host=aarch64-unknown-linux-gnu --build=x86_64-pc-linux-gnu \
    --with-build-python=/usr/bin/python3.14 \
    --disable-shared --disable-ipv6 --without-ensurepip --disable-test-modules \
    --with-openssl=build/openssl --with-ssl-default-suites=openssl \
    --prefix=/python
make -j4 && make DESTDIR=build/python/sdroot install
```

| Option | Pourquoi |
|---|---|
| `--disable-shared`, `-static` | programmes statiques, pas de `libpython.so` ; `libpython3.14.a` est gardée pour relier d'autres modules plus tard (§2.5) |
| `--disable-ipv6` | Korin est IPv4 seulement ([POSIX.md](POSIX.md):421) ; `socket.has_ipv6` vaut `False` |
| `--without-ensurepip` | pip se pose à part (§2.7) |
| `--with-openssl=build/openssl` | l'OpenSSL 3.5.9 de Korin (`tools/openssl/build.sh`, [OPENSSL.md](OPENSSL.md)), déjà éprouvé par curl, git, .NET ; son magasin par défaut est `/etc/ca-bundle` ([OPENSSL.md](OPENSSL.md):80-83) : `ssl.create_default_context()` vérifie sans réglage |
| `--prefix=/python` | un chemin virtuel (§2.6) : `sys.prefix` sans `:` |
| `--with-system-libmpdec` (défaut) | se replie sur la copie de libmpdec fournie si la bibliothèque manque (`cpython:configure.ac:4173-4190`) |

### 2.4 Les modules de la bibliothèque standard

Tous **compilés dans l'interpréteur** (statique ; `Modules/Setup.local` avec `*static*`, ou `MODULE_*_STATE` de
`configure`). Sources (`cpython:Modules/Setup.stdlib.in`) et dépendances :

| Module | Ligne de `Setup.stdlib.in` | Dépendance | Sur Korin |
|---|---|---|---|
| `_ssl`, `_hashlib` | 155 | OpenSSL | `build/openssl` (déjà là) |
| `zlib` | 70 | zlib 1.3.1 | `src/extra/zlib-1.3.1` ([src/extra/README.md](../src/extra/README.md):17) |
| `_sqlite3` | 152 | SQLite | `src/extra/sqlite-3.50.4` (même ligne) ; **pas de verrous** (`flock`, `F_SETLK` rendent 0, [POSIX.md](POSIX.md):414) : sûr pour un process, pas pour deux sur la même base |
| `pyexpat`, `_elementtree` | 106 | expat | copie fournie par CPython (`Modules/expat`) |
| `_decimal` | 62 | libmpdec | copie fournie (§2.3) |
| `_ctypes` | 145 | **libffi** (à construire : `configure --host=aarch64-...`, cible `AARCH64` générique) | fermetures : pages `PROT_EXEC` permises ([POSIX.md](POSIX.md):394-397). `import ctypes` fait `PyDLL(None)` (`cpython:Lib/ctypes/__init__.py:557`) : `dlopen (NULL)` rend une poignée, donc l'import passe ; `python3` relié en `-rdynamic`, `CDLL(None)` voit toute la libc ; `CDLL("libc.so.6")` échoue (pas de `libc.so`, §2.5 bis) |
| `_zstd` (3.14) | 69 | libzstd | facultatif (Ubuntu `libzstd_1.5.7`) |
| `_bz2`, `_lzma` | 67-68 | bzip2, xz | facultatifs : à ajouter si un paquet en a besoin (`tarfile`, `shutil.unpack_archive`) |
| `select` (`poll`), `_socket`, `_posixsubprocess`, `fcntl`, `termios`, `resource`, `mmap`, `_multiprocessing`, `_asyncio`, `_json`, `_pickle`, `_struct`, `_datetime`, `unicodedata`, `math`, `cmath`, `_random`, `_blake2`, `_sha*`, `_md5`, `array`, `binascii`, `_csv`, `_contextvars`, `_queue`, `_bisect`, `_heapq`, `_statistics`, `_uuid` | — | aucune | C pur sur libposix |
| `readline`, `_curses` | — | GNU readline, ncurses | sans : le REPL de 3.14 est en Python (`_pyrepl`) sur `termios` et les séquences VT du Terminal ; ce qu'il lit des capacités du terminal (terminfo, `_curses`) est **à vérifier** à l'étape 1 ; repli : `PYTHON_BASIC_REPL=1` |
| `_tkinter`, `_dbm`, `_gdbm` | — | — | non |

`asyncio` est du Python sur `select`, `_socket` et `_asyncio`. Son « self-pipe » est un `socket.socketpair()` : sous
Korin un canal du noyau ([POSIX.md](POSIX.md):224-248), qui réveille `poll` tout de suite.

### 2.5 Les modules d'extension statiques (le repli)

Avant le 11/11/2026, `dlopen` d'un fichier échouait toujours ; c'était la seule voie, c'est maintenant le repli (§2.5 bis),
pour un `python3` minimal sans `-rdynamic` ou un module qui pose problème en `.so`. Chaque module natif est **relié dans
le binaire `python3`** et inscrit dans la table `PyImport_Inittab` :

* **C** (`tree_sitter`, `_cffi_backend`) : une ligne de `Modules/Setup.local` par module, sources et options.
* **Noms pointés** : `pydantic_core._pydantic_core`, `rpds.rpds`, `mistralai_vibe_local_harness._native`... Le
  chercheur des modules intégrés ne regarde que la table (`cpython:Lib/importlib/_bootstrap.py:986-987`, `if
  _imp.is_builtin(fullname)`), sans condition sur le chemin : un nom pointé dans la table devrait être trouvé quand son
  paquet parent (le Python de `pydantic_core/`, installé normalement) l'importe. Pas essayé à l'étape 3 : les modules C y ont marché en `.so`, ce repli n'a pas servi ; repli : un
  chercheur de 20 lignes dans `sys.meta_path` (posé par `sitecustomize`).
* **Rust** (§3) : chaque caisse pyo3 est construite en `rlib`, et **une seule** caisse « parapluie » (`korin-pyext`, en
  `staticlib`) les rassemble et réexporte leurs `PyInit_*`. Deux `staticlib` Rust reliées dans un même binaire
  apporteraient chacune leur copie de `std` (symboles en double) : une seule bibliothèque Rust par interpréteur. **Obsolète** depuis `dlopen` : à l'étape 5, chaque caisse pyo3 est sa `cdylib` ([PYTHON-PORT.md](PYTHON-PORT.md), « Étape 5 »).
* Un `pyrelink` sur la carte (GCC et `ld` sont dans l'OS, `/gcc`, [GCC.md](GCC.md)) aurait relié `libpython3.14.a` et
  un nouveau module en un nouveau `python3` ; `dlopen` le rend inutile.

### 2.5 bis `dlopen` : fait le 11/11/2026

Question du propriétaire (11/11/2026) : « il serait peut-être temps d'implémenter un dlopen, non ? ». La réponse de cette
étude était **oui, et tôt**. C'est fait le jour même : `dlopen.c` de libposix, dans main (`f38e5c27`, `06ef7995`,
`5fb03d1a`), décrit dans [POSIX.md](POSIX.md), section « dlopen ». Essai : groupe `dl` de `posixtest`, **24 réussis sous
QEMU et sur le vrai Pi 4** ; 26 sous QEMU avec les pages partagées et `dllinktest` (ces deux-là pas encore sur le Pi).

**Ce que libposix fournit.** `dlopen`, `dlsym` (`RTLD_DEFAULT`, `RTLD_NEXT`), `dlclose` (compteur, jamais de
déchargement), `dlerror` par thread, `dladdr`, `dl_iterate_phdr` qui liste les modules, `__tls_get_addr`. Relocations
immédiates `RELATIVE`, `ABS64`, `GLOB_DAT`, `JUMP_SLOT`, `TLSDESC`, `TLS_TPREL64`, `TLS_DTPMOD64`, `TLS_DTPREL64`,
`IRELATIVE`, `COPY`. `DT_NEEDED` cherchés par `DT_RPATH`, `LD_LIBRARY_PATH`, `DT_RUNPATH` (avec `$ORIGIN`), puis
`/usr/lib` ; jamais `SD0:/lib`, où vivent les kits. Les exceptions C++ traversent les modules (`dlopen` inscrit leur
`.eh_frame` par `__register_frame_info`). Le code des modules est projeté depuis leur fichier, donc partagé entre les
process. Un programme peut aussi être relié à des `.so`, chargés au démarrage avant ses constructeurs.

**Ce que cela impose à `python3`** :

| Point | Règle |
|---|---|
| l'hôte | `python3` se relie en `gcc -specs=posix.specs -rdynamic` ; il exporte libposix, newlib, libm et libgcc (`dlhost.ld`, calculé par `tools/posix/dlexport.sh`). Il ajoute `build/posix/lib/dlhost-cxx.ld` (libstdc++) dès qu'il charge un module C++ (numpy en a ; les modules Rust n'en ont pas besoin, leur dérouleur est celui de libgcc). `libpython` reste dans l'exécutable : pas de `libpython3.14.so` |
| la taille | un hôte emporte **toute** la libc : environ 5 Mo de plus (`posixtest` est passé de 0,3 à 5,8 Mo dépouillé). Pour `python3`, à côté de l'interpréteur et de sa bibliothèque standard, c'est acceptable |
| un module | `gcc -specs=posix.specs -fPIC -shared` (C++ : `-nostdlib++`) : il ne porte pas de libc, la spec ajoute libgcc et `dlmod.o`. C'est ce que fera `build_ext` de setuptools, une fois `sysconfig` réglé (`LDSHARED`, `CCSHARED`, `EXT_SUFFIX`) |
| le TLS | réserve de **32 Ko par thread** pour les `__thread` des modules ; la `std` de Rust en consomme (une par `cdylib`). Si numpy, pydantic-core, cryptography et le harness la dépassent ensemble, relier `python3` avec `-tls-reserve=0x40000` (étape 3 : 4 octets pour cffi, rien pour tree-sitter ; étape 5 : 0x70 à 0x80 octets par module Rust, moins de 400 octets pour les trois) |
| `ctypes` | `CDLL(None)` voit toute la libc de l'hôte. Il n'y a **pas de `libc.so`** (le propriétaire : « pas maintenant ») : `CDLL("libc.so.6")` et `ctypes.util.find_library("c")` échouent ; un correctif de deux lignes dans `ctypes/util.py` peut rendre `None` pour `c` et `m` |
| les outils | libtool et cmake ne savent pas encore que `aarch64-onyx-elf` fait des `.so` : à régler port par port (pour CPython, `configure` : `CCSHARED=-fPIC`, `LDSHARED='$(CC) -shared'`, `LINKFORSHARED=-rdynamic`) |

**Ce que cela change pour Python et pour cette étude :**

* les modules natifs deviennent des `.so` à côté de leur paquet, comme partout : plus besoin de **relier l'interpréteur**
  à chaque montée de `pydantic-core`, de `cryptography` ou du harness (le coût d'entretien le plus pénible du §6.6) ;
* chaque caisse pyo3 se construit en `cdylib` : plus de **caisse parapluie** (§2.5). Chacune garde sa copie de `std` Rust
  (quelques centaines de Ko par module) ;
* **pip installe des sources avec du C** sur la carte : le GCC de Korin compile, `dlopen` charge (`/gcc`, [GCC.md](GCC.md)) ;
  les roues de PyPI, elles, restent inutilisables (glibc ou musl) ;
* hors de Python : modules de Lua, de Node (`process.dlopen`, [NODEJS.md](NODEJS.md) §6.1), greffons d'apps C et C++.

Ce qu'il ne change pas : les programmes sans `-rdynamic` restent statiques et ne chargent rien, et les kits gardent leur
mécanisme à eux. La voie statique du §2.5 reste possible (un `python3` minimal, ou un module qui pose problème en `.so`).

### 2.6 Ce que libposix fournit à Python

Ce que Python demande au système, et ce qu'en dit Korin (les limites connues de [NODEJS.md](NODEJS.md) §4-§6 et de
[POSIX.md](POSIX.md) « Ce qui manque ») :

| Domaine | Python | Korin | Verdict |
|---|---|---|---|
| Process | `subprocess` : `_posixsubprocess.fork_exec` (`vfork` si présent, sinon `fork`) ; `posix_spawn` seulement avec la glibc | `fork` en copie sur écriture, vrai `exec`, `vfork` = `fork` ([POSIX.md](POSIX.md):106-162) | marche |
| **Tubes des fils** | `os.pipe()` (asyncio aussi : `socketpair` seulement sous AIX, `cpython:Lib/asyncio/unix_events.py:837-842`) | **tubes de vfs**, lisibles par les commandes en assembleur (`V_STDIO`) | marche — **le trou bloquant de libuv** ([NODEJS.md](NODEJS.md) §5.2 : `socketpair`) **ne se pose pas** |
| Fin des fils (asyncio) | `_PidfdChildWatcher` si `os.pidfd_open`, sinon `_ThreadedChildWatcher` : un thread par fils dans `waitpid` (`unix_events.py:64-67, 893`) | pas de `pidfd` ; `waitpid` attend dans le noyau (`SYS_WAIT`) | marche |
| ptys | `os.openpty` (repli sur `posix_openpt`/`grantpt`/`unlockpt`/`ptsname`), `pty.fork` (repli sur `openpty` + `fork` + `setsid` + `TIOCSCTTY`) ; `pexpect` | tout est là ; `pty.h` et `forkpty` absents ([NODEJS.md](NODEJS.md):294) | marche par les replis |
| Signaux | PEP 475 : Python relance seul après `EINTR` | `SA_RESTART` sans effet ([POSIX.md](POSIX.md):388) | marche |
| Boucle d'événements | `PollSelector` (§2.2) | `poll` dort dans le noyau si tous sont des canaux, sinon tours de **1 ms** ([POSIX.md](POSIX.md):425-429) | marche ; même coût au repos que pour Node |
| Fichiers | `os`, `pathlib`, `shutil`, `tempfile` | **64 fichiers ouverts**, une seule date (2 s sur FAT), **pas de liens** (`symlink` → `ENOSYS`), pas de verrous, chemins ≈ 254 octets ([POSIX.md](POSIX.md):409-414) | marche ; `venv` sans `--copies` échoue ; `EMFILE` à surveiller (sessions shell + journaux + serveurs MCP) |
| Remplacement atomique | `os.replace` (journaux et fichiers de session de Vibe) | `rename` sur un même volume sans copie ; écrasement d'une cible existante : **à vérifier** | essai à l'étape 2 |
| Casse des noms | `importlib` ne vérifie la casse que sous Windows et macOS (`cpython:Lib/importlib/_bootstrap_external.py:54-57`) | FAT32 et exFAT ignorent la casse | rare ; à noter |
| `.pyc` | invalidés par date et taille | date à 2 s près | bibliothèque précompilée en `unchecked-hash` (§2.7) |
| Réseau | `socket`, `ssl`, `getaddrinfo` dans un thread (asyncio) | IPv4 ; **`connect` toujours bloquant** (rend 0) ; **`bind` sur le port 0 sans port choisi** ([POSIX.md](POSIX.md):421-424, [DOTNET.md](DOTNET.md):291) | marche (asyncio prend un 0 pour « connecté » ; la boucle s'arrête le temps de la poignée de main TCP) ; port 0 : le même correctif de netd que pour Node ([NODEJS.md](NODEJS.md) §5.3) |
| `getifaddrs` | non demandé (`socket.if_nameindex` à part) | `ENOSYS` (`src/lib/posix/misc.c:676`) | sans objet |
| Temps | `time`, `zoneinfo` | **UTC seulement** ([POSIX.md](POSIX.md):444) | `zoneinfo` avec le paquet `tzdata` (pur) |
| Threads | pile par défaut du système | **256 Ko** par pthread ([POSIX.md](POSIX.md):458) ; CPython 3.14 lit les bornes par `pthread_getattr_np` (existe) | `-DTHREAD_STACK_SIZE=0x400000` : 4 Mo, peuplés à la demande ; pydantic et la récursion de `json` sont gourmands |
| Identité | `pwd`, `getpass` | uid 0, `getpwuid` ; `uname` rend `ARMPI` (`src/lib/posix/misc.c:149-155`) | `platform.system()` dira « ARMPI » |
| `/proc` | quelques usages en Python, tous dans des `try` | absent, sauf `/proc/self/exe` | à suivre |
| `multiprocessing` | 3.14 : `forkserver` par défaut, socket `AF_UNIX` nommé | pas d'`AF_UNIX` nommé ([POSIX.md](POSIX.md):415) | `set_start_method("fork")` ; Vibe ne s'en sert pas |
| **Chemins à volume** | `os.path.isabs("SD0:/x")` faux ; `os.getcwd()` rend souvent `SD0:/...` | le même problème que Node ([NODEJS.md](NODEJS.md) §6.3) : Vibe compare et recolle des `Path` partout | **même remède** : l'arbre POSIX `/sd0`, `/sd1`, `/ram0` dans vfs et `getcwd` POSIX dans libposix (décision 6 de [NODEJS.md](NODEJS.md)) |

À noter en passant : la page d'aide [libc_fr.md](../src/apps/help/data/api/libc_fr.md):56-64 dit encore « `fork`,
`vfork` : `ENOSYS` » et « `exec*` lance le programme, l'attend » ; [POSIX.md](POSIX.md):106-162 dit que `fork` et un vrai
`exec` existent depuis le 10/11/2026. La page est en retard (hors de cette étude).

### 2.7 Où vit Python sur la carte

| Quoi | Où |
|---|---|
| l'interpréteur (statique, ≈ 15 Mo seul, ≈ 40 Mo avec les modules Rust de Vibe : à mesurer) | `SD0:/bin/python3` (et `python`, copie) |
| la bibliothèque standard **dans un zip** précompilé (`compileall --invalidation-mode unchecked-hash`) : peu de fichiers ouverts, pas d'écriture de `__pycache__` sur la carte, lecture servie par le cache de fatfs | `SD0:/python/lib/python314.zip` |
| les paquets purs | `SD0:/python/lib/python3.14/site-packages` |
| le chemin virtuel | une ligne `/python = SD0:/python` dans `SD0:/etc/paths.txt` (**16 lignes au plus**, [FICHIERS.md](FICHIERS.md):167 : à compter avec `/sd0`, `/sd1`, `/ram0`, `/node`) |
| l'environnement | `PYTHONHOME=/python`, `PYTHONUTF8=1` (la locale de newlib est minimale), `PYTHONDONTWRITEBYTECODE=1` pour la bibliothèque |

`preload SD0:/bin/python3` (images épinglées, [WEBKIT.md](WEBKIT.md)) et `file_pin` du zip ([POSIX.md](POSIX.md),
« Garder un fichier en mémoire ») pour le démarrage.

### 2.8 pip et les paquets purs

pip est du Python pur ; sur Korin il voit la plateforme `linux_aarch64` sans glibc ni musl (`packaging` cherche la glibc
par `os.confstr` et musl par l'en-tête `PT_INTERP` de l'interpréteur) : **aucune roue binaire de PyPI ne correspond**, il
ne prend que les roues `none-any`, ou compile les sources (possible sur la carte depuis `dlopen`, §2.5 bis, avec `/gcc`). Donc :

* **voie A (recommandée d'abord)** : préparer `site-packages` sur le PC : `pip install --target ... --only-binary=:all:
  --no-deps` des paquets purs du verrou, plus le Python des paquets natifs (les fichiers `.py` de `pydantic_core`, de
  `rpds`, de `cryptography`, sans leur `.so`) ; copier sur la carte ou en faire un paquet `pkg` ;
* **voie B** : `pip install --only-binary=:all:` sur la carte, avec un fichier de contraintes qui marque les paquets
  natifs comme déjà fournis par l'interpréteur ; plus tard, un petit index local de « roues Korin » (métadonnées + Python
  seul).

Sur FAT32 : `--no-compile` ou un `compileall` après coup ; pas de liens (`venv --copies`) ; HTTPS vers `pypi.org` depuis
le Pi : comme npm, à essayer.

**Fait à l'étape 3** (12/11/2026, QEMU, [PYTHON-PORT.md](PYTHON-PORT.md) « Étape 3 ») : la voie A par
`tools/python/site-packages.sh` (pip, setuptools, cffi, tree-sitter sur la carte) ; les modules C construits sur le PC par
`tools/python/build-ext.sh` en roues `linux_aarch64` (pip de la carte les accepte : `sysconfig.get_platform()` y rend
`linux-aarch64`, retouche de `korin.patch`, et seules les roues `manylinux`/`musllinux` restent refusées) ; sur la carte,
`pip install` d'un paquet pur depuis PyPI et d'un paquet en C compilé par `/gcc`.

### 2.9 Durée

CPython : ≈ 500 fichiers C, `make -j4` en **10 à 20 min** la première fois (estimation : une construction native sur 4
cœurs prend ≈ 5 min ; la croisée n'y ajoute presque rien, le Python de construction étant déjà là). libffi, expat,
tree-sitter : quelques minutes. Rien de comparable aux 1 h 30 à 2 h 30 de Node ([NODEJS.md](NODEJS.md) §2.5).

---

## 3. La cible Rust

### 3.1 Pourquoi elle est inévitable

Korin n'a ni compilateur ni cible Rust ; ripgrep a été réécrit en C pour l'éviter ([RG.md](RG.md):99 : « La voie A (cible
Rust) n'a pas été nécessaire » ; [TERMINAL-ET-FORK.md](TERMINAL-ET-FORK.md):318-326). Pour Vibe, ce n'est plus possible :
`pydantic-core` et le cœur du harness (≈ 49 900 lignes) sont du Rust et n'ont pas de remplaçant. La
machine de construction n'a pas non plus de Rust (`cargo`, `rustup` absents de WSL, 11/11/2026).

### 3.2 (a) Compiler pour `aarch64-unknown-linux-musl` et lier contre newlib + libposix — **non**

La `std` et la caisse `libc` de la cible musl décrivent l'**ABI de Linux** : numéros d'erreur (Linux `ENOSYS` = 38,
newlib **88**, `aarch64-onyx-elf/include/sys/errno.h:130`), drapeaux (`O_CREAT` = `0100` sous Linux, **`0x0200`** sous
newlib, `sys/_default_fcntl.h:18, 52`), dispositions de `struct stat`, `dirent64`, `sigaction`, `pthread_*_t` (celles de
libposix suivent Onyx, [POSIX.md](POSIX.md) « En-têtes de la chaîne »). Et sous Linux la `std` fait ses verrous par
**`syscall(SYS_futex)`** : le `syscall` de libposix ne connaît que `gettid`, `getrandom` et `membarrier`
(`src/lib/posix/misc.c:245-257`). Il faudrait une libc à l'ABI de musl par-dessus libposix, traduisant tout : une couche
de compatibilité Linux en espace utilisateur, ce que la décision 0 de [NODEJS.md](NODEJS.md) a écarté. Des erreurs
silencieuses (un `errno` mal lu, un drapeau faux) plutôt que des refus de compiler.

### 3.3 (b) Une vraie cible `aarch64-unknown-korin` — **oui**

Sur le modèle des cibles Rust qui reposent déjà sur **newlib** : `armv6k-nintendo-3ds` (`target_os = "horizon"`),
`armv7-sony-vita-newlibeabihf` (`"vita"`), `riscv32imc-esp-espidf` (`"espidf"`) ; toutes `target_family = "unix"`,
`target_env = "newlib"`. La caisse `libc` a déjà un tronc `src/unix/newlib/` et un sous-module par système.

| Pièce | Travail |
|---|---|
| **Fichier de cible** `tools/rust/aarch64-unknown-korin.json` | `arch` aarch64, `os` `"korin"`, `env` `"newlib"`, `families` `["unix"]`, `linker` = `aarch64-onyx-elf-gcc` avec `-specs=build/posix/lib/posix.specs`, `crt-static-default`, `relocation-model` `static`, `panic-strategy` `unwind` (libgcc_eh de la chaîne : les exceptions C++ marchent déjà, `posixtest-cxx` 82/82) |
| **Caisse `libc`** | `src/unix/newlib/korin/mod.rs` : les constantes et structures de newlib 4.4 + libposix (`pthread_t`, `pthread_attr_t`, `stat`, `dirent`, `sockaddr`, `termios`, `pollfd`, `sigaction`, `epoll` non) relevées dans `build/posix/include` ; patch par `[patch.crates-io]` |
| **`std`** | ajouter `target_os = "korin"` aux listes `cfg` de `library/std/src/sys/pal/unix/` à côté de `horizon`/`espidf`/`vita` (fichiers, temps, threads par `pthread`, verrous par `pthread_mutex` comme ces cibles, aléa par `getrandom`) ; **mais** prendre la branche générique pour `process` (`fork` + `exec` existent : ni Horizon ni ESP-IDF ne l'ont) ; `-Zbuild-std=std,panic_unwind` |
| **Chaîne** | Rust **1.97.1** (celle du harness) + `rust-src` patché, dans une chaîne liée par `rustup toolchain link korin ...` ; `-Zbuild-std` demande nightly, ou `RUSTC_BOOTSTRAP=1` sur la stable |
| **Caisses à `cfg` par système** | `getrandom` (moteur `custom` sur `getrandom` de libposix), `pyo3-build-config` (fichier `PYO3_CONFIG_FILE` : CPython 3.14, statique, abi3), `openssl-sys` (`OPENSSL_DIR=build/openssl`, `OPENSSL_STATIC=1`), éventuellement `memchr`, `ahash`, `parking_lot_core` (branches `unix` génériques) |
| **Essai** | `std` : un `hello`, `std::thread`, `std::fs`, `std::process::Command("ls")`, `std::net::TcpStream` vers 127.0.0.1 ; puis une partie des essais de `std` lancée sur la carte |

Ce travail sert au-delà de Vibe : **le vrai ripgrep**, et les outils en Rust en général (`fd`, `uv`, `ruff`...). Ordre de
grandeur : la caisse `libc` ≈ 1 000 lignes de déclarations, la `std` ≈ 50 à 100 branches `cfg` et quelques fichiers ;
moins qu'un V8 ([NODEJS.md](NODEJS.md) §4), plus qu'un `rg` en C. Le risque est l'**entretien** : la cible n'est pas dans
l'amont, chaque version de Rust demande de rejouer le patch de `std` (§6.6).

### 3.4 (c) Remplacer les paquets

| Paquet | Remplaçable ? |
|---|---|
| `uuid-utils` | **oui** : `uuid_utils/__init__.py` de 30 lignes sur `uuid.uuid7` de 3.14 |
| `watchfiles` | **oui** : `awatch`/`watch`/`Change` par sondage (`os.scandir` + `stat` toutes les secondes) ; la cible Rust le compilerait aussi (`notify` a un `PollWatcher` ; qu'il le prenne d'office pour un système inconnu est à vérifier) |
| `setproctitle`, `miniaudio` | omis (§1.4) |
| `rpds-py` | possible (trois classes immuables en Python, plus lent) ; inutile une fois la cible Rust faite |
| `cryptography` | une version d'avant Rust (3.3, 2020) ne convient plus à `authlib` et `joserfc` d'aujourd'hui : compiler |
| `pydantic-core`, harness | **non** |

### 3.5 Verdict

**(b) + (c)** : la cible Rust est le prix d'entrée ; `uuid-utils` et `watchfiles` se remplacent pour réduire ce qu'elle
doit savoir compiler (pas de `notify`, pas de `rand`).

---

## 4. Le moteur de Vibe sur Korin

### 4.1 Quelle version, quel patch

| Version | Pour | Contre |
|---|---|---|
| **3.1.x + patch « sans code mode »** (recommandée) | à jour ; le cœur sans V8 est du Rust de calcul pur (§1.3) ; c'est le moteur que Mistral entretient | un patch à rejouer à chaque version (petit, à un seul endroit) ; un outil en moins (le modèle n'écrit pas de TypeScript) |
| 2.24.5, figée | moteur tout Python, ni harness ni `deno` | figée comme Claude Code 2.1.112 ; ancienne API de l'app-server ; `pydantic-core` reste en Rust : **la cible Rust est nécessaire quand même** |
| 3.1.x avec V8 | complète | `rusty_v8` construit V8 par **gn + clang de Chromium** pour un `target_os` inconnu : tout le travail V8 de [NODEJS.md](NODEJS.md) §4 dans une autre chaîne de construction. Non |

Le patch `tools/vibe/korin.patch` :

1. `harness/core/Cargo.toml` : `deno_ast` et `deno_core` deviennent `optional`, derrière une fonction `code-mode`
   (absente sous Korin) ;
2. `code_mode/mod.rs` : sans `code-mode`, `evaluate()` rend `Err("code mode is not available on this platform")` et
   `MAX_SOURCE_BYTES` garde sa valeur ; `mod v8` n'est compilé qu'avec la fonction ;
3. le réglage `programmatic_tool_calling.mode` à « off » par défaut sous Korin, pour que l'outil ne soit pas proposé au
   modèle (clé exacte de `config.toml` à relever à l'étape 0) ;
4. les deux remplaçants en Python (§3.4) dans `site-packages` ;
5. le module `_native` inscrit dans la caisse parapluie (§2.5), sans maturin.

Rien d'autre dans le code Python de Vibe n'est à toucher, si l'étape 0 (§9) le confirme sur le PC.

### 4.2 Ce que le moteur fait du système

| Besoin | Comment | Korin |
|---|---|---|
| shell (outil `bash`, ou les « managed shell tools » : `bash`, `bash_output`, `bash_stdin`, sessions, journaux sous `~/.vibe/shell-tool/`, `vibe:docs/managed-shell-tools.md:1-17`) | `subprocess`/`pexpect` sur un pty | bash 5.2, ptys de vterm ; commandes de `/bin` en assembleur, peu d'options GNU (comme pour Claude Code, [NODEJS.md](NODEJS.md) §7.3) |
| recherche (outil `grep`) | `rg` ou `grep` (`vibe:vibe/core/tools/builtins/grep.py:86-89`) | `rg` de [RG.md](RG.md) |
| git | `gitpython` (lance `git`), `giturlparse` | git 2.51 |
| fichiers | `read_file`, `write_file`, `edit` | chemins POSIX (§2.6, dernière ligne) |
| réseau | `httpx2`/`httpcore2`, `mistralai` 3.5 (SDK), TLS par `ssl` + `truststore` | `api.mistral.ai` en HTTPS, OpenSSL de Korin |
| serveurs MCP | sous-process en stdio, ou HTTP | tubes de vfs ; HTTP local : port 0 (§2.6) |
| `AGENTS.md`, compétences | lus dans le dossier de travail (`vibe:docs/programmatic-mode.md:221`), compétences en YAML + Markdown | le `AGENTS.md` et les `.agents/skills` écrits par l'autre session serviront tels quels |
| télémétrie | `opentelemetry`, `sentry-sdk` | à couper par la configuration |

### 4.3 La connexion

Vibe demande « sign in with your Mistral account or an API key » (`vibe:README.md:46`) : la connexion par navigateur
(OIDC, rappel sur un serveur local, `vibe/setup/auth/browser_sign_in.py`, `_oidc_callback.py`) ou une clé. Sous Korin,
dans l'ordre : **recopier les identifiants** d'un PC où Vibe est connecté (`$VIBE_HOME/.env`, puisque le trousseau
n'existe pas sous Korin, §1.4) ; une **clé d'API** (`MISTRAL_API_KEY`) ; plus tard la connexion par Jet sur la carte
(serveur local : port fixe ou port 0, à relever). Le Chat Pro couvre l'usage par le compte ; le compte gratuit, avec quota.

### 4.4 Licences

Vibe : **Apache 2.0** ; CPython : licence PSF ; pydantic-core, rpds-py, cryptography (Apache/BSD), tree-sitter (MIT) :
**tout se redistribue dans un paquet de Korin**, contrairement à Claude Code ([NODEJS.md](NODEJS.md) §7.2). Le nom
« Mistral Vibe » reste une marque de Mistral : la façade porte un nom à nous (§10, décision 9).

---

## 5. La façade native

### 5.1 Deux protocoles possibles

| | app-server (`vibe-app-server`) | ACP (`vibe-acp`) |
|---|---|---|
| Nature | protocole **interne** de Vibe : JSON-RPC 2.0 en lignes sur stdio (`vibe:docs/adr/0016-rust-cli-delivery-surface.md:7-9`) | **Agent Client Protocol**, standard public (Zed, JetBrains, Xcode, Neovim), `agent-client-protocol==0.11.0` (`vibe:pyproject.toml:31`) |
| Taille | **164 méthodes** (`vibe:vibe/app_server/protocol.py:113-277`), notifications à numéros d'événement et filigrane, entrées d'historique modifiées par **JSON Patch** (`protocol.py:3166-3196`), files d'attente, rappels, « handoffs » | une douzaine de méthodes et de types de mises à jour |
| Rigueur | champs inconnus refusés (« Unknown protocol methods, fields and variants fail explicitly », `vibe:docs/reference/app-server-protocol.md:516`) | versionné, tolérant |
| Stabilité | change avec chaque version de `vibe-rs` (8 versions en 3 semaines) | stable ; l'adaptateur de Vibe suit l'app-server pour nous (`vibe:vibe/acp/agent.py:137-182, 732-762`) |
| Moteur | le même | le même, en process |
| Autres agents | Vibe seul | tout agent ACP (Gemini CLI ; Claude Code et d'autres par des adaptateurs) |

**Recommandation : ACP** (§10, décision 6). La façade suit un standard au lieu de courir après le protocole interne de
Vibe, et elle reste utile si un autre agent arrive sur Korin.

### 5.2 Ce que la façade parle (ACP sur stdio)

Le client lance `python3 -m vibe.acp.entrypoint` (ou un script `#!` `vibe-acp`) avec deux tubes ; stdout ne porte que du
JSON-RPC, les journaux vont sur stderr (`vibe:docs/reference/app-server-protocol.md:23-26`, même règle pour ACP).

| Sens | Message | Rôle |
|---|---|---|
| client → agent | `initialize` | versions, capacités du client (fichiers, terminal : à ne **pas** annoncer d'abord, l'agent fait alors tout lui-même) ; réponse : capacités de l'agent, méthodes d'authentification (`vibe:vibe/acp/agent.py:475`) |
| client → agent | `authenticate` | si demandé (`agent.py:539`) |
| client → agent | `session/new` `{cwd, mcpServers}` ; `session/load`, `session/list`, `session/resume`, `session/fork` | ouvrir ou reprendre une session (`agent.py:545-598, 1049-1153`) |
| client → agent | `session/prompt` `{sessionId, prompt: [blocs texte, image, ressource]}` | un tour ; la **réponse arrive à la fin du tour** avec `stopReason` (`agent.py:810-842`) |
| agent → client (notification) | `session/update` : `user_message_chunk`, `agent_message_chunk`, `agent_thought_chunk`, `tool_call`, `tool_call_update` (état, sortie, diff), `plan`, `available_commands_update`, `usage_update`, `session_info_update`, `config_option_update` (`vibe:vibe/acp/session_updates.py:9-27` ; `agent.py:1215-1220, 1796-1828`) | le flux : texte au fil de l'eau, appels d'outils et leur progression |
| agent → client (requête) | **`session/request_permission`** `{sessionId, toolCall, options}` (`agent.py:902-912`) | **l'approbation d'un outil** ; le client répond par l'option choisie. Vibe y projette ses décisions `approve`, `approve_for_session`, `approve_permanently`, `deny`, `cancel_turn` (`vibe:vibe/app_server/models.py:320-345`) ; les questions du modèle à l'utilisateur (`ask_user_question`) passent par le même canal (`agent.py:965`) |
| client → agent (notification) | `session/cancel` | Échap : interrompre le tour (`agent.py:997`) |
| client → agent | `session/set_mode`, `session/set_config_option` | mode d'approbation (`ask`, `accept-edits`, `auto-approve`), modèle (`agent.py:1071-1082`) |
| les deux | `_vibe/...` (`ext_method`, `ext_notification`) | extensions de Vibe : confiance du dossier, retour arrière, boucles, revue (`agent.py:1177-1666`) ; facultatives |

Pour mémoire, l'équivalent dans l'app-server : `initialize` → `initialized` → `session/start` → `turn/start` ;
notifications `turn/started`, `history/entryAdded`, `history/entryUpdated` (JSON Patch), `turn/completed`,
`session/snapshot`, `stats/updated` ; approbations par **`callback/call`** du serveur, accusé, puis **`callback/result`**
du client (`vibe:docs/reference/app-server-protocol.md:36-43, 196-206`).

### 5.3 La forme de la façade

Une bibliothèque C commune, **`libacp`** (client JSON-RPC 2.0 en lignes sur deux tubes, lecteur et écrivain, table des
requêtes en vol, analyseur JSON), et deux clients, dans cet ordre :

1. **`agent`, client terminal en C** (`src/bin/agent` ou `tools/agent/`, comme `rg`) : lancé dans le Terminal (pty) ;
   invite d'une ligne, texte du modèle en flux, appels d'outils sur une ligne (`▸ bash: git status` puis son état),
   diff des modifications en couleur, approbation par une question `[o]ui / [s]ession / [t]oujours / [n]on`, Échap pour
   `session/cancel`, Ctrl+D pour finir. Petit, utilisable par SSH ou depuis le shell, et le banc d'essai du protocole.
2. **une app UIKit** : la conversation en bulles, une fenêtre modale d'approbation (`WF_MODAL`, [UIKIT-MANQUES.md](UIKIT-MANQUES.md)),
   la vue des diffs, la liste des sessions, le mode poche plein écran ([MODE-POCHE.md](MODE-POCHE.md)), le thème du bureau
   (jetons d'UIKit). **PocketQI** a déjà, en C++ sur UIKit, une conversation, un analyseur JSON (`src/apps/pocketqi/json.h`)
   et des appels à Mistral (`brain.h:212`) : à reprendre plutôt qu'à refaire, ou à y intégrer comme un mode « code ».

Le moteur reste un process fils en Python : la façade démarre vite, le moteur peut mettre quelques secondes (§8, risque 4)
et le client l'affiche.

---

## 6. Python, langage de Korin, et les artefacts à la demande

Précision du propriétaire (11/11/2026) : Python ne sert pas qu'au moteur de Vibe ; ce serait « une grosse plus-value »
pour Korin en général, « surtout si Vibe peut par exemple, à la demande de l'utilisateur, faire des artefacts ». Cette
partie traite Python comme **langage général du système** et ce qu'un agent pourrait produire avec.

### 6.1 Ce que Python apporte à Korin

| Usage | Détail |
|---|---|
| Les scripts du dépôt sur la carte | `tools/mkimages.py`, `tools/gcc/pitest.py` ([GCC.md](GCC.md):79), `tools/rg/difftest.py` et `cardtest.py`, `tools/tz/mktz.py`, `tools/fas_sym.py`, les générateurs `tools/kits/abi2*.py`... : aujourd'hui seulement sur le PC |
| L'auto-hébergement de FASMARM | `tools/x86toa64.py` (≈ 3 100 lignes) régénère `fasmarm.asm` ; sa réécriture en commande Korin est au programme ([FEUILLE-DE-ROUTE.md](FEUILLE-DE-ROUTE.md):810-813) : avec Python, il tourne tel quel (l'ordre des tables dépend du hachage : `PYTHONHASHSEED` fixe) |
| Les constructions qui demandent Python | meson (Mesa et beaucoup de projets C), les générateurs de Mesa (mako), gyp de Node, `configure` de nombreux paquets : un pas vers construire plus de ports **dans** l'OS |
| Serveurs MCP et outils d'agent | l'écosystème MCP est surtout en Python et en TypeScript : un agent sur Korin pourra en lancer |
| **Le langage de script de l'utilisateur** | à côté de bash et du BASIC ([BASIC.md](BASIC.md)) : un langage connu de tous, une bibliothèque standard large (fichiers, CSV, JSON, SQLite, HTTP, ZIP, dates, expressions régulières), et, par le module `korin` (§6.2), les kits du système : des fenêtres UIKit, du son, le réseau, le bus, les broches GPIO |
| **Les artefacts à la demande** (§6.3) | ce qu'un agent écrit et lance pour l'utilisateur : le langage que les modèles écrivent le mieux, et celui de l'écosystème des graphiques et des données |
| La cible Rust (§3) | le vrai ripgrep et les outils en Rust ; elle vient avec ce chantier |

### 6.2 Le module `korin` : les kits depuis Python

Les kits sont des tables de fonctions en assembleur, ouvertes par nom (`armpi_kit_open`) et appelées par rang
(`armpi_kit_fn`), qui suivent l'AAPCS64 et **ne prennent que des entiers et des pointeurs** : « chaque argument est un
`long` » (`tools/kits/abi2c.py:6-10`). Douze kits, ≈ 600 fonctions : UIKit 214, AudioKit 81, DocumentKit 60, AppKit 53,
ArchiveKit 36, PrinterKit 35, GpioKit 34, NetKit 33, MediaKit 15, GpuKit 12, EmuKit 11, BusKit 9 (lignes `kit_fn` des
`src/kits/*/*.abi`). Deux façons de les appeler depuis Python :

| | **ctypes par adresse** | **extension C générée** (recommandée) |
|---|---|---|
| Principe | un tout petit module intégré `_korinkit` rend l'adresse de la table (`armpi_kit_open`) et d'une fonction (`armpi_kit_fn`) ; `ctypes.CFUNCTYPE(c_long, c_long, ...)(adresse)` en fait une fonction Python. `import ctypes` passe (§2.4 : `dlopen (NULL)` rend une poignée) | `tools/kits/abi2py.py`, frère de `abi2c.py`, `abi2bas.py`, `abi2bi.py` et `abi2cs.py` : depuis chaque `.abi`, un module C (`_uikit`, `_audiokit`...) en `.so` dans `korin/` (§2.5 bis ; ou intégré à l'interpréteur, §2.5), et un `korin/uikit.py` de constantes (les `.inc`, comme `--const`) |
| **Retours multiples** | ctypes ne lit que `x0`. Or des fonctions rendent `x1`, `x2` (`ui_image_size`, `ui_surface_pixels`, `ui_w_event`...) : l'en-tête C a pour cela `uikit_call3` en assembleur en ligne (`src/kits/uikit/include/uikit.h:63-77`) | le générateur lit le commentaire (« `-> x0 = largeur, x1 = hauteur, x2 = pixels` », `src/kits/uikit/uikit.abi:128`) et rend un tuple |
| Le GIL | ctypes le relâche pendant l'appel | relâché autour des attentes (`ui_event`, `bus_wait`, `audio_*` bloquants) |
| Coût | rien à compiler ; plus lent ; erreurs de type silencieuses | un générateur de ≈ 300 lignes, refait à chaque kit qui grandit (lignes ajoutées à la fin du `.abi`) |
| Usage | essais, et tout kit pas encore généré | le module livré |

Au-dessus, **`korin`**, en Python, pour l'utilisateur et pour l'agent :

* `korin.ui` : `Window`, la boucle (`ui_event` attend avec un délai et rend un type : un modèle **par sondage, sans
  rappel**, `src/apps/help/data/api/uikit_fr.md:25-45`, qui se marie bien avec Python et avec `asyncio`), les widgets
  (boutons, champs, listes, onglets), les dialogues (fichier, message), les menus ; le thème du bureau par les jetons
  d'UIKit, comme tout port ([UIKIT-MANQUES.md](UIKIT-MANQUES.md)) ; un `Canvas` dont les pixels (`ui_canvas_pixels`,
  `0xAARRGGBB`) se voient comme un `memoryview` : Pillow et numpy y écrivent directement.
* `korin.audio` (jouer un tampon, un fichier), `korin.net` (ce que `socket` ne dit pas : `net_info`), `korin.bus`
  (publier, s'abonner : thème, volumes, réseau, [BUS.md](BUS.md)), `korin.gpio` (les broches, comme GPIO Lab),
  `korin.print` (PrinterKit), `korin.archive`, `korin.document` (DocumentKit : XML, OOXML, ODF).
* `korin.open(chemin)` = `ui_assoc_open` (le fichier dans son app : Sheet pour un XLSX, Lecteur PDF, Visionneuse,
  Letters, `src/kits/uikit/uikit.abi:211`) ; `korin.capture()` = `ui_screen_grab` (l'écran tel qu'il est montré,
  `uikit.abi:215`, version 210).

Un seul thread doit parler à UIKit (celui de la fenêtre), comme en C.

### 6.3 Les artefacts à la demande

Ce que l'utilisateur demande à l'agent (« fais-moi un graphique de mes dépenses », « un petit minuteur », « convertis ces
photos », « un tableau des températures du mois ») et ce que l'agent écrit en Python :

| Genre d'artefact | Exemple | Ce qu'il faut | Où il se voit |
|---|---|---|---|
| **Script** | renommer des photos par date, extraire un CSV, interroger une API | la bibliothèque standard | sa sortie dans la façade ou le Terminal |
| **Traitement de fichiers** | réduire des images, assembler des PDF, nettoyer un CSV, archiver | standard + **Pillow** ; `pypdf` (pur) ; ArchiveKit ou `zipfile` | les fichiers produits, ouverts par `korin.open` |
| **Tableau** | un XLSX mis en forme, avec formules | `openpyxl` (pur) ; CSV | **Sheet** (`opens = xlsx ods csv tsv`, `src/apps/sheet/app.txt:4`) |
| **Graphique** | courbes, barres, camemberts, cartes de chaleur | **numpy + matplotlib** (sortie PNG, SVG, PDF) | la Visionneuse (`png`, `src/apps/imageview/app.txt`), le Lecteur PDF, ou une fenêtre (backend UIKit, §6.4) |
| **Document** | un rapport, une lettre, une fiche | DOCX : `python-docx` (demande `lxml`) ; ODT ou DOCX écrits par `zipfile` + gabarits, sans dépendance ; PDF : `reportlab` (Pillow) ou `matplotlib` ; HTML pour Jet | **Letters** (`opens = rtf docx odt txt`), Lecteur PDF, Jet |
| **Petite app graphique** | minuteur, convertisseur, tableau de bord du réseau, jeu simple | `korin.ui` ; pour un jeu, pygame sur la SDL déjà portée ([SDL.md](SDL.md)) | sa fenêtre sur le bureau ; gardée comme **app** : `SD0:/apps/<nom>.app/main` en script `#!/bin/python3` + `app.txt` (vfs lance un script par son interprète, [FICHIERS.md](FICHIERS.md):125, 183-214) : elle apparaît dans le lanceur |
| **Matériel** | allumer une DEL, lire un capteur, un son d'alerte | `korin.gpio`, `korin.audio` | l'effet lui-même |

### 6.4 Les bibliothèques tierces à porter, par priorité

Leurs modules C deviennent des `.so` dans leur paquet (§2.5 bis) ; la voie statique (§2.5) reste le repli. Les dépendances C sont
déjà dans `src/extra` pour la plupart (pour WebKit, MuPDF, MediaKit : [src/extra/README.md](../src/extra/README.md)).

| Priorité | Bibliothèque | Langage, construction | Dépendances | Coût estimé |
|---|---|---|---|---|
| 1 | **Pillow** | C, `setup.py` ; ≈ 6 modules (`_imaging`, `_imagingft`, `_imagingmath`, `_imagingmorph`, `_webp`...) | zlib, libjpeg-turbo 3.1.4, libpng, freetype 2.14.3, libwebp 1.4.0 : **tous dans `src/extra`** | **faible** : quelques jours |
| 1 | `openpyxl`, `et-xmlfile`, `pypdf`, `tabulate`, `jinja2`, `markdown`, `fonttools`, `cycler`, `pyparsing` | Python pur | — | **nul** : `site-packages` |
| 2 | **numpy** 2.x | C (et un peu de C++), **meson** + `meson-python` ; Cython seulement sur le PC (sources générées) ; ≈ 10 modules (`_multiarray_umath`, `_umath_linalg`, `random/*`, `fft/_pocketfft_umath`, `linalg/lapack_lite`...) | BLAS/LAPACK facultatifs (sans : `lapack_lite` fourni, plus lent) ; NEON pris par la répartition SIMD | **moyen** : fichier croisé de meson, liste des modules, essais de `numpy.test` choisis |
| 2 | **matplotlib** | C++ (`_path`, `_image`, `ft2font`, `_tri`, `_qhull`, `_c_internal_utils`), meson ; `kiwisolver` (C++), `contourpy` (C++, pybind11) | numpy, Pillow, freetype, qhull (fourni) | **moyen**, après numpy |
| 2 | **backend UIKit** de matplotlib (`module://korin.mpl_backend`) | Python, ≈ 300 lignes : `FigureCanvasAgg` dessine dans un tampon RGBA, copié dans le `Canvas` de `korin.ui` (canaux réordonnés en `0xAARRGGBB` par numpy) ; souris et molette changées en événements de matplotlib (zoom, déplacement) | `korin.ui` | **faible** ; sans lui, `savefig` + la Visionneuse suffit au début |
| 3 | `lxml` (pour `python-docx`, `python-pptx`) | C (Cython sur le PC) | **libxml2 2.13.8** (dans `src/extra`) + **libxslt** (à ajouter) | moyen ; à défaut, DOCX écrit par `zipfile` (§6.3) |
| 3 | `reportlab` | Python + accélérateurs C facultatifs | Pillow | faible |
| 3 | `pygame-ce` | C sur SDL2 | `sdl2-compat` + SDL 3 déjà portés ([SDL.md](SDL.md)) | moyen |
| 4 | `pandas` | C et Cython, beaucoup | numpy | élevé ; plus tard, si les données tabulaires l'exigent |
| — | `scipy` | C, C++ **et Fortran** | la chaîne `aarch64-onyx-elf` n'a pas de `gfortran` (11/11/2026) | écarté |

Tout cela **ne demande pas la cible Rust** : Python + Pillow + numpy + matplotlib + `korin` se construisent avec la chaîne
C actuelle. La partie « langage général » ne dépend donc pas du coût le plus incertain de l'étude.

### 6.5 Comment l'agent lance et montre le résultat

1. **Écrire** l'artefact dans un dossier à lui : `SD0:/home/artifacts/<date>-<nom>/` (script, données, sortie), jamais
   sur `SD1:`, la partition de l'utilisateur, sauf demande ([libc_fr.md](../src/apps/help/data/api/libc_fr.md):40).
2. **Lancer** par l'outil shell de Vibe (`python3 script.py`) ; la sortie texte revient au modèle comme toute commande.
   Une app graphique ouvre sa fenêtre sur le bureau, comme tout programme lancé du Terminal.
3. **Vérifier** : l'agent ne voit pas l'écran. `korin.capture(fenêtre)` (ou la commande `screenshot`, BMP de l'écran
   composé, `src/bin/screenshot/screenshot.asm:1-4`) donne une image que le modèle lit : Vibe sait passer des images au
   modèle (`mistralai_vibe_local_harness/vibe/_file_images.py`, `vibe/app_server/_vision.py`), Mistral a des modèles
   qui voient. L'agent corrige, relance.
4. **Montrer** :
   * dans la **façade** (§5.3) : ACP porte des images et des liens de ressource dans les messages et les résultats
     d'outils ; l'app UIKit affiche un PNG ou un graphique **dans la conversation**, avec « Ouvrir » (`korin.open`) et
     « Enregistrer sous… » ; le client terminal affiche le chemin ;
   * dans **l'app du fichier** : `korin.open` (Sheet, Lecteur PDF, Visionneuse, Letters) ;
   * **gardé** : une petite app devient un `.app` du lanceur (§6.3), ou un paquet `pkg` si l'utilisateur veut la
     partager.
5. Une **compétence** de l'agent (`.agents/skills/`, que la session parallèle écrit pour Korin) décrit `korin`, le dossier
   des artefacts, `korin.open` et `korin.capture`, pour que le modèle s'en serve sans tâtonner.

Les artefacts tournent avec tous les droits (un seul utilisateur, uid 0) : l'approbation des commandes par Vibe (§5.2)
est le garde-fou, comme pour le reste de l'agent.

### 6.6 Le coût d'entretien

| Pièce | Rythme | Coût |
|---|---|---|
| CPython 3.14.x | une version corrective tous les deux mois | refaire `build.sh` : quelques minutes ; le patch de `configure` reste vide avec la voie 1 (§2.2) |
| CPython 3.15, 3.16... | une par an | `config.site`, `Setup.local` à revoir |
| Rust (`std` patchée) | Vibe épingle sa version (1.97.1) | **le plus lourd** : rejouer le patch de `std` et de `libc` à chaque montée ; ne monter qu'avec Vibe |
| Vibe | chaque semaine, dépendances épinglées | avec `dlopen` (§2.5 bis), une montée de `pydantic-core`, `cryptography`, `rpds-py` ou du harness remplace un `.so` (en statique, il faudrait relier l'interpréteur). Rejouer `korin.patch` ; refaire `site-packages`. Suivre une version par mois, pas chaque version |
| Pillow, numpy, matplotlib | quelques versions par an | refaire leurs `.so`, sans toucher à l'interpréteur |
| `korin` (module des kits) | avec les kits | régénéré par `abi2py.py` quand un `.abi` grandit, comme les en-têtes C |
| La façade | à notre rythme | ACP est stable ; une montée d'ACP se suit sans urgence |

---

## 7. Comparaison avec Node + Claude Code 2.1.112

| | Node + Claude Code 2.1.112 ([NODEJS.md](NODEJS.md)) | Python + Vibe 3.1.x |
|---|---|---|
| Moteur à porter | **V8 avec JIT** (couche système, mémoire, caches sur 4 cœurs réels), libuv, le C++ de Node, ICU complet, 15 dépendances | **CPython interprété** (C, autoconf, cible croisée prévue), libffi, expat, tree-sitter |
| Sous-système nouveau | V8 (`platform-korin.cc`, `v8config.h`), `mksnapshot` par simulateur | **la cible Rust** (`std` + `libc` pour Korin), puis quatre caisses pyo3 |
| Construction | 1 h 30 à 2 h 30 la première fois | CPython 10 à 20 min ; Rust : quelques minutes par caisse (à mesurer) |
| L'agent | **figé** à la 2.1.112 (avril 2026), l'API d'Anthropic peut le laisser derrière | **à jour** (patch de quelques lignes par version) |
| Abonnement | Claude Pro ou Max (OAuth recopié) ou clé d'API | Le Chat Pro, compte gratuit avec quota, ou clé d'API |
| Licence | Claude Code non redistribuable : script d'installation | tout redistribuable : un paquet `vibe` complet |
| Interface | celle de Claude Code (Ink dans le Terminal), rien à écrire | **façade à écrire** (préférence du propriétaire) : client C puis app UIKit |
| Tubes des fils | **trou bloquant** (`socketpair` de libuv, illisible par les commandes en assembleur) | aucun (`os.pipe`) |
| `which`, `ps` | à écrire | `shutil.which` en Python ; pas de `ps` demandé (à vérifier) |
| Chemins `SD0:/` | arbre POSIX nécessaire | **le même** arbre POSIX |
| `bind` sur le port 0, `fs.watch` | à corriger, sondage | port 0 : le même correctif ; `watchfiles` en Python |
| Réutilisé | OpenSSL de Korin, zlib, fork/exec/ptys/contrôle des tâches, bash, git, rg | **tout cela aussi**, plus SQLite et le magasin de certificats ; les deux voies partagent l'arbre POSIX et le correctif de netd |
| Artefacts à la demande | du JavaScript pour Jet (HTML, canvas) ; pas de bibliothèque de données ni de graphiques en natif, pas d'accès aux kits | Python + `korin` (fenêtres UIKit, son, GPIO), Pillow, numpy, matplotlib, openpyxl : le terrain naturel des artefacts (§6) |
| Ce qui reste après | Node.js (npm, outils JavaScript) | **Python, langage du système** (scripts, artefacts, meson, MCP), **une cible Rust** (ripgrep, outils Rust), une façade ACP, et un `dlopen` dans libposix |

Effort, en ordre de grandeur (à mesurer aux premières étapes) : V8 seul pèse autant que CPython **et** la cible Rust
réunis ; la voie Python ajoute en revanche la façade, que la voie Node n'a pas. À effort comparable, la voie Python laisse
un agent à jour, redistribuable, avec l'interface native voulue, et deux outils durables (Python, Rust) au lieu d'un.

Une 5e voie, pour mémoire : un agent écrit par nous en C++ (la boucle d'outils sur l'API de Mistral, à partir de
PocketQI). Léger, mais tout l'agent est à écrire et à tenir (compaction du contexte, permissions, sous-agents, MCP) : ce
que Vibe donne déjà.

---

## 8. Risques et inconnues

| # | Risque | Comment le lever tôt |
|---|---|---|
| 1 | Le moteur 3.1.x ne marche pas sans code mode (V8 initialisé au démarrage d'une session, outil proposé quand même) | **étape 0, sur le PC** : harness compilé sans `deno_*`, `vibe-acp` lancé, une session complète |
| 2 | La cible Rust coûte plus que prévu (`std` hors de l'amont) | étape 4 sur un `hello` avant toute caisse pyo3 ; repli partiel : `rpds` en Python ; pas de repli pour `pydantic-core` |
| 3 | pyo3 lié en statique, noms pointés dans `PyImport_Inittab` | levé par `dlopen` (§2.5 bis) : à l'étape 3, les modules C sont des `.so` ; un module Rust en `cdylib` à l'étape 5 |
| 4 | **Démarrage lent** : des centaines de modules Python (pydantic compile des schémas au chargement, `mcp`, `mistralai`, opentelemetry), sur FAT32 par vfs et AppKit | zip précompilé, `preload`, `file_pin` ; Vibe a son propre outil de mesure (`vibe:scripts/check_startup_import_cost.py`) ; viser < 10 s sur le Pi |
| 5 | 64 fichiers ouverts | compter les descripteurs pendant une longue session (shell, journaux, MCP) |
| 6 | Chemins `SD0:/` dans `pathlib` | l'arbre POSIX avant l'étape 7 |
| 7 | Vibe change vite (harness, protocole) | ACP plutôt que l'app-server ; épingler une version ; un essai de bout en bout scripté à chaque montée |
| 8 | Fonctions Linux supposées par `sys.platform == "linux"` (`/proc`, `prctl` par `ctypes`) | relever à l'étape 7 ce qui part dans les `except` ; repli : la voie 2 du §2.2 |
| 9 | `cryptography` importée au démarrage malgré une clé d'API | étape 0 : lancer le moteur sans `cryptography` installée |
| 10 | Remplacement atomique (`os.replace` sur une cible existante) sur fatfs | essai à l'étape 2 |
| 11 | `poll` en boucle de 1 ms (tubes + pty + sockets) | mesurer la charge au repos (`cpuwatch`), comme pour Node |
| 12 | Mémoire : CPython + pydantic + Vibe, 150 à 300 Mo | le Pi 4 a 4 ou 8 Go |
| 13 | `dlopen` : réserve TLS de 32 Ko épuisée par plusieurs `cdylib` Rust, outils de construction qui ignorent les `.so` de la cible | les modules C et C++ (TLS, exceptions, une seule libc) sont essayés (`posixtest`, groupe `dl`) ; reste un module Rust avec `thread_local!` à l'étape 3, `-tls-reserve` au besoin ; repli : la voie statique (§2.5) |
| 14 | numpy et matplotlib par meson en croisé, sans `__linux__` | fichier croisé de meson essayé d'abord sur numpy seul ; `numpy.test` choisi sur la carte |
| 15 | UIKit appelé de plusieurs threads Python | `korin.ui` vérifie le thread et lève une erreur claire |

---

## 9. Ordre des étapes

> **Étapes 1 et 2 faites sous QEMU le 11-12/11/2026** (CPython 3.14.10), **étape 3 le 12/11/2026** (tree-sitter et cffi
> en `.so`, pip, un paquet en C compilé sur la carte) : construction, résultats des essais et ce qui reste dans
> [PYTHON-PORT.md](PYTHON-PORT.md). Pas encore essayé sur le Pi.
>
> **Étape 0 faite sur le PC le 12/11/2026** : Vibe 3.1.2 tourne sans V8 (`tools/vibe/`, résultats dans
> [PYTHON-PORT.md](PYTHON-PORT.md), section « Étape 0 »).
>
> **Étape 4 faite sous QEMU le 12/11/2026** : la cible `aarch64-unknown-korin` (Rust 1.97.1, `-Zbuild-std`, patchs de
> `libc` et de la `std`), une `cdylib` chargée par `dlopen` ; construction, essais et limites dans [RUST.md](RUST.md).
> Pas encore essayé sur le Pi.
>
> **Étape 5 faite sous QEMU le 12/11/2026** : `pydantic-core` 2.46.4, `rpds-py` 0.30.0 et `cryptography` 50.0.0, chacun
> sa `cdylib` (pas de caisse parapluie), chargés ensemble dans `python3`, OpenSSL pris à l'hôte ; essais et `pytest`
> dans [PYTHON-PORT.md](PYTHON-PORT.md), section « Étape 5 ». Pas encore essayé sur le Pi.
>
> **Étape 6 faite sous QEMU le 12/11/2026** : le harness de Vibe (`_native`, sans code mode) en `cdylib` pour Korin
> (`tools/vibe/build-korin.sh`), le `site-packages` de Vibe sur la carte (147 Mo, 86 paquets), `vibe-acp` et
> `vibe-app-server` qui répondent à `initialize` ; l'arbre POSIX des chemins dans vfs et libposix (`/sd0`, `/sd1`,
> `/ram0`, `getcwd` → `/sd0/...`, `HOME` = `/sd0`) ; essais dans [PYTHON-PORT.md](PYTHON-PORT.md), section « Étape 6 »,
> et [POSIX.md](POSIX.md). Pas encore essayé sur le Pi.
>
> **Étapes 7, 8 et 9 faites sous QEMU le 12/11/2026** : une séance complète de Vibe sur la carte avec le compte
> Mistral (connexion par lien, lecture, modification approuvée, `git status` et `rg`, Échap, `session/load`) ;
> `libacp` (`src/lib/acp`) et `agent`, le client terminal en C (`tools/agent`, 17 essais sur 17 dans le pty de Korin,
> la même séance dans l'app Terminal) ; **l'app Vibe** (`src/apps/vibe`), en C sur l'UIKit de l'OS, une nouvelle app
> plutôt que le mode « code » de PocketQI (reprise prévue dans la version x86-64) : un terminal sobre au milieu, les
> séances à gauche, fichiers, infos et commandes à droite, l'approbation dans un dialogue modal, Givre et Onyx Noir, le
> mode poche ; essais dans [PYTHON-PORT.md](PYTHON-PORT.md), sections « Étape 7 », « Étape 8 », « Étape 9 ». À la
> souris seulement ; pas encore essayé sur le Pi.
>
> **Pour reprendre à l'étape 10** sans le contexte des sessions précédentes : [PYTHON-SUITE.md](PYTHON-SUITE.md)
> (état, reconstruction depuis un dépôt vierge, étapes suivantes, règles).

Chaque étape se valide avant la suivante, sous QEMU puis sur le PocketTerm. Pas de JIT ici : QEMU suffit plus longtemps
que pour Node.

| # | Étape | Dépend de | Essai d'acceptation |
|---|---|---|---|
| 0 | **Sur le PC (Linux), sans Korin** : Vibe 3.1.x depuis ses sources, `korin.patch` appliqué (harness sans `deno_*`, code mode coupé, `uuid_utils` et `watchfiles` remplacés, `setproctitle`/`miniaudio`/`cryptography` absents) | — | `cargo build` du harness sans `deno` ; `vibe-acp` répond à `initialize`, `session/new`, `session/prompt` (« liste les fichiers ») par un petit client ACP en Python : un appel d'outil, une demande de permission, une réponse ; la même chose avec une clé d'API **et** avec les identifiants du compte recopiés |
| 1 | **CPython minimal** : sources (archive officielle, ≈ 23 Mo), `tools/python/build.sh`, `config.site`, sans OpenSSL ni modules tiers | — | `python3 -c "print(6*7)"` → `42` ; `python3 -c "import sys; print(sys.version, sys.platform)"` ; le REPL dans le Terminal (flèches, historique, Ctrl+C) |
| 2 | **CPython complet** : OpenSSL, zlib, SQLite, expat, libffi, libmpdec ; zip de la bibliothèque ; `/python` dans `paths.txt` | 1 | une partie de `test.regrtest` sur la carte (`test_os`, `test_subprocess`, `test_asyncio`, `test_socket`, `test_ssl`, `test_sqlite3`, `test_ctypes`, `test_json`, `test_decimal`, `test_pty`, `test_signal`) ; sautés et notés : `symlink`, IPv6, `AF_UNIX` nommés, fuseaux ; `subprocess.run(["bash","-c","ls | cat"])` ; `urllib.request.urlopen("https://api.mistral.ai")` (une réponse HTTP) ; `os.replace` sur une cible existante |
| 2 bis | **`dlopen` dans libposix** (§2.5 bis) : **fait le 11/11/2026** (dans main) ; reste `python3` relié en `-rdynamic` et `sysconfig` réglé pour les `.so` | 2 pour le reste | fait : groupe `dl` de `posixtest`, 24 réussis sous QEMU et sur le Pi ; reste : `python3` charge `_testcapi` en `.so` |
| 3 | **Modules natifs et pip** : `tree_sitter` + `tree_sitter_bash`, `_cffi_backend` en `.so` (repli : un module C pointé dans `Setup.local`) ; pip (voie A) | 2 | `import tree_sitter` ; il analyse `ls -la | grep x` ; `pip --version` ; `pip install --only-binary=:all:` d'un paquet pur ; `pip install` d'un petit paquet en C compilé sur la carte par `/gcc` |
| 4 | **Cible Rust** : `aarch64-unknown-korin.json`, `libc`, `std`, `tools/rust/build.sh` | — | `hello` ; threads, fichiers, `Command::new("ls")`, `TcpStream` vers 127.0.0.1, `panic` rattrapé ; essais de `std` choisis lancés sur la carte |
| 5 | **Modules Rust** : `pydantic-core`, `rpds-py`, `cryptography` (sur l'OpenSSL de `python3`), une `cdylib` chacun (la caisse parapluie `korin-pyext` est devenue inutile avec `dlopen`, §2.5 bis) | 3, 4 | `import pydantic; pydantic.TypeAdapter(list[int]).validate_json("[1,2]")` ; `jsonschema.validate` ; `cryptography` : un SHA-256, une signature Ed25519, un JWT par `joserfc` ; une partie de leurs essais (`pytest` pur Python) |
| 6 | **Harness** : `_native` sans code mode, sa propre `cdylib` (`tools/python/build-rust-ext.sh`) ; `site-packages` de Vibe ; arbre POSIX des chemins ([NODEJS.md](NODEJS.md) §6.3, si la voie Node ne l'a pas déjà fait) | 5 | `python3 -c "import mistralai_vibe_local_harness"` ; `vibe-app-server` répond à `initialize` ; `os.getcwd()` → `/sd0/...` |
| 7 | **Vibe de bout en bout** avec le client ACP de l'étape 0 (en Python, sur la carte) | 6 | une session : lire un fichier, le modifier (approbation demandée et accordée), `git status` et `rg` par le shell, Échap ; reprise de la session (`session/load`) ; temps de démarrage noté |
| 8 | **`libacp` et `agent`** (client terminal en C) | 7 | la même séance qu'à l'étape 7 depuis le Terminal ; redimensionnement ; Ctrl+C ; aucune séquence affichée en clair |
| 9 | **L'app UIKit** (ou le mode « code » de PocketQI) | 8 | la même séance à la souris et au tactile ; fenêtre d'approbation modale ; thème Givre et Onyx Noir ; mode poche |
| 10 | **Paquets** : `python` (interpréteur + bibliothèque), `vibe` (site-packages + façade), `rust` hors de la carte | 7 | `pkg install vibe` sur une carte neuve, connexion, l'essai de l'étape 7 |
| 11 | **Vitesse** : `preload`, `file_pin`, imports paresseux, mesure de la charge au repos | 8 | démarrage chronométré avant et après ; charge du processeur quand l'agent attend |
| 12 | **Le module `korin`** : `tools/kits/abi2py.py`, `_korinkit` (ctypes par adresse), `korin.ui`, `korin.open`, `korin.capture`, `korin.audio`, `korin.gpio`, `korin.bus` | 2 | une fenêtre Python avec un bouton et un champ, au thème du bureau (Givre, Onyx Noir) ; un `Canvas` rempli depuis un `bytearray` ; `korin.open("x.xlsx")` ouvre Sheet ; `korin.capture()` écrit un PNG ; un son joué ; une DEL de GPIO Lab allumée |
| 13 | **Pillow, openpyxl et paquets purs** des artefacts | 3 | réduire un dossier de JPEG, écrire un PNG et un WebP ; un XLSX à formules ouvert dans Sheet ; un PDF par `pypdf` |
| 14 | **numpy, matplotlib, backend UIKit** | 13 | `numpy.test` choisi ; un graphique en PNG, SVG et PDF ; la même figure dans une fenêtre, zoom à la molette |
| 15 | **Artefacts par l'agent** : la compétence `korin-artifacts`, le dossier des artefacts, l'affichage dans la façade | 9, 12, 14 | trois demandes de bout en bout : « un graphique de ce CSV », « un minuteur dans une fenêtre », « convertis ces photos » ; l'agent lance, capture, vérifie, et l'utilisateur voit le résultat dans la conversation et dans son app ; le minuteur gardé comme `.app` du lanceur |

Les étapes 0, 1 et 4 sont indépendantes (2 bis est faite) : elles peuvent avancer en parallèle. Les étapes 12 à 14 (Python langage
général) ne dépendent ni de la cible Rust ni de Vibe. **L'étape 0 tranche tout le reste** : si
le moteur ne tourne pas sans V8 sur le PC, la voie Vibe 3.x s'arrête là (repli : 2.24.5 figée, §4.1).

---

## 10. Décisions pour le propriétaire

**Recommandation nette : oui, porter CPython 3.14 sous Korin, comme langage général du système, avec un vrai `dlopen`
dans libposix ; et en faire la voie de l'agent par Mistral Vibe 3.1.x sans code mode, derrière une façade ACP à nous,
qui produit des artefacts en Python à la demande.**

* Python vaut la peine **pour lui-même**, Vibe à part : scripts du dépôt et de l'utilisateur, x86toa64, meson, MCP, et
  surtout les **artefacts** (graphiques, tableaux, documents, petites apps UIKit) par le module `korin` et Pillow, numpy,
  matplotlib. Cette partie ne demande **ni Rust ni Vibe** : elle se fait avec la chaîne C actuelle (étapes 1, 2, 12 à 14).
* Son portage est plus simple que celui de Node : un interpréteur en C, sans JIT, sans le trou des tubes de libuv.
* **`dlopen`** : fait le 11/11/2026 (étape 2 bis, dans main, essayé sur le Pi). Il évite de relier l'interpréteur à
  chaque montée, rend `pip` utile sur la carte et sert à d'autres ports ; `python3` se relie en hôte (`-rdynamic`).
* Pour l'agent, Vibe est à jour, ouvert et redistribuable. Son prix réel est **la cible Rust**, inévitable pour
  `pydantic-core` et le cœur du harness ; elle se rentabilise ensuite (le vrai ripgrep, les outils Rust). L'**étape 0 sur
  le PC** dit en une journée si le moteur tourne sans V8.

Ordre conseillé : étape 0 (PC) et CPython (1, 2) en même temps (`dlopen` est déjà là), le module `korin` et les bibliothèques
d'artefacts (12 à 14), puis la cible Rust et Vibe (4 à 7), la façade (8, 9), et les artefacts par l'agent (15).

| # | Question | Choix possibles | Recommandation |
|---|---|---|---|
| 1 | Faire ce chantier ? et par rapport à Node ? | Python + Vibe ; Node + Claude Code 2.1.112 ([NODEJS.md](NODEJS.md), décidé le 11/11) ; les deux | **Les deux restent possibles ; à n'en faire qu'un d'abord, Python + Vibe** (à jour, redistribuable, façade native). L'arbre POSIX et le port 0 de netd servent aux deux |
| 2 | Version de CPython | 3.14.x (Python de construction 3.14.4 déjà là ; Vibe ≤ 3.14) ; 3.15 | **3.14** |
| 3 | Ce que voit Python | `sys.platform == "linux"` (`--host=aarch64-unknown-linux-gnu`, sans patch de `configure`) ; `"korin"` (patch de `configure.ac`) | **`"linux"`**, comme Node ; revoir si l'étape 7 montre trop de suppositions Linux |
| 4 | Version de Vibe | 3.1.x + patch « sans code mode » ; 2.24.5 figée ; 3.1.x avec V8 | **3.1.x sans code mode**, une montée par mois |
| 5 | La cible Rust | (a) musl + libposix ; (b) `aarch64-unknown-korin` ; (c) remplacer les paquets | **(b)**, et (c) pour `uuid-utils` et `watchfiles` ; (a) écartée (ABI de Linux, §3.2) |
| 6 | Protocole de la façade | ACP (`vibe-acp`) ; app-server (`vibe-app-server`) | **ACP** |
| 7 | Forme de la façade | client terminal en C ; app UIKit ; mode « code » de PocketQI | **client terminal d'abord** (banc d'essai du protocole), **puis l'app UIKit**, en reprenant ce que PocketQI a déjà |
| 8 | Où vit Python | `SD0:/bin/python3` + `SD0:/python` (chemin virtuel `/python`, bibliothèque en zip) ; tout sous `SD0:/python` | `SD0:/bin/python3` + `SD0:/python` |
| 9 | Noms | façade : `agent` (terminal) et `agent.app`, ou `vibe` ; paquets : `python`, `vibe` | **`agent`** pour la façade (ACP n'est pas propre à Vibe, et « Mistral Vibe » est une marque) ; paquets `python` et `vibe` (le moteur) |
| 10 | Connexion | identifiants recopiés du PC ; clé d'API ; navigateur (Jet) sur la carte | identifiants recopiés, puis clé ; le navigateur plus tard |
| 11 | pip | voie A (`site-packages` préparé sur le PC) ; voie B (pip sur la carte) | A d'abord, B à l'étape 11 |
| 12 | Chantiers communs avec Node | arbre POSIX des chemins (`/sd0`...), `bind` sur le port 0 dans netd, page d'aide [libc_fr.md](../src/apps/help/data/api/libc_fr.md) à remettre à jour (`fork`, `exec`) | à faire une fois, avant la première des deux voies qui en a besoin |
| 13 | `dlopen` | un chargeur d'`ET_DYN` dans libposix (§2.5 bis) ; rester tout statique (§2.5, `pyrelink`) | **le chargeur** : fait le 11/11/2026 ; la voie statique en repli |
| 14 | Les kits depuis Python | extension C générée par `abi2py.py` ; ctypes par adresse ; les deux | **les deux** : ctypes pour essayer tout de suite, l'extension générée pour le module livré |
| 15 | Bibliothèques d'artefacts | Pillow et les paquets purs ; + numpy et matplotlib ; + lxml, reportlab, pygame ; pandas ; scipy | **Pillow + purs d'abord, numpy + matplotlib ensuite** ; lxml et pygame à la demande ; pandas plus tard ; scipy écarté (Fortran) |
| 16 | Où vont les artefacts | `SD0:/home/artifacts/<date>-<nom>/` ; le dossier courant ; `SD1:` | `SD0:/home/artifacts`, `SD1:` seulement sur demande |
