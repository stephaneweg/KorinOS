# Les apps Craft pour Korin

Les « Crafting Apps » de storytold (<https://github.com/storytold>, <https://getartcraft.com>) : dix-sept applications
de bureau libres (MIT ou Apache-2.0) en **Rust pur**, sur egui. Elles se compilent avec notre cible Rust
([RUST.md](RUST.md)), en compilation croisée depuis WSL (ou une session du cloud), pour **Korin PC seulement**
(décision de l'utilisateur du 20/11/2026). L'hôte egui de Korin et l'eframe de Korin : [EGUI.md](EGUI.md).
Reprise dans le cloud : [agents/CRAFT-HANDOVER.md](agents/CRAFT-HANDOVER.md).

## État (20/11/2026)

| App | Construite | Essayée sous QEMU PC | Ce qui bloque |
|---|---|---|---|
| **Craft** (le lanceur, `src/apps/craft`) | oui | **oui** : la grille de la suite, ouvre les apps | — |
| WordCraft (+ `wordcraft-cli`) | oui | **oui** : accueil, document d'exemple, ruban, saisie, correcteur ; la CLI convertit md, docx, pdf, png | — |
| GridCraft | oui | **oui** : s'ouvre depuis Craft (ruban, grille) | — |
| DeckCraft, CADCraft, DesignCraft | oui | non (pas encore lancées) | — |
| PdfCraft | non | | `aws-lc-sys` (C, par `craft-pkcs7` : signatures) ; `thread-priority` coupé |
| FilmCraft | non | | `open` 5.4.5 (patch dans `patches-pending`) ; une erreur de type dans `ring` (à voir) |
| LightCraft | non | | il nomme `eframe::Renderer`, `NativeOptions::renderer`, `eframe::glow` (à ajouter à eframe-korin) ; `candle`, wgpu |
| MaterialCraft, TextureCraft | non | | `mio` 1.2.4 (tokio) : patch dans `patches-pending` + `--cfg mio_unsupported_force_*` |
| PhotoCraft | non | | `rustix` 1.1.5 épinglé (patch à faire pour 1.1.5) ; `appearance.rs` d'une autre forme ; canevas GPU |
| FigCraft | non (5 erreurs dans l'app) | | à lire |
| SoundCraft | non | | hôte VST3, `cpal`, `midir` (pas de son pour Korin dans cpal) |
| VectorCraft, EffectCraft | non | | `winit` pris directement (fenêtre native : presse-papiers, verrou des majuscules…) |
| Craft Anything, AniCraft | non | | egui et eframe **0.36** (l'hôte suit egui 0.37) |

La suite est dans `/apps/<app>.app`, catégorie **« Craft »**, que le lanceur et le menu de Korin ne montrent pas
(`src/sys/common/applist.c`, `cat_hidden`, comme « Émulateurs ») : on l'ouvre par **Craft** (`/apps/craft.app`), seul
dans le lanceur et le dock. Une app pas encore construite y est grisée.

## Construire

```sh
bash tools/craft/all.sh                       # tout, depuis zéro : chaîne Rust, libposix et UIKit du PC, les apps,
                                              # la démo d'egui, Craft et les icônes, panel et menubar
bash tools/craft/all.sh gridcraft deckcraft   # seulement ces apps (puis le lanceur)
bash tools/craft/build.sh <app> [paquet]      # une app de tools/craft/apps.txt ; sans argument : wordcraft-cli
bash tools/craft/launcher.sh                  # Craft, ses icônes (PIL), l'icon.rgba de chaque app
bash tools/craft/egui.sh                      # la démo d'egui (/apps/eguidemo.app)
```

Sorties : `build/craft/x86_64/apps/<app>.app` (`main`, `app.txt`, `icon.rgba`), `build/craft/x86_64/bin`,
`build/craft/x86_64/status.txt` (l'état de chaque app après `all.sh`), journaux dans `build/craft/x86_64/logs`.
Travail hors du dépôt : `~/korin-craft` (`KORIN_CRAFT`).

Une app : de cinq à quinze minutes la première fois. Binaires strippés : WordCraft 161 Mo, DesignCraft 95 Mo,
CADCraft 60 Mo, DeckCraft 46 Mo, GridCraft 35 Mo, Craft 6,4 Mo (polices et données embarquées).

## Comment `build.sh` construit une app

1. **libc** : la `std` garde la 0.2.185 de `rust-src` ; les projets prennent une 0.2.191 patchée de la même
   `tools/rust/libc.patch` (deux entrées dans le `[patch]` de cargo : une seule remplacerait celle de la `std`).
2. **le dépôt** de l'app à la révision de `tools/craft/apps.txt`, remis à neuf (`git checkout -- .`), puis :
   - `tools/craft/korin_app.py` : `eframe` → `src/lib/eframe-korin` dans le `[patch.crates-io]` ; WordCraft : son
     presse-papiers sur Korin (`tools/craft/app/korin.rs`), sans `gpu-startup` ; PdfCraft : sans `rustix` ni
     `thread-priority` ;
   - `tools/craft/korin_cfg.py` : les `cfg` des « Unix autres que macOS » (X11, Wayland, D-Bus, portails) excluent
     Korin, qui prend la branche « aucun bureau connu » (et son repli `not(any(unix, windows))` quand la branche Unix
     du fichier était celle du bureau) ;
   - chaque caisse que Korin patche est ramenée à notre version (`cargo update --precise`) si l'app en a une autre de
     la même série.
3. **les caisses de crates.io** patchées : chaque `tools/craft/patches/<caisse>-<version>.patch`, appliqué à une copie
   dans `~/korin-craft/vendor`, branchée par le `.cargo/config.toml` de l'app.
4. **craft-libs** : une app la prend à plusieurs révisions (une par caisse). Une copie par révision
   (`~/korin-craft/craft-libs-<rev>`) reçoit `korin_cfg.py` et `korin_libs.py` (par ancres : `appearance.rs`,
   `craft-font-store` sans `cap-primitives`, où que soit son code) ; `craftlibs_patch.py` ne redirige que les
   révisions dont l'app prend une caisse changée, et de celles-ci toutes ses caisses (sinon deux copies des mêmes
   types), sauf celles qu'elle prend à plusieurs révisions.
5. `cargo korin build --release -p <paquet>`, contrôle des patchs restés de côté, `strip`, et l'app si
   `tools/craft/app/<paquet>.txt` existe.

## Ce qui a été patché

| Caisse | Pourquoi | Le patch |
|---|---|---|
| `libc` 0.2.191 (projets) | `nix` 0.31 veut `^0.2.186` | `libc.patch` rejoué ; `f!` et `safe_f!` réécrits (build.sh) |
| `libc` (module `korin`) | `memmap2`, `cap-std`, rustix | `mprotect`, `openat`, `fstatat`, `linkat`, `symlinkat`, `fchownat` (libposix les a) |
| `getrandom` 0.2.17, 0.4.3 | « target is not supported » | `korin` dans le backend `getrandom ()` |
| `ring` 0.17.14 | `SecureRandom` ; l'assembleur absent au lien | `korin` dans les OS de `getrandom` et dans `LINUX_ABI` |
| `filetime` 0.2.29 | `lutimes` | backend `utimensat` |
| `tempfile` 3.27.0 | `rustix` | la `std` |
| `rfd` 0.17.2 | aucun backend | **les dialogues de Korin** (`ui_file_open`, `ui_file_save`, `ui_folder_choose`, `ui_message`) |
| `arboard` 3.7.0 | X11, Wayland | **le presse-papiers de Korin** (texte) |
| `hdrshot-egui-backend` 0.2.0 | le cache oubliait les textures qui changent (les pages de WordCraft restaient figées) | tout redessiner quand une texture change |
| `rustix` 1.1.6 | ne connaît pas Korin (140 erreurs) | Korin comme Horizon (newlib), sauf ce que libposix a (`flock`, `pipe2`, les `*at`, la lecture des dossiers…) : `tools/craft/rustix_korin.py` |
| `cap-primitives` 4.0.3 | `O_DSYNC`, `O_RSYNC`, `ttyname`, `uid_t` 16 bits | `tools/craft/crate_korin.py` |
| `fs4` 1.1.0 | `allocate` | la branche sans `fallocate` |

Les trois derniers patchs sont produits par les scripts (`rustix_korin.py`, `crate_korin.py`) : une autre version de
la caisse se refait en les relançant (docs/agents/CRAFT-HANDOVER.md, « Refaire un patch »).

`tools/craft/patches-pending` : les patchs de l'ancien port Spotify (`open` 5.4.5, `mio` 1.2.4, `socket2` 0.6.5,
`tokio` 1.54.1), pas encore essayés avec la suite (MaterialCraft, TextureCraft, FilmCraft en ont besoin) ; `mio`
demande aussi `RUSTFLAGS=--cfg mio_unsupported_force_poll_poll --cfg mio_unsupported_force_waker_pipe`.

## Essai (QEMU PC)

`tools/craft/pctest.py` (dans WSL ou le cloud, `. ~/korin-cloud/env.sh` d'abord : l'OVMF avec le shell UEFI) : une
copie de `build/pc/sysroot` (sans `/devtools`) avec les apps de `build/craft/x86_64/apps`, `build/craft/x86_64/bin` et
`tools/craft/test` dans `HOME:/craft`, le panel et le menubar de `build/sys/x86_64` ; puis les commandes de
`tools/python/pctest.py` (captures `shot:`, clics `click:`, touches `key:` et `type:`). Exemple :

```sh
python3 -I tools/craft/pctest.py prep --mb 1800
python3 -I tools/craft/pctest.py run --qemu "-device usb-tablet,bus=xhci.0" \
    "/apps/craft.app/main &" sleep:20 shot:craft click:550,195 sleep:40 shot:gridcraft
```

`wordcraft-cli` (20/11) : `--version`, md → docx (1,8 s), `info` (mise en page 889 ms), `text`, docx → pdf (4,6 s),
docx → png (5,9 s). L'image d'essai fait près de 2 Go : effacer `build/pc/crafttest` après.

## Limites connues

- **Pas de GPU** : l'eframe de Korin ne crée jamais d'état wgpu ; les apps qui l'exigent (PhotoCraft, FilmCraft,
  LightCraft…) devront prendre leur chemin CPU, ou attendre wgpu sur Mesa.
- Pas de **certificats racines** pour rustls (« No CA certificates were loaded from the system ») : pas de mise à jour,
  pas de téléchargement de polices ni de packs de langue.
- **Verrous de fichiers** : `File::lock` n'est pas pris en charge par la `std` de Korin ; les réglages s'enregistrent
  sans verrou, la récupération des documents ne peut pas effacer ses copies.
- Le presse-papiers de Korin ne prend que le **texte** (les copies riches restent dans l'app).
- Pas de son (`cpal` n'a pas d'hôte pour Korin).
