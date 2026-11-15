# Les paquets d'ARMPI

Port du gestionnaire de paquets d'Onyx (`docs/pkg/README.md` là-bas) : la commande **`pkg`**, l'app
**Logiciels** (`pkgman`, aussi une entrée de Réglages), le service **`pkgd`**, et côté PC **`tools/pkg/`**.

| Où | Quoi |
|---|---|
| `src/lib/pkg/pkglib.h` | la bibliothèque commune : index, signature, téléchargement, installation, retrait, commit |
| `src/bin/pkg/main.cpp` | la commande → `/bin/pkg` |
| `src/apps/pkgman/` | Logiciels, sur UIKit → `/apps/pkgman.app` |
| `src/sys/pkgd/main.cpp` | le service → `/sys/pkgd`, lancé par `/etc/session` |
| `src/svc/init2` | `commit_boot` : `pkg commit` au démarrage, avant tout le reste |
| `src/apps/control/applets/pkgman.inc` | l'entrée Logiciels de Réglages : lance `pkgman` |
| `tools/pkg/build.sh` | construit les trois programmes (WSL) dans `build/office/sdroot` |
| `tools/pkg/mkrepo.py`, `packages.ini`, `versions.ini` | fabriquent le dépôt |
| `tools/pkg/checkrepo.py`, `publish.sh` | vérifient et publient |

Du C++ sur libposix, hors du selfhost comme Mail : **OpenSSL** pour TLS, SHA-256 et la signature (le seul TLS de
l'OS), **ArchiveKit** pour les ZIP, `ui_*` pour la fenêtre. Pas d'onyxcompat.

## Le dépôt

`https://stephaneweg.github.io/KorinOS/packages` : le dossier `packages` de `stephaneweg/KorinOS`,
mêmes formats qu'Onyx :

```
index.txt              [repo] (date, abi, board = armpi), puis une section par paquet
index.sig              signature ECDSA P-256 / SHA-256 de index.txt, DER en hexa
pkgs/<nom>-<version>.opk
icons/<nom>.bmp        (si l'app en a une ; Logiciels dessine les icônes par ui_icon, d'après icon.rgba du paquet installé)
```

`pkg` refuse un index sans bonne signature, ou dont le `board` n'est pas `armpi`. Le dépôt peut aussi être **un
dossier** (`repo = /repo`, ou `pkg -r /repo ...`) : les essais, une carte sans réseau.

`/etc/pkg/` : `onyx.pub` (la clé publique), `pkg.ini` (`repo =`, `key =`, `check = daily|never`), `abi` (ci-dessous).

**La confiance** : l'index est signé par une clé gardée hors des dépôts ; chaque archive a sa taille et son SHA-256
dans l'index signé. L'archive est téléchargée dans `/tmp/pkg` (à défaut `/var/pkg/dl`), vérifiée, et seulement
alors écrite sur la carte. Le certificat HTTPS est vérifié par `/etc/ca-bundle`.

## Un paquet

Un `.opk` est un ZIP dont l'arborescence est celle de `/`, plus `PKG/manifest.ini` (`name`, `title`, `version`,
`category`, `author`, `summary`, `needs`, `required`, `restart`, `installed`, `config`, `boot`, `replaces`).

- **`needs`** : d'autres paquets (`fmtracker`, `koton >= 1.2`), et `abi >= N`.
- **Gros paquets** : GitHub refuse un fichier de plus de 100 Mo. `split = MOTIF MO` dans `packages.ini` met les
  fichiers qui suivent le motif dans des paquets `<nom>-data-1`, `-2`… de moins de MO Mo, que le paquet demande
  par `needs` : `pkg` et Logiciels les installent avec lui (SuperTux, SuperTuxKart).
- **`abi`** remplace le `kapi` d'Onyx : la somme des tables de tous les kits (`kit_fn` des `.abi`, en ajout seul :
  elle ne fait que monter), calculée par `mkrepo.py` et écrite dans `/etc/pkg/abi` par le paquet `armpi`. Une app
  construite contre des tables plus grandes attend donc le système et son redémarrage. Un paquet étagé n'a pas ce
  besoin : il s'installe sur l'ancien système.
- **`config`** : les fichiers de l'utilisateur. Écrits s'ils manquent ou s'ils sont tels que le paquet les a laissés ;
  sinon le nouveau va à côté en `<fichier>.new`. Tout `home/` est traité plus strictement : rien n'y est écrasé,
  même pas de `.new`.
- **`boot`** (12/11/2026) : les fichiers de démarrage, des motifs comme `files =` (`kernel8.img kernel_2712.img *.mod
  config.txt` pour `armpi`, `start4.elf fixup4.dat *.dtb overlays/` pour `pi-firmware` ; sur PC, plus tard,
  `EFI/BOOT/BOOTX64.EFI korin/`). Dans l'archive ils restent à la racine (un `pkg` plus ancien les met donc à la
  racine de `/`, comme avant) ; `pkg` les écrit dans **`/boot/`**, la vraie racine de la partition de démarrage,
  et le reste dans `/` (qui peut avoir un rootpath : `/korinARM`). `/boot` est monté à la demande par vfs le
  temps d'une installation, d'un retrait ou d'un commit (`VFS_BOOT_BEGIN` / `VFS_BOOT_END`, d'après la ligne
  `BOOT = …, ondemand` de `/etc/mounts.txt`), puis démonté ; sans cette ligne, `/boot` est `/`
  ([FICHIERS.md](FICHIERS.md), 4.4). La clé va dans le manifeste et dans la base (`boot = …` de `[package]`) ; elle
  compte dans le contenu du paquet (une nouvelle clé : une nouvelle version). `firmware/` (le Wi-Fi, lu par le
  service wifi à chaque démarrage) n'en est pas : il reste dans `/`.
- **Jamais dans un paquet** (refusé par `mkrepo.py`, `checkrepo.py` et à l'installation) : `var/`, `etc/wifi*`, un nom
  avec `..`, `:`, `\` ou commençant par `/`. Rien n'est écrit hors de `/` et `/boot` ; `/media/sd1` (UserFS) n'est jamais
  touché.
- L'installation écrit `<fichier>.pkgtmp` puis renomme (`rename` de fatfs remplace la cible). Un programme lancé ne
  craint rien : son image est en mémoire. Un programme épinglé (`etc/preload.txt`) : vfs oublie son image quand le
  fichier change, `pkglib` le recharge.

La base : `/var/pkg/db/<nom>.ini` (`[package]`, le `mode`, `[files]` : chaque fichier et son SHA-256). Elle est
générée à la construction (`mkrepo.py --db-only`, appelé par `build.ps1` : `build/sdroot/var/pkg/db`), jamais à la main.

Les modes : `manual` (par défaut), `auto` (pkgd met à jour), `never`.

## La commande

```
pkg list [-a] [filtre]       installés (-a : tout le dépôt)
pkg info NOM
pkg add NOM... | FICHIER.opk
pkg delete [-p] NOM...       -p : les réglages changés aussi (jamais home/)
pkg update NOM... | -a
pkg upgrade                  un tour de pkgd
pkg check                    relit l'index
pkg mode NOM manual|auto|never
pkg commit                   les paquets étagés
-r DEPOT                     avant la commande : un autre dépôt
```

Une app qui tourne n'est pas mise à jour ni retirée : `2048 tourne : le fermer, puis recommencer`.

## Logiciels (pkgman)

Les trois onglets des maquettes d'Onyx : **Mises à jour** (une case par paquet, `redémarrage` sur le système),
**Installés** (Manuel / Auto / Jamais, Retirer), **Disponibles** (Installer). La recherche filtre les trois. Le travail
tourne dans un thread, la progression s'affiche sur la ligne et au pied de la fenêtre. Quand un paquet est étagé, un
bandeau propose de redémarrer. Au toucher : glisser fait défiler, les lignes font 58 px. Une seule instance. S'il se
met à jour lui-même, il se relance, et pkgd avec lui.

Une app ajoutée ou retirée relance `panel` et `menubar`, qui lisent `/apps` à leur départ.

## pkgd

Sans fenêtre, lancé par `etc/session` (`build.ps1 sd` ajoute la ligne aux cartes existantes). Quand le réseau et
l'heure sont là, une fois par jour (`/var/pkg/lastcheck`) : l'index, les paquets `auto` mis à jour, une notification
pour le reste. `pkgd --once` : un tour tout de suite. `check = never` dans `pkg.ini` : il s'arrête.

## Le système et le redémarrage

`restart = 1` : `armpi` (noyaux `kernel8.img`, `kernel_2712.img`, modules `*.mod`, `config.txt`, kits, Elegant, `sys`,
services, `bin`, `etc`, polices, apps de base) et `pi-firmware` (firmware du Pi, `firmware/` du Wi-Fi). L'archive vérifiée
est extraite dans `/var/pkg/stage/<nom>/` avec `<nom>.ini`. Pourquoi : un kit chargé reste en mémoire, et ce que
lit le firmware ne repart qu'au démarrage.

Au démarrage, **init2**, avant hwmanager et les services, voit `/var/pkg/stage` : il lance `/bin/pkg commit` et
continue de servir ses demandes (kits, redémarrage) jusqu'à sa fin, 5 minutes au plus. `pkg commit` déplace les
fichiers par `rename` (de `/var/pkg/stage` vers `/boot/` pour les fichiers `boot =` : permis entre deux
volumes d'un même service, sinon libposix recopie), garde l'ancien noyau en `kernel8.img.old` / `kernel_2712.img.old`, enregistre le paquet dans la
base, note ce qu'il a fait dans `/var/pkg/commit.log`. Si un fichier lu par le firmware ou `init2` a changé (les
fichiers à la racine de la carte), il redémarre le Pi. Un commit interrompu reprend au démarrage suivant. pkg absent
ou en panne : le système démarre, les paquets restent étagés.

Pas de tryboot pour l'instant : si le nouveau noyau ne démarre pas, il faut remettre `kernel8.img.old` à la main depuis
le PC.

## Le PC

```sh
sh tools/pkg/build.sh                         # WSL : pkg, pkgd, pkgman dans build/office/sdroot
.\build.ps1                                   # build/sdroot, build/modules, build/sdroot/var/pkg/db
ONYX_PKG_KEY_FILE=/chemin/cle.pem sh tools/pkg/publish.sh [--push]
```

`publish.sh` : `build/pkgboot` (noyaux 800x600 comme `build.ps1 sd`, modules, firmware, `config.txt`) ;
`mkrepo.py --bump --db` dans `packages/` de ce dépôt (GitHub Pages le sert) ; `checkrepo.py` (signature avec la clé des
cartes `skel/sd0/etc/pkg/onyx.pub`, chaque archive, chaque nom, chaque besoin) ; un commit de `packages/` et de
`tools/pkg/versions.ini` ; **pousse seulement avec `--push`**. Sans `ONYX_PKG_KEY_FILE`, la clé est prise dans la
variable `OnyxPkgKey` (la clé PEM elle-même, agents du cloud) ; sans clé, rien. Avec une clé qui n'est pas celle des
cartes, arrêt avant le commit. `KORIN_REPO_DIR` désigne un autre clone de KorinOS, mis à jour avant.

Depuis une machine qui n'a pas construit les gros paquets de WSL (WebKit, .NET, bureautique...) : `mkrepo.py
--keep-unbuilt` garde tels quels, entrée de l'index et archive, les paquets déjà publiés qui ne sont pas construits ici.

`mkrepo.py` affiche la taille de chaque paquet et le total. GitHub refuse un fichier de plus de 100 Mo : un tel paquet
est **retiré de l'index** et signalé (à découper ou alléger). `packages.ini` : une section par paquet ; `[*apps]` fait un
paquet de chaque app restante, `root =` prend les fichiers d'un autre arbre s'ils ne sont pas sur la carte (`gcc`,
`dotnet`) ; un paquet sans fichier (pas construit) est sauté.

Un volet de Réglages peut dépendre d'un paquet : le volet .NET n'est dans la liste que si `/devtools/dotnet/dllpreload`
(paquet `dotnet`) est là (`Applet.needs`, `src/apps/control/main.cpp`) ; sans le paquet, pas de volet .NET.

Un paquet peut aussi **apporter son volet** sans toucher à Réglages : un module `/sys/applets/<nom>.so` que
Réglages découvre au démarrage (`src/apps/control/control.h`, « les applets installées » ; [WEB.md](WEB.md) §4). Le
paquet `webd` (le serveur web) pose ainsi le volet Serveur web.

**La clé publique des cartes** : `skel/sd0/etc/pkg/onyx.pub`, portée par le paquet `armpi`. Elle n'est pas dans git.

## Korin pour PC (x86-64)

Décision du 12/11 (`docs/etude/PC-PILOTES.md`, « Dépôt de paquets du PC »), publié le 14/11 : un index à part,
signé par la même clé, dans **`packages/pc/`** (`https://stephaneweg.github.io/KorinOS/packages/pc`) :
`index.txt` (`board = pc`), `index.sig`, `pkgs/*.opk`. L'index du Pi (`packages/index.txt`, `board = armpi`) ne change
pas. Les mêmes outils, avec **`ARCH=x86_64`** (ou `BOARD=pc`) dans l'environnement ; sans la variable, le Pi, octet pour
octet comme avant (vérifié : même index, mêmes archives).

| | Pi | PC |
|---|---|---|
| carte construite | `build/sdroot` (`build.ps1`) | `build/pc/sysroot` (`tools/pc/sysroot*.sh`) |
| paquets | `tools/pkg/packages.ini` | `tools/pkg/packages-pc.ini` |
| versions | `versions.ini`, `[versions]` | `[pc]` d'abord (ce qui diffère du Pi), puis `[versions]` |
| système | `armpi` | `pc` : `EFI/Korin/korin.efi`, `korin/kernel.elf`, `korin/initrd.tar` (`boot =`, aux places de `korin.ps1 install` ; jamais `korin/boot.cfg`), `sys/`, `lib/`, `bin/`, `etc/`, `res/`, `fonts/`, les apps de base |
| Release | `build/pkg-release`, `<nom>-<version>.opk` | `build/pkg-release-pc`, `pc-<nom>-<version>.opk` |
| programmes | `tools/pkg/build.sh` (aarch64) | `ARCH=x86_64 sh tools/pkg/build.sh` → `tools/pc/apps.sh pkg pkgd pkgman` |

- **Paquets sans code machine** : `mkrepo.py --any packages/index.txt` compare chaque paquet du PC à celui du Pi de
  même nom (`content` : mêmes fichiers, mêmes besoins). Égaux, l'archive n'est pas refaite : l'index du PC cite celle
  du Pi, `file = ../pkgs/<nom>-<version>.opk`, avec sa taille, son SHA-256, sa version, et `arch = any`
  (SoundFonts, exemples de FM Tracker, de Koton, d'Arabesque). `pkglib` résout `../` lui-même (le serveur ne voit
  jamais `..`) et refuse tout autre `..`.
- **Jamais dans un paquet du PC** (en plus des noms du Pi) : `etc/options.txt`, `etc/pointer.ini`, `korin/boot.cfg`,
  `korin/installed.txt`, `.vibe/.env`, `.vibe/config.toml`, `acpi/` (`mkrepo.py` et `checkrepo.py`).
- **Gros paquets** (`release = 1`) : `jet`, `python`, `vibe` (+ `-data-1..2`), `dotnet` (+ `-data-1..2`), `avalonia`,
  `gcc` (+ `-data-1`). Le reste, environ 60 Mo, va sur GitHub Pages (site sous 1 Go : 618 Mo avant le PC).
- `pkg`, `pkgd`, Logiciels du PC : `PKG_REPO` et `PKG_BOARD` de `pkglib.h` selon `__x86_64__` ; `/etc/pkg/pkg.ini`
  vient de `skel/pc/etc/pkg/pkg.ini` (`…/packages/pc`), `onyx.pub` de `skel/sd0/etc/pkg/` ; `pkgd` dans
  `skel/pc/etc/session`. `pkg commit` garde l'ancien noyau en `korin/kernel.elf.old`.
- **La base** : `ARCH=x86_64 mkrepo.py --db` écrit `build/pc/sysroot/var/pkg/db` (sans les paquets de `home/` seul) ;
  `tools/pc/korin.ps1 install` copie `var\pkg\db` avec l'arbre : pkg et Logiciels savent ce qui est installé.

```sh
wsl sh tools/pc/build.sh … sysroot_gcc.sh             # l'arbre du PC (verrou build/pc.lock)
ARCH=x86_64 KORIN_REPO_DIR=../clone-de-main ONYX_PKG_KEY_FILE=/chemin/cle.pem sh tools/pkg/publish.sh [--push]
```

`publish.sh` en mode PC : `build/pkgboot-pc`, `mkrepo.py --bump --db --any`, `checkrepo.py` (avec `ARCH=x86_64` :
`board = pc`, archives `../pkgs/` lues à côté, celles de la Release dans `build/pkg-release-pc`), un commit de
`packages/pc/` seul. Essai : une image `tools/pc/mkkorindisk.py --layout zbook --korin-mb 700 --sysroot …` en NVMe sous
QEMU q35, un index d'essai servi en local (adresses de la Release remplacées, re-signé) ; `pkg -r URL add`, `update pc`,
redémarrage, `pkg commit` par init2, second redémarrage (le noyau a changé).

**Mettre à jour le ZBook** : une fois `korin.ps1 install` fait avec un arbre qui a `bin/pkg` et `var/pkg/db` (à partir
du 14/11), `pkg check`, `pkg update -a` (ou `pkg update pc`, puis les apps), redémarrer pour le système (deux
redémarrages : le commit, puis le nouveau noyau) ; ou Logiciels, onglet Mises à jour. `pkg add jet python vibe …`
installe ce qui manque.

## Ce qui diffère d'Onyx

- Pas de `pkg assoc` ni `fileassoc.ini` / `runners.ini` : ARMPI lit `opens =` dans `app.txt`.
- Pas de carte « lite » ni d'adoption d'une carte sans base (`build.ps1` écrit la base).
- `abi` au lieu de `kapi`, `board = armpi` obligatoire.
- Pas de tryboot (Onyx non plus, en pratique) ; `init2` lance le commit, pas `etc/autostart`.
- Le téléchargement va directement dans un fichier, haché au passage : la mémoire ne limite pas la taille d'un paquet.
- Le bouton de la notification n'ouvre pas Logiciels : notifyd d'ARMPI n'a pas d'action.
