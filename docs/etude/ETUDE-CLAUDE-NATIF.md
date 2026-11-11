# Claude Code natif (exécutable Bun) sous Korin : étude

Étude personnelle du 11/11/2026, **hors du dépôt**, avant tout port. Suite de la décision 0 de `docs/NODEJS.md`
(§10 : Node.js + Claude Code 2.1.112, sans couche de compatibilité Linux ni BSD) : depuis la 2.1.113, Claude Code n'est
plus publié que sous forme d'**exécutables natifs** ; le propriétaire demande d'analyser le binaire Linux pour voir
comment porter la version courante sous Korin, **sans couche de compatibilité**.

Matériel lu, en lecture seule, sans jamais lancer l'exécutable :

* `/opt/claude-code/bin/claude` : Claude Code **2.1.322**, Linux **x86-64** (341 914 128 octets, BuildID GNU
  `69e08210…`) ;
* pour comparer : le paquet npm **2.1.42** installé sous `/opt/node22/lib/node_modules/@anthropic-ai/claude-code`
  (`cli.js` de 11,5 Mo, construit pour Node) ;
* Node **22.23.4** (`/opt/node22`) et **24.22.1** (`/opt/node24`) de la machine, pour des **analyses syntaxiques
  seules** des morceaux extraits (`vm.SourceTextModule`, sans évaluation ni édition des liens).

Outils écrits pour l'étude (dans `ccbin/tools/`) : `bunpayload.py` (lit la section `.bun`, écrit la table des modules
en JSON), `peek.py` (premier décodage), `parsecheck.mjs` (analyse syntaxique). Les fichiers extraits sont dans
`ccbin/out/files/` (usage d'analyse seulement, jamais dans git). **Ce document ne cite pas le code de Claude Code** : il
en donne la structure, les tailles, les formats, les noms d'API et des comptes.

Les décisions à prendre sont rassemblées à la fin (§8).

---

## 1. L'exécutable

### 1.1 ELF

| Point | Valeur |
|---|---|
| Type | ELF64 `EXEC` (non PIE), x86-64, entrée `0x319a680`, 14 en-têtes de programme, 48 sections, **non dépouillé** (mais `.symtab` ne compte que 1 255 symboles) |
| Liaison | **dynamique**, glibc : `ld-linux-x86-64.so.2`, `libc.so.6`, `libpthread.so.0`, `libdl.so.2`, `librt.so.1`, `libm.so.6` ; 521 symboles importés |
| Note ABI | `NT_GNU_ABI_TAG` Linux 3.2.0 |
| Compilateurs (`.comment`) | **clang 23.1.1 + LLD 23.1.1**, **rustc 1.100.0-nightly (2026-09-14)**, GCC 13.1 (crt d'Ubuntu 20.04), clang 23.1.2 d'Ubuntu |
| Pile | `GNU_STACK` de 0xc35000 (12,2 Mo) |
| Fichier `claude.hot` à côté | 9,5 Ko : « hot ranges » (taille et SHA-256 du binaire, puis des plages de pages) : préchargement des pages chaudes, sans effet sur le format |

Sections propres à Bun :

| Section | Taille | Contenu |
|---|---|---|
| `.note.bun` | 4 Ko | note `Bun`/`NT_VERSION`, **vide** (zéros) : réservée |
| `.note.bun.build-id` | 16 o | vide |
| `.bun_trim` | 115 Ko | en-tête `BUNTRIM` + tables (rognage de l'image) |
| `.bun_aot` | 3,7 Ko | en-têtes `BUNAOTTB`, `BUNAOTLO`, `BUNAOTHO`, `BUNAOTRT` ; contient le **commit de Bun en entier** |
| `.bun_features` | 536 o | `BUNFEAT`, vide |
| `.bun_builtins` | 2,76 Mo | le JavaScript interne de Bun (modules `node:*`, etc.) |
| `__DATA,__jsc_opcodes`, `__DATA,__wtf_config` | 16 Ko chacune | tables de JavaScriptCore (LLInt) et de WTF |
| **`.bun`** | **192,47 Mo** | **le graphe de modules autonome** (§2), dans le segment `LOAD` RW |

### 1.2 Répartition de la taille

| Partie | Octets | Part |
|---|---:|---:|
| `.text` (code machine : Bun, JavaScriptCore, dépendances, ripgrep) | 115 869 133 | 33,9 % |
| `.rodata` (dont ICU 79 : `icudt79l`) | 28 070 584 | 8,2 % |
| `.bun_builtins` | 2 757 816 | 0,8 % |
| reste du runtime (`.eh_frame`, `.data`, tables dynamiques…) | ≈ 2,7 Mo | 0,8 % |
| **Runtime Bun en tout** (tout sauf `.bun`) | **149 443 235** | **43,7 %** |
| **Charge utile `.bun`** | **192 470 893** | **56,3 %** |

### 1.3 Version de Bun

| Indice | Valeur |
|---|---|
| Chaînes | `Bun v1.4.4 (424a542a9) Linux x64`, `bun-v1.4.4`, URL de mise à jour `…/bun-v1.4.4/bun-linux-x64.zip` |
| `.bun_aot` | commit complet `424a542a9c79d679fcdacc37a52d4fb2134408ca` |
| WebKit | fourche `oven-sh/WebKit`, construite en LTO (chemins `cache/webkit-4d45755fed4aeef1-lto-aot/…`) |
| Langage du runtime | **Rust** d'après le binaire : rustc dans `.comment`, chemins `src/bundler/bundle_v2.rs`, `src/jsc/sizes.rs`, `src/js_parser/p.rs`, messages de panique et `RUST_BACKTRACE` de la `std` de Rust ; **aucun chemin `.zig`** ; des noms hérités subsistent (`ZigSourceProvider`, `asZigRepr`). Bun 1.4 semble donc être la version de Bun réécrite en Rust (à confirmer dans ses sources) |
| Fourche | le code de Claude Code exige une API `Bun.ant.*` absente de Bun public, et renvoie dans ses messages au paquet **`@anthropic-ai/bun-internal`** « épinglé dans package.json » : **Claude Code tourne sur une fourche interne de Bun** (§3.3) |

Dépendances reconnues par leurs chaînes dans le runtime : JavaScriptCore (avec LLInt et JIT), ICU 79, BoringSSL,
mimalloc, uWebSockets/uSockets, c-ares, zlib, libdeflate, brotli, zstd, highway, lol-html, TinyCC (`bun:ffi`),
SQLite 3.53.4, et **ripgrep** (Rust, lié dans le binaire : Claude Code se relance lui-même avec `argv0 = "rg"`).

Ce que le runtime demande à Linux (symboles importés de la glibc) : `epoll_*`, `eventfd`, `signalfd`, `inotify_*`,
`pidfd_spawnp`, `pidfd_getpid`, `posix_spawn*` (avec `addchdir`), `fork`, `vfork`, `execve`, `wait4`, `waitid`, `prctl`,
`sched_{get,set}affinity`, `recvmmsg`, `sendmmsg`, `splice`, `sendfile`, `accept4`, `getifaddrs`, `dlopen`, et
`syscall` (appels directs). Pas de chaîne `io_uring`.

---

## 2. La charge utile : le graphe de modules de `bun build --compile`

### 2.1 Format (Bun 1.4.4)

La section `.bun` commence par un `u64` (la taille de ce qui suit : 192 470 885) ; la charge utile suit. Elle se lit
**par la fin** :

| Morceau | Taille | Champs |
|---|---|---|
| Fin de fichier | 16 o | `"\n---- Bun! ----\n"` |
| `Offsets` | 32 o | `byte_count` (u64) ; `modules` (décalage u32, longueur u32) ; `entry_point_id` (u32) ; `compile_exec_argv` (u32, u32) ; `flags` (u32) |
| Table des modules | 163 800 o = **3 150 × 52 o** | par module : six « StringPointer » (décalage u32, longueur u32) — `name`, `contents`, `sourcemap`, `bytecode`, `module_info`, `bytecode_origin_path` — puis quatre octets : `encoding`, `loader`, `module_format`, `side` |

Valeurs de cet exécutable : point d'entrée **n° 7, `/$bunfs/root/cli`** ; `compile_exec_argv` = `--zero-fill-buffers`
(19 octets : option de Bun ajoutée au lancement) ; `flags` = `0x3bff`. Tous les noms sont sous **`/$bunfs/root/`**, le
système de fichiers virtuel de Bun (un seul sous-dossier : `src/plugins/functionHooks/hooks-worker/`).

Disposition de la charge utile (décalages depuis son début) :

| Plage | Octets | Contenu |
|---|---:|---|
| 0 – 24 082 296 | 24 082 296 | **zone non référencée par la table** (15,6 Mo non nuls) : noms de modules et chaînes internes de Bun ; vraisemblablement une image précalculée (« AOT », cf. `.bun_aot`) **liée à ce runtime précis** — hypothèse, non décodée |
| → 133 464 456 | 109 382 160 | **cache de bytecode de JavaScriptCore**, 2 900 modules (le champ `bytecode` donne le début ; sa « longueur » court jusqu'à la fin de la zone : la taille réelle est l'écart au module suivant) |
| → 187 357 552 | 53 893 096 | **sources JavaScript** (2 902 modules) |
| → 192 090 935 | 4 733 383 | autres fichiers (248) |
| fin | 379 882 + 51 | noms, `bytecode_origin_path`, argv, table, `Offsets`, fin de fichier |

`sourcemap` et `module_info` sont vides partout. Le décodage est vérifié par la somme des tailles (à un octet près, le
remplissage) et par la cohérence de chaque nom.

### 2.2 Inventaire des 3 150 fichiers

| Genre (`loader`, `encoding`) | Nombre | Octets | Remarque |
|---|---:|---:|---|
| JavaScript ESM (`js`, latin1, `module_format` = esm), avec bytecode | 2 900 | 53,9 Mo | `cli` (26 Ko, l'entrée) et **2 898 `chunk-*.js`** : morceaux de 190 o à 5,5 Mo (médiane 4,7 Ko, 90 % < 28 Ko) ; **plus `hooks-worker.js`** |
| JavaScript sans bytecode (`js`) | 2 | — | `clipboard-napi.js`, `audio-capture.js` : enveloppes des modules natifs |
| Modules natifs `.node` (`napi`) | 2 | 1,57 Mo | `clipboard-napi.node`, `audio-capture.node` : **ELF x86-64** liés dynamiquement |
| Fichiers `.zst` (`file`) | 137 | 1,65 Mo | Markdown, scripts `.mjs`/`.py`, JSON, `.d.ts` compressés par zstd (documentation et scripts de compétences, de l'outil d'API, etc.) |
| `.js` compressés (`file`) | 3 | 1,02 Mo | `mermaid.min.js`, `chart.umd.min.js`, `hljsBundle.generated.min.js`, **compressés par zstd** (pour des pages HTML produites) |
| Texte (`text`) `.md`, `.txt`, `.mjs` | 104 | 0,47 Mo | importés comme des chaînes |
| Polices `.woff2.asset` | 2 | 26 Ko | |

**Absents** : pas de WebAssembly (ni tree-sitter, ni `yoga.wasm`, ni `resvg.wasm`, que le paquet npm 2.1.42 avait
encore), pas de binaire `rg` (ripgrep est lié dans le runtime), pas de `computer_use.node`.

### 2.3 Le JavaScript est là en **source**

Chaque morceau commence par le pragma `// @bun @bytecode` puis la ligne de droits d'Anthropic ; `cli` donne
« Version: 2.1.322 ». Les 53,9 Mo sont du **JavaScript complet, minifié, lisible** (identificateurs courts, chaînes et
noms d'options en clair), à côté de son bytecode. Le bytecode n'est qu'un **cache** : il dépend de la version exacte de
JavaScriptCore et Bun revient à la source s'il ne correspond pas. Pour nos deux voies, **seules les sources comptent**.

Empreinte des 2 905 fichiers JavaScript extraits (SHA-256 de la liste `js-sha256.txt`) : `18cd5048452906cd…` — à
comparer avec celle d'une autre plateforme (§5.3).

### 2.4 Analyse syntaxique par Node (sans exécution)

| Moteur | Modules acceptés | Refusés |
|---|---:|---|
| Node **22.23.4** (V8 12.4) | 2 842 / 2 901 | **59** : déclarations **`using` / `await using`** (102 occurrences dans 59 fichiers, dont l'enveloppe des fonctions de fichiers, chemin chaud) |
| Node **24.22.1** (V8 13.6) | **2 901 / 2 901** | aucun (les 3 refus restants sont les `.js` compressés par zstd, qui ne sont pas des modules) |

Autres nouveautés du langage employées : `Promise.withResolvers` (106), `Map.groupBy` (24), `toSorted` / `toSpliced`,
`Iterator.from`, `Symbol.dispose` / `asyncDispose` (130) ; `Error.isError` et `Uint8Array.fromBase64` sont testés
avant usage. **Conséquence : Node 22 ne peut pas charger ce code tel quel** ; Node 24 oui (ou une passe d'abaissement
de `using` sur le PC, §8 décision 2).

---

## 3. Ce que le code demande à son runtime

### 3.1 Comparaison avec le paquet npm

| | npm 2.1.42 (construit pour Node) | natif 2.1.322 (extrait) |
|---|---:|---:|
| Taille du JavaScript | 11,5 Mo, un fichier | 53,9 Mo, 2 900 modules ESM |
| Références `Bun.*` | **17**, toutes précédées d'un test | **113** (35 membres + 13 de `Bun.ant`) |
| Tests `typeof Bun` | 21 | 32 |
| `import.meta.require` | 0 | **737** |
| `using` | 0 | 102 |

Le constat de `docs/NODEJS.md` §1.1 (« le code teste `typeof Bun` avant chaque appel propre à Bun ») valait pour le
code construit pour Node ; **le bundle natif ne l'est plus** : il est écrit pour Bun, avec des replis pour Node à
certains endroits seulement.

### 3.2 API de Node

Les modules de Node importés sont ceux de `docs/NODEJS.md` §1.2, aux mêmes comptes à peu près : `path` 1 184,
`fs/promises` 643, `crypto` 496, `fs` 312, `os` 222, `util` 106, `child_process` 64, `net` 48, `async_hooks` 37,
`url` 35, `http` 35, `stream` 31, `vm` 26, `process` 22, `readline` 17, `zlib` 15, `buffer` 15, `https` 14,
`stream/promises` 13, `string_decoder` 12, `tls` 11, `events` 9, `timers/promises` 8, `tty` 6, `dns` 5 (+3
`dns/promises`), `util/types` 3, `assert` 3, `module` 2, `worker_threads`, `perf_hooks`, `inspector`,
`stream/consumers` (1 chacun). La plateforme est lue **à l'exécution** (`os.platform()`, environnement WSL…), pas
figée à la construction.

### 3.3 API propres à Bun

Comptes dans les 2 905 fichiers extraits ; « gardé » = un test `typeof Bun` / `?.` juste avant ; « try » = dans un
bloc `try`.

| API | N | Gardé / try | Rôle | Chemin | Remplacement sur Node |
|---|---:|---|---|---|---|
| **`Bun.ant.CellSegmenter`** | 2 (+121 appels de l'enveloppe) | test qui **lève une erreur** | découpe en graphèmes et largeurs, écrite dans des `Int32Array` : **la grille de cellules du moteur de rendu du terminal** | **chaud, obligatoire** (mode interactif) | à réécrire : `Intl.Segmenter` + tables de largeur ; contrat **privé**, à déduire des appels |
| `Bun.sliceAnsi` | 6 | non | couper du texte avec séquences ANSI | chaud (rendu) | `slice-ansi` (MIT) adapté |
| `Bun.wrapAnsi` | 3 | non | retour à la ligne | chaud (rendu) | `wrap-ansi` (MIT) adapté |
| `Bun.stringWidth` | 3 | non | largeur à l'écran | chaud (rendu) | `string-width` (MIT) |
| `Bun.stripANSI` | 1 | non | | rendu | `strip-ansi` |
| `Bun.hash` (+ `.xxHash64`, `.crc32`) | 15 | non | empreintes (compétences, messages, gzip maison) | fréquent, obligatoire | wyhash 64 bits (valeur par défaut de Bun), xxHash64, `zlib.crc32` |
| `Bun.YAML` | 2 | non | en-têtes YAML des compétences et agents | démarrage | paquet `yaml` (ISC) |
| `Bun.TOML` | 1 | non | configuration | ponctuel | `smol-toml` (BSD-3) |
| `Bun.JSONL` | 1 | `?.` | lecture de JSONL par morceaux | repli prévu | absent → repli |
| `Bun.semver` | 2 | try | `order`, `satisfies` | démarrage | paquet `semver` (ISC) |
| `Bun.which` | 3 | 1 sur 3 | trouver une commande | obligatoire | parcours de `PATH` (≈ 30 lignes) |
| `Bun.spawn` (+ `Bun.file` en stdio) | 6 (+1) | 2 try | hôte pty des tâches de fond, travailleur des modules de crochets (`--smol`) | fonctions annexes | `Subprocess` sur `child_process` |
| `Bun.Terminal` | 2 | try | pty des tâches de fond | facultatif (état « crashed » prévu) | absent |
| `Bun.serve` | 4 | non | serveurs locaux sur 127.0.0.1, port 0 (relais, aperçus, intégrations) | fonctions annexes | `http.createServer` + conversion `Request`/`Response` |
| `Bun.listen`, `Bun.connect` | 2 + 2 | 1 try | relais TCP locaux | annexes | `net` |
| `Bun.stdin` | 3 | non | lire l'entrée (sous-commandes de crochets) | annexes | `Readable.toWeb(process.stdin)` |
| `Bun.deepEquals` | 2 | non | comparaison de réglages | ponctuel | `util.isDeepStrictEqual` (sémantique à vérifier) |
| `Bun.SHA256` | 1 | non | empreintes de blocs | ponctuel | `crypto` |
| `Bun.zstdDecompress(Sync)` | 2 | 1 try | lire les 137 fichiers `.zst` | quand servis | `zlib.zstdDecompress*` (Node ≥ 22.15) |
| `Bun.gzipSync` (`library: "libdeflate"`) | 1 | non | compression | ponctuel | `zlib.gzipSync` |
| `Bun.sleepSync` | 1 | try | attente active courte | ponctuel | `Atomics.wait` |
| `Bun.Transpiler` | 2 | non | TypeScript des crochets et greffons | annexes | `module.stripTypeScriptTypes` (Node 24) ou esbuild |
| `Bun.FetchSession` | 6 | oui | transport d'axios plus rapide | **repli prévu** | absent |
| `Bun.Image` | 4 | oui | décodage d'images | repli (erreur propre) | absent |
| `Bun.unsafe.setJITPolicy`, `Bun.gc`, `bun:jsc` (2 `import()`) | 3 + 2 + 2 | oui / try | réglages et statistiques de JSC | facultatif | absents |
| `Bun.version`, `Bun.main`, `Bun.isStandaloneExecutable` | 3 | oui | détection | — | `isStandaloneExecutable` **faux** → ripgrep du système |
| `Bun.WebView`, `Bun.generateHeapSnapshot`, `Bun.$` | 3 | — | dans des tests ou des messages | — | rien |
| `Bun.ant.loginTty`, `ttyName`, `getPeerUid`, `getPeerPid`, `memoryPressureLevel`, `setDumpable` | 7 | try | terminal de contrôle, pairs de sockets, mémoire | facultatif | absents (ou petits équivalents) |
| `Bun.ant.withThreadWorkingDirectory` | 2 | `?.` | lancer ripgrep **dans** un dossier ouvert | une erreur est levée dans ce mode s'il manque : **à suivre** | `chdir` temporaire ou `cwd` du `spawn` |
| `Bun.ant.posixFsAt` | 1 | message | fichiers relatifs à un dossier (macOS) | macOS | — |
| `Bun.ant.windows*` (4) | 5 | oui | Windows | — | — |

Ce qui n'est pas une API `Bun.*` mais en dépend :

| Mécanisme | N | Sur Node |
|---|---:|---|
| Spécificateurs absolus **`/$bunfs/root/…`** (imports statiques et dynamiques, `require`, chemins de fichiers lus par `fs`) | ≈ 290 000 | crochet de résolution (`module.register`) ou réécriture du préfixe à l'installation |
| **`import.meta.require(...)`** : `require` **synchrone de modules ESM** (`chunk-*.js`) | 737 | injecter `createRequire(import.meta.url)` ; `require(esm)` existe depuis Node 22.12, **refusé si le graphe a un `await` de premier niveau** (`ERR_REQUIRE_ASYNC_MODULE`) : à mesurer |
| `import.meta.dir` (Bun) / `import.meta.dirname` | 9 / 142 | `dir` à injecter ; `dirname` existe |
| `require` de fichiers **`.md`/`.txt`** (loader `text`) | 104 fichiers | extension de `require` qui rend une chaîne |
| `Worker` **global** (API Web) | 4 | Node n'en a pas : adaptateur sur `worker_threads` (un des cas, URL `blob:`, est dans un `try`) |
| `process.execPath` relancé avec des options de Bun (`--smol`, `--bg-pty-host`, `--bg-spare`, `--daemon-worker`, `argv0 "rg"`) | ≈ 15 | sous Node, `isStandaloneExecutable` est faux : chemins « non autonomes » ; à vérifier un par un |
| Modules natifs `.node` | 2 | facultatifs (chargés dans un `try`) ; Korin n'a pas `dlopen` |

---

## 4. Voie A : le bundle extrait sur le Node de Korin, avec une cale Bun

### 4.1 Principe

Sur le PC, à chaque version : **extraire** les sources de l'exécutable officiel ; sur Korin : `node` (le port de
`docs/NODEJS.md`) lance `cli` avec une **cale** (`node --import korin-bun.mjs …/cli`) qui (1) enregistre les crochets
de modules (`/$bunfs/root/` → dossier d'installation, `import.meta.require` et `import.meta.dir` injectés, chargeur de
texte), (2) définit `globalThis.Bun` avec les API du §3.3 sur celles de Node, (3) un `Worker` global.

### 4.2 Taille de la cale (estimation)

| Partie | Lignes de JavaScript |
|---|---:|
| Crochets de modules, chargeur de texte, `import.meta.*` | 150 – 250 |
| `Bun.ant.CellSegmenter` (graphèmes, largeurs, emoji, tabulations, codage des cellules, tampons qui grandissent) | 400 – 800 |
| `sliceAnsi`, `wrapAnsi`, `stringWidth`, `stripANSI` (sur des paquets MIT) | 300 – 500 |
| `hash` (wyhash, xxHash64, crc32), `SHA256`, `zstd`, `gzipSync`, `sleepSync`, `which`, `deepEquals` | 250 – 350 |
| `spawn`/`Subprocess`, `file`, `stdin` | 250 – 350 |
| `serve`, `listen`, `connect` | 300 – 400 |
| `YAML`, `TOML`, `semver`, `Transpiler` (enveloppes de paquets) | 50 – 100 + paquets |
| `Worker` global, `Bun.ant.*` facultatifs, détection | 100 – 150 |
| **Total** | **≈ 1 800 – 2 900**, plus 4 ou 5 paquets npm libres (`yaml`, `smol-toml`, `semver`, `string-width`/`wrap-ansi`/`slice-ansi`) |

S'y ajoute un **vérificateur d'inventaire** (≈ 200 lignes) qui refait le §3 sur chaque nouvelle version et échoue si
une API apparaît que la cale ne couvre pas.

### 4.3 Risques

| # | Risque | Poids | Parade |
|---|---|---|---|
| 1 | `CellSegmenter` est une API **privée** de la fourche interne de Bun : contrat déduit des appels, peut changer à chaque version | **fort** (cœur du rendu) | essais différentiels sur le PC contre l'exécutable officiel (mêmes écrans) ; le vérificateur d'inventaire signale tout changement d'appel |
| 2 | Écarts de comportement Bun / Node : `fetch` (options propres à Bun, mandataires, décompression), `Bun.serve` vs `http`, `spawn` (flux Web, `exited`), signaux, `Buffer` | moyen | essais du mode `-p` puis interactif sur le PC avant Korin |
| 3 | `require(esm)` et `await` de premier niveau | moyen | vérifier sur le PC au premier chargement ; sinon transformer à l'extraction |
| 4 | Syntaxe : `using` exige V8 ≥ 13.x | levé | **Node 24**, ou abaissement par esbuild à l'extraction (§8 décision 2) |
| 5 | Sémantique de JSC qu'attendrait le code (format des piles, `Error.captureStackTrace`, ordre des propriétés…) | faible | rien de vu ; les essais le diront |
| 6 | Démarrage : 2 900 modules ESM et 290 000 imports à résoudre, sans cache de bytecode | moyen (le Pi) | `NODE_COMPILE_CACHE`, réunir les morceaux en un fichier à l'extraction (esbuild) |
| 7 | Rythme des versions (2.1.113 → 2.1.322 en sept mois : plusieurs par semaine) | moyen | extraction automatique + vérificateur ; épingler une version qui passe les essais |
| 8 | Format de la section `.bun` lié à la version de Bun (enregistrements de 52 o, `Offsets`) | faible | l'extracteur vérifie la fin de fichier, les tailles et la cohérence, et refuse un format inconnu |

### 4.4 Mises à jour

Sur le PC : télécharger (par l'utilisateur, `npm pack @anthropic-ai/claude-code-linux-arm64@<version>` ou le paquet
de sa plateforme), lancer l'extracteur, le vérificateur d'inventaire, les essais de la cale sur Node du PC, puis
copier le dossier sur la carte. **N'importe quelle plateforme convient** si le JavaScript est le même partout :

* ce qui le laisse penser : la plateforme est lue à l'exécution (`os.platform()`, tables de toutes les plateformes),
  aucune constante de plateforme n'est figée dans le code, et les seuls fichiers propres à l'architecture sont les deux
  `.node` (ELF x86-64), le bytecode et la zone de 24 Mo — que nous n'employons pas ;
* **non vérifié ici** (aucune autre plateforme sur la machine, rien téléchargé) : comparer l'empreinte du §2.3 avec
  celle de l'extraction d'un `linux-arm64` de la même version. Si elles diffèrent, prendre `linux-arm64`, le plus
  proche de Korin.

### 4.5 Ce que la voie A réutilise

Tout le port de Node (`docs/NODEJS.md` §2 à §9 : V8 avec JIT, libuv, OpenSSL, ICU, Terminal, `fork`, ptys, bash, git,
`rg` en C) ; la cale ne demande **rien de plus au système** que Claude Code 2.1.112. Seul changement : **Node 24 au
lieu de 22** (ou une passe d'abaissement).

---

## 5. Voie B : porter Bun sous Korin

### 5.1 Le travail

| Morceau | Ce qu'il faut | Ordre de grandeur |
|---|---|---|
| **Langage du runtime** | Bun 1.4.4 est construit par rustc nightly (§1.3) : une **cible Rust `aarch64-unknown-korin`** (`libc` + `std`, famille `unix`, sur newlib + libposix) — la voie A de `docs/TERMINAL-ET-FORK.md` §5.3, **écartée pour ripgrep** comme « du même ordre que CoreCLR » ; Korin n'a ni compilateur Rust ni cible. (Si c'était encore du Zig : même travail avec la `std` de Zig) | gros |
| Couche système de Bun | boucle d'événements (epoll, eventfd, signalfd sous Linux ; kqueue ailleurs), `spawn` (`posix_spawn`, pidfd), surveillance de fichiers (inotify), ptys (`Bun.Terminal`), appels directs par `syscall`, `/proc` : **une troisième plateforme à écrire**, sans équivalent de `posix-poll` de libuv | gros |
| uSockets / uWebSockets | moteur d'événements epoll/kqueue/libuv : un moteur `poll` pour Korin | moyen |
| **JavaScriptCore avec JIT** | notre WebKit tourne en **C_LOOP, sans JIT** (`docs/WEBKIT.md` : LLInt, WebAssembly et réservations de plusieurs Go « restent à essayer ») ; Bun construit JSC avec LLInt, JIT et Gigacage, sur la fourche `oven-sh/WebKit` (pas la nôtre) | gros |
| Dépendances C/C++ | BoringSSL (pas l'OpenSSL de Korin), mimalloc, ICU 79, c-ares, zlib, libdeflate, brotli, zstd, highway, SQLite, TinyCC, lol-html (Rust), libarchive selon les options | moyen (une quinzaine, comme Node) |
| Chaîne | clang/LLD 23, rustc nightly, CMake ; sortie liée statiquement par la chaîne de Korin | moyen |
| **`Bun.ant.*`** | la fourche interne d'Anthropic n'est pas publique : un Bun public **n'a pas** `CellSegmenter` ; il faudrait l'écrire quand même (en Rust ou en JavaScript), comme dans la voie A | moyen |

Ensemble : **au moins le port de Node**, plus une cible Rust, plus JSC avec JIT ; **rien n'en est réutilisé** par
le reste du plan (Node reste utile en soi, Bun non).

### 5.2 La charge utile sur un Bun de Korin

* Le format de `.bun` est **lié à la version** de Bun (disposition des enregistrements, `Offsets`, drapeaux) ; la zone
  de 24 Mo et `.bun_aot` portent le commit `424a542a9…` ; le bytecode dépend de la construction exacte de JSC. Un Bun
  pour Korin construit depuis un autre commit, un autre compilateur ou une autre architecture **ne réutiliserait ni le
  bytecode ni cette zone** ; seul le format de la table resterait lisible à version égale.
* En pratique, il lancerait **les sources extraites** (`bun …/cli` avec le préfixe `/$bunfs/root/` remappé), ou les
  ré-empaquetterait par son propre `bun build --compile` : la même extraction que la voie A, sans la cale des
  API publiques de Bun, mais toujours avec `Bun.ant`.
* Et chaque version de Claude Code épingle **sa** version de la fourche interne : il faudrait suivre Bun (et ses
  API nouvelles : `FetchSession`, `Terminal`, `Image`, `JSONL`… toutes apparues entre 2.1.42 et 2.1.322) au même
  rythme.

---

## 6. Recommandation : la voie A

| Critère | A. Bundle + cale sur Node | B. Port de Bun |
|---|---|---|
| Travail propre | cale de 2 000 – 3 000 lignes + extracteur + vérificateur | cible Rust + couche système de Bun + JSC avec JIT + 15 dépendances + `Bun.ant` |
| Réutilise le plan Node | entièrement | non |
| Développable **dès maintenant** | **oui, sur le PC** (Node 24 Linux), avant que Node tourne sur Korin | non |
| Fidélité | dépend de la cale (surtout `CellSegmenter`) | meilleure pour l'API publique de Bun ; `Bun.ant` à réécrire de toute façon |
| Suivi des versions | ré-extraction + vérificateur ; la cale grandit avec les nouvelles API | suivre Bun **et** la fourche privée |
| Risque principal | une API privée changée ou ajoutée | l'ampleur |

**La voie A**, sur **Node 24**, avec la 2.1.112 en JavaScript gardée comme premier jalon de `docs/NODEJS.md`.

### 6.1 Ordre des étapes

| # | Étape | Où | Essai d'acceptation |
|---|---|---|---|
| 1 | **Extracteur** (Python, sans dépendance) : section `.bun` d'un ELF (et à terme Mach-O / PE), vérifications du §2.1, fichiers + manifeste (noms, genres, tailles, SHA-256) ; refus d'un format inconnu | PC | l'exécutable 2.1.322 donne 3 150 fichiers, l'empreinte du §2.3 ; un fichier tronqué est refusé |
| 2 | **Vérificateur d'inventaire** : refait le §3 (API `Bun.*`, `Bun.ant.*`, `import.meta.*`, analyse syntaxique par le Node visé) et compare à la liste couverte par la cale | PC | sur 2.1.322 : la liste du §3.3, 0 module refusé par Node 24 |
| 3 | **Crochets de modules** + `globalThis.Bun` minimal | PC, Node 24 | `node --import korin-bun.mjs cli --version` → `2.1.322 (Claude Code)` |
| 4 | **Mode non interactif** : `hash`, `YAML`, `TOML`, `semver`, `which`, `zstd`, `spawn`, `file`, `stdin`, `Worker` | PC | `claude -p "Réponds juste bonjour"` (clé d'API) → réponse ; outils Read, Edit, Bash, Grep (ripgrep du système) dans un `-p` |
| 5 | **Rendu** : `CellSegmenter`, `sliceAnsi`, `wrapAnsi`, `stringWidth` | PC | séance interactive ; **captures comparées** à l'exécutable officiel sur un même script (texte, emoji, CJK, tabulations, redimensionnement) |
| 6 | **Serveurs locaux** et fonctions annexes (`serve`, `listen`, `connect`, `Transpiler`) | PC | un serveur MCP local, un crochet en TypeScript |
| 7 | **Node 24 sur Korin** : les étapes de `docs/NODEJS.md` §9, en visant 24 (V8 13.6) | Korin | celles de `NODEJS.md` jusqu'à l'étape 6 |
| 8 | **Installation** : script `#!/bin/bash` `claude` → `node --import …/korin-bun.mjs …/cli`, `DISABLE_AUTOUPDATER=1`, `USE_BUILTIN_RIPGREP` réglé pour le `rg` de Korin | Korin | `claude --version` ; `claude -p` → réponse |
| 9 | **Interactif sur le PocketTerm** | Korin | l'essai de `NODEJS.md` étape 8 avec la version courante |
| 10 | **Vitesse** : morceaux réunis à l'extraction, `NODE_COMPILE_CACHE`, `preload` | Korin | démarrage chronométré, comparé à la 2.1.112 |

Les étapes 1 à 6 ne dépendent pas du port de Node : elles peuvent avancer en parallèle.

---

## 7. Notes pratiques et juridiques

* **Droits** : Claude Code est du logiciel propriétaire (« © Anthropic PBC. All rights reserved », usage soumis aux
  « Legal Agreements » de `code.claude.com/docs/en/legal-and-compliance`, selon le `LICENSE.md` du paquet npm et la
  ligne d'en-tête de chaque morceau). Bun est sous licence MIT ; sa fourche interne (`Bun.ant`) n'est pas publiée.
* **Ne rien redistribuer** : Korin ne livre que **ses propres outils** (extracteur, cale, vérificateur, script de
  lancement). Le code d'Anthropic est téléchargé par l'utilisateur depuis le paquet officiel et extrait **sur sa
  machine**, à l'installation ; jamais dans git, dans un paquet de Korin ni dans une image de carte.
* **Usage personnel** sur son propre appareil, avec son propre compte ou sa clé d'API.
* **À vérifier dans les conditions** (Commercial Terms ou Consumer Terms selon le compte, et la page « Legal and
  compliance » de Claude Code) : les clauses sur la rétro-ingénierie, la modification, l'extraction de composants et
  l'usage hors des plateformes prises en charge ; ce qu'elles disent de l'emploi d'un abonnement (et non d'une clé
  d'API) avec un client modifié. En droit européen, l'exception d'interopérabilité de la directive 2009/24/CE (art. 6)
  existe mais a des conditions strictes : **ceci n'est pas un avis juridique**. Dans le doute, demander à Anthropic.
* **Support** : un Claude Code sur un autre runtime n'est pas pris en charge ; les rapports `/bug` doivent le dire.
  La mise à jour automatique doit être coupée (elle remplacerait le lanceur par un binaire Linux).
* **Cale** : notre code, sous la licence de Korin ; les paquets npm qu'elle reprend sont MIT, ISC ou BSD.

---

## 8. Décisions pour le propriétaire

| # | Question | Choix possibles | Recommandation |
|---|---|---|---|
| 1 | Voie | **A** : bundle extrait + cale sur Node ; **B** : port de Bun (cible Rust, JSC avec JIT, couche système) | **A** |
| 2 | Node visé | **24** (V8 13.6 : tout le bundle s'analyse ; LTS jusqu'en 04/2028) ; 22 + **abaissement de `using`** par esbuild à l'extraction (un outil de plus à chaque version) | **24** : revient sur la décision 1 de `NODEJS.md` (qui recommandait 22) |
| 3 | Jalons | 2.1.112 en JavaScript d'abord (décision 0), puis la version courante par A ; ou directement A | 2.1.112 pour valider Node, A ensuite |
| 4 | Chargement des modules | crochets `module.register` (fichiers intacts) ; réécriture du préfixe `/$bunfs/root/` et réunion en un fichier à l'extraction (plus rapide, transforme le code) | crochets d'abord, réunion à l'étape 10 |
| 5 | Où extraire | sur le PC (Python, puis copie) ; sur la carte (télécharger 200 à 340 Mo sur le Pi) | le PC |
| 6 | Fidélité du rendu | accepter une réécriture de `CellSegmenter` validée par captures ; ou attendre une API publique | réécriture + essais différentiels |
| 7 | Rythme | suivre chaque version ; épingler une version validée et monter par paliers | épingler, monter par paliers après le vérificateur |
| 8 | Conditions d'Anthropic | lire les clauses du §7 avant de publier quoi que ce soit (même la cale, qui nomme des API internes) | les lire d'abord ; garder la cale privée tant que ce n'est pas clair |
