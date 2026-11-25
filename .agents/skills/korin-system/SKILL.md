---
name: korin-system
description: Les bases pour agir sur Korin OS depuis son shell — l'arbre unique des chemins (/, /tmp, /media/…, HOME:/), bash et sh, les commandes de /bin et leurs limites, règles de prudence. À charger avant toute commande lancée sur Korin ; les compétences korin-sys-* détaillent fichiers, process, réseau, réglages et paquets.
---

# Se servir de Korin OS

Korin n'est pas Linux. Vérifiez une commande ou une option avant de compter dessus : essayez-la sur un fichier de
`/tmp`.

Pour aller plus loin :
- `korin-sys-files` : volumes et points de montage, clés USB, place libre, formatage.
- `korin-sys-processes` : lancer, lister, arrêter, services, journal, redémarrage.
- `korin-sys-network` : Wi-Fi, IP, DNS, transferts.
- `korin-sys-settings` : thème, son, langue, mode du bureau, bus, notifications, captures.
- `korin-sys-packages` : installer et mettre à jour des logiciels.

**Se servir d'une app** (écrire un fichier qu'elle ouvre, la piloter) : une app peut apporter sa propre compétence,
`korin-<app>`. Son `app.txt` a alors la clé `skill = SKILL.md`, et le fichier est à côté, dans `/apps/<app>.app/`
(`grep -l '^skill' /apps/*.app/app.txt` les liste). Lisez-la avant les sources de l'app. Sans compétence ni serveur
MCP, lisez d'abord ses exemples (`/home/public/<app>`).

## L'arbre des chemins

Un seul arbre, comme sous Unix : `/` est le volume système, les autres volumes sont montés dans des dossiers. Il n'y
a plus de préfixe de volume (`SD0:`, `RAM0:`, `USB0:`… sont refusés) ; `/x` est toujours absolu.

| Dossier | Système de fichiers | Rôle |
|---|---|---|
| `/` | FAT32 | le système : `bin`, `apps`, `lib`, `etc`, `sys`, `home`, `devtools` |
| `/devtools` | (sur `/`) | les outils de développement : `gcc`, `dotnet`, `python` |
| `/home` | (sur `/`) | le dossier personnel de l'utilisateur, aussi nommé `HOME:/` |
| `/tmp` | ramfs (1 Go) | temporaire, perdu à l'extinction |
| `/media/usb0`, `/media/usb1`… | selon la clé | les clés USB, montées au branchement |
| `/media/sd1` | exFAT | sur le Pi, **les données personnelles de l'utilisateur : ne jamais y écrire, formater ni « réparer »** |
| `/net/net0`… | smbfs | les partages réseau |
| `/boot` | FAT32 | la partition de démarrage, montée seulement le temps d'y écrire |

**`HOME:/`** est le dossier personnel de l'utilisateur en cours (aujourd'hui `/home`, plus tard
`/home/<nom>`) ; deux autres alias : **`PUBLIC:/`** (`/home/public`, partagé) et **`APPS:/`** (`/apps`). Les apps y rangent les documents (`HOME:/documents`…). `getcwd` et `pwd` rendent des chemins
ordinaires (`/home/x`, `/bin`). Les montages sont dans `/etc/mounts.txt` (`/tmp = ram:1024M, ramfs`).

**Environnement** : `HOME=/home`, `PATH=/bin:/devtools/gcc/bin`, `TMPDIR=/tmp`, `TERM=xterm-256color`.

**Limites des fichiers :**
- environ 254 octets par chemin, en tout ;
- 63 octets au plus par nom sur `/tmp` ;
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
dans `/devtools/gcc/bin`.

## Prudence

- **Avant d'effacer, d'écraser, de formater, d'installer, de supprimer un paquet ou de redémarrer :** dites ce qui va
  se passer et attendez l'accord de l'utilisateur.
- Travaillez dans `/tmp` ou dans le dossier du projet.
- Ne modifiez `/sys`, `/lib` ni `/etc` que sur demande explicite, et gardez une copie de tout fichier de
  `/etc` que vous modifiez (par exemple `cp /etc/x.txt /tmp/x.txt.bak`).
