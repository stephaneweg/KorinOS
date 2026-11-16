---
name: korin-sys-files
description: Gérer les fichiers et les volumes de Korin OS — lister les volumes et leurs points de montage, place libre, monter et éjecter une clé USB, formater, ouvrir un fichier avec son application, associations de fichiers, archives. À charger pour toute question de disque, de clé USB ou de fichiers en dehors d'un projet de code.
---

# Fichiers et volumes

L'arbre des chemins est décrit dans `korin-system` : `/` est le volume système, chaque autre volume est monté dans un
dossier (`/tmp`, `/media/usb0`, `/net/net0`…). Référence : `docs/FICHIERS.md` et `docs/OUTILS.md`.

## Volumes

| Commande | Effet |
|---|---|
| `mount` | les volumes montés : point de montage, nom, système de fichiers, taille, place libre, étiquette, source, service |
| `df [point-nom-ou-chemin…]` | la place des volumes ; avec un chemin, celle du volume qui le porte |
| `mount POINT` (ou `mount usb0`) | remonte un volume (une clé éjectée mais encore branchée, une ligne de `/etc/mounts.txt`) |
| `mount POINT SOURCE SERVICE [ROOTPATH]` | monte une source dans un dossier (`mount /media/img2 blk.img2p1 fatfs`) |
| `eject [-f] [/media/usb0 ou usb0]` | écrit ce qui reste puis démonte la clé ; refuse si des fichiers sont ouverts, sauf `-f` |

Les clés USB se montent d'elles-mêmes au branchement (`/media/usb0`, `/media/usb1`…). Seules les clés s'éjectent, jamais la
carte. Une commande accepte le point de montage (`/media/usb0`) ou le nom du volume (`usb0`) ; le volume système
s'appelle `root`. Les montages permanents sont dans `/etc/mounts.txt`, une ligne `POINT = source, service[, rootpath]`
(`/tmp = ram:1024M, ramfs`). L'app **Disques** (`disks`) fait la même chose en fenêtres.

## Formater

`mkfs [-t fat32] [-L ÉTIQUETTE] [-c octets] [-y] [-f] [--card] POINT` efface tout et écrit un FAT32 neuf.
- Sans `-y`, la commande demande de taper le nom du volume (`usb0`).
- `--card` est nécessaire pour une partition de la carte. `/` ne se formate jamais.
- **Ne jamais formater `/media/sd1`** (sur le Pi) : ce sont les données personnelles de l'utilisateur.
- **Toujours demander l'accord** de l'utilisateur, en nommant le volume et sa taille (`df /media/usb0`).

## Ouvrir un fichier

- Pour ouvrir un fichier avec une app précise, lancez-la avec le chemin : `notes HOME:/notes/a.txt`, `pdf /tmp/x.pdf`.
- L'app associée à chaque extension est dans `/etc/fileassoc.ini`. Une app déclare les siennes dans la ligne
  `opens =` de son `app.txt`.
- Les apps graphiques le font par UIKit : `ui_assoc_open(path)`, `ui_open_with(path)` (voir `korin-kit-uikit`).
- Pour trouver l'app d'une extension, lisez `/etc/fileassoc.ini`, puis les `opens =` de
  `/apps/*.app/app.txt`.

## Archives

- `zip`, `unzip` et `arc` en ligne de commande.
- En C, ArchiveKit (`korin-kit-archivekit`) lit et écrit deflate, gzip, ZIP et tar.
- L'app **Archiveur** (`archiver`) le fait en fenêtres.

## Pièges

- 64 fichiers ouverts au plus par process.
- `rename` d'un dossier d'un volume à l'autre (de `/tmp` vers `/home`, vers une clé) rend `EXDEV` : copier puis effacer.
- Sur FAT32 (`/`) : pas de fichier de plus de 4 Go, et la casse des noms n'est pas distinguée.
- `/tmp` est vidé à l'extinction : n'y laissez rien que l'utilisateur veut garder.
- Exécutables : tout ce qui est sous `/bin`, `/sys`, `apps/*.app/main` et dans les dossiers de
  `/etc/exec.txt` est exécutable. Ailleurs, il faut `chmod +x`, et `cp` perd ce bit.
