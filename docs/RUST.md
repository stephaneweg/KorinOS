# Rust pour Korin : la cible `aarch64-unknown-korin`

Étape 4 de [PYTHON.md](PYTHON.md) (§3 pour le choix de la cible, §9 pour l'ordre des étapes). Faite le 12/11/2026 :
**programmes, threads, fichiers, processus, réseau, paniques et `cdylib` chargée par `dlopen` marchent sous QEMU**.
**Pas encore essayé sur le Pi.**

C'est de la **compilation croisée** : `rustc` et `cargo` tournent sur le PC (WSL) et produisent des programmes et des
`.so` pour Korin, qu'on copie sur la carte. Aucun compilateur Rust ne tourne sur Korin ; pour Vibe, ce n'est pas
nécessaire (pydantic-core et les autres modules se compilent sur le PC et s'installent tout faits).

## Ce qui est livré

| Où | Quoi |
|---|---|
| `tools/rust/aarch64-unknown-korin.json` | le fichier de cible |
| `tools/rust/libc.patch` | la caisse `libc` 0.2.185 (celle de la `std` 1.97.1) : un module `src/unix/korin/mod.rs` (≈ 1 100 lignes) et trois branches dans `src/unix/mod.rs` |
| `tools/rust/std.patch` | la `std` : 14 fichiers, dont `std/src/os/korin/` (nouveau) ; ≈ 300 lignes de patch |
| `tools/rust/target-lexicon.patch`, `tools/rust/getrandom.patch` | (étape 5) deux caisses de crates.io qui apprennent le système `korin` (plus bas, « Caisses patchées ») |
| `tools/rust/build.sh` | tout, depuis zéro (WSL) : rustup, la chaîne, les patchs, le contrôle d'ABI, les essais |
| `tools/rust/abicheck.py` | compare le module `korin` de `libc` aux en-têtes C (902 contrôles) |
| `tools/rust/testimage.py` | la carte d'essai de QEMU, `build/rust/sd.img` (les essais dans `SD0:/home/rust`) |
| `tools/rust/tests/` | `korintest` (essais d'acceptation), `rdl` (une `cdylib`), `rdlhost.c` (son hôte C) |
| libposix | `__errno`, le dérouleur partagé avec les modules, `getsockname`, délais saturés (plus bas) |

`build.sh` est en fins de ligne LF depuis l'étape 5 (le dépôt garde les fichiers tels quels, `* -text` : la version
de l'étape 4 était en CRLF et `bash` sous WSL s'arrêtait à la première ligne, « set: -: invalid option »).

Rien de l'arbre de Rust n'est dans le dépôt : `build.sh` télécharge rustup et la chaîne dans `~/.cache/korin-rust`
(`KORIN_RUST` pour un autre dossier), copie la chaîne et applique les deux patchs à son `rust-src`.

## Se servir de la cible

```sh
bash tools/rust/build.sh                  # quelques minutes ; SKIP_TESTS=1 : sans les essais
. ~/.cache/korin-rust/env.sh              # RUSTUP_HOME, CARGO_HOME, RUSTC_BOOTSTRAP=1, PATH
cargo korin build --release               # dans un projet : target/aarch64-unknown-korin/release/
```

`cargo korin` (`tools/rust/build.sh:64-70`) est `cargo +korin <commande> -Zjson-target-spec -Zbuild-std=std,panic_unwind
--target tools/rust/aarch64-unknown-korin.json`. La `std` se reconstruit dans chaque `target/` (une minute la
première fois). Les caisses d'un projet prennent la même `libc` par un `[patch.crates-io]` que `build.sh` écrit dans
`$CARGO_HOME/config.toml` (`tools/rust/build.sh:72-78`), avec l'éditeur de liens. Le programme se copie sur la carte et
devient exécutable par `chmod +x` (le bit d'exécution de Korin, [POSIX.md](POSIX.md)).

## Les choix

### La chaîne : Rust 1.97.1 stable + `RUSTC_BOOTSTRAP=1`, pas une nightly

`-Zbuild-std` et une cible en JSON demandent une nightly, ou une stable avec `RUSTC_BOOTSTRAP=1`. La **1.97.1**
est celle du harness de Vibe (§1.3) : c'est déjà une version exacte, épinglée (la date d'une nightly ne dirait pas
plus), et son compilateur est celui que tout le monde emploie. La chaîne `korin` est une **copie** de la 1.97.1 liée
par `rustup toolchain link` (`tools/rust/build.sh:45-47`) : la 1.97.1 elle-même reste intacte, et son `rust-src`
sert de référence pour refaire les patchs.

### La `std` : patch de `rust-src` et `-Zbuild-std`, pas de fourche

La `std` se construit depuis le `rust-src` patché ; pas de fourche à suivre. Le patch reste petit parce que Korin
prend presque partout la **branche Unix générique** : `std.patch` ajoute `target_os = "korin"` à quelques listes et un
module `os::korin`. Une nouvelle version de Rust demande de rejouer les deux patchs (§6.6 de l'étude) ; les morceaux
sont des listes de `cfg`, qui se rejouent bien.

### Le fichier de cible

| Clé | Valeur | Pourquoi |
|---|---|---|
| `os`, `env`, `families` | `korin`, `newlib`, `unix` | comme `vita`, `horizon` et `espidf` (§3.3) |
| `llvm-target` | `aarch64-unknown-none-elf` | ELF, TLS natif ; rien de Linux |
| `cpu`, `features` | `cortex-a72`, `+v8a,+crc` | le Pi 4 ; pas d'`outline-atomics` (pas de LSE sur l'A72 : LL/SC en ligne) |
| `relocation-model` | `pic` | une `cdylib` doit être PIC, `std` comprise ; les programmes sont reliés sans PIE (`position-independent-executables: false`) à l'adresse de `posix.ld`, et ld détend les TLSDESC en « local exec » |
| `dynamic-linking` | `true` | permet `cdylib` (module de `dlopen`) |
| `has-thread-local` | `true` | `thread_local!` en TLS natif (`#[thread_local]`) |
| `linker` | `aarch64-korin-gcc` | `gcc -specs=build/posix/lib/posix.specs` (`tools/rust/build.sh:59-62`) : crt0, libposix, newlib, `posix.ld` ; en `-shared`, rien que `dlmod.o` et `libgcc_mod.a` |
| `no-default-libraries` | `false` | rustc ne passe pas `-nodefaultlibs` : c'est la spec qui décide des bibliothèques |
| `panic-strategy` | `unwind` | le dérouleur de libgcc ; `catch_unwind` marche |
| `relro-level` | `none` | `posix.ld` n'a pas de segment RELRO (`off` passerait `-z norelro`, que ld refuse) |

### La caisse `libc`

Un module **à part**, `src/unix/korin/mod.rs`, choisi avant le tronc `newlib` (`libc.patch`, branches de
`src/unix/mod.rs`). Le tronc `newlib` de la caisse décrit des consoles 32 bits (`time_t` de 32 bits, `sockaddr` de BSD
avec `sa_len`, `pthread_*` de la Vita) ; Korin a newlib 64 bits, des sockets, `termios`, `ioctl`, `mmap` et `epoll`
aux valeurs de Linux (en-têtes de libposix), et les erreurs, drapeaux d'`open`, `stat` et signaux de newlib. Le
module ne porte **aucun `#[link]`** : la spec ajoute libposix, newlib et libm aux programmes, et rien aux modules, qui
prennent la libc de l'hôte (`tools/rust/libc.patch:1436`, la branche `target_os = "korin"` des `#[link]` ; le choix du module : `tools/rust/libc.patch:1446`).

`tools/rust/abicheck.py` relit le module (types, constantes, structures), écrit un C qui donne par `gcc -S` les
tailles, alignements, signes, valeurs et décalages, puis des `const _: () = assert!(...)` que `cargo check` compile
pour la cible : **902 contrôles**, tous justes. Il a trouvé trois signaux faux (`SIGWINCH` 28, `SIGUSR1` 30, `SIGUSR2`
31 : newlib d'aarch64 prend son second bloc de numéros) et quatre signaux que la `std` croyait absents (`SIGXCPU`,
`SIGXFSZ`, `SIGVTALRM`, `SIGPROF`). Un nom absent des en-têtes C fait échouer le contrôle : onze constantes que
newlib cache derrière `__LINUX_ERRNO_EXTENSIONS__` (`ENOTBLK`, `EBADFD`...) ou qu'il n'a pas (`O_PATH`, `O_TMPFILE`...)
ont été retirées plutôt qu'inventées. `build.sh` vérifie aussi que chaque fonction déclarée existe dans libposix ou
newlib (`tools/rust/build.sh:94-100`).

`errno` : newlib en TLS natif n'a que la variable `_tls_errno`, qu'un autre langage ne lit pas ; libposix a reçu
`__errno ()` (`src/lib/posix/misc.c:671-675`), que la caisse déclare (`tools/rust/libc.patch:1096`) et que la `std` appelle
comme sur les autres newlib.

### Les branches de la `std` (`std.patch`)

| Domaine | Korin prend | Où |
|---|---|---|
| liste des systèmes connus | ajout (sinon `restricted_std`) | `std/build.rs` (`tools/rust/std.patch:11`) |
| `std::os::korin` | `fs::MetadataExt` (dates à la nanoseconde), `raw` | `std/src/os/korin/` (`tools/rust/std.patch:23-175`) |
| `uid_t`, `gid_t` | 16 bits, comme `vita` | `std/src/os/unix/process.rs` (`tools/rust/std.patch:202`) |
| arguments | gardés par `init` (crt0 appelle `main (argc, argv)`) | `std/src/sys/args/unix.rs` (`tools/rust/std.patch:215`) |
| `consts::OS` | `"korin"`, `.so`, pas de suffixe d'exécutable | `std/src/sys/env_consts.rs` (`tools/rust/std.patch:227`) |
| `current_exe` | `readlink ("/proc/self/exe")` de libposix, comme Linux | `std/src/sys/paths/unix.rs` (`tools/rust/std.patch:270`) |
| aléa | `getrandom` (libposix : `SYS_GETRANDOM`) | `std/src/sys/random/mod.rs` (`tools/rust/std.patch:284`) |
| `available_parallelism` | `sysconf (_SC_NPROCESSORS_ONLN)` | `std/src/sys/thread/unix.rs` (`tools/rust/std.patch:297`) |
| `DirEntry::ino` | `d_ino` | `std/src/sys/fs/unix.rs` (`tools/rust/std.patch:249`) |
| `remove_dir_all` | `openat`, `unlinkat`, `fdopendir`, mais le dossier **lu en entier avant d'effacer** (comme WASI) : RamFS saute une entrée sur deux quand on efface pendant `readdir` (plus bas) | `std/src/sys/fs/unix.rs` (`tools/rust/std.patch:249`) |
| verrous, `Condvar`, parking | `pthread_mutex` / `pthread_cond` (branche générique ; libposix les fait sur ses futex) | sans changement |
| processus | `fork` + `exec` (branche générique ; ni Horizon ni ESP-IDF ne l'ont) | sans changement |
| fichiers, sockets, temps, signaux | branches Unix génériques | sans changement |
| `thread_local!` | TLS natif ; destructeurs par `pthread_key` (pas de `__cxa_thread_atexit_impl`) | sans changement |
| pile débordée | pas de gestionnaire (branche des systèmes inconnus) | sans changement |

Une seule constante de `libc` vient s'ajouter en C pour la `std` : les options IPv6 (`IPV6_ADD_MEMBERSHIP`...),
que `std::net` nomme sans `cfg` ; libposix les a reçues aux valeurs de Linux
(`src/lib/posix/include/netinet/in.h:100`), sans socket `AF_INET6` (toujours `EAFNOSUPPORT`).

### Caisses patchées (étape 5, 12/11/2026)

Des caisses de crates.io décident de leur code par `target_os` ou en lisant `$TARGET`, et refusent un système
qu'elles ne connaissent pas. `build.sh` télécharge les versions suivantes (sha256 vérifié), leur applique un patch de
`tools/rust` dans `$KORIN_RUST/vendor`, et les ajoute au `[patch.crates-io]` de `$CARGO_HOME/config.toml`, à côté de
`libc` :

| Caisse | Version | Qui s'en sert | Sans le patch | Le patch |
|---|---|---|---|---|
| `target-lexicon` | 0.13.5 | `pyo3-build-config` (toutes les caisses pyo3), d'autres scripts de construction | `TARGET=aarch64-unknown-korin` : « Unrecognized TARGET environment variable value », le script panique | `OperatingSystem::Korin` (`"korin"`) ; l'énumération est `non_exhaustive` |
| `getrandom` | 0.3.3 | `ahash` (pydantic-core), `rand` 0.9 | `compile_error!` « target is not supported » | `korin` dans la branche du backend `getrandom ()` (celui de libposix, `SYS_GETRANDOM`), `errno` par `libc::__errno`, la dépendance `libc` pour `korin` |

Un `[patch]` ne vaut que pour la version exacte : un projet dont le `Cargo.lock` en a une autre de la même série
(`libc` 0.2.189, `target-lexicon` 0.13.3, `getrandom` 0.3.4...) doit y être ramené par `cargo update -p <caisse>@<v>
--precise <v patchée>` (cargo le signale sinon : « patch was not used »). `tools/python/build-rust-ext.sh` le fait
avant chaque construction. Une caisse qui exigerait une version plus récente demanderait de rejouer le patch sur
celle-ci. `getrandom` 0.2 (une autre série) n'est pas patché : aucune caisse de Vibe ne l'a demandé jusqu'ici.

## Les `cdylib` : un module Rust chargé par `dlopen`

Ce que fera un module pyo3 dans `python3` (hôte `-rdynamic`, [POSIX.md](POSIX.md) « dlopen ») : `crate-type =
["cdylib"]`, `cargo korin build`, un `.so` que l'hôte charge. La `std` est dans le module (≈ 370 Ko dépouillé pour
`rdl`) ; la libc, l'allocateur, `errno`, les threads et le dérouleur sont ceux de l'hôte : le module n'a que des
références non définies (51 pour `rdl`), toutes dans les exports de l'hôte (`dlhost.ld`).

Deux corrections de libposix ont été nécessaires, valables aussi pour les modules C et C++ :

* **Un seul dérouleur par processus.** Le dérouleur de libgcc (`unwind-dw2.o`, `unwind-dw2-fde.o`) était recopié
  dans chaque module par `-lgcc`, mais `dlopen` inscrit les `.eh_frame` des modules **chez l'hôte** : la panique d'une
  `cdylib` partait dans un dérouleur privé qui ne connaissait aucune table, et le process finissait en `abort`.
  Les fonctions du dérouleur sont `GLOBAL HIDDEN` dans libgcc.a : l'hôte ne peut pas les exporter telles quelles.
  libposix exporte donc des relais `__posix_unw_X` (un saut vers `X`, aucun cadre de plus :
  `src/lib/posix/dlunwind.S`), et les modules reçoivent `libgcc_mod.a` (`tools/posix/build.sh:76-84`, spec
  `*libgcc: %{shared:-lgcc_mod;:-lgcc}`, `tools/posix/build.sh:101-102`) : libgcc sans le dérouleur, avec
  `dlunwmod.o`, des `_Unwind_*` cachés qui sautent vers ces relais (`src/lib/posix/dlunwmod.S`). Le module C++
  d'essai de `posixtest` passe ainsi de 32 à 9 Ko.
* **Le mot nul qui termine `.eh_frame`** (`dlmod.o`) était retiré par `--gc-sections`, que rustc passe toujours :
  le dérouleur lisait au-delà de la fin (faute dans `strlen` sur une augmentation de CIE). Sa section est maintenant
  marquée `R` (`SHF_GNU_RETAIN`, `src/lib/posix/dlmod.S:22`).

TLS : les `thread_local!` du module (≈ 0x90 octets pour la `std`) sont dans la réserve de 32 Ko de l'hôte, résolus
en TLSDESC ; `std::thread::spawn` dans le module passe par `pthread_create` de l'hôte.

## Essais (QEMU, 12/11/2026)

Construction : `bash tools/rust/build.sh` (vérifiée depuis un `KORIN_RUST` vide), `.\build.ps1`, puis
`python -I tools/rust/testimage.py` ; QEMU par `tools/qemu_drive.py --fwd 2370:23 --sd build/rust/sd.img` ;
commandes par telnet (`ARMPI_REMOTE_PW=essai python -I tools/armpi_remote.py telnet 127.0.0.1:2370 "@600
commande"`), dans `SD0:/home/rust` après `chmod +x`.

### Essais d'acceptation : tous passés

| Essai | Résultat |
|---|---|
| `korintest hello` | `hello from Rust on korin (aarch64)`, `HashMap` (aléa), `stderr` |
| `korintest env` | `args`, `set_var`/`var`/`remove_var`, `current_dir` (`SD0:/home/rust`), `current_exe`, `temp_dir` (`/tmp`), 4 cœurs |
| `korintest time` | `Instant`, `sleep` 50 ms, `SystemTime` |
| `korintest threads` | 8 threads nommés et `Arc<Mutex>` (80 000), `thread_local!` par thread et ses destructeurs, `thread::scope`, pile de 8 Mo, `park`/`unpark` |
| `korintest sync` | `mpsc` (4 producteurs), `sync_channel(0)`, `Condvar` et `wait_timeout`, `RwLock`, `Barrier`, `OnceLock` |
| `korintest fs` (`RAM0:/tmp`, `SD0:/home/rust`, `SD1:/`) | `create_dir_all`, écrire, lire, ajouter, `seek`, `set_len`, `metadata` (taille, type, date), `read_dir`, `rename`, `copy`, `remove_file`, erreurs `NotFound`/`AlreadyExists`, `canonicalize`, `remove_dir_all` |
| `korintest cmd` | `Command::new("ls").arg("/").output()` (101 octets), code de sortie 3, fils avec `stdin`/`stdout` en tubes, `env` et `current_dir`, programme absent → `NotFound` |
| `korintest tcp` | `TcpListener` sur `127.0.0.1:0`, `TcpStream`, écho de 100 000 octets ; connexion refusée → `ConnectionRefused` ; `read_timeout` |
| `korintest udp` | aller-retour sur 127.0.0.1 |
| `korintest panic` | `panic!` rattrapé par `catch_unwind` (destructeurs pendant le déroulement), panique dans un thread → `join` rend `Err`, index hors bornes, `expect`, `Mutex` empoisonné |
| `rdlhost librdl.so` (la `cdylib`) | `dlopen`, `dlsym` ; constructeur du module ; `thread_local!` du module sur le thread principal et sur 4 pthreads de l'hôte (un compteur chacun) ; destructeurs de TLS à la fin des 4 threads ; `Mutex` du module ; **panique levée et rattrapée dans le module** ; `println!` du module ; chaîne allouée par le module et libérée par lui ; `std::thread::spawn` dans le module ; `dladdr` |

### Essais de la `std` lancés sur la carte

Les essais d'intégration de la `std` (`library/std/tests` du `rust-src`, compilés par `tools/rust/build.sh:115-138`),
`RUST_TEST_THREADS=4` :

| Essai | Réussis | Échecs | Ignorés |
|---|---|---|---|
| `sync` (mpsc, mpmc, `Mutex`, `RwLock`, `Condvar`, `Once`, `LazyLock`, `Barrier`...) | **337** | 0 | 1 (instable par nature, ignoré en amont) |
| `path` | 35 | 0 | 0 |
| `num` | 25 | 0 | 0 |
| `time` | 15 | 0 | 0 |
| `error` | 14 | 0 | 0 |
| `thread_local` | 12 | 0 | 0 |
| `env` | 7 | **1** | 0 |
| `env_modify` | 7 | 0 | 1 (ignoré en amont) |
| `istr`, `thread` | 5, 5 | 0 | 0 |
| `create_dir_all_bare`, `panic`, `process_spawning`, `pipe_subprocess` | 1 chacun | 0 | 0 |

**466 réussis, 1 échec.** Ce qui a échoué en route, et pourquoi :

* **`env::test_self_exe_path`** (reste) : `current_exe` rend `SD0:/home/rust/std/env`, que `Path::is_absolute`
  de Rust ne tient pas pour absolu (il veut un `/` en tête). C'est la forme des chemins de Korin
  (`getcwd` rend aussi `SD0:/...`), le même écart que pour CPython ([PYTHON-PORT.md](PYTHON-PORT.md),
  `test_subprocess`) : l'arbre POSIX des chemins (§2.6 de l'étude) le réglera pour tous.
* `sync` : 4 essais de `Condvar` avec un délai immense (`Duration::from_millis(u64::MAX)`) rendaient « délai
  écoulé » tout de suite : la `std` sature l'échéance à `tv_sec = i64::MAX` et `__posix_ms_until` débordait en la
  convertissant en nanosecondes. Corrigé dans libposix (`src/lib/posix/pthread_sync.c:67`), et de même `nanosleep`
  (`src/lib/posix/time.c:253`).
* `korintest udp` : `UdpSocket::local_addr` rendait `0.0.0.0`, et pour TCP l'adresse de l'interface (`10.0.2.15`),
  même lié à `127.0.0.1` : netd ne lie que des ports, et `getsockname` ignorait l'adresse donnée à `bind`. libposix
  la garde maintenant (`src/lib/posix/socket.c:216`, rendue `src/lib/posix/socket.c:756`), la passe aux sockets
  d'`accept` (`src/lib/posix/socket.c:284`) et rend 127.0.0.1 pour un socket relié à 127.x.
* `korintest fs` sur `RAM0:` : `remove_dir_all` échouait (`ENOTEMPTY`) : RamFS saute une entrée sur deux quand on
  efface pendant `readdir` (essai en C : 8 fichiers, 4 vus ; exFAT les voit tous). Contourné dans la `std` (lecture
  complète d'abord) ; RamFS lui-même reste à corriger (suite plus bas).

**Pas lancés** : les essais unitaires de la `std` (`std/src/**/tests.rs` : `fs`, `net`, `process`...). Ils se
compilent dans l'arbre `library` contre une `std` du sysroot construite par le même compilateur que celle qu'on
essaie (l'amorçage de `x.py`) ; avec une chaîne publiée, les deux `core` diffèrent (`duplicate lang item`).
`korintest` couvre ces domaines à la place.

Non-régression de libposix (QEMU, même jour) : `posixtest` dl 26, net 24 (1 sauté, comme avant), thread 19, time 7 ;
`posixtest-cxx` 82.

## Limites connues

* **Pas essayé sur le Pi.** Sous QEMU seulement.
* `current_exe` et `current_dir` rendent la forme `SD0:/...` (plus haut).
* L'autre bout d'un `accept` reste `0.0.0.0:0` (NetKit ne le donne pas, [POSIX.md](POSIX.md)).
* Pas d'IPv6, pas de socket `AF_UNIX` nommé (`std::os::unix::net::UnixListener::bind` échoue : `EAFNOSUPPORT`).
* Le patch de `libc` vise la 0.2.185 : une caisse qui exigerait une `libc` plus récente ne prendrait pas le patch
  (cargo le dit : « patch was not used ») et ne compilerait pas pour Korin. Remède : rejouer `libc.patch` sur cette
  version.
* Pas de gestionnaire de débordement de pile (un débordement est une faute, pas le message de la `std`).
* `std::fs::File::lock` : non pris en charge (branche des systèmes inconnus) ; `flock` de libposix est sans effet.

## Suite

* **Étape 5** de [PYTHON.md](PYTHON.md) : **faite le 12/11/2026** (QEMU) : `pydantic-core`, `rpds-py` et `cryptography`,
  chacun sa `cdylib` (plus de caisse parapluie depuis `dlopen`), `PYO3_CONFIG_FILE` pour CPython 3.14 ;
  [PYTHON-PORT.md](PYTHON-PORT.md), « Étape 5 ».
* RamFS : `readdir` pendant les effacements (une entrée sur deux sautée).
