# cargo dans Korin PC

Premier pas du port de Rust **dans** Korin (et non plus seulement vers Korin, [RUST.md](RUST.md)) : **cargo 0.98.0**,
le gestionnaire de projets de Rust 1.97.1, construit pour `x86_64-unknown-korin` et lancé dans Korin. Fait le
21/11/2026 dans une session du cloud ; **QEMU q35 seulement**, pas encore essayé sur le ZBook.

cargo tourne, lit et écrit les projets, résout les dépendances, télécharge les caisses en HTTP et en **HTTPS** (le vrai
crates.io, index « sparse »), clone les dépendances git en HTTP, crée des projets (avec leur dépôt git). Ce qui
compile du Rust attend **rustc dans Korin**, qui n'y est pas encore (plus bas, « La suite »).

## Construire

```sh
ARCH=x86_64 bash tools/rust/build.sh      # la chaîne Rust du PC (docs/RUST.md), si elle n'est pas là
bash tools/cargo/build.sh                 # -> build/pc/cargo/cargo (34 Mo, statique, strippé) ; ≈ 3 min sur 4 cœurs
sh tools/pc/sysroot.sh && sh tools/pc/sysroot_cargo.sh   # -> /devtools/rust/bin/cargo de l'arbre du PC
```

`ARCH=aarch64 bash tools/cargo/build.sh` vise le Pi (`build/cargo/cargo`) : pas essayé.

| Où | Quoi |
|---|---|
| `tools/cargo/build.sh` | la construction : la source de cargo prise sur crates.io (`cargo-0.98.0.crate`, sha256 vérifié, avec son `Cargo.lock`), les caisses patchées, puis `cargo korin build --release --bin cargo --features curl/static-curl,vendored-libgit2` ; travail dans `~/korin-cargo` (`KORIN_CARGO`), `target/` gardé |
| `tools/cargo/patches/` | les caisses de crates.io retouchées pour Korin (plus bas) ; `build.sh` prend aussi celles de `tools/craft/patches` (communes avec [CRAFT.md](CRAFT.md)) |
| `tools/rust/libc-projects.sh` | la `libc` 0.2.191 des projets (`libc.patch` rejoué), sortie de `tools/craft/build.sh` pour servir aux deux |
| `tools/pc/sysroot_cargo.sh` | pose cargo dans `/devtools/rust/bin` (où ira rustc) et `/etc/ca-bundle` s'il manque |
| le `PATH` (21/11/2026) | `/devtools/rust/bin` ajouté au `PATH` par défaut (`/bin:/sbin:/devtools/gcc/bin:/devtools/rust/bin` : libposix `env.c` et `confstr`, `sh`, `sudo`), au bashrc du système et aux dossiers exécutables (`/etc/exec.txt`, défaut de vfs) : `cargo` se lance par son nom. Le `sh` et le vfs en assembleur du Pi ne changent pas (pas de Rust sur le Pi) |
| `tools/cargo/test/` | `registry.py` (un registre « sparse » local), `projects/` (les projets d'essai) |

### Les bibliothèques en C

| Bibliothèque | D'où | Pour |
|---|---|---|
| OpenSSL 3.5.9 | celle du système (`build/pc/thirdparty/openssl`, `ARCH=x86_64 sh tools/openssl/build.sh`, lancé s'il manque), en statique (`OPENSSL_DIR`, `OPENSSL_STATIC`) | https, libgit2, libssh2 |
| libcurl 8.20 + nghttp2 | compilées par `curl-sys` (`curl/static-curl`) et `libnghttp2-sys` : **HTTP/2**, comme le cargo de tout le monde (le libcurl de git, 8.18, n'a que HTTP/1.1) | crates.io |
| libgit2 1.9.2, libssh2 1.11 | compilées par `libgit2-sys` (`vendored-libgit2`) et `libssh2-sys` | dépendances git, `cargo new` |
| zlib, SQLite | `libz-sys`, `libsqlite3-sys` (`bundled`, comme cargo l'exige) | archives, cache des registres |

Compilées par le `gcc` de la chaîne avec `posix.specs` (`CC_x86_64_unknown_korin`) ; aucune n'a demandé de retouche
de son C.

### Caisses patchées

| Caisse | Pourquoi | Le patch |
|---|---|---|
| `curl-sys` 0.4.88 | son `build.rs` compile curl pour un Unix « Linux » : IPv6, `socketpair`, sockets `AF_UNIX` | pour `*-korin` : IPv4 seulement, le réveil du multi (`curl_multi_wakeup`) par un tube (`HAVE_PIPE`, comme `tools/webkit/ports/curl`), ni `socketpair` ni `USE_UNIX_SOCKETS`, autorités dans `/etc/ca-bundle` (`CURL_CA_BUNDLE`, comme le libcurl de git) |
| `socket2` 0.6.3 (de la caisse `curl`) | des options que Korin n'a pas | Korin ajouté aux listes d'exclusion : sources multicast (`ip_mreq_source`), `ip_mreqn`, `IP_RECVTOS`, `IPV6_RECVTCLASS`, `SOCK_RDM` ; `msg_iovlen` en `size_t` comme Linux |
| `gix-sec` 0.14.0 | `uid_t` de 16 bits (newlib) | `uid.into()` |
| `gix-prompt` 0.15.0 | il coupe l'écho du terminal par `rustix::termios`, que rustix n'a pas pour Korin | une version pour Korin par `libc::tcgetattr`/`tcsetattr` (termios de libposix) ; `deny(unsafe_code)` au lieu de `forbid` |
| `terminal_size` 0.4.4 | `rustix::termios` aussi (largeur du terminal) | `isatty` et l'ioctl `TIOCGWINSZ` de la libc |

Et celles de Craft que cargo prend : `rustix` 1.1.6, `tempfile` 3.27.0, `filetime` 0.2.29, `getrandom` 0.2.17 et
0.4.3 ; `getrandom` 0.3.4 et `libc` (0.2.185 pour la `std`, 0.2.191 pour le reste) de [RUST.md](RUST.md). Un
`Cargo.lock` d'une autre version de la même série y est ramené par `cargo update --precise` ; `build.sh` s'arrête si
un patch n'a pas été pris.

Une version de caisse peut contenir un tiret (`0.4.88+curl-8.20.0`) : le nom est ce qui précède le premier
« `-chiffre` ».

## Essais (QEMU q35, 4 cœurs, 21/11/2026)

```sh
python3 -I tools/cargo/test/registry.py fetch build/pc/cargo/registry
python3 -I tools/cargo/test/registry.py serve build/pc/cargo/registry 47390 &      # vu de QEMU : 10.0.2.2:47390
python3 -I tools/git/serve.py build/pc/cargo/git 47391 &                           # un dépôt nu tiny.git
python3 -I tools/rust/testimage.py --pc                                            # cargo et projects/ dans HOME:/cargo
python3 -I tools/rust/testimage.py --pc-run "cd HOME:/cargo && chmod +x cargo" "@300 HOME:/cargo/cargo ..."
```

`KORIN_TEST_CA=fichier` ajoute des autorités au `/etc/ca-bundle` du disque d'essai seulement : dans le cloud, le proxy
présente son propre certificat (`/root/.ccr/ca-bundle.crt`).

| Essai | Résultat |
|---|---|
| `cargo --version`, `cargo -vV` | `cargo 1.97.0`, hôte `x86_64-unknown-korin`, libgit2 1.9.2, libcurl 8.20.0 (OpenSSL 3.5.9) |
| `generate-lockfile` sur le registre local (4 caisses) | le `Cargo.lock` avec les sommes de l'index |
| `vendor --respect-source-config` | 4 caisses téléchargées en HTTP, vérifiées, dépaquetées dans `vendor/` |
| `metadata --no-deps`, `locate-project`, `pkgid`, `package --list` | justes |
| dépendance git (`gitproj`, `tiny.git` en HTTP « intelligent ») | `generate-lockfile` (clone par libgit2 dans `/home/.cargo/git/db`), `vendor` |
| `new hello` (dépôt git créé par libgit2), `new --lib --vcs none` | les fichiers du projet, `.git`, `.gitignore` |
| `search serde` (HTTPS, le vrai crates.io) | 2 résultats sur 26 071 |
| `generate-lockfile` puis `vendor` d'un projet `heck`, `itoa` sur le vrai crates.io | `itoa` 1.0.19 et `heck` 0.5.0 téléchargés et dépaquetés |

`CARGO_HOME` est `/home/.cargo` (`$HOME` = `/home`).

### Ce qui demande rustc

cargo lance `rustc -vV` (version, cible) avant tout ce qui dépend de la cible ou de la version de Rust ; sans rustc :
« could not execute process `rustc -vV` (never executed) ». Ainsi : `build`, `check`, `run`, `test`, `fetch`, `tree`,
`metadata` avec dépendances, `add`, `info`, et **tout projet en édition 2024** (le résolveur v3 tient compte de
`rust-version`, donc de rustc), même pour `generate-lockfile`. Les projets en édition 2021 se résolvent sans rustc.

## Limites connues

* QEMU seulement, pas encore sur le ZBook.
* Pas de paquet (`pkg`) pour l'instant.
* Pas de liens symboliques dans Korin : une caisse de crates.io qui en contient ne se dépaquette pas.
* Verrous de fichiers sans effet (`flock` de libposix) : un seul cargo à la fois sur le même `CARGO_HOME`.
* Les dates d'ext4fs et de FAT32 sont à la seconde (2 s sur FAT32) : comptera pour la recompilation quand rustc
  sera là.

## La suite

L'ordre décidé le 20/11/2026 (Korin PC) : cargo croisé (ce document), puis la cible intégrée au compilateur
(**faite le 21/11**, [RUST.md](RUST.md) « La cible dans le compilateur »), LLVM pour Korin, `librustc_driver.so` (bibliothèque Rust
`dylib`, jamais essayée sur Korin), les macros procédurales (`.so` chargées par `dlopen` dans rustc), et le paquet
`/devtools/rust`.
