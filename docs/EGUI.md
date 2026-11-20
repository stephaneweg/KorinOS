# egui sur Korin : l'hôte `egui-korin`

[egui](https://github.com/emilk/egui) est la bibliothèque d'interface en Rust des apps Craft ([CRAFT.md](CRAFT.md)) et
de beaucoup d'autres programmes Rust. Sur Linux, Windows et macOS, egui passe par `eframe` : une fenêtre `winit` et un
dessin par le GPU (`wgpu` ou OpenGL). Sur Korin, `egui-korin` (`src/lib/egui-korin`) remplace les deux :

| Couche | Ailleurs | Sur Korin |
|---|---|---|
| fenêtre, évènements | `winit` (X11, Wayland, Win32, Cocoa) | UIKit : `ui_window_ex` redimensionnable, `ui_event` |
| dessin | `wgpu` ou `glow` | le CPU, `hdrshot-egui-backend` (egui_software_backend 0.0.3 porté sur egui 0.37), **dans les pixels de la fenêtre** (`ui.surf`), sans copie |
| presse-papiers | `arboard` | `ui_clip_set`, `ui_clip_get` (le service `clipd`) |
| dialogues | `rfd` | `ui_file_open`, `ui_file_save`, `ui_message` |
| ouvrir une adresse | `webbrowser` | `ui_open_with` |

**État (20/11/2026)** : la démo d'egui (`egui_demo_lib`) **tourne sous QEMU PC** : le dessin, la souris (clic, survol),
le clavier (texte en AZERTY, majuscules), le presse-papiers dans les deux sens (Ctrl+C donne la notification de
Korin, Ctrl+V colle) et les émojis. Le PC seulement ([CRAFT.md](CRAFT.md)).

## Se servir de l'hôte

```rust
fn main() {
    egui_korin::run(egui_korin::Options::new("Bonjour").size(800, 600), |ui, host| {
        ui.label("Bonjour de Korin");
        if ui.button("Ouvrir…").clicked() {
            let _ = host.file_open("HOME:/", Some("*.txt;*.md"));
        }
    }).unwrap();
}
```

- `run(options, |ui, host| …)` : la passe d'egui à chaque image ; `host` ferme la fenêtre, change le titre, ouvre
  les dialogues, ouvre une adresse.
- `run_with(options, &ctx, |ctx, input, host| -> FullOutput)` : la passe faite par l'appelant (un contexte à lui,
  une passe à deux temps comme celle d'`eframe`).
- `clipboard_get`, `clipboard_set`, `system_theme` : à part de la boucle.

La démo, une app de Korin (`/apps/eguidemo.app`) :

```sh
bash tools/craft/egui.sh       # -> build/craft/x86_64/apps/eguidemo.app (main, app.txt)
```

Il faut la chaîne Rust du PC ([RUST.md](RUST.md), `ARCH=x86_64 bash tools/rust/build.sh`) et les interfaces C des kits
du PC (`wsl sh tools/pc/apps.sh` : `build/pc/gcc/include/kits/uikit_abi.h`, `build/pc/gcc/lib/libuikit.a`).

## Comment c'est fait

- **Le relais C** (`csrc/host.c`) : la fenêtre, ses pixels, ses évènements, le presse-papiers, les dialogues, avec
  `uikit.h`. `build.rs` le compile par le gcc de Korin (cc-rs le trouve par l'éditeur de liens de la cible) et lie
  `libuikit.a`. Rust n'appelle que ces quelques fonctions, avec des types C simples (`src/sys.rs`).
- **Les évènements** (`src/lib.rs`, `Window::event`) :
  - clavier : le code HID (`EV_CODE`) donne la touche physique ; la touche logique suit le caractère de la disposition
    (`EV_CHAR`), pour que Ctrl+Z soit la touche marquée Z en AZERTY (`src/keys.rs`). Le texte : un caractère
    imprimable sans Ctrl, ou avec Ctrl+Alt (AltGr). Ctrl+C, X, V donnent les évènements Copy, Cut, Paste d'egui ;
  - souris : déplacement, appui, relâché (les boutons qui ont changé, d'après `EV_CODE`), molette en lignes ;
  - `WEV_RESIZE` : `uikit_w_event` refait le tampon, l'hôte relit ses pixels ; `WEV_THEME` : le thème du système
    (`TJ_DARK`) devient celui d'egui ; `WEV_FOCUS`.
- **La boucle** : on attend un évènement jusqu'à la prochaine image demandée par egui (`repaint_delay` ; 15 ms au
  plus vite), on prend ceux qui suivent sans attendre, puis une passe. Sans animation ni évènement, rien ne tourne.
- **Le dessin** : le fond aux couleurs du thème (`TJ_FACE`), puis la toile d'`hdrshot-egui-backend` (mise en cache
  par primitive) composée dessus, puis `ui_update` de toute la fenêtre. Les pixels de Korin sont `0x00RRGGBB` en
  petit-boutiste, soit B, G, R, 0 en mémoire : l'ordre `Bgra` de la caisse.

## Essayer sous QEMU PC

`tools/craft/pctest.py` (dans WSL, `. ~/korin-cloud/env.sh` d'abord) : une copie de `build/pc/sysroot` avec les apps de
`build/craft/x86_64/apps`, un disque, puis les commandes de `tools/python/pctest.py` (captures, clics, touches) :

```sh
python3 -I tools/craft/pctest.py prep
python3 -I tools/craft/pctest.py run --qemu "-device usb-tablet,bus=xhci.0" \
    "/apps/eguidemo.app/main &" sleep:25 shot:demo click:340,281 "type:Korin OS" key:ctrl-c shot:demo2
```

`key:` (noms de `sendkey`) et `type:` (un texte, en AZERTY) ont été ajoutés à `tools/python/pctest.py` pour cela.
L'image fait près d'un Go : effacer `build/pc/crafttest` après l'essai.

## Limites et suite

- **Toute la fenêtre est recopiée à chaque image** (`ui_update` de tout) ; la caisse sait quelles tuiles ont changé :
  ne montrer qu'elles.
- Pas d'IME (saisie composée), pas de dépôt de fichiers (`WEV_DROP`), pas de textures d'images de l'app au-delà de
  celles d'egui (la caisse ne prend pas encore les textures « utilisateur »).
- Une seule fenêtre (pas de fenêtres secondaires d'egui, les « viewports »).
- Le redimensionnement passe par `uikit_w_event`, et la démo s'est redessinée après le bouton agrandir, mais la
  fenêtre était déjà presque à la taille de l'écran : pas vu à des tailles bien différentes.
- **WordCraft avec fenêtre** : un `eframe` pour Korin, fait sur cet hôte, à la place de celui de storytold
  ([CRAFT.md](CRAFT.md), « La suite »).
