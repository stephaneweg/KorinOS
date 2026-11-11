# Node.js sous Korin OS (vers Claude Code) : étude

Étude du 11/11/2026, avant tout code. But : faire tourner **Claude Code** (le paquet npm `@anthropic-ai/claude-code`, qui
demande Node.js ≥ 18) dans le Terminal de Korin. Les fondations sont faites : étapes 1 à 9 de
[TERMINAL-ET-FORK.md](TERMINAL-ET-FORK.md) (Terminal VT, ptys de vterm, `fork` en copie sur écriture, vrai `exec`, bit
d'exécution, scripts `#!`, `SIGALRM`, contrôle des tâches, bash 5.2, git 2.51, `rg`). Reste Node.js lui-même : V8 (le
moteur JavaScript, avec son JIT), libuv (la boucle d'événements) et le C++ de Node, plus une quinzaine de dépendances.

Sources lues : l'archive Debian de **Node.js 22.22.1** (`/opt/src/node/nodejs_22.22.1+dfsg+~cs22.19.15.orig.tar.xz`,
dépliée hors du dépôt ; les références `node-v22.22.1/...:ligne` s'y rapportent), le Claude Code installé sur la machine de
construction (`/opt/claude-code/bin/claude`, **version 2.1.322**, lu par `strings` comme dans [RG.md](RG.md)), et le code
de Korin (références `fichier:ligne` du dépôt). Les décisions à prendre sont rassemblées à la fin (§10).

> **Constat du 11/11/2026, après l'étude : Claude Code n'est plus du JavaScript sur npm.** Depuis la version **2.1.113**
> (avril 2026), le paquet `@anthropic-ai/claude-code` ne contient qu'un lanceur : `bin/claude.exe` est un texte de 500
> octets que `install.cjs` remplace, à l'installation, par le **binaire natif** de la plateforme
> (`@anthropic-ai/claude-code-linux-arm64`, `-linux-arm64-musl`, `-darwin-*`, `-win32-*`...) ; `cli-wrapper.cjs` ne fait
> que lancer ce binaire. La **dernière version en JavaScript est la 2.1.112** (16/04/2026, `bin : cli.js`, Node ≥ 18).
> Les binaires Linux arm64 de la 2.1.322 (vérifiés sans les exécuter) sont des ELF **liés dynamiquement** : 341 Mo
> contre la glibc (`/lib/ld-linux-aarch64.so.1`, `libc.so.6`, `libpthread`, `libdl`, `librt`, `libm`), ou 326 Mo contre
> **musl** (`/lib/ld-musl-aarch64.so.1`, une seule bibliothèque). Node.js ne fait donc tourner que Claude Code 2.1.112,
> figée ; la version courante demande d'exécuter un binaire Linux. Voir la décision 0 du §10.

---

## 1. Ce que Claude Code demande à Node

### 1.1 Méthode

L'exécutable natif de Claude Code est un Bun autonome : son JavaScript est découpé en morceaux
`/$bunfs/root/chunk-*.js`, lisibles par `strings` après la ligne `// @bun @bytecode`. Les `import ... from "module"` de
ces morceaux donnent la liste des modules de Node employés (le paquet npm est le même code, construit pour Node : il
référence toujours `cli.js`, et le code teste `typeof Bun` avant chaque appel propre à Bun). Les noms de fonctions sont
minifiés ; les noms de modules, d'options et les chaînes sont sûrs.

### 1.2 Modules et API

| Module (nombre d'`import`) | Ce qui est importé | Ce qu'il faut sous Korin |
|---|---|---|
| `path` (1 183), `fs` (310), `fs/promises` (642) | tout ; `watch` (9 endroits), `watchFile`, `unwatchFile` | fichiers par AppKit ; **chemins à volume** (§6.3) ; `fs.watch` : §5.4 |
| `crypto` (493) | `createHash`, `createHmac`, `randomBytes`, `randomUUID`, `createCipheriv`, `generateKeyPairSync`, `sign`, `verify`, `X509Certificate`, `timingSafeEqual` | OpenSSL |
| `os` (222) | `homedir`, `tmpdir`, `hostname`, `platform`, `arch`, `release`, `type`, `userInfo`, `cpus`, `availableParallelism`, `totalmem`, `freemem`, `loadavg`, `uptime`, **`networkInterfaces`**, `getPriority`, `setPriority`, `EOL`, `devNull` | libuv ; `getifaddrs` manque (§5.3) |
| `child_process` (64) | `spawn`, `spawnSync`, `exec`, `execFile`, `execFileSync`, `execSync`, `ChildProcess` | `fork` + `exec` (faits), tubes (§5.2) |
| `net` (48), `http` (33), `https` (12), `tls` (11), `dns` (5), `http2` (3) | `Socket`, `connect`, `createServer`, `isIP*`, `BlockList` ; `request`, `Agent`, `createServer` ; `TLSSocket`, `connect`, `createSecureContext`, `rootCertificates` ; `lookup` (`ADDRCONFIG`) | NetKit (IPv4), OpenSSL, c-ares, nghttp2 |
| `zlib` (15) | `gzip`, `gunzip`, `deflate*`, `inflateRaw*`, **`createZstdDecompress`** | zlib, brotli, **zstd** (Node ≥ 22.15) |
| `readline` (16), `tty` (6) | `createInterface` ; `isatty`, `ReadStream` (`setRawMode`) | ptys de vterm |
| `stream`, `stream/promises`, `events`, `buffer`, `string_decoder`, `url`, `util`, `util/types`, `timers/promises`, `assert` | — | JavaScript seul |
| `async_hooks` (36), `vm` (26), `process` (22), `module` (2), `v8` (1), `perf_hooks` (1), `worker_threads` (1) | `AsyncLocalStorage`, `AsyncResource` ; `isContext` ; `createRequire` ; `getHeapStatistics` ; `monitorEventLoopDelay` ; `workerData` | V8, libuv |

Pas d'import de `node:sqlite`, `inspector`, `wasi`, `cluster`, `dgram` dans le code de Claude Code (les occurrences de
`strings` viennent du runtime de Bun).

### 1.3 Points précis

| Question | Réponse (Claude Code 2.1.322) | Conséquence |
|---|---|---|
| Version de Node | le paquet npm demande Node ≥ 18 | Node 22 ou 24 conviennent |
| **Intl / ICU** | `Intl.Segmenter` (7 endroits, **dont un au chargement** : `var dx=new Intl.Segmenter("en")`, découpage en graphèmes et en mots pour la largeur du texte d'Ink), `Intl.ListFormat` (24), `Intl.DateTimeFormat` (10), `NumberFormat`, `RelativeTimeFormat`, `DisplayNames`, `Collator`, `Locale` ; `normalize("NFC")`, `localeCompare` | **`--without-intl` est exclu** : `Intl` n'existe pas et Claude Code s'arrête au chargement. `small-icu` retire les règles de coupure (`"brkitr": "none"`, `"brkfiles": "none"`, `node-v22.22.1/tools/icu/icu_small.json:19-29`) dont `Intl.Segmenter` a besoin : à vérifier, sinon ICU complet ou filtré (§10, décision 3) |
| **WebAssembly** | une seule fois, dans un `try` (multiplication 64 bits de Long.js) | non demandé par Claude Code lui-même, **mais `fetch` de Node en a besoin** (ligne suivante) |
| `fetch`, undici | l'API d'Anthropic passe par `globalThis.fetch` ; `require("undici")` seulement pour un mandataire (`setGlobalDispatcher`) ; axios ailleurs | le `fetch` de Node est undici 6, chargé à la première requête (`node-v22.22.1/lib/internal/bootstrap/web/exposed-window-or-worker.js:63-75`), dont l'analyseur HTTP est **llhttp compilé en WebAssembly** (`lib/dispatcher/client-h1.js` d'undici, absent de l'archive Debian : à revoir dans l'archive officielle). Avec `--jitless`, V8 n'expose pas WebAssembly (`deps/v8/src/flags/flag-definitions.h:1524`) : **le JIT est nécessaire pour parler à l'API** |
| `worker_threads` | `workerData` seulement (un module annexe) | threads de V8 et de libuv : rien de plus |
| `child_process` | `spawn` de bash pour l'outil Bash (tubes, `detached`, `process.kill(-pid)`) ; `git` (39 appels), `rg`, **`which`** (pour trouver une commande sous Node : `if(typeof globalThis.Bun<"u")return Bun.which(e); ... spawnSync("which",[e])`), **`ps -o pid= -o ppid= -o pgid= -o stat= -o lstart=`** (tâches de fond, arbre des process ; avec délai, en `try`), `tmux`, `gh`, `open`/`xdg-open` | `which` et `ps` manquent dans `/bin` (§7.3) |
| pty | aucun (`node-pty` n'apparaît que dans un message) | le Terminal donne le pty, Claude Code lit `process.stdin` en mode brut |
| `setRawMode`, `SIGWINCH`, `isTTY`, `columns` | oui (Ink) | §5.3 |
| Modules natifs (`.node`) | `clipboard-napi.node`, `audio-capture.node`, `computer_use.node` : chargés dans un `try`, `null` en cas d'échec | **aucun module natif obligatoire** ; `process.dlopen` peut échouer |
| `fs.watch` | chokidar (avec `usePolling:true` à plusieurs endroits, et `CHOKIDAR_USEPOLLING` lu dans l'environnement), `fs.watch` direct pour quelques fichiers, `watchFile` | §5.4 |
| `/proc` | `/proc/self/cgroup`, `/proc/self/mountinfo` (WSL, sessions distantes), `/proc/self/fd/N`, `/proc/uptime` : tous dans des `try` | rien d'obligatoire |
| Serveur local | `http.createServer` + `listen` (rappel OAuth de la connexion par navigateur, intégrations d'éditeurs) | **`bind` sur le port 0 ne choisit pas de port** ([DOTNET.md](DOTNET.md):291) : se connecter par clé d'API ou par code collé (§7.4) |

### 1.4 Ce qui n'est pas demandé

`node:sqlite`, le débogueur (`inspector`), les modules natifs, QUIC, `cluster`, IPv6, les sockets `AF_UNIX` nommés (sauf
intégrations d'éditeurs, facultatives). Le bac à sable de Claude Code (bubblewrap sous Linux) est désactivé par défaut.

---

## 2. Construire Node 22 pour Korin

### 2.1 Le principe : « gyp voit Linux, le compilateur ne voit pas `__linux__` »

`configure.py` n'accepte que des systèmes connus (`valid_os`, `node-v22.22.1/configure.py:48-49`). Ajouter un système à
gyp, à Node, à V8 et à libuv toucherait des centaines de conditions `OS=="linux"`. Comme pour .NET (« ARMPI se fait
passer pour Linux arm64 », [DOTNET.md](DOTNET.md)), mais plus prudemment :

* **gyp** reçoit `--dest-os=linux --dest-cpu=arm64` : les listes de sources, les drapeaux et le générateur restent
  ceux de Linux ; `process.platform` vaut `"linux"` (`NODE_PLATFORM`), ce que Claude Code connaît (§10, décision 5).
* **Le compilateur** (`aarch64-onyx-elf-gcc`, qui ne définit que `__aarch64__` et `__ELF__` ; `posix.specs` ajoute
  `__unix__` et `__armpi__`) **ne définit pas `__linux__`** : le code C et C++ prend ses chemins POSIX génériques, pas
  `epoll`, `inotify`, `io_uring`, `/proc`, `prctl` ni le gestionnaire de fautes de WebAssembly.
* Un **patch** (`tools/node/korin.patch`, à écrire) ajoute une branche Korin là où il en faut une : `v8config.h`,
  le fichier de plateforme de V8 (§4.1), la liste des sources de libuv (§5.1), les en-têtes `uv/unix.h`, et retire
  `-pie`, `-rdynamic`, `-ldl`, `-lrt` comme pour .NET.

### 2.2 La compilation croisée et les instantanés

| Outil (toolset `host`) | Rôle | Sur l'hôte x86-64 |
|---|---|---|
| `torque`, `bytecode_builtins_list_generator`, `gen-regexp-special-case` | générateurs de V8 | compilés par le g++ de l'hôte (13.3 ici) |
| **`mksnapshot`** | compile les builtins de V8 (`embedded.S`) et l'instantané de démarrage de V8 (`snapshot.cc`) | **toolset `host` quand on compile en croisé** (`node-v22.22.1/tools/v8_gypfiles/v8.gyp:1798-1820`), V8 visant arm64 sur un hôte x86-64 : `USE_SIMULATOR` (`deps/v8/src/common/globals.h:42-44`) ; il génère du code arm64 qu'il exécute dans le **simulateur arm64** de V8. Lancé avec `--target_os=<(OS)` `--target_arch=arm64` (`v8.gyp`, action `run_mksnapshot`). Pas d'option : sans lui pas de builtins |
| `node_js2c` | met le JavaScript de `lib/` dans le binaire | hôte |
| `genccode`, `icupkg`, `genrb`... | données d'ICU | hôte (ICU construit deux fois : hôte et cible) |
| `node_mksnapshot` | l'instantané **de Node** (amorçage JavaScript déjà fait) et le cache de code | **désactivés d'office en croisé** (`configure.py:1509-1522` : `node_use_node_snapshot` et `node_use_node_code_cache` valent `not cross_compiling`) |

Donc : V8 a son instantané (indispensable, fait par le simulateur), **Node n'a pas le sien** : chaque démarrage refait
l'amorçage de `lib/internal/bootstrap/*` et compile les modules internes sans cache. Coût estimé : quelques centaines de
ms sur le PC, de l'ordre de la seconde sur le Pi (à mesurer). Deux remèdes plus tard (étape 10, §9) :

1. **instantané en deux temps** : construire `node_mksnapshot` pour Korin, le lancer sur la carte (ou QEMU), récupérer
   `node_snapshot.cc`, relier sur le PC (`--node-snapshot-main` refuse le croisé, `configure.py:1505-1506` : script à
   nous) ;
2. **à l'exécution**, sans rien reconstruire : `NODE_COMPILE_CACHE` (Node ≥ 22.1, cache de code sur disque pour les
   modules de l'utilisateur, donc les 10 Mo de `cli.js`) ; `node --build-snapshot` / `--snapshot-blob` pour un
   instantané de l'amorçage.

### 2.3 Options de `configure`

```sh
CC_host=gcc CXX_host=g++ \
CC=aarch64-onyx-elf-gcc CXX=aarch64-onyx-elf-g++ AR=aarch64-onyx-elf-ar \
CFLAGS="-specs=build/posix/lib/posix.specs" CXXFLAGS="-specs=build/posix/lib/posix.specs" LDFLAGS=... \
./configure --dest-os=linux --dest-cpu=arm64 --cross-compiling --fully-static --ninja \
    --without-inspector --without-npm --without-corepack --without-amaro --without-sqlite \
    --without-node-snapshot --without-node-code-cache \
    --with-intl=full-icu [--with-icu-source=...] \
    --shared-openssl --shared-openssl-includes=build/openssl/include --shared-openssl-libpath=build/openssl/lib \
    --openssl-use-def-ca-store \
    [--enable-d8] [--v8-options=...]
```

| Option | Pourquoi |
|---|---|
| `--dest-os=linux --dest-cpu=arm64 --cross-compiling` | §2.1 ; `want_separate_host_toolset=1`, `GYP_CROSSCOMPILE=1` (`configure.py:1484-1492`) |
| `--fully-static` | programmes statiques, pas de `dlopen` (ajoute `-static`) |
| `--ninja` | `ninja` est là ; plus rapide que `make` pour les reconstructions |
| `--without-inspector` | pas de débogueur ; évite aussi le générateur `inspector_protocol` (jinja2) |
| `--without-npm --without-corepack` | npm se pose à part (§7) |
| `--without-amaro` | pas de TypeScript intégré (`configure.py:2309-2310`) |
| `--without-sqlite` | `node:sqlite` inutile (§1.4) |
| `--with-intl=full-icu` | §1.3, §10 décision 3 |
| `--shared-openssl` | l'OpenSSL de Korin (3.5.9, [OPENSSL.md](OPENSSL.md)), déjà éprouvé (curl, git, .NET : TLS 1.2 et 1.3) ; pas de QUIC : `quic.h` d'OpenSSL 3.5 ne définit pas `OPENSSL_INFO_QUIC` (`tools/getsharedopensslhasquic.py`), donc ni ngtcp2 ni nghttp3 |
| `--openssl-use-def-ca-store` | facultatif : Node a ses 144 autorités dans le binaire (`src/node_root_certs.h`) ; `NODE_EXTRA_CA_CERTS=SD0:/etc/ca-bundle` ajoute celles de Korin |
| `--enable-d8` | le shell de V8 seul, pour la première étape (§9, étape 2) |
| pas `--experimental-enable-pointer-compression` | Node la laisse coupée par défaut, et le **bac à sable de V8 est forcé à 0** (`configure.py:1724-1735`) : ni cage de 4 Go, ni réservation de 1 To |
| pas `--v8-lite-mode` | il coupe WebAssembly (`configure.py:1713`), donc `fetch` (§1.3) |
| `--no-ifaddrs` | à voir : ne sert qu'à SunOS (`SUNOS_NO_IFADDRS`, `configure.py:1594-1595`) ; Korin doit plutôt avoir `getifaddrs` (§5.3) |

Les variables d'environnement `*_host` donnent la chaîne de l'hôte, les autres celle de la cible (convention de gyp).

### 2.4 Outils de l'hôte

* python3 ≥ 3.9 (3.13 sur la machine de construction ; Node 22.22 accepte jusqu'à 3.14) ;
* g++ ≥ 10.1 pour l'hôte (`BUILDING.md:244` ; 13.3 ici) ; GCC 14.2 pour la cible (C++20 : vérifié, `std::bit_cast`,
  `std::span`, `std::to_chars` flottant, `thread_local`, `std::thread` se compilent et se lient avec `posix.specs`) ;
* **gyp-next** : `tools/gyp` est **retiré de l'archive Debian** (Debian construit avec le paquet `gyp`) ; Node 22.22
  attend gyp-next 0.21.1 (CHANGELOG) ; l'archive officielle l'a ;
* `ninja` (présent), ccache conseillé.

### 2.5 Durée

Mémoire : 8 Go pour `-j4` (`BUILDING.md:238`) ; la machine a 4 cœurs et 15 Go. Volume : ≈ 970 sources C++ de V8 pour la
cible, **le même ordre pour l'hôte** (`mksnapshot` relie tout V8, compilateur compris), ≈ 180 de Node, ≈ 200 d'abseil,
ICU deux fois (≈ 600 fichiers légers), libuv. Estimation : **1 h 30 à 2 h 30 à `-j4` la première fois** (une
construction native de Node sur 4 cœurs prend ≈ 1 h, la croisée y ajoute un V8 d'hôte), quelques minutes ensuite avec
ccache. À mesurer à l'étape 1. Pour comparaison, WebCore : 40 min à 12 travaux ([WEBKIT.md](WEBKIT.md)).

---

## 3. Les dépendances

### 3.1 Ce que l'archive Debian garde et retire

Gardés : `deps/v8` (V8 **12.4.254.21**, avec `third_party/abseil-cpp`, `zlib`, `fp16`, `glibc`...), `deps/uv` (libuv
**1.51.0**), `uvwasi`, `inspector_protocol`, `ncrypto`, `nbytes`, `histogram`, `googletest`, `postject`. Retirés :
le reste de `deps/`, **`tools/gyp`**, et les JavaScript embarqués que `node.gyp` liste (`deps/acorn/...`,
`deps/minimatch/index.js`, `node-v22.22.1/node.gyp:59-61` ; `undici`, `cjs-module-lexer`, `amaro`,
`configure.py:61-65`). Les versions attendues viennent de `doc/changelogs/CHANGELOG_V22.md` (la dernière « deps: update »
de chacune) ; l'archive Ubuntu a été listée (`archive.ubuntu.com/ubuntu/pool/...`), rien n'a été téléchargé.

| Dépendance | Node 22.22.1 l'attend | Utile ? / option | Où la prendre |
|---|---|---|---|
| OpenSSL | 3.5.5 | oui (TLS) ; `--shared-openssl` | **déjà construit** : 3.5.9 par `tools/openssl/build.sh` ; 3.5.5 en local (`/opt/src/git-deps/openssl_3.5.5.orig.tar.gz`) |
| ICU | 78.2 (minimum 73, `tools/icu/icu_versions.json`) | **oui** (§1.3) ; `--with-icu-source` | Ubuntu `main/i/icu` : `icu_78.2.orig.tar.gz` (78.3 aussi) ; `src/extra/icu-78.3` et `tools/webkit/ports/icu` (WebKit) |
| zlib | 1.3.1 (fourche de Chromium `e00f703`) | oui ; `--shared-zlib` possible | local : `/opt/src/git-deps/zlib_1.3.dfsg+really1.3.1.orig.tar.gz` ; `src/extra/zlib-1.3.1` |
| brotli | 1.1.0 | oui (`zlib.brotli*`) ; `--shared-brotli` | Ubuntu `main/b/brotli` : 1.1.0, 1.2.0 ; `src/extra/brotli-1.1.0` |
| zstd | 1.5.7 | oui (`createZstdDecompress`) ; `--shared-zstd` | Ubuntu `main/libz/libzstd` : `libzstd_1.5.7+dfsg.orig.tar.xz` |
| c-ares | 1.34.6 | compilé de toute façon (`cares_wrap`) ; `--shared-cares` | Ubuntu `main/c/c-ares` : 1.34.8, 1.34.9 |
| nghttp2 | 1.64.0 | `http2` (importé, peu employé) ; `--shared-nghttp2` | Ubuntu `main/n/nghttp2` : 1.68.0, 1.70.0 ; `src/extra/nghttp2-1.70.0` |
| llhttp (C) | 9.3.0 | oui (analyseur de `http`) ; `--shared-http-parser` | Ubuntu `universe/l/llhttp` : 9.4.3, **sources TypeScript** (le C se génère avec Node et llparse) ; le C généré est dans l'archive officielle |
| simdutf | 6.4.2 | oui (`Buffer`, encodages) ; `--shared-simdutf` | Ubuntu `main/s/simdutf` : 8.0.0, 8.2.0, 9.3.0 (**majeures plus récentes** : API à vérifier) |
| simdjson | 4.2.4 | oui (`package.json`) ; `--shared-simdjson` | Ubuntu `universe/s/simdjson` : **4.2.4** |
| ada | 2.9.1 | oui (`URL`) ; `--shared-ada` | Ubuntu `universe/a/ada-url` : 3.4.3, 3.4.4 (**ada 3 change l'API** : Node 22 ne la prend pas telle quelle) |
| sqlite | 3.51.2 | non : `--without-sqlite` | — |
| ngtcp2, nghttp3 | 1.11.0, 1.6.0 | non (pas de QUIC, §2.3) | — |
| undici (JS) | 6.23.0 | **oui** (`fetch`, §1.3) ; `--shared-builtin-undici/undici-path` (lu à l'exécution) | **absent d'Ubuntu** (`universe/n/node-undici` : 5.26, 7.x, 8.x) ; le fichier embarqué `deps/undici/undici.js` est une sortie d'esbuild : archive officielle ou npm |
| acorn, acorn-walk (JS) | 8.15.0, 8.3.4 | oui (REPL, `--experimental-*`, assertions) | Ubuntu `universe/a/acorn` : 8.17.0 (+ composants) ; `dist/` à reconstruire |
| minimatch (JS) | 10.2.4 | oui (`fs.glob`) | Ubuntu `node-minimatch` : 9.0.x seulement |
| cjs-module-lexer (JS) | 2.2.0 | oui (import ESM de CommonJS) | Ubuntu `universe/n/node-cjs-module-lexer` : 2.2.0 |
| amaro (JS) | 1.1.5 | non : `--without-amaro` | — |
| npm, corepack | 10.9.4, 0.34.6 | npm : §7 ; corepack : non | Ubuntu `universe/n/npm` : 9.2.0, 11.16.0, 12.2.0 |
| gyp-next | 0.21.1 | outil de construction | Ubuntu `universe/g/gyp` : 0.16.2 (ancien) |
| libuv | 1.51.0 | gardée dans `deps/uv` | (Ubuntu `main/libu/libuv1` : 1.51.0 exacte) |

### 3.2 Bilan

Reconstituer l'archive Debian demande **quinze morceaux**, dont quatre JavaScript produits par esbuild ou rollup
(undici, acorn, minimatch, et llhttp pour le C), deux bibliothèques à une majeure de trop (ada, simdutf) et gyp-next.
L'**archive officielle** `node-v22.22.1.tar.xz` de nodejs.org (avec `SHASUMS256.txt` signé) contient tout cela aux
versions essayées par Node, plus `deps/npm`. Recommandation : l'archive officielle (§10, décision 2), en gardant
`--shared-openssl` pour l'OpenSSL de Korin. Les autres dépendances se construisent alors par les `.gyp` de Node, dans le
même arbre, sans script de port à écrire.

Ubuntu a aussi **Node 24.22.2** (`main/n/nodejs/nodejs_24.22.2+dfsg+~cs24.19.2.orig.tar.xz`, même découpage que la 22).

---

## 4. V8 sous Korin

### 4.1 La couche système de V8

V8 choisit son système par les macros du compilateur (`deps/v8/include/v8config.h:87-166`) ; sans `__linux__` ni aucune
autre, **rien n'est défini, pas même `V8_OS_POSIX`**. Il faut une branche :

```c
#elif defined(__armpi__)
# define V8_OS_POSIX 1
# define V8_OS_KORIN 1
# define V8_OS_STRING "korin"
```

et un `platform-korin.cc` à côté de `platform-posix.cc` (qui fait l'essentiel : `mmap`, `mprotect`, threads, temps).
Point de départ : **`platform-freebsd.cc`** (125 lignes : pas de `/proc`) plutôt que `platform-linux.cc` (351 lignes, lit
`/proc/self/maps` : `deps/v8/src/base/platform/platform-linux.cc:97, 177`, et appelle `mremap`, absent de libposix) :

| Fonction | Korin |
|---|---|
| `OS::GetSharedLibraryAddresses`, `SignalCodeMovingGC` | vide (ne sert qu'à `--prof`) |
| `OS::CreateTimezoneCache` | `PosixDefaultTimezoneCache` (UTC seulement : [POSIX.md](POSIX.md):333) |
| `OS::GetFirstFreeMemoryRangeWithin`, `RemapPages` | `nullopt` / `false` (facultatifs) |
| nom des threads | `pthread_setname_np` existe (`src/lib/posix/pthread.c:279`) ; `prctl` et `<sys/prctl.h>` non (`platform-posix.cc:62`, sous Linux seulement) |
| `GetCurrentThreadId` | `syscall(__NR_gettid)` sous Linux (`platform-posix.cc:830`) ; `syscall` de libposix sait `gettid` |
| début de la pile | `pthread_getattr_np` (`platform-posix.cc:1339`) : existe, y compris pour le thread principal (`src/lib/posix/pthread.c:298`, `misc.c:300`) |
| détection du processeur (`base/cpu.cc`) | `getauxval(AT_HWCAP)` sous Linux ; Korin ne dit que FP et NEON ([POSIX.md](POSIX.md):325-326) : suffisant (Cortex-A72 = ARMv8.0, sans LSE ni PAC ; pas de chiffrement matériel sur le BCM2711) |
| aléa | `/dev/urandom` (existe) |
| `backtrace` | existe (`execinfo.h`) ; `dladdr` rend le programme sans nom de symbole |

### 4.2 Mémoire

| Besoin de V8 (Node 22, sans compression des pointeurs) | Korin |
|---|---|
| Réserver en `PROT_NONE` puis engager par `mprotect` | `VM_MAP` : région à zéro peuplée à la demande, `PROT_NONE` = simple réservation, **de plusieurs Go** ; arène `0x58_0000_0000`–`0x68_0000_0000` (**64 Go**) ([ABI.md](ABI.md) `0x06` ; `src/kernel/vmspace.asm:971-987`) |
| Cage de 4 Go, bac à sable de 1 To | **aucun** : `v8_enable_sandbox=0` toujours, compression des pointeurs coupée par défaut (`configure.py:1724-1735`) |
| Plage de code | 256 Mo réservés (`deps/v8/src/common/globals.h:419-421`, arm64 sans compression) |
| Pages alignées (256 Ko) : réserver plus, rendre les bords | `VM_UNMAP` coupe aux bords, trous permis |
| `MAP_FIXED` par-dessus une réservation (`DecommitPages`) | `VM_FIXED` dans l'arène remplace ce qui y est (`vmspace.asm:1017-1023`) |
| `madvise(MADV_DONTNEED / MADV_FREE)` | `VM_ADVISE` (`src/lib/posix/mman.c:242`) |
| `MAP_NORESERVE` | accepté, sans effet (rien n'est réservé d'avance) |
| Adresses suggérées au hasard (`GetRandomMmapAddr`, masque `0x3FFFFFFFF000`) | une suggestion hors de l'arène est ignorée : premier trou depuis la base (`vm_find_gap`, `vmspace.asm:870-891`) |
| Page de 4 Ko (`sysconf(_SC_PAGESIZE)`) | 4 Ko |
| **Nombre de régions** | **4 096 régions au plus par process** (`VMA_MAX`, `vmspace.asm:79`), voisines de mêmes droits recollées. Chaque page de code de V8 en fait quatre (en-tête RW, garde, code, garde). Un tas de 1 Go reste sous la limite si les pages se suivent ; à surveiller (`VM_QUERY`) |

Comparaison : CoreCLR a dû limiter son tas (`DOTNET_GCHeapHardLimit`, [DOTNET.md](DOTNET.md):47) parce qu'il voulait
réserver plus que l'arène ; JavaScriptCore n'a tourné qu'en **C_LOOP, sans JIT ni Gigacage**, et ses grandes réservations
restent à essayer ([WEBKIT.md](WEBKIT.md):262-263). V8 sans compression des pointeurs ne réserve que la plage de code et
des pages au fil de l'eau : le cas le plus simple des trois.

### 4.3 Le JIT

* **Droits des pages de code** : sous Linux arm64 sans PKU, V8 12.4 met les pages de code en **RWX une fois pour toutes**
  (`MutablePageMetadata::GetCodeModificationPermission`, `deps/v8/src/heap/mutable-page.h:191-197` ; le W^X rapide n'existe
  que sur macOS arm64, `V8_HEAP_USE_PTHREAD_JIT_WRITE_PROTECT`). Pas de bascule par `mprotect`, pas de double projection :
  **ce que CoreCLR a dû couper** (`DOTNET_EnableWriteXorExecute=0`, [DOTNET.md](DOTNET.md):46) **ne se pose pas**.
  `VM_MAP` accepte `PROT_READ|PROT_WRITE|PROT_EXEC` : le JIT de PCRE2 dans `rg` s'en sert ([RG.md](RG.md) §4).
* **Caches** : V8 vide lui-même (`CpuFeatures::FlushICache`, `deps/v8/src/codegen/arm64/cpu-arm64.cc:27-80`) par
  `dc cvau` / `ic ivau` en EL0, après lecture de `CTR_EL0`. Le noyau les permet : **`SCTLR_EL1.UCI` et `SCTLR_EL1.UCT`**
  sont posés sur tous les cœurs (`src/kernel/arch/pi4/mmu.asm:21-22, 83` ; `src/kernel/arch/common/smp.asm:79`), comme
  pour `__clear_cache` de libgcc qu'emploient CoreCLR et sljit. `ic ivau` est diffusé aux autres cœurs par le matériel.
* **Précédents sur le vrai Pi** : le JIT de CoreCLR y tourne (`hello` en ≈ 3 s, [DOTNET.md](DOTNET.md):233). QEMU ne
  reproduit pas les caches incohérents : **le JIT de V8 se valide sur le PocketTerm**, pas seulement sous QEMU.
* **`--jitless`** (option d'exécution) : V8 n'interprète que (Ignition), aucune page exécutable n'est allouée (les builtins
  sont dans le `.text` du binaire) ; mais **pas de WebAssembly** (§1.3). Parfait pour le premier essai, pas pour Claude Code.

### 4.4 Threads, signaux, divers

| Point | V8 / Node | Korin |
|---|---|---|
| Threads de la plateforme | 4 par défaut (`--v8-pool-size`), plus les 4 du pool de libuv, plus le principal | pthreads : 256 Ko de pile par défaut, page de garde `PROT_NONE`, 64 Mo au plus ; principal : 64 Mo à la demande ([POSIX.md](POSIX.md):347-350) |
| Pile des threads de libuv | `uv__thread_stack_size` part de `RLIMIT_STACK` | `getrlimit(RLIMIT_STACK)` rend la pile du principal (64 Mo) : dans les limites, peuplé à la demande |
| Débordement de pile JS | V8 compare à une limite (`--stack-size`, ≈ 1 Mo), pas de signal | rien à faire |
| Gestionnaire de fautes de WebAssembly | seulement si `V8_TRAP_HANDLER_SUPPORTED` (Linux, macOS, Windows, FreeBSD : `deps/v8/src/trap-handler/trap-handler.h:20-28`) et `NODE_USE_V8_WASM_TRAP_HANDLER` | **coupé** de lui-même sans `__linux__` : V8 vérifie les bornes dans le code de WebAssembly ; ni `SIGSEGV`, ni réservation de 8 Go par mémoire |
| `SIGPROF` | seulement avec `--prof` / `--cpu-prof` | inutile |
| `thread_local` C++ | V8 et Node s'en servent | TLS natif, `posixtest-cxx` 82/82 ([POSIX.md](POSIX.md):98) |
| Horloges | `CLOCK_MONOTONIC`, `CLOCK_REALTIME`, `CLOCK_THREAD_CPUTIME_ID` | `clock_gettime` les connaît (`src/lib/posix/time.c:192-196`) ; `CNTPCT_EL0` lisible en EL0 |
| `Atomics.wait`, `SharedArrayBuffer` | mutex et variables de condition | futex du noyau |
| En-têtes | — | présents : `execinfo.h`, `sys/syscall.h`, `sys/auxv.h`, `link.h`, `dlfcn.h`, `ifaddrs.h`, `sys/statfs.h`, `spawn.h`, `ucontext.h`, `sys/membarrier.h`, `sys/random.h` ; **absents** : `sys/prctl.h`, `sys/eventfd.h`, `sys/sysctl.h`, `pty.h`, `xlocale.h`, `threads.h`, `nl_types.h` (essai de compilation, 11/11/2026) |

Rien dans V8 ne demande `membarrier` (CoreCLR, si).

---

## 5. libuv sous Korin

### 5.1 La plateforme

libuv a une famille « POSIX minimale » pour les systèmes sans `epoll` ni `kqueue` : GNU/Hurd, Haiku, QNX, Cygwin
(`node-v22.22.1/deps/uv/CMakeLists.txt:314-323, 410-449`). Pour Korin, sur le modèle de Hurd :

| Fichier | Rôle |
|---|---|
| `src/unix/async.c`, `core.c`, `dl.c`, `fs.c`, `getaddrinfo.c`, `getnameinfo.c`, `loop.c`, `loop-watcher.c`, `pipe.c`, `poll.c`, `process.c`, `signal.c`, `stream.c`, `tcp.c`, `thread.c`, `tty.c`, `udp.c` | le tronc commun |
| **`posix-poll.c`** | `uv__io_poll` sur `poll()` (385 lignes) |
| `posix-hrtime.c` | `uv_hrtime` par `clock_gettime(CLOCK_MONOTONIC)` |
| `no-proctitle.c`, `no-fsevents.c` | titre du process ; `fs.watch` → `UV_ENOSYS` (`no-fsevents.c:28-33`) |
| `procfs-exepath.c` | `uv_exepath` par `readlink("/proc/self/exe")` : libposix le déduit d'`argv[0]` et de `PATH` ([POSIX.md](POSIX.md):329) |
| `random-getentropy.c` ou `random-devurandom.c` | `getentropy` et `/dev/urandom` existent |
| **`korin.c`** (à écrire, ≈ 150 lignes comme `hurd.c`) | `uv_cpu_info` (cœurs de `SYS_SYSINFO`, « Cortex-A72 »), `uv_get_free_memory` / `total`, `uv_resident_set_memory` (somme des pages présentes par `VM_QUERY`, ou 0), `uv_uptime`, `uv_loadavg` (zéros), `uv_get_constrained_memory` |
| `bsd-ifaddrs.c` | `uv_interface_addresses` par `getifaddrs` (§5.3) |

Et dans `include/uv/unix.h` une branche `__armpi__` qui inclut `uv/posix.h`.

### 5.2 Ce que libposix fournit

Fonctions présentes (vérifié par `nm` sur `libposix.a` et la newlib de la chaîne) : `pipe2`, `accept4`, `dup3`,
`preadv`, `pwritev`, `readv`, `writev`, `sendmsg`, `recvmsg`, `socketpair`, `posix_spawn`, `fork`, `execve`, `waitpid`,
`sigaction`, `pthread_sigmask`, `pthread_atfork`, `sem_*`, `pthread_barrier_*`, `pthread_condattr_setclock`,
`getrlimit`, `setrlimit`, `getrusage`, `sched_getaffinity`, `statfs`, `utimensat`, `futimens`, `realpath`, `scandir`,
`mkdtemp`, `getpwuid_r`, `getaddrinfo`, `getnameinfo`, `if_indextoname`, `ttyname_r`, `cfmakeraw`, `posix_openpt`,
`getentropy`, `getrandom`, `epoll_create1`. Absentes : `sendfile`, `copy_file_range`, `lutimes`, `mremap`,
`sendmmsg`, `recvmmsg`, `eventfd`, `inotify_init1`, `getloadavg`, `sysinfo`, `prctl` — libuv a un chemin sans chacune
hors de Linux (`sendfile` émulé par lecture et écriture, copie de fichier par blocs...).

| Domaine | libuv | Korin | Verdict |
|---|---|---|---|
| Boucle | `poll()` sur tous les descripteurs surveillés | **attente dans le noyau seulement si tous sont des canaux locaux** ; sinon boucle en espace utilisateur **toutes les 1 ms** qui interroge chaque tube (vfs), socket (netd) et pty (vterm) (`src/lib/posix/poll.c:1-8, 176` ; [POSIX.md](POSIX.md):314-318) | marche ; coûte au repos ≈ 1 000 tours/s × (pty + 2 tubes internes + sockets) appels de service. Mesurer, puis une attente unifiée (§9, étape 10) |
| Réveil de la boucle (`uv_async`) | `eventfd` sous Linux, sinon un tube (`deps/uv/src/unix/async.c:268-306`) | tube de vfs | marche |
| Signaux (`uv_signal`) | gestionnaire `sigaction` qui écrit dans un tube | signaux asynchrones du noyau ; `SA_RESTART` sans effet (`EINTR`, que libuv relance) | marche |
| `SIGCHLD` pour les fils | gestionnaire + `waitpid(WNOHANG)` | le noyau envoie `SIGCHLD` si un gestionnaire est installé ([POSIX.md](POSIX.md):280-282) | marche |
| `uv_spawn` | **`fork` + `exec`** hors de macOS (`deps/uv/src/unix/process.c:841`), tube `CLOEXEC` pour l'erreur d'`exec` | `fork` en copie sur écriture, `exec` réel, `FD_CLOEXEC` respecté | marche |
| Tubes vers le fils (`stdio: 'pipe'`) | **`socketpair(AF_UNIX, SOCK_STREAM)`** (`process.c:202-207`) | canaux du noyau : `poll` y dort vraiment. **Mais** un programme en assembleur (les `cat`, `ls`, `grep`... de `/bin`) lit ses entrées et sorties par `V_STDIO` de vfs, qui ne connaît que tubes, ptys et fichiers ([POSIX.md](POSIX.md):145-146) | **bloquant** pour l'outil Bash : bash hérite d'un canal et le passe à un `ls` en assembleur. Patch : `uv__make_pipe` (tubes de vfs) au lieu de `uv_socketpair` sous Korin, ou apprendre les canaux à vfs |
| TCP | non bloquant, `connect` → `EINPROGRESS` | IPv4 ; **`connect` toujours bloquant** (rend 0) ; `bind` sur le port 0 sans port choisi ([POSIX.md](POSIX.md):310-313 ; [DOTNET.md](DOTNET.md):291) | marche (la boucle s'arrête le temps de la poignée de main) ; port 0 : à corriger dans netd |
| DNS | `getaddrinfo` dans le pool de threads | `net_resolve` de NetKit | marche ; `AI_ADDRCONFIG` sans objet (IPv4) |
| `AF_UNIX` nommés (`uv_pipe_bind`) | sockets de fichiers | `EAFNOSUPPORT` | pas demandé (§1.4) |
| TTY | `uv_tty_init` rouvre l'esclave par `ttyname_r` (`deps/uv/src/unix/tty.c:182`), `tcsetattr` (mode brut), `TIOCGWINSZ` | `/dev/pts/N`, `termios`, `TIOCGWINSZ`, `SIGWINCH`, `O_NONBLOCK` (`src/lib/posix/tty.c:227-234`) ; `isatty` vrai **seulement** sur un pty | marche (le Terminal lance bash sur un pty) |
| Type des descripteurs (`uv_guess_handle`) | `fstat` : `S_IFCHR` + `isatty`, `S_IFIFO`, `S_IFSOCK` | `fstat` rend ces types (`src/lib/posix/stat.c:167-206`) | marche |
| Fichiers | pool de threads, `open`/`read`/`pread`/`fstat`... | AppKit : **64 fichiers ouverts au plus** (`src/kits/appkit/fileclient.inc:58-59`), une seule date, pas de liens, chemins ≈ 254 octets | marche ; `EMFILE` possible avec beaucoup de lectures parallèles |
| `uv_os_*` | `getpwuid_r`, `HOME`, `TMPDIR`, `uname` | `uname` rend `ARMPI` (`src/lib/posix/misc.c:149-159`) ; `TMPDIR=/tmp` | marche ; `os.type()` dira « ARMPI » |

### 5.3 Trous à combler

1. **Tubes des fils** : ci-dessus (bloquant pour l'outil Bash).
2. **`getifaddrs`** rend `ENOSYS` (`src/lib/posix/misc.c:676`) : `os.networkInterfaces()` lève une exception. À faire
   dans libposix depuis `net_info` de NetKit (boucle locale et interface Wi-Fi, IPv4).
3. **`bind` sur le port 0** dans netd (un port éphémère) : `server.listen(0)` sert à Claude Code et à npm.
4. **`fs.watch`** : §5.4.
5. `ps` et `which` : §7.3.

### 5.4 `fs.watch`

Pas d'`inotify` ni de notification de vfs : `uv_fs_event_start` rend `UV_ENOSYS`, `fs.watch` lève une erreur.
`fs.watchFile` (sondage par `stat`, `uv_fs_poll`) marche. Claude Code passe surtout par chokidar, qui sait sonder
(`usePolling`, et `CHOKIDAR_USEPOLLING=1` dans l'environnement) ; ses `fs.watch` directs ne surveillent que quelques
fichiers de réglages. Deux temps : d'abord `ENOSYS` et `CHOKIDAR_USEPOLLING=1`, voir ce qui casse ; puis, si besoin,
`uv_fs_event` dans `korin.c` par sondage (une minuterie de la boucle, `stat` du fichier ou liste du dossier comparée).

---

## 6. Le C++ de Node

### 6.1 Ce qui dépend du système

Les `#ifdef __linux__` de `src/` (28 dans `node.cc`, 13 dans `large_pages`, 9 dans `debug_utils.cc`, 7 dans
`node_watchdog.cc`, 6 dans `node_credentials.cc`...) ont tous une branche POSIX générique, prise sans `__linux__` :

| Fichier | Point | Korin |
|---|---|---|
| `src/node.cc:579` (`PlatformInit`) | `fstat` des descripteurs 0 à 2 (sinon `/dev/null`), gestionnaires de signaux remis par défaut, `SIGPIPE` ignoré, état des terminaux gardé, `RLIMIT_NOFILE` monté au maximum | `fstat`, `/dev/null`, `sigaction`, `setrlimit` existent |
| `node_credentials.cc` | `getuid`, `setgid`, `getgroups` ; sous Linux, `AT_SECURE` | une seule identité, uid 0 |
| `node_os.cc`, `node_process_methods.cc` | `uv_*` (§5) ; `process.memoryUsage` | `uv_resident_set_memory` de `korin.c` |
| `node_report.cc` | `process.report` (`uv_*`, `uname`) | à garder (Claude Code lit `process.report` une fois) |
| `node_binding.cc` (`process.dlopen`) | `dlopen` | échoue proprement : **modules natifs impossibles**, Claude Code n'en exige aucun (§1.3) |
| inspecteur | `--without-inspector` | `require('inspector')` lève `ERR_INSPECTOR_NOT_AVAILABLE` |
| modèle de permissions | JavaScript | rien de propre au système |
| `large_pages` | Linux seulement | sans objet |

### 6.2 Entrées et sorties standard

Node regarde le type de 0, 1, 2 (`uv_guess_handle`, `src/node_util.cc:228`) : un pty de vterm devient un `tty.WriteStream`
(`isTTY`, `columns`, `setRawMode`) ; un tube, un `net.Socket` ; la console de Korin (`S_IFCHR` sans être un terminal), un
flux synchrone. Le Terminal ouvre bash sur un pty : `claude` lancé de bash a un vrai terminal. Ink écrit des séquences que
le Terminal VT connaît (§1.4 de [TERMINAL-ET-FORK.md](TERMINAL-ET-FORK.md)) ; les requêtes `DSR` et `DA` reçoivent une
réponse (`src/apps/terminal/terminal.asm:358`) ; une requête inconnue (`OSC 11 ;?`, protocole clavier de kitty) est
avalée et Claude Code attend sa réponse jusqu'à un délai.

### 6.3 Les chemins à volume

`path.posix` tient pour absolu ce qui commence par `/` (`node-v22.22.1/lib/path.js:1337-1341`) et `path.resolve` part
de `process.cwd()` (`lib/path.js:1239-1290`). Or `getcwd` rend le plus souvent « `SD0:/projets/x` » (sauf après un
`chdir` vers un chemin virtuel, [POSIX.md](POSIX.md):359-365). Alors `path.isAbsolute(process.cwd())` est faux, et un
chemin peut être recollé au dossier courant (« `SD0:/a/SD0:/a/x` ») : **l'erreur qu'ont eue git et bash**, corrigée chez
eux par un patch (`git-0002-volumes`, [GIT.md](GIT.md):53-57 ; `01-chemins-volume.patch`, [BASH.md](BASH.md):30). Claude
Code fait 1 183 imports de `path` et compare souvent des chemins par `startsWith("/")` : patcher Node ne suffirait pas.

Proposition (§10, décision 6) : **un arbre POSIX vu des programmes C**.

* La table des chemins virtuels de vfs (`SD0:/etc/paths.txt`, 16 lignes, [FICHIERS.md](FICHIERS.md) « Chemins virtuels »)
  reçoit `/sd0 = SD0:/`, `/sd1 = SD1:/`, `/ram0 = RAM0:/` (à côté de `/bin`, `/gcc`, `/tmp`...).
* `getcwd` et `realpath` de libposix rendent la forme POSIX (« `/sd0/projets/x` ») au lieu de « `SD0:/projets/x` »
  quand une ligne de la table couvre le chemin ; `HOME` vaut `/sd0` (ou `/sd0/home`).
* Les deux formes restent acceptées partout en entrée : git et bash, déjà patchés, n'y perdent rien.
* À vérifier : `lstat("/")` et `lstat("/sd0")` (le `realpath` en JavaScript de Node remonte composant par composant).

---

## 7. npm et l'installation de Claude Code

### 7.1 npm

Debian sépare npm (`universe/n/npm` : 9.2.0, 11.16.0, 12.2.0) ; l'archive officielle de Node a `deps/npm` (10.9.4), du
JavaScript seul. Sur Korin, npm a trois difficultés : **les liens symboliques** (`symlink` → `ENOSYS`) qu'il pose pour
les commandes (`bin-links`) — `npm config set bin-links false`, ou les petits scripts que npm écrit pour Windows ; le
**réseau** vers `registry.npmjs.org` depuis le Pi (HTTPS : à essayer) ; et beaucoup de petits fichiers sur FAT32 (cache
`~/.npm`, `rename` atomiques).

### 7.2 Installer Claude Code

| Voie | Comment | Pour | Contre |
|---|---|---|---|
| **A. Préparée sur le PC** (recommandée d'abord) | `npm pack @anthropic-ai/claude-code@<version>` sur le PC → archive ; son dossier `package/` copié dans `SD0:/node/lib/node_modules/@anthropic-ai/claude-code` ; `SD0:/bin/claude` = script `#!/bin/bash` → `exec /bin/node /node/lib/node_modules/@anthropic-ai/claude-code/cli.js "$@"` | aucune dépendance d'exécution (le paquet est un seul `cli.js` regroupé) ; pas de npm sur la carte | mise à jour à la main |
| B. npm sur la carte | `npm install -g --no-bin-links @anthropic-ai/claude-code` | comme sous Linux | npm, réseau, FAT32 : plus d'inconnues |

Dans les deux cas : `DISABLE_AUTOUPDATER=1` (la mise à jour automatique remplace des liens et des binaires natifs),
et le script `claude` à la main (le `#!/usr/bin/env node` de `cli.js` ne sert pas : pas de `/usr/bin/env`).

**À vérifier en tout premier** (sur le PC, une minute) : que la version visée est toujours publiée sur npm comme un `cli.js`
pour Node (le code de 2.1.322 parle encore d'installations « npm-global » et de `cli.js`, mais l'installeur natif est mis
en avant), et ce que contiennent ses `optionalDependencies` (traitement d'images : facultatif).

**Licence** : Claude Code n'est pas un logiciel libre ; le copier sur sa propre carte va, le **redistribuer dans un paquet
de Korin** est à vérifier (§10, décision 9). Node.js (MIT) et ses dépendances se redistribuent.

### 7.3 Ce que Claude Code lance

| Commande | Pourquoi | Korin |
|---|---|---|
| `bash` | outil Bash, instantané de l'environnement du shell (`-c -l`, `declare -f`, `alias`) | bash 5.2 ; `SHELL=/bin/bash` |
| `rg` | Grep, Glob, index `@` | `rg` de [RG.md](RG.md) |
| `git` | état du dépôt, diffs | git 2.51 |
| **`which`** | trouver une commande sous Node (`spawnSync("which",[nom])`) | **absent** : un script `#!/bin/bash` sur `command -v` suffit |
| **`ps -A -o pid= -o ppid= -o pgid= -o stat= -o lstart=`**, `ps -o args= -p N` | tâches de fond, arbre des process | **absent** : un petit `ps` en C sur `SYS_PROCLIST` (les colonnes demandées seulement) |
| `open`, `xdg-open` | ouvrir une URL (connexion) | absent : Claude Code affiche alors l'URL |
| commandes du modèle (`ls -la`, `find`, `sed -i`...) | l'outil Bash | les commandes de `/bin` sont en assembleur, avec peu d'options GNU : à suivre, hors de cette étude |

### 7.4 Connexion

Le rappel OAuth demande un serveur local sur un port choisi par le système (§5.3) et un navigateur. Plus simple : la
variable `ANTHROPIC_API_KEY`, ou le code affiché par Claude Code à coller dans le Terminal, ou le fichier de
connexion (`~/.claude/.credentials.json`) recopié d'une machine connectée.

---

## 8. Risques et inconnues

| # | Risque | Comment le lever tôt |
|---|---|---|
| 1 | Le paquet npm de Claude Code n'est plus un `cli.js` pour Node | `npm view` / `npm pack` sur le PC avant tout (§7.2) |
| 2 | V8 12.4 se compile surtout avec clang ; GCC 14.2 sans `__linux__`, gyp et un nouveau système | étape 1 : `configure` + les outils de l'hôte + `mksnapshot` (simulateur) ; étape 2 : `d8` |
| 3 | JIT : caches sur quatre vrais cœurs (QEMU ne le montre pas) | `d8` puis `node` avec JIT **sur le PocketTerm** dès l'étape 2 ; `--jitless` comme repli de diagnostic |
| 4 | Limite de 4 096 régions, arène de 64 Go, recherche de trou linéaire | `VM_QUERY` pendant une longue session ; relever `VMA_MAX` si besoin |
| 5 | Chemins `SD0:/...` (§6.3) | décider l'arbre POSIX avant l'étape 6 ; essai `path.resolve` / `process.cwd()` |
| 6 | Tubes des fils = canaux, illisibles par les commandes en assembleur (§5.2) | essai `spawn('bash',['-c','ls | cat'])` dès l'étape 5 |
| 7 | `poll` en boucle de 1 ms : processeur et batterie au repos | mesurer la charge (`cpuwatch`) avec `claude` inactif |
| 8 | Démarrage lent : binaire de ≈ 100 Mo (ICU complet), pas d'instantané de Node, 10 Mo de `cli.js` à analyser | `preload SD0:/bin/node` (images épinglées, [WEBKIT.md](WEBKIT.md)), `NODE_COMPILE_CACHE`, instantané en deux temps |
| 9 | `Intl.Segmenter` avec `small-icu` | essai dès l'étape 4 si `small-icu` est choisi |
| 10 | 64 fichiers ouverts (AppKit), `connect` bloquant, IPv4 seul, `bind` port 0 | tests de libuv à l'étape 3 |
| 11 | `fs.watch` absent | `CHOKIDAR_USEPOLLING=1` ; `korin.c` par sondage si besoin |
| 12 | Mémoire : Claude Code occupe 200 à 400 Mo sous Linux | le Pi 4 a 4 ou 8 Go ; `--max-old-space-size` si besoin |

Pourquoi pas autrement : l'exécutable natif de Claude Code est un ELF Linux (Bun lié à la glibc ou à musl), et Korin n'a
pas de personnalité Linux ; porter Bun demanderait JavaScriptCore avec son JIT et Zig ; `jsc` (déjà là) n'a aucune API
de Node.

---

## 9. Ordre des étapes

Chaque étape se valide avant la suivante, sous QEMU puis sur le PocketTerm (le JIT : surtout sur le PocketTerm).

| # | Étape | Dépend de | Essai d'acceptation |
|---|---|---|---|
| 1 | **Sources et construction** : archive officielle (ou Debian + dépendances, §3) hors du dépôt ; `tools/node/build.sh`, `tools/node/korin.patch` ; `configure` (§2.3) ; outils de l'hôte | — | `configure` passe ; `torque` et `mksnapshot` (hôte, simulateur arm64) tournent et produisent `embedded.S` et `snapshot.cc` ; durée de la construction notée |
| 2 | **V8 seul (`d8`)** : `V8_OS_KORIN`, `platform-korin.cc`, liaison statique | 1 | `d8 --jitless -e "print(6*7)"` → `42` ; `d8 --jitless SD0:/webkit/smoke.js` → `smoke: ok (27 checks)` (l'essai de `jsc`) ; puis **avec JIT sur le PocketTerm** : `smoke.js`, `bench.js` (chiffres comparés à `jsc` C_LOOP), un module WebAssembly instancié, 10 min de calcul sans faute |
| 3 | **libuv** : `posix-poll`, `korin.c`, tubes des fils par vfs ; dans libposix : `getifaddrs` ; dans netd : port 0 | — | les tests de libuv (`deps/uv/test`, construits comme un programme de Korin) : boucle, minuteries, `async`, fichiers, tubes, TCP sur 127.0.0.1, `spawn` (y compris d'un programme en assembleur), signaux, tty, pool de threads ; sautés et notés : `fs_event`, `AF_UNIX` nommés, IPv6, multidiffusion |
| 4 | **`node` minimal** : `--without-intl --without-ssl`, `--jitless` à l'exécution | 2, 3 | `node --jitless -e "console.log(1)"` ; `node -p process.versions` ; `node -e "require('fs').readdirSync('.')"` ; le REPL dans le Terminal (flèches, Ctrl+C) |
| 5 | **`node` complet** : JIT, ICU, OpenSSL, zlib, brotli, zstd, c-ares, nghttp2, llhttp | 4 | `nodecheck.js` (comme `dotnetcheck`) : fichiers, `child_process` (bash, tubes, `ls | cat`), minuteries, `crypto`, `zlib` gzip/brotli/zstd, `Intl.Segmenter` en graphèmes, `fetch("https://api.anthropic.com")` (une réponse HTTP, 401 ou 404), WebAssembly, `worker_threads`, mode brut et `SIGWINCH` ; une partie des tests de Node (`test/parallel/test-fs-*`, `-child-process-*`, `-http-*`, `-crypto-*`, `-zlib-*`) lancée par un petit exécuteur en JavaScript sur la carte |
| 6 | **Arbre POSIX des chemins** (§6.3) : table de vfs, `getcwd` et `realpath` de libposix | 5 | `node -p process.cwd()` → `/sd0/...` ; `path.resolve`, `fs.realpathSync` ; `posixtest`, les essais de bash et de git inchangés (`testing_prep_nodejs`) |
| 7 | **Installation de Claude Code** (voie A, §7.2) ; `which`, `ps` ; environnement (`SHELL`, `CHOKIDAR_USEPOLLING`, `DISABLE_AUTOUPDATER`, `NODE_EXTRA_CA_CERTS`) | 5, 6 | `claude --version` ; `claude -p "Réponds juste bonjour"` → une réponse de l'API (clé d'API) |
| 8 | **Claude Code interactif** dans le Terminal | 7 | l'invite d'Ink s'affiche, se redessine au redimensionnement ; une séance : lire un fichier, le modifier (outil Edit), lancer `ls` et `git status` (outil Bash), chercher (`rg`), Échap et Ctrl+C ; aucune séquence affichée en clair |
| 9 | **Paquets** : `nodejs` (`bin/node` et sa bibliothèque), `npm` ; le script d'installation de Claude Code | 7 | `pkg install nodejs` sur une carte neuve, puis l'étape 7 refaite |
| 10 | **Vitesse et finitions** : instantané de Node en deux temps, `NODE_COMPILE_CACHE`, `preload` de `node`, `fs.watch` par sondage, attente unifiée dans le noyau pour `poll` (tubes de vfs, ptys, sockets de netd), npm sur la carte | 8 | démarrage de `claude` chronométré avant et après ; charge du processeur au repos ; `npm install -g` d'un petit paquet |

Les étapes 2 et 3 sont indépendantes : elles peuvent avancer en parallèle.

---

## 10. Décisions pour le propriétaire

0. **Quel Claude Code ?** (constat en tête de ce document) : soit **Node.js + Claude Code 2.1.112**, la dernière en
   JavaScript, figée (le reste de cette étude) ; soit une **couche de compatibilité Linux** dans le noyau (à la manière
   du « Linuxulator » de FreeBSD : un process marqué Linux voit les numéros d'appels de Linux arm64, traduits vers ceux
   de Korin ; plus la `libc.so` de musl, sous licence MIT, comme chargeur) pour lancer le binaire natif à jour, et avec
   lui d'autres programmes Linux arm64 ; soit les deux, Node.js restant utile en soi. À étudier avant de choisir : la
   liste des appels système Linux qu'emploient musl et le binaire (Bun : `epoll`, `futex`, `clone`, `mmap`, signaux,
   `/proc/self/...`).

| # | Question | Choix possibles | Recommandation |
|---|---|---|---|
| 1 | Version de Node | **22.22.1** (sources ici ; V8 12.4 ; LTS de maintenance jusqu'au 30/04/2027) ; **24.22.2** (Ubuntu ; V8 13.6 ; LTS jusqu'en 04/2028) | 22 d'abord (sources locales, V8 plus ancien et mieux connu de GCC) ; patch petit, pour passer à 24 ensuite |
| 2 | Sources | archive **officielle** de nodejs.org (toutes les dépendances, `tools/gyp`, `deps/npm`, sommes signées : un téléchargement) ; ou l'archive **Debian** + quinze morceaux (§3) | l'archive officielle |
| 3 | Intl | `full-icu` (≈ 30 Mo de données de plus, tout marche) ; ICU filtré (`small-icu` + règles de coupure, comme `tools/webkit/ports/icu/data-filter.json`) ; `none` (impossible : Claude Code s'arrête au chargement) | `full-icu` d'abord, filtré ensuite |
| 4 | Instantané de Node | aucun (croisé : par défaut) ; en deux temps sur la carte ; à l'exécution (`--snapshot-blob`, `NODE_COMPILE_CACHE`) | aucun d'abord, mesurer, puis `NODE_COMPILE_CACHE` |
| 5 | Ce que voit Claude Code | `process.platform === "linux"` (gyp en Linux, sans `__linux__` dans le code) ; ou un nom à nous (`"korin"`), inconnu de Claude Code | `"linux"` |
| 6 | Chemins | arbre POSIX `/sd0`, `/sd1`, `/ram0` dans vfs et `getcwd` POSIX dans libposix (§6.3) ; ou patch de `lib/path.js` | l'arbre POSIX (sert aussi aux autres ports) |
| 7 | Où vit Node sur la carte | `SD0:/bin/node` (binaire) + `SD0:/node/lib/node_modules` (ligne `/node = SD0:/node` dans `paths.txt`) ; ou tout sous `SD0:/node` avec `/node/bin` dans `PATH` | `SD0:/bin/node` + `SD0:/node` |
| 8 | Installation de Claude Code | préparée sur le PC (`npm pack`, voie A) ; npm sur la carte (voie B) | A d'abord, B à l'étape 10 |
| 9 | Paquets | `nodejs`, `npm` dans `tools/pkg/packages.ini` ; Claude Code : paquet ou script qui l'installe (licence à vérifier) | `nodejs` et `npm` ; pour Claude Code, un script d'installation, pas le code d'Anthropic dans un paquet |
| 10 | OpenSSL | celui de Korin (`--shared-openssl`, 3.5.9) ; celui de Node (3.5.5, construit par ses `.gyp`) | celui de Korin |
| 11 | Tubes des fils de libuv | tubes de vfs (patch de libuv, `poll` en boucle) ; canaux du noyau appris à vfs pour `V_STDIO` (travail dans vfs et AppKit) | tubes de vfs d'abord |
