# Reprise : la suite Craft sur Korin PC (session du cloud)

Passation du 20/11/2026, depuis une session locale (WSL), pour finir dans une session du cloud ce que l'utilisateur
a demandé : **toute la suite Craft de storytold sur Korin PC, ouverte par un lanceur unique**. Le contexte, l'état
app par app et le fonctionnement des scripts : [../CRAFT.md](../CRAFT.md) ; l'hôte egui et l'eframe de Korin :
[../EGUI.md](../EGUI.md). Mémoire du projet : `.agents/memory/armpi-craft.md`.

## Décisions de l'utilisateur

- **Le PC seulement**, pas le Pi (20/11/2026). Le code reste neutre (`ARCH=aarch64` compile WordCraft), mais rien
  n'est à faire ni à essayer pour le Pi.
- **Un lanceur unique** : Craft (`/apps/craft.app`, `src/apps/craft`) ; les apps de la suite sont dans la catégorie
  « Craft », cachée du lanceur et du menu de Korin (`src/sys/common/applist.c`). Fait et essayé.
- Tout ce qui sert à reconstruire est dans le dépôt (scripts, patchs, bibliothèques) ; rien n'est à récupérer de la
  machine locale.

## Mettre la session en place

```sh
sudo sh tools/cloud/setup.sh root                    # (une fois, si root) paquets, QEMU, OVMF
sh tools/cloud/setup.sh base toolchains qemu sources rust
. ~/korin-cloud/env.sh                                # QEMU, OVMF avec le shell UEFI, chaînes
pip install --user pillow                             # les icônes (tools/craft/icon_rgba.py)
```

Pour l'essai sous QEMU, il faut l'arbre du PC (`build/pc/sysroot`), comme dans [CLOUD.md](CLOUD.md) section 4 :
`sh tools/pc/build.sh`, `sh tools/pc/apps.sh`, `sh tools/pc/sysroot.sh`, `sh tools/pc/sysroot_apps.sh`.

Puis tout Craft :

```sh
bash tools/craft/all.sh          # 1 à 3 h la première fois (17 apps) ; l'état : build/craft/x86_64/status.txt
```

`all.sh` fait ce qui manque : libposix du PC, la chaîne Rust `x86_64-unknown-korin`, l'interface C d'UIKit
(`build/pc/gcc/lib/libuikit.a`), chaque app de `tools/craft/apps.txt` (journal : `build/craft/x86_64/logs/<app>.log`),
la démo d'egui, Craft et les icônes, panel et menubar.

## Ce qui reste, par ordre de rendement

État au moment de la passation : construites et essayées : **Craft, WordCraft, GridCraft** ; construites sans essai :
**DeckCraft, CADCraft, DesignCraft**. Les diagnostics ci-dessous viennent de la dernière série (journaux non gardés :
relancer `bash tools/craft/build.sh <app>`).

1. **Essayer DeckCraft, CADCraft, DesignCraft** sous QEMU (depuis Craft : `pctest.py`, plus bas).
2. **LightCraft** : il nomme `eframe::Renderer`, `NativeOptions::renderer` et `eframe::glow` (les noms d'eframe 0.37 :
   `enum Renderer { Glow, Wgpu }`, champ `renderer`, module `glow` réexporté sous la fonction `glow`). Les ajouter à
   `src/lib/eframe-korin/src/lib.rs` comme `wgpu_options` (sans effet). Puis `candle` et wgpu : voir ce qui vient.
3. **MaterialCraft, TextureCraft** : `mio` 1.2.4 (tokio). Les patchs de l'ancien port Spotify sont dans
   `tools/craft/patches-pending` (`mio`, `socket2`, `tokio`) : les passer dans `tools/craft/patches` et ajouter à
   `tools/craft/build.sh`, avant `cargo korin build`, `export RUSTFLAGS="${RUSTFLAGS:-} --cfg
   mio_unsupported_force_poll_poll --cfg mio_unsupported_force_waker_pipe"` (comme l'ancien `tools/spotify/build.sh`,
   commit b71cf646). Puis `cap-std` (leur automatisation) : il compile sur notre rustix (essayé seul).
4. **FilmCraft** : `open` 5.4.5 (patch dans `patches-pending`) ; une erreur de type dans
   `ring/src/polyfill/slice/as_chunks.rs` à lire (FilmCraft prend peut-être une autre version de `ring` ou d'une de ses
   fonctions).
5. **PhotoCraft** : son `Cargo.lock` garde `rustix` 1.1.5 (`cargo update --precise 1.1.6` refusé) : faire le patch de
   la 1.1.5 (« Refaire un patch ») ; `crates/platform/src/appearance.rs` de sa révision de craft-libs (e7e877a) n'a pas
   le texte de `korin_libs.py` (le `Worker` à variante vide) : adapter l'ancre ; puis son canevas GPU.
6. **FigCraft** : 5 erreurs dans l'app elle-même, à lire.
7. **PdfCraft** : `aws-lc-sys` (bibliothèque C, par `craft-pkcs7` : les signatures) ; essayer sa construction par `cc`
   pour Korin, ou couper la signature sur Korin (`korin_app.py`). `thread-priority` et `rustix` sont déjà coupés.
8. **SoundCraft** : l'hôte VST3, `cpal`, `midir` : couper l'hôte VST3 sur Korin ; un hôte `cpal` pour Korin (sur
   AudioKit) est un chantier en soi.
9. **VectorCraft, EffectCraft** : `winit` pris directement (la fenêtre native : presse-papiers, Caps Lock, profil
   d'écran, hôte des dialogues). Couper ces chemins sur Korin dans `korin_app.py`, app par app.
10. **Craft Anything, AniCraft** : egui et eframe **0.36**. Il faudrait egui-korin et eframe-korin pour 0.36 (une
    seconde version de la caisse, ou un `cfg` de version), ou attendre qu'elles passent à 0.37.

Après chaque app construite : `bash tools/craft/launcher.sh` (son icône), puis l'essayer depuis Craft.

## Essayer sous QEMU PC

```sh
python3 -I tools/craft/pctest.py prep --mb 1800       # --quick : garder la copie de l'arbre
python3 -I tools/craft/pctest.py run --qemu "-device usb-tablet,bus=xhci.0" \
    "/apps/craft.app/main > /tmp/craft.log 2>&1 &" sleep:20 shot:craft \
    click:550,195 sleep:40 shot:app "tail -20 /tmp/craft.log"
```

Les captures sont dans `build/pc/crafttest/<nom>.png` (écran 1280×800). Dans Craft, les cartes font 300×112 px, trois
par ligne, à partir de (94, 146) environ : la deuxième de la première ligne (GridCraft) est vers (550, 195). Le journal
d'une app : `/home/.config/<app>/logs/<app>.log`. Les commandes de `tools/python/pctest.py` : `shot:`, `sleep:`,
`click:X,Y`, `dblclick:X,Y`, `key:ctrl-c tab ret` (noms de sendkey), `type:Texte` (AZERTY). Effacer
`build/pc/crafttest` après (près de 2 Go).

## Refaire un patch

- `rustix` (une autre version) : `python3 -I tools/craft/rustix_korin.py <copie de rustix-x.y.z>` sur une copie
  fraîche du registre, puis `diff -ruN` vers `tools/craft/patches/rustix-x.y.z.patch` (chemins `a/`, `b/`). Pour
  vérifier rustix seul : un projet d'essai qui prend `rustix` (fonctions `fs process event time pipe param thread
  termios net std`), `cap-std` 4, `fs4` 1.1 et `polling` 3, avec `[patch.crates-io] rustix = { path = <copie> }`, et
  `cargo korin build --release`.
- `cap-primitives`, `fs4` : `python3 -I tools/craft/crate_korin.py <caisse-version> <copie>`, puis `diff`.
- Une caisse de plus : `tools/craft/patches/<caisse>-<version>.patch` (`diff -ruN`, chemins `a/`, `b/`) ; `build.sh`
  l'applique et y ramène les `Cargo.lock` des apps de la même série.

## Pièges rencontrés

- Une seule construction à la fois : les apps partagent les copies de `craft-libs` et `~/korin-craft/vendor`.
- Ne pas modifier `tools/craft/build.sh` pendant qu'il tourne : bash lit le script au fil de l'exécution.
- Une règle de `korin_cfg.py` trop large casse ailleurs : `any(unix, windows)` réécrit donnait deux définitions là où
  la branche Unix était du POSIX (`craft-i18n`, `craft-media`) ; `not(any(target_vendor = "apple", target_os =
  "android"))` est la lecture de LANG et doit rester à Korin.
- `cargo` refuse deux paquets « chemin » de même nom et version : ne rediriger que les révisions de craft-libs
  nécessaires (`craftlibs_patch.py` le fait).
- `hdrshot-egui-backend` est épinglé à `=0.2.0` : son patch (le cache et les textures) n'aurait pas pris sinon.
- QEMU : l'OVMF des distributions n'a pas le shell UEFI ; `. ~/korin-cloud/env.sh` donne celui des sources de QEMU.
- Un programme lancé depuis telnet a besoin de `chmod +x` s'il n'est pas dans une app (`/apps/<app>.app/main` l'est).
- Commiter seulement ses chemins (`git commit -- <fichiers>`) : d'autres sessions travaillent dans le même dépôt.
