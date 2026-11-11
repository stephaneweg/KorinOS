---
name: korin-sys-files
description: Gérer les fichiers et les volumes de Korin OS — lister les volumes, place libre, monter et éjecter une clé USB, formater, ouvrir un fichier avec son application, associations de fichiers, archives. À charger pour toute question de disque, de clé USB ou de fichiers en dehors d'un projet de code.
---

# Fichiers et volumes

Les volumes et les chemins sont décrits dans `korin-system`. Référence : `docs/FICHIERS.md` et `docs/OUTILS.md`.

## Volumes

| Commande | Effet |
|---|---|
| `mount` | les volumes montés : nom, système de fichiers, taille, place libre, étiquette, source, service |
| `df [volume-ou-chemin…]` | la place des volumes ; avec un chemin, celle du volume qui le porte |
| `mount VOL:` | remonte un volume (une clé éjectée mais encore branchée, une ligne de `SD0:/etc/mounts.txt`) |
| `mount VOL: SOURCE [SERVICE]` | monte une source donnée (`mount IMG2: blk.img2p1`) |
| `eject [-f] [USB0:]` | écrit ce qui reste puis démonte la clé ; refuse si des fichiers sont ouverts, sauf `-f` |

Les clés USB se montent d'elles-mêmes au branchement (`USB0:`, `USB1:`…). Seules les clés s'éjectent, jamais la
carte. L'app **Disques** (`disks`) fait la même chose en fenêtres.

## Formater

`mkfs [-t fat32] [-L ÉTIQUETTE] [-c octets] [-y] [-f] [--card] VOL:` efface tout et écrit un FAT32 neuf.
- Sans `-y`, la commande demande de taper le nom du volume.
- `--card` est nécessaire pour une partition de la carte. `SD0:` ne se formate jamais.
- **Ne jamais formater `SD1:`** : ce sont les données de l'utilisateur.
- **Toujours demander l'accord** de l'utilisateur, en nommant le volume et sa taille (`df VOL:`).

## Ouvrir un fichier

- Pour ouvrir un fichier avec une app précise, lancez-la avec le chemin : `notes SD1:/notes/a.txt`, `pdf /tmp/x.pdf`.
- L'app associée à chaque extension est dans `SD0:/etc/fileassoc.ini`. Une app déclare les siennes dans la ligne
  `opens =` de son `app.txt`.
- Les apps graphiques le font par UIKit : `ui_assoc_open(path)`, `ui_open_with(path)` (voir `korin-kit-uikit`).
- Pour trouver l'app d'une extension, lisez `SD0:/etc/fileassoc.ini`, puis les `opens =` de
  `SD0:/apps/*.app/app.txt`.

## Archives

- `zip`, `unzip` et `arc` en ligne de commande.
- En C, ArchiveKit (`korin-kit-archivekit`) lit et écrit deflate, gzip, ZIP et tar.
- L'app **Archiveur** (`archiver`) le fait en fenêtres.

## Pièges

- 64 fichiers ouverts au plus par process.
- `rename` d'un dossier d'un volume à l'autre rend `EXDEV` : copier puis effacer.
- Sur FAT32 (`SD0:`) : pas de fichier de plus de 4 Go, et la casse des noms n'est pas distinguée.
- `RAM0:` est vidé à l'extinction : n'y laissez rien que l'utilisateur veut garder.
- Exécutables : tout ce qui est sous `SD0:/bin`, `SD0:/sys`, `apps/*.app/main` et dans les dossiers de
  `SD0:/etc/exec.txt` est exécutable. Ailleurs, il faut `chmod +x`, et `cp` perd ce bit.
