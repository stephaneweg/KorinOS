---
name: korin-rust
description: Écrire, compiler dans l'OS (cargo, rustc de /devtools/rust) et installer un programme ou une app en Rust sous Korin OS (PC) - la liaison avec UIKit en FFI (libuikit.a, struct ui_vars, jetons du thème), build.rs, cargo test, l'installation dans /apps. À utiliser pour tout programme Rust sur Korin.
user-invocable: true
---

# Rust sous Korin OS

**rustc 1.97.1 et cargo** tournent dans Korin PC, dans **`/devtools/rust`** (s'il manque : `pkg add rust`). La cible
est `x86_64-unknown-korin` (`target_os = "korin"`, `target_family = "unix"`, `target_env = "newlib"`, `panic =
"unwind"`) : `std` entière (fichiers, threads, `Instant`, `SystemTime`, `cargo test`), crates.io par le réseau,
`build.rs` et macros procédurales. Le Pi n'a pas de bibliothèque standard installée (`aarch64-unknown-korin`) : du
Rust pour le Pi ne se construit pas dans l'OS.

**`pibuild` ne connaît pas Rust** : c'est **cargo** qui construit, et un petit script installe (le modèle ci-dessous).

## Avant de commencer

```bash
export PATH=$PATH:/devtools/rust/bin       # s'il n'y est pas déjà (command -v cargo)
rustc --version && cargo --version
```

rustc lie par **gcc** (`/devtools/gcc`, `pkg add gcc` s'il manque), qui ajoute libposix tout seul.

- `error: linking with 'gcc' failed ... collect2: fatal error: cannot find 'ld'` ou `gcc: cannot execute 'as'` :
  `as` et `ld` de `/devtools/gcc/x86_64-onyx-elf/bin` ne sont pas exécutables. Sous Korin, n'est exécutable que ce
  qui est sous un dossier de **`/etc/exec.txt`** (ou marqué par `chmod +x`) ; le système pc publié jusqu'au
  21/11/2026 n'y avait que le dossier du Pi. **Le dire à l'utilisateur** et lui proposer, avec son accord :
  `chmod +x /devtools/gcc/x86_64-onyx-elf/bin/as /devtools/gcc/x86_64-onyx-elf/bin/ld` (ou `pkg update`).
  Contournement sans toucher au système : une copie exécutable dans `/tmp`, donnée à gcc par `-B` :
  ```bash
  mkdir -p /tmp/tc && cp /devtools/gcc/x86_64-onyx-elf/bin/as /devtools/gcc/x86_64-onyx-elf/bin/ld /tmp/tc/
  chmod +x /tmp/tc/as /tmp/tc/ld
  export RUSTFLAGS="-C link-arg=-B/tmp/tc/"
  ```

## Un programme de terminal

```bash
cargo new ~/projects/hello && cd ~/projects/hello
cargo build --release && ./target/release/hello
```

Pour l'installer comme commande : `cp target/release/hello /bin/` (seulement si l'utilisateur le demande ; `/bin` est
au système).

## Une app (une fenêtre)

Partir du **modèle** de cette compétence (`template/` à côté de ce fichier) : un compteur, testé sous Korin PC.

```bash
mkdir -p ~/projects && cp -r <dossier de cette compétence>/template ~/projects/<nom>
cd ~/projects/<nom>
# Cargo.toml : name = "<nom>" (anglais, minuscules) ; app.txt : name = le titre affiché, category, description
bash build.sh         # cargo build --release, puis /apps/<nom>.app/main et app.txt
bash build.sh test    # cargo test --release
```

| Fichier | Rôle |
|---|---|
| `Cargo.toml` | le paquet ; `name` donne le nom de l'app (`/apps/<name>.app`) |
| `build.rs` | la liaison : `-L /devtools/gcc/lib -l static=uikit` |
| `src/uikit.rs` | ce qu'il faut d'UIKit : `extern "C"`, `UiVars`, `open`, `ev`, `token`, `text`, `window` |
| `src/main.rs` | la fenêtre, le dessin, la boucle d'évènements |
| `app.txt` | le manifeste de l'app (comme en C : compétence `korin-app`) |
| `data/` | facultatif : copié dans `/apps/<nom>.app/` (`lang-en.txt`, `help_fr.md`…) |

Lancer l'app **sans bloquer le shell** : `(/apps/<nom>.app/main >/tmp/<nom>.log 2>&1 &)` ; la voir :
`sysstat`, `screenshot /tmp/ecran.bmp`.

## UIKit depuis Rust : les règles

L'API est celle du C (compétence `korin-app` et le guide `uikit_en.md` dans `korin/reference`) ; les prototypes
exacts sont dans **`/devtools/gcc/include/kits/uikit_abi.h`**, les constantes (`WEV_*`, `EV_*`, `TJ_*`, `KEY_*`,
`WF_*`) aussi.

- **Tout est `long`** : chaque fonction `ui_*` se déclare `fn ui_x(a: i64, ...) -> i64` avec le nombre d'arguments
  de son prototype. Une chaîne : l'adresse d'un `CString` vivant pendant l'appel (`c.as_ptr() as i64`).
- **`struct ui_vars`** se reprend champ pour champ en `#[repr(C)]` : `win: i64`, `surf: *mut u32`, `w: i64`,
  `h: i64`, `ev: [u8; 32]`, puis `font`, `font_b`, `font_m` de 320 octets chacun (`font_m` : la chasse fixe). Une
  seule, en `static mut`, passée à `ui_init`.
- **Les fonctions `static inline` de uikit.h** n'existent pas dans la bibliothèque : `uikit_open` = `uikit_kit_open ()`
  puis `ui_init (&UI)` ; `ui_ev_field (off)` = lire un `i32` dans `ev` à `off` (`EV_X` = 8, `EV_Y` = 12…).
  `uikit_w_event` (code et « pris » par la variable TLS `uikit__ret`) ne sert qu'avec des widgets ; sans widget,
  `ui_w_event (0)` suffit pour `WEV_RESIZE`.
- **Les fonctions de rappel** (`ui_on_resize`, `ui_w_on`…) : `extern "C" fn(i64, i64) -> i64`, passées par
  `f as *const () as usize as i64`. Elles sont appelées pendant `ui_w_event`, sur le même thread.
- **Couleurs** : jamais en dur ; `ui_tokens ()` rend l'adresse des jetons, `TJ_x / 4` en est l'indice, masquer par
  `& 0xFFFFFF`. Redessiner sur **`WEV_THEME`** (le thème a changé) et après un changement de taille.
- `KEY_SPACE` (0x2C) n'est pas dans uikit_abi.h : le définir.
- Dessiner l'image entière, puis **un seul `ui_update_all ()`** ; le `y` de `ui_text` est la ligne de base.
- Les autres kits (AudioKit, NetKit…) se lient pareil : `cargo:rustc-link-lib=static=<kit>kit` et
  `<kit>_kit_open ()` au démarrage (voir leur `*_abi.h`).

## Pièges

- Un `cargo` absent : `PATH` sans `/devtools/rust/bin` (l'ajouter, voir plus haut).
- La première construction télécharge les crates (crates.io) : réseau nécessaire, et c'est lent ; sans dépendance,
  rien à télécharger.
- Les projets vont dans **`~/projects/<nom>`** (`$HOME/projects`), jamais dans `/apps` à la main ni dans
  `/home/projects`.
