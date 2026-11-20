# Les apps Craft pour Korin

Les « Crafting Apps » de storytold (<https://github.com/storytold>, <https://getartcraft.com>) : seize applications
de bureau libres (MIT ou Apache-2.0) en **Rust pur** — WordCraft (traitement de texte), GridCraft (tableur),
DeckCraft, DesignCraft, PdfCraft, PhotoCraft, VectorCraft, FigCraft, CadCraft, Craft Anything, SoundCraft,
MaterialCraft, FilmCraft, LightCraft, EffectCraft, TextureCraft, AniCraft. Elles se compilent avec notre cible Rust
([RUST.md](RUST.md)), en compilation croisée depuis WSL.

**État (20/11/2026)** : **WordCraft avec fenêtre marche sous QEMU PC** (`/apps/wordcraft.app` : écran d'accueil, document
d'exemple, ruban, saisie, correcteur), sur l'eframe de Korin (`src/lib/eframe-korin`, [EGUI.md](EGUI.md)) ;
`wordcraft-cli` aussi. **Le PC seulement** (décision de l'utilisateur du 20/11/2026). Les autres apps : en cours
(`tools/craft/apps.txt`).

## Ce qu'elles ont en commun

| Couche | Quoi | Sur Korin |
|---|---|---|
| cœur | formats, mise en page, PDF, codecs (H.264, AAC, Opus…), polices : `craft-libs` et caisses de crates.io, sans C | se compile, avec cinq petits patchs (plus bas) |
| interface | `egui` 0.37 (fourche de storytold), dans une caisse à part (`wordcraft-ui-egui`…) qui ne dépend d'aucune fenêtre | à faire : un hôte egui pour Korin |
| fenêtre | `eframe` + `winit` (X11, Wayland, Win32, Cocoa), `rfd` (dialogues), `arboard` (presse-papiers) | rien : c'est ce que l'hôte remplace |
| dessin | `vello_cpu` ou `tiny-skia` (par le CPU) ; `wgpu` pour cinq apps | le CPU suffit pour onze apps |
| son | `cpal`, `midir` (SoundCraft, EffectCraft, AniCraft) | à faire : un backend AudioKit |

Trois niveaux, d'après les `Cargo.toml` :

| Niveau | Apps | En plus de l'hôte egui |
|---|---|---|
| 1 : CPU | WordCraft, GridCraft, DeckCraft, DesignCraft, PdfCraft, PhotoCraft, VectorCraft, FigCraft, CadCraft, Craft Anything | rien |
| 2 : son | SoundCraft (`cpal`, `midir`), MaterialCraft (`tokio`) | `cpal` sur AudioKit ; `tokio` (mio, socket2 : les patchs de l'ancien port Spotify) |
| 3 : GPU | FilmCraft, LightCraft (+ `candle`), EffectCraft (+ `boa`, `cpal`), TextureCraft, AniCraft (repli `glow`) | `wgpu` sur le GPU du PC (Mesa) ; QEMU n'a pas de GPU |

## Construire

```sh
ARCH=x86_64 bash tools/rust/build.sh          # une fois : la cible du PC
bash tools/craft/build.sh                      # wordcraft-cli -> build/craft/x86_64/bin/wordcraft-cli
bash tools/craft/build.sh <app> <paquet>       # une autre app (sa révision : REV_<app> dans le script)
```

Quatre à huit minutes la première fois (la `std` et ≈ 400 caisses). Le binaire strippé fait 58,6 Mo sur le PC,
52,8 Mo sur le Pi (polices et données de langue embarquées). Le script remet le `Cargo.lock` de l'app à sa version
du dépôt à chaque lancement (la construction précédente l'a réécrit) et s'arrête si l'un de nos patchs est resté de
côté (« patch was not used » : une autre version de la caisse dans le `Cargo.lock`). Il se copie sur la carte et devient exécutable par `chmod +x`.

`tools/craft/build.sh` travaille dans `~/korin-craft` (`KORIN_CRAFT`) : le dépôt de l'app à la révision épinglée
(WordCraft : `b5e0be78`, 0.18.1), les caisses patchées dans `vendor/`, une copie patchée de `craft-libs`, et un
`.cargo/config.toml` dans le dépôt de l'app qui branche tout cela.

## Ce qui a été patché

| Caisse | Pourquoi | Le patch |
|---|---|---|
| `libc` 0.2.191 | `nix` 0.31 (par `zip`) veut `^0.2.186` ; notre patch vise la 0.2.185 | `tools/rust/libc.patch` rejoué tel quel ; deux entrées dans le `[patch]` de cargo ([RUST.md](RUST.md), « Limites connues ») ; la 0.2.191 veut `safe` ou `unsafe` dans `f!` et n'a plus `safe_f!` (réécrit à l'application) |
| `libc` (module `korin`) | `memmap2` appelle `mprotect` | la déclaration ajoutée à `libc.patch` (libposix l'avait déjà) |
| `getrandom` 0.2.17 et 0.4.3 | `compile_error!` « target is not supported » (`ring`, `rand`) | `korin` dans le backend `getrandom ()`, `errno` par `__errno` (repris de l'ancien port Spotify) |
| `ring` 0.17.14 | `SystemRandom` sans `SecureRandom` ; puis l'assembleur AArch64 et x86-64 absent (édition de liens) | `korin` dans la liste des OS de `getrandom` et dans `LINUX_ABI` du `build.rs` (ELF System V comme Linux) |
| `filetime` 0.2.29 | `lutimes` absent | `korin` prend le backend `utimensat` |
| `craft-font-store` (`craft-libs`) | `cap-primitives` tire `rustix`, qui ne connaît pas Korin (142 erreurs) | sur Korin, la suppression des packs de langue passe par des chemins (`std::fs`) au lieu de dossiers ouverts ; Korin n'a pas de liens symboliques à éviter |

Un `[patch]` vers une copie de `craft-libs` doit couvrir **toutes** ses caisses (88 pour WordCraft) : n'en rediriger
qu'une ferait deux copies des autres, et des types différents. `build.sh` les lit dans le `Cargo.lock`, sauf celles que
l'app a déjà en `[patch.crates-io]` (`krilla`, `hayro`… : pas de patch en chaîne).

`ring` et `rustls` viennent de `ureq` (téléchargement de polices et de packs de langue) ; ils compilent, mais le
réseau HTTPS n'a pas été essayé.

## Essai (QEMU PC, 20/11/2026)

Disque d'essai de `tools/rust/testimage.py --pc` avec `wordcraft-cli` et un `hello.md` dans `build/pc/rust/tests`
(depuis : `tools/craft/pctest.py`, qui met `build/craft/x86_64/bin` et `tools/craft/test` dans `HOME:/craft`),
QEMU de WSL (`. ~/korin-cloud/env.sh` : l'OVMF avec le shell UEFI, [agents/CLOUD.md](agents/CLOUD.md)), puis
`testimage.py --pc-run` :

| Commande | Résultat |
|---|---|
| `wordcraft-cli --version` | `wordcraft-cli 0.18.1` (3,6 s, le premier lancement) |
| `convert hello.md hello.docx` | 8 208 octets, 1,8 s |
| `info hello.docx` | 1 page, 11 paragraphes, 30 mots ; mise en page 889 ms |
| `text hello.docx` | le texte, accents, puces et tableau compris |
| `convert hello.docx hello.pdf` | 29 872 octets, 4,6 s |
| `render hello.docx hello.png --scale 1` | 612 × 792, 18 771 octets, 5,9 s |

Pas regardé : le contenu du PDF et du PNG (sur le disque de QEMU, en snapshot).

## La suite

1. **L'hôte egui pour Korin : fait le 20/11/2026** ([EGUI.md](EGUI.md), `src/lib/egui-korin`) : la démo d'egui tourne
   sous QEMU PC. Reste WordCraft avec fenêtre : un `eframe` pour Korin sur cet hôte, branché par `[patch]`.
   `apps/wordcraft/src/main.rs` et `craft-ui-shell` n'utilisent d'eframe que `App`, `Frame`, `NativeOptions`,
   `CreationContext`, `run_native` et `icon_data` ; mais l'exécutif de `craft` (`embed::install`, `WindowHost`, les
   dialogues de `FilePanels`, rattachés à `eframe::Frame`) et les caisses qui prennent X11 ou Wayland parce que Korin
   est `unix` sans être macOS (`winit`, `wayland-*`, `zbus`, `x11-dl` ; `rfd`, `arboard`, `egui-wgpu`) sont à couper
   ou à rediriger.
2. Le niveau 1, presque sans travail de plus ; le niveau 2 avec `cpal` sur AudioKit ; le niveau 3 en dernier.
