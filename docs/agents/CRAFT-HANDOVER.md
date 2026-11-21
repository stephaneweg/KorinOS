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

## Fait (session du cloud, 20-21/11/2026)

Les **17 apps sont construites et s'ouvrent sous QEMU PC** ; le lanceur s'appelle **Suite ArtCraft** ; les paquets
sont décrits (`craft` + un par app qui en dépend, `tools/pkg/packages-pc.ini`) et vérifiés par `mkrepo.py --no-sign`
(archives de 11 à 82 Mo). L'état app par app et ce qui a été patché : [../CRAFT.md](../CRAFT.md).

## Ce qui reste

1. **Publier** (sur le PC de l'utilisateur : la clé de signature et `gh` pour la Release) : `pc` d'abord (le panel et
   le menubar qui cachent la catégorie « Craft » sont postérieurs à `pc` 2026.11.64), puis `craft` avec `needs = pc >=`
   la nouvelle version, puis les 17 apps (`release = 1`). Il faut l'arbre : `bash tools/craft/all.sh` (WSL), puis
   `sh tools/pc/sysroot.sh` et `sh tools/pc/sysroot_craft.sh`, et `pub.py --new` app par app (docs/PAQUETS.md).
2. **Barre de titre** : les apps réservent la place des boutons de fenêtre de Windows ou de macOS ; sur Korin leurs
   boutons de droite passent sous ceux de la fenêtre. Voir `craft-window` (craft-libs) : `policy` (« Use system title
   bar », `CRAFT_TITLEBAR=system`) et `title_bar.rs` ; une variante Korin dans `korin_libs.py` (titre à gauche contre
   l'icône, réserve à droite).
3. **Menus dans la barre de menus de Korin** : `craft-menu` (craft-libs) décrit les menus en arbre et les confie à un
   backend (macOS : muda). Un backend Korin sur `ui_menu`, `ui_menu_get`, `ui_menu_command`, `ui_menu_state`,
   `ui_menu_key` (UIKit). FigCraft, PdfCraft, MaterialCraft, TextureCraft le prennent directement.
4. **`File::lock`** dans la std de Korin (libposix a `flock`) : LightCraft (photothèque), la récupération de
   PdfCraft et de DesignCraft.
5. **Le son** : un hôte `cpal` pour Korin (sur AudioKit) : SoundCraft, FilmCraft, EffectCraft.
6. Essayer chaque app au-delà de l'ouverture (ouvrir, modifier, enregistrer).

## Essayer sous QEMU PC

```sh
python3 -I tools/craft/pctest.py prep --mb 2400       # --quick : garder la copie de l'arbre
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
- La place : une session du cloud a ≈ 70 Go ; les dossiers `target` des apps font de 1 à 3,3 Go chacun (≈ 30 Go en
  tout). Les effacer quand une construction s'arrête sur « No space left on device » (les programmes sont gardés dans
  `build/craft/x86_64/apps`).
- Un `[patch]` de version plus haute que celle du `Cargo.lock` n'est pas pris (« patch was not used ») : la copie 0.36
  d'eframe prend la version du lock de l'app (`egui036.py`).
- `korin_cfg.py` ne reconnaît pas tous les replis : `not(any(windows, unix))` (FigCraft) ou `not(any(target_os =
  "macos", windows, unix))` (SoundCraft) se retouchent app par app dans `korin_app.py`.
- Sous QEMU sans KVM (cloud), suspendre la construction pendant un essai (`kill -STOP`/`-CONT` de cargo et rustc) ;
  l'arbre du PC a besoin de bash (`ARCH=x86_64 sh tools/bash/build.sh`) pour les commandes `… &` de `pctest.py`.
