# Les apps Craft pour Korin

Les « Crafting Apps » de storytold (<https://github.com/storytold>, <https://getartcraft.com>) : dix-sept applications
de bureau libres (MIT ou Apache-2.0) en **Rust pur**, sur egui. Elles se compilent avec notre cible Rust
([RUST.md](RUST.md)), en compilation croisée depuis WSL (ou une session du cloud), pour **Korin PC seulement**
(décision de l'utilisateur du 20/11/2026). L'hôte egui de Korin et l'eframe de Korin : [EGUI.md](EGUI.md).
Reprise dans le cloud : [agents/CRAFT-HANDOVER.md](agents/CRAFT-HANDOVER.md).

## État (21/11/2026)

Les **17 apps sont construites et s'ouvrent sous QEMU PC** (21/11/2026, captures de chaque app à l'ouverture).

| App | Essayée sous QEMU PC | Remarques |
|---|---|---|
| **Suite ArtCraft** (le lanceur, `src/apps/craft`) | la grille de la suite, ouvre les apps ; « 17 apps sur 17 » | une app pas installée : un clic ouvre Logiciels |
| WordCraft (+ `wordcraft-cli`) | accueil, document d'exemple, ruban, saisie, correcteur (20/11) ; la CLI convertit md, docx, pdf, png | — |
| GridCraft | ruban, grille (20/11) | — |
| DeckCraft | accueil, présentation neuve, ruban | — |
| CADCraft | dessin, outils, calques, propriétés ; une ligne tracée (LINE) | — |
| DesignCraft | accueil, le magazine d'exemple rendu en entier | avertit à tort que « la première image n'est pas arrivée en 20 s » |
| PdfCraft | accueil, outils | la signature sans aws-lc : **ni signature ni clé RSA** (P-256, P-384 oui) |
| FilmCraft | le projet de démonstration : timeline, aperçu | pas de décodage matériel, pas de son |
| LightCraft | la photothèque | « Settings file problem » : la photothèque ne peut pas être verrouillée (`File::lock`) |
| PhotoCraft, VectorCraft, FigCraft, TextureCraft, MaterialCraft, EffectCraft | l'accueil de chacune | chemins CPU (pas de GPU) |
| AniCraft, Craft Anything | l'accueil (egui **0.36** : l'hôte copié par `tools/craft/egui036.py`) | Craft Anything : capture des programmes par tubes (pas de terminal virtuel) |
| SoundCraft | une session vide, la console | **pas de son** (`cpal` n'a pas de sortie pour Korin), pas de MIDI, pas d'éditeur VST3 |

Au-delà de l'ouverture, rien n'a été essayé app par app (ouvrir, modifier, enregistrer un document) sauf WordCraft,
DeckCraft, CADCraft et DesignCraft.

La suite est dans `/apps/<app>.app`, catégorie **« Craft »**, que le lanceur et le menu de Korin ne montrent pas
(`src/sys/common/applist.c`, `cat_hidden`, comme « Émulateurs ») : on l'ouvre par **Suite ArtCraft**
(`/apps/craft.app`, catégorie Bureautique), seule entrée de la suite dans les applications de Korin.

## Paquets

Un paquet pour le lanceur, **`craft`** (« Suite ArtCraft », Bureautique), et un paquet par app (`wordcraft`,
`gridcraft`…, catégorie « Suite ArtCraft » dans Logiciels) qui **dépend de `craft`** : `tools/pkg/packages-pc.ini`,
sections `[app.craft]` et `[app.<app>]`. Les apps (de 26 à 191 Mo : polices et données dans le programme) sont
`release = 1` : leurs archives vont dans la Release « packages » de KorinOS, pas sur GitHub Pages. L'arbre :
`sh tools/pc/sysroot_craft.sh` après `tools/pc/sysroot.sh` (le lanceur et les apps de `build/craft/x86_64/apps`, sans
la démo d'egui). **Publiés le 21/11/2026** (index du PC, 139 paquets) : `craft` 1.0.0
sur GitHub Pages, les 17 apps 1.0.0 dans la Release. Depuis le cloud (pas de `gh`, le proxy refuse les envois binaires) :
`pub.py --new --api --manual-upload` prépare chaque archive (elle se refait à l'identique), un workflow d'une branche
de transit (`.github/workflows/…` sur une branche sans historique, déclenché par le push) la dépose dans la Release avec
le jeton d'Actions, puis `pub.py` la retélécharge, vérifie son SHA-256 et commite l'index signé. Une nouvelle version :
`pub.py <app>` (sans `--new`), même chemin.

## Construire

```sh
bash tools/craft/all.sh                       # tout, depuis zéro : chaîne Rust, libposix et UIKit du PC, les apps,
                                              # la démo d'egui, Craft et les icônes, panel et menubar
bash tools/craft/all.sh gridcraft deckcraft   # seulement ces apps (puis le lanceur)
bash tools/craft/build.sh <app> [paquet]      # une app de tools/craft/apps.txt ; sans argument : wordcraft-cli
bash tools/craft/launcher.sh                  # le lanceur, ses icônes (PIL), l'icon.rgba de chaque app
bash tools/craft/egui.sh                      # la démo d'egui (/apps/eguidemo.app)
```

Sorties : `build/craft/x86_64/apps/<app>.app` (`main`, `app.txt`, `icon.rgba`), `build/craft/x86_64/bin`,
`build/craft/x86_64/status.txt` (l'état de chaque app après `all.sh`), journaux dans `build/craft/x86_64/logs`.
Travail hors du dépôt : `~/korin-craft` (`KORIN_CRAFT`).

Une app : de cinq à quinze minutes la première fois sur 16 cœurs, de vingt à quarante sur 4 (le cloud). Binaires
strippés (polices et données dans le programme) : PhotoCraft 191 Mo, WordCraft 155, VectorCraft 150, EffectCraft 95,
DesignCraft 91, LightCraft 76, PdfCraft 71, FigCraft 64, FilmCraft 60, CADCraft 57, MaterialCraft 49, DeckCraft 45,
TextureCraft 39, GridCraft 34, SoundCraft 26, le lanceur 7. Le dossier `target` d'une app fait de 1 à 3,3 Go :
`build.sh` le garde (reconstruction incrémentale) ; l'effacer quand la place manque (une session du cloud a ≈ 70 Go).

## Comment `build.sh` construit une app

1. **libc** : la `std` garde la 0.2.185 de `rust-src` ; les projets prennent une 0.2.191 patchée de la même
   `tools/rust/libc.patch` (deux entrées dans le `[patch]` de cargo : une seule remplacerait celle de la `std`).
2. **le dépôt** de l'app à la révision de `tools/craft/apps.txt`, remis à neuf (`git checkout -- .`), puis :
   - `tools/craft/korin_app.py` : `eframe` → `src/lib/eframe-korin` dans le `[patch.crates-io]` (une app sur eframe
     0.36 : la copie 0.36 de l'hôte, `tools/craft/egui036.py`, à la version de son `Cargo.lock`) ; VectorCraft et
     EffectCraft : `winit` → `src/lib/winit-korin` (les types de winit sans fenêtre) ; WordCraft : son
     presse-papiers sur Korin (`tools/craft/app/korin.rs`), sans `gpu-startup` ; PdfCraft : sans `rustix` ni
     `thread-priority`, la signature sans aws-lc (la branche wasm32) ; FigCraft : le dossier XDG et la langue (POSIX)
     gardés ; SoundCraft : sans `winit` (X11 des BSD) ni fenêtre d'éditeur VST3 ;
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
5. `cargo korin build --release -p <paquet>` (avec `--cfg mio_unsupported_force_poll_poll` et
   `mio_unsupported_force_waker_pipe` : mio sur poll() et un tube), contrôle des patchs restés de côté (une caisse
   de l'app dans la série d'un patch, à une version que nous ne patchons pas), `strip`, et l'app si
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
| `rustix` 1.1.5 (PhotoCraft), 1.1.6 | ne connaît pas Korin (140 erreurs) | Korin comme Horizon (newlib), sauf ce que libposix a (`flock`, `pipe2`, les `*at`, la lecture des dossiers, les horloges de temps CPU…) : `tools/craft/rustix_korin.py` |
| `cap-primitives` 4.0.3 | `O_DSYNC`, `O_RSYNC`, `ttyname`, `O_PATH`, `uid_t` 16 bits | `tools/craft/crate_korin.py` |
| `mio` 1.2.4, `socket2` 0.6.5, `tokio` 1.54.1 | tokio (MaterialCraft, TextureCraft, FilmCraft…) | de l'ancien port de Spotify ; mio par ses replis « unsupported » (`--cfg` de build.sh) |
| `open` 5.4.5 | aucun lanceur de fichiers | celui de l'ancien port de Spotify |
| `fs4` 1.1.0 | `allocate` | la branche sans `fallocate` |

Les trois derniers patchs sont produits par les scripts (`rustix_korin.py`, `crate_korin.py`) : une autre version de
la caisse se refait en les relançant (docs/agents/CRAFT-HANDOVER.md, « Refaire un patch »).

Dans craft-libs (`korin_libs.py`) : `craft-pkcs7` sans aws-lc-rs sur Korin, comme pour PdfCraft.

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
- PdfCraft : pas de signature RSA ni de création de clé RSA (aws-lc n'est pas construit pour Korin).
- Pas de stockage d'eframe (`eframe::get_value` ne lit rien) : les apps qui ont leurs propres réglages (toutes, à
  peu près) les gardent ; l'état d'egui (panneaux…) n'est pas gardé d'une fois sur l'autre.
- La **barre de titre** : les apps dessinent la leur, avec la place des boutons de fenêtre de Windows ou de macOS ;
  sur Korin, leurs boutons de droite passent sous ceux de la fenêtre (DeckCraft, DesignCraft). À faire : une variante
  Korin dans craft-window (titre à gauche contre l'icône, réserve à droite), et les menus dans la barre de menus de
  Korin par un backend Korin de `craft-menu` (celui de macOS passe par muda).
