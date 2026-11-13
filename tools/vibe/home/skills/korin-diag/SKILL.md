---
name: korin-diag
description: Diagnostiquer Korin OS - un programme qui plante ou ne répond pas, la charge des cœurs, le journal du noyau (fautes, fins de process), la mémoire, la carte et les volumes, le réseau et le Wi-Fi, le son, le GPU, le bus du système, les paquets. À utiliser quand quelque chose ne marche pas, est lent, ou pour un état du système.
user-invocable: true
---

# Diagnostiquer Korin OS

Pas de `ps`, `top`, `dmesg`, `free`, `strace`, `gdb`, `lsof`, `/proc` ni `systemctl` : Korin a ses propres outils
(dans `/bin`, en assembleur). Commencer par **lire** ; tout ce qui change l'état (plus bas) se demande d'abord.

## La carte d'identité

```sh
uname -a        # Korin, version du système (paquet armpi), niveau d'ABI des kits, cœurs, mémoire
df              # volumes : système de fichiers, taille, pris, libre, étiquette
mount           # volumes montés, leur source (blk.sd0p1...) et leur service
bus list        # l'état publié sur le bus : mode (korin.shell.mode), volumes, USB, son, réseau, thème...
pkg list        # paquets installés ; pkg check : mises à jour disponibles
```

## Les process

* **`sysstat`** : chaque process (`PID`, appels système par seconde mesurés sur une seconde, en tout, `NOM`). C'est la
  liste des process. `sysstat <pid|nom>` : un process et ses 8 appels les plus fréquents. Des dizaines de milliers
  d'appels par seconde au repos = un programme qui tourne en boucle (sondage).
* **`cpuwatch`** : la charge de chaque cœur (`load a b c d`, en %), puis chaque thread : `pid tid cœur ms nom`.
  Le lancer **deux fois** à quelques secondes d'écart : la différence des `ms` dit qui consomme.
* Arrêter un process : `kill <pid>` (bash). Les petits pids sont des services du système : ne pas les tuer.
* Un programme lancé depuis le shell rend son code : 139 (ou -11 dans le journal) = faute mémoire (SIGSEGV) ;
  127 = introuvable ou bibliothèque manquante.

## Le journal du noyau

Pas de `dmesg` : **`journal`** copie le journal (64 Ko) dans `/sd0/journal.txt` **20 s après son lancement**, puis
encore 60 s plus tard. Le lancer détaché, attendre, lire :

```sh
(journal >/dev/null 2>&1 &); sleep 25; tail -40 /sd0/journal.txt
```

Ce qu'on y lit :

* `[ 418.188] [pid 65584 crash] faute, process tué. ESR=0x...9200000E  ELR=0x0000005000000758  FAR=0x...80000` :
  une faute. `ELR` est l'adresse de l'instruction, `FAR` l'adresse touchée (petite : pointeur nul ou presque).
  Pour retrouver la ligne : recompiler **avec `-g` et sans `strip`** (`gcc -g -O0 ...`, pas `pibuild` qui retire les
  symboles), reproduire, puis `objdump -dS --start-address=0x... --stop-address=0x... programme` (les adresses du
  journal sont celles de l'ELF) ; `nm -n programme` pour la fonction qui contient l'adresse.
* `[pid N nom] terminé (code X)` : chaque fin de process ; un service qui finit tout seul est suspect.
* Les messages des services et des programmes système (`SD0:/sys`), des pilotes (USB, carte, Wi-Fi), le chargement
  des images (`image SD0:/... : chargée`).
* **Mémoire épuisée** : le noyau tue le process qui demande une page, **quel qu'il soit** (parfois un service :
  `ramfs` mort fait disparaître `/tmp`). Une fin inexpliquée d'un service après un gros programme : chercher là.

Les journaux des programmes eux-mêmes : Vibe écrit le sien dans `/tmp/agent-vibe.log` (client `agent`) ou
`/tmp/vibe-app.log` (l'app) ; les apps .NET et Avalonia acceptent souvent `--log fichier`.

## La carte, les volumes, les fichiers

* `df`, `mount` : place, volumes. `SD0:` (FAT32) système ; `SD1:` (exFAT) données de l'utilisateur ; `RAM0:`
  (`/tmp`) mémoire.
* `fscache [volume]` : le cache de fatfs (succès, défauts, en mémoire). `readbench /sd0/fichier`, `blkbench` :
  débit de lecture de la carte (lecture seule). `writebench /tmp/x` écrit 8 Mo (le mettre dans `/tmp`).
* `preload` : les images de programmes gardées en mémoire (épinglées, passagères) ; `unload <chemin>` force à
  relire un programme remplacé.
* FAT : pas de liens symboliques ; 254 octets par chemin ; 64 fichiers ouverts par programme ; un « fichier
  inaccessible » peut être un chemin trop long.

## Le réseau

```sh
netstat -i                  # interfaces, adresse, masque, passerelle, DNS, bail DHCP
netstat -t ; netstat -u -l  # sockets TCP, UDP, à l'écoute (de tout le système)
ping <hôte> ; nslookup <nom>
wget -S -O /dev/null http://...   # en-têtes (wget : HTTP seulement ; HTTPS : python3, curl absent)
wifi ; wifi scan            # état du Wi-Fi, réseaux visibles (pas de Wi-Fi sous QEMU)
```

Pas d'IPv6. Sous QEMU, l'hôte est `10.0.2.2`, le DNS `10.0.2.3`.

## Son, GPU, écran

* `volume` (sortie principale, `volume apps`, `volume output`), `soundctl` (le mixer complet) ; pas de son ? voir
  `bus list | grep audio`.
* `gpuinfo` : le GPU V3D (prêt, jobs, température, fréquences, alertes du firmware) ; « pas de service v3d » sous
  QEMU, normal.
* `screenshot /tmp/screen.bmp` : l'écran tel qu'il est montré (BMP 32 bits).

## Ce qui demande l'accord de l'utilisateur

`reboot` ; `kill` d'un service ; `mkfs` ; `eject -f` ; `fscache off|drop` ; `pkg add|delete|update|commit` ; écrire
dans `SD1:` ; modifier `/etc`, `SD0:/sys`, `/bin`, `/lib`. Le diagnostic lui-même ne change rien.

## Défauts connus (ne pas chercher plus loin)

* Mémoire épuisée : le noyau tue le demandeur, quel qu'il soit (plus haut).
* RamFS : `readdir` pendant des effacements peut sauter des entrées.
* Les programmes reliés avant l'arbre POSIX (`/sd0/...`) rendent encore `SD0:/...` pour `getcwd`.
* `make` dit « jobserver mkfifo : Function not implemented » à chaque lancement : sans effet.
* Pas de fuseau horaire : tout est en UTC.
