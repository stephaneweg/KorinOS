---
name: korin-system
description: Les bases pour agir sur Korin OS depuis son shell — volumes (SD0:, SD1:, RAM0:), chemins virtuels, bash et sh, les commandes de /bin et leurs limites, règles de prudence. À charger avant toute commande lancée sur Korin ; les compétences korin-sys-* détaillent fichiers, process, réseau, réglages et paquets.
---

# Se servir de Korin OS

Korin n'est pas Linux. Vérifiez une commande ou une option avant de compter dessus : essayez-la sur un fichier de
`/tmp`.

Pour aller plus loin :
- `korin-sys-files` : volumes, montage, place libre, formatage.
- `korin-sys-processes` : lancer, lister, arrêter, services, journal, redémarrage.
- `korin-sys-network` : Wi-Fi, IP, DNS, transferts.
- `korin-sys-settings` : thème, son, langue, mode du bureau, bus, notifications, captures.
- `korin-sys-packages` : installer et mettre à jour des logiciels.

## Volumes et chemins

| Volume | Système de fichiers | Rôle |
|---|---|---|
| `SD0:` | FAT32 | le système : `bin`, `apps`, `gcc`, `lib`, `etc`, `sys` |
| `SD1:` | exFAT | **les données de l'utilisateur : ne jamais formater ni « réparer »** |
| `RAM0:` | ramfs (1 Go) | temporaire, perdu à l'extinction |
| `USB0:`… | selon la clé | branchés à chaud |

**Chemins virtuels** (`SD0:/etc/paths.txt`) :
- `/bin`, `/gcc`, `/apps`, `/lib`, `/etc` et `/dotnet` mènent à `SD0:/…` ;
- `/tmp` mène à `RAM0:/tmp` ;
- tout autre `/x` désigne la racine du **volume courant**.

`getcwd` rend la forme `SD0:/…`, sauf après un `chdir` vers un chemin virtuel.

**Environnement** : `HOME=SD0:/`, `PATH=/bin:/gcc/bin`, `TMPDIR=/tmp`, `TERM=xterm-256color`.

**Limites des fichiers :**
- environ 254 octets par chemin, en tout ;
- 63 octets au plus par nom sur `RAM0:` ;
- une seule date par fichier, précise à 2 s sur FAT ;
- pas de propriétaires, pas de droits sauf le bit x, pas de liens ;
- `cp` ne garde pas le bit x : refaire `chmod +x`.

## Shell

- **bash 5.2** (`/bin/bash`) est le shell du Terminal et de telnet, avec le contrôle des tâches. Il lit
  `/etc/bash.bashrc`. Ne marchent pas : `<(...)`, `[[ =~ ]]`, les locales.
- `/bin/sh` est un petit shell en assembleur : pas de fonctions, pas de heredocs, pas de `trap`, et `&` agit comme
  `;`. C'est lui qui lance les commandes de `make` et les scripts sans `#!`.
- Écrivez donc vos scripts avec `#!/bin/bash` : il n'y a pas de `/usr/bin/env`.
- Un groupe `{ …; }` redirigé, mêlé à des commandes en assembleur, peut perdre sa sortie : préférez un fichier de
  `/tmp`.
- `system()` et `popen()` d'un programme C ne lancent que le premier mot, sans shell : passez par `bash -c`.

## Commandes

**En assembleur, avec peu d'options :** `cat cp mv rm mkdir rmdir ls find grep sed cut tr sort uniq head tail wc diff
tee touch test echo date basename dirname hexdump nl ed zip unzip arc chmod pwd uname sleep`.

**Absents :**
- `awk`, `xargs`, `od`, `expr`, `mkfifo` et `curl` ;
- `sort -k` ;
- les regex `{n,m}` et les références arrière ;
- `wget` et `httpget` ne savent que le HTTP, pas le HTTPS.

Ce qui manque se fait en bash ou dans un petit programme C (`korin-lang-c`).

**Portés, complets :** `bash`, `git` 2.51 (HTTPS compris), `rg` (ripgrep réécrit en C), et `gcc`, `g++`, `make`
dans `/gcc/bin`.

## Prudence

- **Avant d'effacer, d'écraser, de formater, d'installer, de supprimer un paquet ou de redémarrer :** dites ce qui va
  se passer et attendez l'accord de l'utilisateur.
- Travaillez dans `/tmp` ou dans le dossier du projet.
- Ne modifiez `SD0:/sys`, `SD0:/lib` ni `SD0:/etc` que sur demande explicite, et gardez une copie de tout fichier de
  `SD0:/etc` que vous modifiez (par exemple `cp SD0:/etc/x.txt /tmp/x.txt.bak`).
