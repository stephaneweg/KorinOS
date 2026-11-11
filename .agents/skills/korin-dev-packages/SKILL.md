---
name: korin-dev-packages
description: Faire et publier des paquets de Korin OS depuis le dépôt (sur le PC) — tools/pkg/packages.ini, versions.ini, mkrepo.py, publish.sh, paquets d'apps, gros paquets découpés ou en Release. À charger pour ajouter une app à un paquet, changer une version ou publier ; pour installer sur la machine, voir korin-sys-packages.
---

# Paquets : construire et publier (dépôt)

Référence : `docs/PAQUETS.md`, et l'en-tête de `tools/pkg/packages.ini`.

## Ce qui fait un paquet

- **`tools/pkg/packages.ini` :** une section `[nom]` par paquet, qui prend des fichiers de `build/sdroot/`.
  - `title`, `category`, `summary`, `author`.
  - `files` : un fichier, un `dossier/` (tout ce qu'il contient) ou un motif ; `!motif` laisse ces fichiers à un
    paquet plus loin. **Un fichier va à la première section qui le nomme.**
  - `config` : les fichiers de l'utilisateur, écrits une fois et jamais écrasés s'ils ont changé.
  - `needs = autre >= 1.0` ; la ligne `abi >= N` est ajoutée d'office depuis les tables des kits.
  - `required = 1` : le paquet ne se retire pas. `restart = 1` : il est mis en place au prochain démarrage.
  - `replaces`, `root`, `split = MOTIF MO` (découpe en `<nom>-data-N`), `release = 1` (archives dans la Release
    `packages` de KorinOS, au-delà de la limite de GitHub Pages).
- **`[*apps]`** fait un paquet de chaque app restante (titre et catégorie pris dans son `app.txt`). `[app.<nom>]` lui
  ajoute des extras.
- **`home/`** n'est jamais écrasé. `var/` et `etc/wifi*` ne vont dans aucun paquet.
- **`tools/pkg/versions.ini`** porte les versions : `armpi = AAAA.MM.N` pour le système, `1.0.0` par défaut pour un
  nouveau paquet. **Changer les fichiers d'un paquet sans augmenter sa version est une erreur** de `mkrepo.py` ;
  `--bump` augmente le dernier nombre.

## Publier (PC)

1. `wsl sh tools/pkg/build.sh` : pkg, pkgd et pkgman.
2. Construire ce qui a changé : `wsl sh tools/office/build.sh <app>` pour une app en C ou C++.
3. `.\build.ps1` : prépare `build/sdroot` et la base `var/pkg/db`.
4. `ONYX_PKG_KEY_FILE=<clé.pem> sh tools/pkg/publish.sh [--push]` :
   - il lance `mkrepo.py --bump --db` dans `packages/`, puis `checkrepo.py` (signature, archives, noms, besoins) ;
   - il committe `packages/` et `versions.ini` ;
   - **il ne pousse qu'avec `--push`**.

Sans clé, il ne fait rien ; avec une clé qui n'est pas celle des cartes, il s'arrête avant le commit.

Depuis une machine qui n'a pas construit les gros paquets de WSL (WebKit, .NET…), `mkrepo.py --keep-unbuilt` garde
tels quels ceux déjà publiés.

## Règles

- **Publier est visible de toutes les cartes :** ne lancez `publish.sh --push` que si l'utilisateur le demande, et dites
  quels paquets changent de version.
- **La clé de signature est un secret :** ne la lisez pas et ne la recopiez pas. Demandez son chemin à l'utilisateur.
- **N'augmentez que les paquets dont les fichiers ont changé.** Les commits du dépôt le disent, par exemple
  « Paquets : budget 1.0.2 (…), seul paquet changé ».
