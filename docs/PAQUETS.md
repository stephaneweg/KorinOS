# Les paquets d'ARMPI

Port du gestionnaire de paquets d'Onyx (`docs/pkg/README.md` là-bas) : la commande **`pkg`**, l'app
**Logiciels** (`pkgman`, aussi une entrée de Réglages), le service **`pkgd`**, et côté PC **`tools/pkg/`**.

| Où | Quoi |
|---|---|
| `src/lib/pkg/pkglib.h` | la bibliothèque commune : index, signature, téléchargement, installation, retrait, commit |
| `src/bin/pkg/main.cpp` | la commande → `SD0:/bin/pkg` |
| `src/apps/pkgman/` | Logiciels, sur UIKit → `SD0:/apps/pkgman.app` |
| `src/sys/pkgd/main.cpp` | le service → `SD0:/sys/pkgd`, lancé par `SD0:/etc/session` |
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
icons/<nom>.bmp        (si l'app en a une ; Logiciels dessine les icônes par ui_icone, d'après icon.rgba du paquet installé)
```

`pkg` refuse un index sans bonne signature, ou dont le `board` n'est pas `armpi`. Le dépôt peut aussi être **un
dossier** (`repo = SD0:/repo`, ou `pkg -r SD0:/repo ...`) : les essais, une carte sans réseau.

`SD0:/etc/pkg/` : `onyx.pub` (la clé publique), `pkg.ini` (`repo =`, `key =`, `check = daily|never`), `abi` (ci-dessous).

**La confiance** : l'index est signé par une clé gardée hors des dépôts ; chaque archive a sa taille et son SHA-256
dans l'index signé. L'archive est téléchargée dans `RAM0:/pkg` (à défaut `SD0:/var/pkg/dl`), vérifiée, et seulement
alors écrite sur la carte. Le certificat HTTPS est vérifié par `/etc/ca-bundle`.

## Un paquet

Un `.opk` est un ZIP dont l'arborescence est celle de `SD0:`, plus `PKG/manifest.ini` (`name`, `title`, `version`,
`category`, `author`, `summary`, `needs`, `required`, `restart`, `installed`, `config`, `replaces`).

- **`needs`** : d'autres paquets (`fmtracker`, `koton >= 1.2`), et `abi >= N`.
- **Gros paquets** : GitHub refuse un fichier de plus de 100 Mo. `split = MOTIF MO` dans `packages.ini` met les
  fichiers qui suivent le motif dans des paquets `<nom>-data-1`, `-2`… de moins de MO Mo, que le paquet demande
  par `needs` : `pkg` et Logiciels les installent avec lui (SuperTux, SuperTuxKart).
- **`abi`** remplace le `kapi` d'Onyx : la somme des tables de tous les kits (`kit_fn` des `.abi`, en ajout seul :
  elle ne fait que monter), calculée par `mkrepo.py` et écrite dans `SD0:/etc/pkg/abi` par le paquet `armpi`. Une app
  construite contre des tables plus grandes attend donc le système et son redémarrage. Un paquet étagé n'a pas ce
  besoin : il s'installe sur l'ancien système.
- **`config`** : les fichiers de l'utilisateur. Écrits s'ils manquent ou s'ils sont tels que le paquet les a laissés ;
  sinon le nouveau va à côté en `<fichier>.new`. Tout `home/` est traité plus strictement : rien n'y est écrasé,
  même pas de `.new`.
- **Jamais dans un paquet** (refusé par `mkrepo.py`, `checkrepo.py` et à l'installation) : `var/`, `etc/wifi*`, un nom
  avec `..`, `:`, `\` ou commençant par `/`. Rien n'est écrit hors de `SD0:` ; `SD1:` (UserFS) n'est jamais touché.
- L'installation écrit `<fichier>.pkgtmp` puis renomme (`rename` de fatfs remplace la cible). Un programme lancé ne
  craint rien : son image est en mémoire. Un programme épinglé (`etc/preload.txt`) : vfs oublie son image quand le
  fichier change, `pkglib` le recharge.

La base : `SD0:/var/pkg/db/<nom>.ini` (`[package]`, le `mode`, `[files]` : chaque fichier et son SHA-256). Elle est
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

Une app ajoutée ou retirée relance `panel` et `menubar`, qui lisent `SD0:/apps` à leur départ.

## pkgd

Sans fenêtre, lancé par `etc/session` (`build.ps1 sd` ajoute la ligne aux cartes existantes). Quand le réseau et
l'heure sont là, une fois par jour (`SD0:/var/pkg/lastcheck`) : l'index, les paquets `auto` mis à jour, une notification
pour le reste. `pkgd --once` : un tour tout de suite. `check = never` dans `pkg.ini` : il s'arrête.

## Le système et le redémarrage

`restart = 1` : `armpi` (noyaux `kernel8.img`, `kernel_2712.img`, modules `*.mod`, `config.txt`, kits, Elegant, `sys`,
services, `bin`, `etc`, polices, apps de base) et `pi-firmware` (firmware du Pi, `firmware/` du Wi-Fi). L'archive vérifiée
est extraite dans `SD0:/var/pkg/stage/<nom>/` avec `<nom>.ini`. Pourquoi : un kit chargé reste en mémoire, et ce que
lit le firmware ne repart qu'au démarrage.

Au démarrage, **init2**, avant hwmanager et les services, voit `SD0:/var/pkg/stage` : il lance `SD0:/bin/pkg commit` et
continue de servir ses demandes (kits, redémarrage) jusqu'à sa fin, 5 minutes au plus. `pkg commit` déplace les
fichiers par `rename`, garde l'ancien noyau en `kernel8.img.old` / `kernel_2712.img.old`, enregistre le paquet dans la
base, note ce qu'il a fait dans `SD0:/var/pkg/commit.log`. Si un fichier lu par le firmware ou `init2` a changé (les
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
`tools/pkg/versions.ini` ; **pousse seulement avec `--push`**. Sans clé, rien. Avec une clé qui n'est pas celle des
cartes, arrêt avant le commit. `KORIN_REPO_DIR` désigne un autre clone de KorinOS, mis à jour avant.

Depuis une machine qui n'a pas construit les gros paquets de WSL (WebKit, .NET, bureautique...) : `mkrepo.py
--keep-unbuilt` garde tels quels, entrée de l'index et archive, les paquets déjà publiés qui ne sont pas construits ici.

**Publier quelques paquets seulement** (fait le 13/11/2026 pour l'étape 10 de Python, [PYTHON-PORT.md](PYTHON-PORT.md)
« Étape 10 ») : ne pas partir d'un `build/sdroot` entier quand il ne vient pas de la même construction que le dépôt
(une carte d'essai a d'autres `etc/` que la carte de première installation, `#setup:`, et peut manquer de fichiers
faits ailleurs : `lib/glkit.so`, les `.bi` du BASIC, `sys/setup`). On monte plutôt un arbre `build/sdroot_pub` qui ne
contient que les paquets à republier : chacun **extrait de son archive publiée**, puis **les seuls fichiers que les
sources ont changés** pris de la construction (comparer d'abord, fichier par fichier, l'archive publiée et la
construction). Puis :

```sh
python tools/pkg/mkrepo.py --sd build/sdroot_pub --out packages --key "$ONYX_PKG_KEY_FILE" --keep-unbuilt --bump
python tools/pkg/checkrepo.py packages build/sdroot_pub/etc/pkg/onyx.pub
```

Un paquet dont un besoin n'est construit que là (pas gardé) est sauté : y mettre aussi ce besoin, tel que publié
(`rg` pour `vibe`). Un essai à blanc d'abord : la même commande avec `--no-sign`, `--out` et `--versions` vers une
copie ; la ligne « montés » doit nommer exactement les paquets voulus.

`mkrepo.py` affiche la taille de chaque paquet et le total. GitHub refuse un fichier de plus de 100 Mo : un tel paquet
est **retiré de l'index** et signalé (à découper ou alléger). `packages.ini` : une section par paquet ; `[*apps]` fait un
paquet de chaque app restante, `root =` prend les fichiers d'un autre arbre s'ils ne sont pas sur la carte (`gcc`,
`dotnet`) ; un paquet sans fichier (pas construit) est sauté.

**La clé publique des cartes** : `skel/sd0/etc/pkg/onyx.pub`, portée par le paquet `armpi`. Elle n'est pas dans git.

## Ce qui diffère d'Onyx

- Pas de `pkg assoc` ni `fileassoc.ini` / `runners.ini` : ARMPI lit `opens =` dans `app.txt`.
- Pas de carte « lite » ni d'adoption d'une carte sans base (`build.ps1` écrit la base).
- `abi` au lieu de `kapi`, `board = armpi` obligatoire.
- Pas de tryboot (Onyx non plus, en pratique) ; `init2` lance le commit, pas `etc/autostart`.
- Le téléchargement va directement dans un fichier, haché au passage : la mémoire ne limite pas la taille d'un paquet.
- Le bouton de la notification n'ouvre pas Logiciels : notifyd d'ARMPI n'a pas d'action.
