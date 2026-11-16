# Étude : une clé USB d'installation pour le PC

Demandée le 15/11/2026. Rien n'est fait. Ce document propose un modèle et un plan. Les réponses de l'utilisateur
(15/11) sont au §10 et déjà reportées dans le texte.

But : démarrer le PC sur une clé USB, choisir les partitions du disque interne (supprimer, créer, formater, points
de montage), installer Korin, configurer le démarrage UEFI (Korin, et Windows s'il existe encore), choisir les
composants, régler écran, Wi-Fi, clavier et région, puis créer le mot de passe d'administration et le premier
utilisateur.

Cible : le PC (ZBook d'abord, mais rien de propre au ZBook). Korin s'installe sur le **disque NVMe interne** ou sur
un **disque USB externe** (réponse 6). IDE, SATA et PATA viendront peut-être plus tard. Le Pi garde ses images de
carte SD et l'étape Disque de Setup.

## 0. Résumé

* La clé est un **Korin complet qui démarre en mémoire de travail** (« live ») : le même `korin.efi`, le même noyau.
  Son initrd lance `usbd` et `usbstorage` au lieu de `nvme` en premier. `/` est la clé, en lecture seule, et `/tmp`
  est en mémoire. Le programme d'installation est **Setup avec plus d'étapes**, pas une seconde application.
* Le disque interne s'ouvre en écriture par un **mode installation du pilote NVMe** (`nvme -I`). Ce mode est présent
  seulement dans l'initrd de la clé, jamais dans un système installé. Un « plan de disque » validé par l'utilisateur
  dit exactement quelles partitions peuvent être écrites. Les règles d'aujourd'hui restent valables partout ailleurs.
* La **partition EFI existante n'est jamais écrite** (décision du 12/11). Le chargeur va sur la partition de Korin,
  comme aujourd'hui sur `KORIN OS`. Il n'y a qu'un cas où nous écrivons une partition EFI : quand le disque a été
  vidé et que nous la créons nous-mêmes.
* L'entrée « Korin OS » du firmware (NVRAM) est créée **par `korin.efi`, avant `ExitBootServices`**. Le noyau n'a
  donc pas besoin des services d'exécution UEFI. Le menu actuel trouve déjà Windows sur n'importe quel volume : il
  n'y a rien à faire pour l'entrée Windows.
* La racine est en **FAT32** (réponse 1). Le multi-utilisateur fait ses droits dans vfs (étude MULTI-UTILISATEUR,
  §2.3), pas dans le système de fichiers. Le système de fichiers est un **choix dans la liste** de chaque partition :
  aujourd'hui FAT32 (et exFAT hors de `/`), plus tard ceux que Korin saura écrire.
* Les **composants** sont des **paquets** (réponse 3). La clé porte le système de base, quelques accessoires et
  leur dépôt hors ligne signé. Si le réseau est établi pendant l'installation, on choisit dans le dépôt en ligne ;
  sinon, parmi les paquets de la clé. `pkg` apprend à installer dans une autre racine (`--root`).
* L'**élévation de droits** est accordée à des utilisateurs ou à des groupes nommés, pas seulement aux `admin`
  (réponse 4).

## 1. Ce qui existe

| Élément | État | Utile ici |
|---|---|---|
| `korin.efi` (`src/kernel/arch/pc/boot/`) | menu Korin / Windows / firmware, lit `\korin\…` sur **son propre volume** | démarre tel quel depuis la clé (`\EFI\BOOT\BOOTX64.EFI`) |
| Recherche de Windows | `find_windows` : `\EFI\Microsoft\Boot\bootmgfw.efi` sur tout volume, partition EFI préférée | entrée Windows automatique, absente si le disque est vidé |
| `boot.cfg` | `default`, `timeout`, `cmdline` | écrit par l'installateur |
| Pilote `nvme` + `policy.c` | lecture de la GPT, écriture seulement selon `nvme.txt` (voir §8) | à étendre d'un mode installation |
| `blkserv.c` | GPT lue (CRC, 16 partitions), crochets `blk_table_fn`, `blk_deny_fn` | inchangé ; l'écriture de la GPT est à ajouter |
| `usbd` (xHCI en C, MSI-X) + `usbstorage` | clé lue sous QEMU q35 | `/` de la clé |
| `mkfs` | FAT32 seulement, sur un volume monté ou une source | à appeler sur une source `blk.*` non montée ; exFAT à ajouter si on le veut |
| `fatfs` | FAT32 et exFAT, agrandit un FAT32 au montage (`fat_agrandir`) | — |
| Setup (`src/sys/setup`) | Bienvenue, Région (pays, fuseau, clavier), Affichage, Disque (Pi), Wi-Fi, Logiciels, Accès à distance | à reprendre presque entier |
| `korin.ps1` (Windows) | installe sur `KORIN OS`, entrée du firmware par `bcdedit` | reste l'outil du développeur ; l'installateur fait la même chose depuis Korin |
| `mkgpt.py`, `qemu_nvme.py` | disque d'essai qui imite le ZBook, QEMU avec NVMe | base des essais (§7) |
| Multi-utilisateur | étudié, étape 1 faite (arbre unique, `HOME:`) ; comptes : étapes 2 et 6 | l'étape Comptes en dépend |

Ce qui manque : écrire une GPT depuis Korin, un mode installation pour `nvme`, `pkg --root`, la création de l'entrée
NVRAM, le repérage de la partition racine par son GUID, des règles `nvme.txt` propres à chaque machine, les comptes.
Pas d'AHCI/SATA non plus : l'installateur ne verra que les disques NVMe et les disques USB (§3.5).

## 2. La clé

### 2.1 Contenu

Une clé en GPT (ou MBR, les deux sont lus) avec **une partition FAT32 de type EFI System**, nommée `KORIN INST` :

```
\EFI\BOOT\BOOTX64.EFI        korin.efi (le firmware démarre ce chemin sur un support amovible)
\korin\kernel.elf
\korin\initrd.tar            initrd de la clé : init.txt « install » (§2.2), nvme en mode -I
\korin\boot.cfg              default = korin, timeout = 0, cmdline = install
\bin \sbin \sys \lib \etc \res \apps \fonts …   le système « live » (sysroot minimal + Setup)
\packages\pc\index.txt, index.sig, pkgs\*.opk   le dépôt hors ligne : système de base et quelques accessoires
\iwlfw\…                     micrologiciel du Wi-Fi (déjà dans le sysroot)
```

Taille : le système de base, quelques accessoires et leurs paquets, donc quelques centaines de Mo. Une clé de
2 Go suffit. .NET, GCC, Python, Godot, la CAO et les autres gros paquets viennent par Internet (§4). La liste des
paquets de la clé se règle dans `tools/pc/installkey.ini` (groupes `base` et `accessories`).

Construction : `tools/pc/installkey.sh` (WSL) produit `build/pc/installkey/` (l'arbre) et une image
`build/pc/installkey.img`. L'écriture sur une vraie clé passe par `tools/pc/installkey.ps1 write -Disk N`. Ce script
refuse tout disque qui n'est pas USB et amovible, ou qui est le disque de Windows, et demande de taper le numéro.

### 2.2 Démarrage de la clé

1. Le firmware lance `\EFI\BOOT\BOOTX64.EFI` sur la clé (F9 sur le ZBook, Secure Boot désactivé).
2. `korin.efi` lit noyau, initrd et `boot.cfg` **sur son volume** : c'est déjà le cas aujourd'hui.
3. **Nouveau, utile partout** : le chargeur ajoute à la ligne de commande l'identité du volume d'où il vient. Il la
   lit dans le nœud `HARDDRIVE_DEVICE_PATH` de `LoadedImage->DeviceHandle` : `bootpart=GUID` en GPT,
   `bootpart=SIGNATURE-N` en MBR. L'initrd attend alors **ce** volume, au lieu d'un nom de volume ou d'un numéro de
   partition.
4. `init.txt` de la clé :
   ```
   pcid -n
   usbd                     # xHCI : la clé
   usbstorage
   wait-part $bootpart 30000   # blk.usbNpM dont le GUID est celui de la ligne de commande
   vfs / $bootpart fatfs ro
   vfs /tmp - ramfs
   nvme -I                  # disque interne : lecture seule, sauf le plan validé (§3)
   ```
   Puis le système monte comme d'habitude (`init2`, Elegant), et `/etc/session` lance Setup en mode `install`.

`wait-part` (ou `wait blk.part.<GUID>`) demande que `blkserv` publie aussi chaque partition sous son GUID
(`blk.part.<GUID>`). C'est un petit ajout, qui sert ensuite au système installé (§5.3).

Tout ce qui est écrit pendant la session (`/etc/options.txt`, `wifi.txt`, journaux) va dans `/tmp`, par un
répertoire de travail (`/tmp/install/etc`). Le système de la clé ne change jamais, et la même clé sert pour d'autres
installations.

## 3. Le disque : partitions et sécurité

### 3.1 Les cas proposés

L'étape Disque montre les disques possibles (NVMe interne et disques USB, sauf la clé elle-même) : modèle, bus,
taille, et la table avec chaque partition (numéro, type, nom, étiquette du volume, système de fichiers, taille,
« Windows », « EFI », « récupération »). Elle offre trois choix :

| Choix | Ce qui se passe | Partition EFI |
|---|---|---|
| **A. À côté de Windows** | l'espace libre (ou une partition choisie, à supprimer) devient Korin ; le reste est intact | gardée, jamais écrite |
| **B. Disque entier** | nouvelle GPT : ESP 512 Mo (la nôtre), Korin racine, et, si demandé, une partition de données | créée et écrite par nous |
| **C. Manuel** | liste des partitions : supprimer, créer (taille, type, système de fichiers, point de montage), formater ou garder | gardée ou créée, selon le cas |

Le choix B correspond au cas où le disque est entièrement effacé. La partition EFI n'est préservée que si elle
reste sur le disque. Sinon, nous en créons une neuve, **séparée de `/`** (réponse 2). C permet les deux, avec
« garder l'ESP » coché par défaut. Sans ESP gardée, C en crée une, et ce n'est jamais la partition `/`.

Réduire la partition NTFS de Windows ne fait **pas** partie du plan : Korin ne sait pas déplacer un NTFS. Il faut le
faire depuis la gestion des disques de Windows, avant. L'étape le dit quand il n'y a pas d'espace libre.

### 3.2 Points de montage

Pour chaque partition de Korin :

| Point | Obligatoire | Système de fichiers | Remarque |
|---|---|---|---|
| `/` | oui | FAT32 (pour l'instant le seul choix) | avec une ESP gardée, le chargeur y est (`/EFI/Korin/korin.efi`), comme sur `KORIN OS` |
| `/boot/efi` | si l'ESP est la nôtre | FAT32 (imposé par l'UEFI) | ESP créée par nous (cas B, ou C sans ESP gardée) |
| `/home` | non | FAT32 ou exFAT | sinon `/home` est un dossier de `/` |
| autre (`/data`, `/media/…`) | non | FAT32, exFAT, ou une partition existante gardée (NTFS en lecture seule) | — |

La colonne « Système de fichiers » est une liste tirée d'une table (`fs` → `mkfs -t`, service, points de montage
permis). Un nouveau système de fichiers que Korin sait écrire s'y ajoute par une ligne, sans changer les pages.

Avec le multi-utilisateur, `HOME:` = `/home/<nom>/`. Une partition `/home` séparée permet de réinstaller sans
toucher aux données. C'est le même principe que `/media/sd1` sur le Pi.

### 3.3 Le plan, puis l'exécution

L'étape Disque ne fait qu'**écrire un plan** (`/tmp/install/plan.ini`), sans rien toucher. Le plan contient la GPT
cible complète et, pour chaque partition, son état : `keep`, `create`, `format` ou `delete`, avec le GUID, le type,
le début et la fin.

À la dernière étape (Résumé), une seule confirmation : la liste en rouge de ce qui sera **détruit** (nom, étiquette,
taille, « contient Windows » si on y trouve `\Windows\System32`), puis le nom du disque à taper. Viennent ensuite,
dans cet ordre :

1. `nvme` reçoit le plan (`BLK_PLAN`, voir §8) et le vérifie lui-même ;
2. écriture de la GPT (principale et de secours, MBR protecteur), puis relecture et contrôle ;
3. `mkfs` des partitions `create` et `format` ;
4. montage de la cible sur `/target` (et `/target/home`, …) ;
5. copie du système, installation des paquets (§4) ;
6. configuration (§5) ;
7. démarrage (§6) ;
8. `sync`, démontage, `nvme flush`, puis « Retirez la clé » et redémarrage.

Une erreur à l'étape 2 arrête tout avant que le disque soit formaté. Une erreur après laisse un disque partitionné,
mais sans démarrage de Korin. Comme l'entrée NVRAM vient en dernier, rien ne démarre une installation à moitié faite.

### 3.4 La bibliothèque GPT

Il faut **écrire** une GPT dans Korin : `blkserv.c` ne fait que la lire, et `mkgpt.py` ne tourne que sur le PC hôte.
Le code proposé est `src/lib/gpt/` (`gpt_read`, `gpt_plan_apply`, CRC32, en-tête de secours, alignement sur 1 Mo,
GUID aléatoires par `getrandom`). C'est du C commun. La règle « ce qui sert à plusieurs programmes va dans le kit de
son domaine » dit qu'elle servira aussi à Disks (formater, partitionner une clé), plus tard. Elle peut entrer dans un
kit de stockage le jour où Disks en a besoin. Pour commencer, une bibliothèque statique liée à Setup, `nvme` (pour
la vérification) et une commande `gpt` (essais, `gpt show`, `gpt apply plan.ini`) suffit.

### 3.5 Disque USB externe

Un disque USB externe est une cible au même titre que le NVMe (réponse 6). Différences :

* **Protection** : `usbstorage` n'a pas de règles d'écriture aujourd'hui, et une clé s'écrit librement. Pendant
  l'installation, il reçoit le même plan (`BLK_PLAN`) que `nvme`, avec la même vérification, pour le disque choisi.
  Il refuse toute écriture sur **la clé d'où l'on a démarré** (`bootpart`), quoi qu'il arrive.
* **Démarrage** : un disque amovible démarre surtout par le chemin de secours. Le chargeur va donc toujours sur
  l'ESP du disque externe, en `\EFI\BOOT\BOOTX64.EFI` et en `\EFI\Korin\korin.efi`. L'entrée NVRAM est facultative
  (cochée si le disque reste branché à cette machine). On ne touche jamais l'ESP du disque interne.
* **Initrd** : le système installé doit lancer `usbd` et `usbstorage` avant d'attendre `/`. L'`init.txt` générique
  (§5.3) lance donc les deux pilotes, NVMe et USB, et attend `bootpart`, quel que soit le bus.

## 4. Composants

Les composants sont les **paquets** du dépôt PC, groupés comme dans Logiciels :

* **Système** (obligatoire, coché grisé) : noyau, services, commandes, Elegant, UIKit, apps de base ;
* **Bureau** : Jet, Courier, Photos, Lecteur, etc. ;
* **Développement** : GCC (`/devtools/gcc`), .NET, Python, Git, Studio ;
* **Création** : Godot, CAO (Forge), Vector, Arabesque ;
* **Jeux et émulateurs**.

On reprend l'étape Logiciels de Setup, avec deux sources (réponse 3) :

* **réseau établi** (Wi-Fi réglé à l'étape précédente, et l'heure réglée par NTP pour vérifier le certificat) : la
  liste est celle du **dépôt en ligne**. Les paquets déjà présents sur la clé et à la même version sont pris sur la
  clé, et les autres sont téléchargés ;
* **pas de réseau** : seulement les paquets de la clé (`file:///packages/pc/`, même signature, même `index.sig`).
  La page le dit et ajoute que le reste s'installera ensuite par Logiciels.

Le système de base est toujours installé depuis la clé, même en ligne, pour qu'une coupure du réseau ne laisse
pas une installation sans système.

**À ajouter à `pkg`** : `--root /target` (installer dans une autre racine, avec la base `var/pkg/db` de cette
racine), et des groupes dans l'index (`group = dev`), ou bien une liste de groupes dans le Setup. Le système de base
pourrait aussi être une simple copie de `/` (clé) vers `/target`. Mais passer par les paquets donne d'emblée une
base `var/pkg/db` juste, et les mises à jour fonctionnent dès le premier démarrage. C'est ce que fait déjà
`korin.ps1` avec `mkrepo.py --db`.

## 5. Configuration de la cible

### 5.1 Reprise des pages de Setup

| Page | Ce qui est écrit sur la cible |
|---|---|
| Langue, Région (pays, fuseau, clavier) | `/etc/options.txt` (langue, clavier), fuseau (aujourd'hui toujours UTC) |
| Affichage | résolution, thème, interface, taille des contrôles : `/etc/options.txt` |
| Wi-Fi | `/etc/wifi.txt` (le réseau choisi et sa clé) : la machine installée a le réseau au premier démarrage |
| Accès à distance | `/etc/…` comme aujourd'hui |
| Composants | paquets installés (§4) |
| Comptes (nouveau) | §5.2 |

Ces pages changent tout de suite le système de la clé (le clavier se prend à l'essai), puis écrivent la cible à
l'étape 6 de §3.3. Setup a donc besoin d'une **racine cible** (`setup --install --target /target`). Sans
`--install`, il garde son comportement actuel (premier démarrage du Pi). Après une installation, le premier
démarrage du PC ne relance **pas** Setup : tout a déjà été demandé.

### 5.2 Comptes

L'étape dépend du multi-utilisateur (étude MULTI-UTILISATEUR.md, étapes 2 et 6). Après la réponse 4 :

* **Mot de passe root** : celui du compte `system`, l'« root » de Korin, sans session graphique.
* **Qui peut élever ses droits** : une liste d'**utilisateurs et de groupes** dans `/etc/elevate.ini`, lisible par
  authd, comme le `sudoers` d'Unix mais plus simple :
  ```
  [elevate]
  users = stephane
  groups = admin, dev
  ```
  Être `admin` n'est pas obligatoire. N'importe quel groupe ou utilisateur peut être listé, et la liste peut être
  vide (seul `system` administre alors la machine). Reste à trancher dans MULTI-UTILISATEUR (§4, question 2) quel
  mot de passe l'élévation demande : celui de l'utilisateur (comme `sudo`) ou celui de root.
* **Premier utilisateur** : nom, nom complet, mot de passe (facultatif), image, groupes ; une case « peut élever ses
  droits », cochée par défaut, qui l'ajoute à `users =` ; connexion automatique proposée quand c'est le seul compte.
* Écrit dans `/etc/users.ini` (comptes, uid, groupes), `/etc/elevate.ini` et `/etc/shadow.ini` (empreintes), ce
  dernier lisible seulement par `system` (règle vfs). Empreinte : **yescrypt ou PBKDF2-SHA-512** par OpenSSL (déjà porté), avec un
  sel aléatoire. Jamais de mot de passe en clair, même dans `/tmp`.
* `/home/<nom>/` créé avec les dossiers par défaut, `HOME:` le désigne.

Tant que le multi-utilisateur n'est pas fait, l'étape peut exister **sans effet** : elle écrit les fichiers, crée
`/home/<nom>`, et le système lit `users.ini` quand l'étape 2 du multi-utilisateur arrive. Le mot de passe ne sert à
rien avant l'étape 4 (authd, sudo).

### 5.3 Ce que le système installé doit trouver au démarrage

C'est le point le plus délicat, parce qu'aujourd'hui l'initrd du PC est **construit pour le ZBook** : `nvme.txt`
cherche « KORIN OS », `init.txt` monte `blk.korin`. Une installation par la clé doit marcher sur un autre disque,
avec d'autres GUID, sans reconstruire l'initrd. Proposition :

1. **Racine par GUID** : `korin.efi` passe `bootpart=GUID` (§2.2). C'est la partition d'où il a démarré, donc la
   racine. `init.txt` (générique) fait `wait-part $bootpart` puis `vfs / $bootpart fatfs`. Ni le nom du volume ni le
   numéro de partition n'interviennent plus.
2. **Règles NVMe de la machine** : l'initrd vient des paquets et est remplacé à chaque mise à jour. Les règles
   propres à la machine ne peuvent donc pas y vivre. Le chargeur lit un **second module facultatif**,
   `\korin\local.tar`, et le noyau le superpose à l'initrd (un fichier de `local.tar` remplace celui du même nom).
   L'installateur y met `nvme.txt` :
   ```
   write = guid=<GUID de />
   name = root, guid=<GUID de />
   write = guid=<GUID de /home>
   name = home, guid=<GUID de /home>
   ```
   Les règles par GUID sont plus sûres que par étiquette : deux disques « KORIN OS », ou une clé portant le même nom,
   ne peuvent pas se confondre. Le ZBook actuel garde ses règles par étiquette jusqu'à ce qu'il soit réinstallé.
3. `/etc/mounts.txt` : les autres partitions, par `blk.part.<GUID>` ou par l'alias (`blk.home`).

## 6. Démarrage UEFI

### 6.1 Où va le chargeur

| Cas | Chargeur | Entrée du firmware |
|---|---|---|
| A, C avec ESP gardée | `/EFI/Korin/korin.efi` **sur la partition racine de Korin** (FAT32), comme aujourd'hui sur `KORIN OS` | « Korin OS » → ce fichier, sur cette partition |
| B, ou ESP créée par nous (séparée de `/`, réponse 2) | `\EFI\Korin\korin.efi` sur notre ESP, et aussi `\EFI\BOOT\BOOTX64.EFI` (chemin de secours : le firmware le démarre même sans entrée) | « Korin OS » → `\EFI\Korin\korin.efi` sur l'ESP |
| disque USB externe | comme B, sur l'ESP du disque externe | facultative (§3.5) |

Avec notre ESP, `korin.efi` démarre depuis l'ESP. Il doit donc lire noyau et initrd **sur la racine**, et non plus
sur son propre volume. `\korin\boot.cfg` (sur l'ESP) reçoit `root = GUID`. Le chargeur cherche ce volume par
`EFI_PARTITION_INFO_PROTOCOL` ou par le nœud `HARDDRIVE` des volumes connus, puis passe `bootpart=` avec le GUID de la
**racine**, et non de l'ESP. C'est un petit ajout au chargeur. L'ESP n'a que le chargeur et `boot.cfg`. Noyau,
initrd et `local.tar` restent dans `/korin` sur la racine, là où `pkg` les met à jour. Korin monte l'ESP sur
`/boot/efi`, à la demande, comme `/boot` aujourd'hui, quand une mise à jour du chargeur l'exige.

### 6.2 L'entrée NVRAM sans services d'exécution

Le noyau Korin n'appelle pas les services d'exécution UEFI (`SetVariable` après `ExitBootServices`). Les ajouter est
possible : garder les zones `EfiRuntimeServices*` mappées, `SetVirtualAddressMap` ou appel en identité, comme Linux.
Mais c'est risqué sur des firmwares fragiles, et c'est du code noyau pour une seule écriture. Je propose plutôt :

* `korin.efi` lit dans `boot.cfg` une ligne `firmware = install`. **Avant** `ExitBootServices`, avec les services
  d'amorçage, il crée ou met à jour `Boot####` (« Korin OS », chemin de **son** fichier sur **son** volume), le met
  en tête de `BootOrder`, puis remplace la ligne par `firmware = done` (écriture FAT par le firmware, sur notre
  partition).
* L'installateur écrit `firmware = install` sur la cible. Au redémarrage, la clé est encore là : son menu propose
  « Démarrer Korin installé » quand il trouve un volume qui a `\korin\boot.cfg` avec `firmware = install`. Il
  chaîne alors **le `korin.efi` de la cible** par `LoadImage`, comme il chaîne déjà Windows. C'est ce chargeur-là qui
  crée l'entrée, en connaissant son propre chemin.
* Dans le cas B, le chemin de secours `\EFI\BOOT\BOOTX64.EFI` démarre même sans la clé, et l'entrée est créée au
  premier démarrage.
* Le même mécanisme sert plus tard à Réglages › Démarrage (« Korin d'abord », « Windows d'abord », « ouvrir les
  réglages du firmware ») : Korin écrit `boot.cfg`, et le chargeur applique la demande au démarrage suivant.

L'entrée Windows n'est jamais créée ni supprimée par nous. Le menu de `korin.efi` montre Windows s'il trouve
`bootmgfw.efi` (déjà le cas), et `boot.cfg` reçoit `default` et `timeout` choisis à l'étape Résumé (par défaut :
Korin, 5 s si Windows existe, 0 sinon).

## 7. Essais

* QEMU q35 + OVMF : `qemu_nvme.py` avec `--usb-key build/pc/installkey.img` (clé `usb-storage` sur xHCI), le disque
  d'essai de `mkgpt.py` (imite le ZBook : ESP, MSR, « C: », récupération) et des variables OVMF à part.
* Scénarios : A sur l'espace libre (sommes `mkgpt.py sums` : **seules** les zones prévues changent), A en supprimant
  une partition, B disque entier, C manuel avec `/home` séparé, coupure au milieu de la copie, GPT au CRC faux
  (refus), plan modifié entre la validation et l'écriture (refus par `nvme`).
* Premier démarrage sans la clé : entrée NVRAM présente (OVMF la garde dans ses variables), menu Korin et faux
  Windows (`menu_esp.sh`), racine trouvée par GUID.
* Vrai ZBook, seulement à la fin, avec une sauvegarde et la clé de récupération BitLocker à portée. Changer la GPT ou
  l'ordre de démarrage peut faire demander cette clé par Windows. D'abord le cas A sur un espace libre, puis le
  cas B si tu le confirmes.

## 8. Sécurité : le mode installation de `nvme`

Aujourd'hui, aucune règle ne peut ouvrir le disque entier, l'ESP ou un contenu NTFS. L'installateur doit pourtant
écrire la GPT, et dans le cas B, effacer tout. Pour ne rien affaiblir ailleurs :

* **`nvme -I` seulement dans l'initrd de la clé** (option refusée si `bootpart` est sur le disque NVMe lui-même). Un
  système installé n'a jamais ce mode.
* En mode `-I`, tout est en lecture seule au départ, **même la partition `KORIN OS`**. Une seule opération ouvre
  l'écriture : `BLK_PLAN` sur `blk.nvme0`, avec le plan complet (§3.3). Le pilote vérifie lui-même :
  * que la GPT actuelle est celle sur laquelle le plan a été fait (CRC de l'en-tête et des entrées), sinon refus ;
  * que chaque partition `keep` garde exactement son début, sa fin, son type, son GUID et ses attributs ;
  * que l'ESP gardée n'est **pas** dans les partitions écrites ;
  * qu'aucune partition `create` ne chevauche une `keep`.
* Ensuite, seules sont écrites : les secteurs de la GPT (LBA 0 à 33 et la fin du disque), puis les partitions
  `create` et `format`, par leur GUID. Toute autre écriture est refusée et journalisée, comme aujourd'hui.
* Le mode « disque entier » (B) est un plan où tout est `delete` : il demande un drapeau de plus dans le plan
  (`wipe = yes`) et la saisie du nom du disque dans Setup.

Ce sont les mêmes trois verrous que `policy.c` (règles, refus fixes, journal), avec une règle temporaire de plus,
vérifiée par le pilote et non par l'application.

## 9. Plan par étapes

| # | Étape | Contenu |
|---|---|---|
| 1 | **Racine par GUID** | `korin.efi` passe `bootpart=` ; `blkserv` publie `blk.part.<GUID>` ; `wait-part` ; `init.txt` générique ; `local.tar` superposé à l'initrd ; règles `guid=` déjà acceptées. Le ZBook continue de démarrer comme avant. |
| 2 | **La clé live** | `installkey.sh` ; initrd « install » (usbd, usbstorage, `/` en lecture seule, `/tmp`) ; Setup démarre ; QEMU puis ZBook (lecture seule du NVMe) |
| 3 | **GPT** | `src/lib/gpt`, commande `gpt show/plan/apply` sur image d'essai ; `nvme -I` et `BLK_PLAN` ; essais `mkgpt.py sums` |
| 4 | **Setup --install** | pages Disque (A, B, C), Points de montage, Résumé ; `mkfs` sur une source non montée ; copie et `pkg --root` ; dépôt hors ligne |
| 5 | **Démarrage** | chargeur sur la racine ou l'ESP, `root=` dans `boot.cfg`, `firmware = install/done`, entrée « Démarrer Korin installé » du menu de la clé |
| 6 | **Comptes** | page Comptes, `users.ini`, `elevate.ini`, `shadow.ini`, empreintes ; effet réel avec le multi-utilisateur (étapes 2, 4, 6) |
| 7 | **Disque USB externe** | plan et protection dans `usbstorage`, chargeur sur l'ESP du disque, `init.txt` générique qui lance aussi l'USB |
| 8 | **Vrai ZBook** | cas A, puis B si voulu |
| 9 | **Plus tard : réparer, mettre à jour** | la clé reconnaît une installation de Korin (`/korin`, `var/pkg/db`), garde `/home`, `/etc` et `local.tar`, remplace le système, recrée l'entrée NVRAM si elle manque |

Les étapes 1 à 3 servent aussi en dehors de l'installateur : racine robuste, `blk.boot`, partitionner une clé.

### 9.1 Étape 1 : faite le 15/11/2026 (QEMU seulement)

Ce qui a été fait diffère de §2.2 et §5.3 sur un point. Les noms `blk.part.<GUID>` sont trop longs : vfs garde
24 caractères par source et blkserv 32 par nom. La partition de démarrage est donc servie sous le nom fixe
**`blk.boot`**.

| Fichier | Changement |
|---|---|
| `src/kernel/arch/pc/boot/bootx64.asm`, `bootinfo.inc` | `find_bootpart` lit le nœud HARDDRIVE du chemin du volume de démarrage. Il écrit dans `bootinfo.bootpart` le GUID unique de la partition en minuscules (GPT), ou `SSSSSSSS-NN` (MBR, comme le PARTUUID de Linux). Trace : `boot: partition de demarrage …`. Le bloc de démarrage passe de 240 à 304 octets. |
| `menu.inc` | `korin_load` lit aussi `\korin\local.tar` (facultatif), dans `bootinfo.local`. Trace : `boot: \korin\local.tar lu`. |
| `src/kernel/arch/pc/bootinfo.h`, `pc_main.c` | `local.tar` est retiré de la mémoire libre. Le module texte `bootpart` est fourni par le noyau. Journal : `Local : …`, `Partition de démarrage : …`. |
| `src/kernel/core/initrd.c` | `initrd_init_local` : un nom de `local.tar` passe avant celui de l'initrd. `initrd_set_text` : les modules texte du noyau passent avant les deux. `MOD_READ`, `MOD_LAUNCH` et les kits en profitent. |
| `src/svc/common/blkserv.c` | lit le module `bootpart`, et la partition de ce disque qui a cette identité est servie aussi en `blk.boot`. Un emplacement en plus est prévu, en dehors des 4 noms de plus. Après une relecture de la table, le nom suit la partition ou se ferme. Sur le Pi, il n'y a pas de module, donc rien ne change. |
| `src/svc/init/init.c` | `wait A|B ms` attend le premier venu (A est préféré : 300 ms de plus pour lui). Les lignes suivantes reçoivent le nom trouvé à la place de `A|B`. |
| `skel/pc/etc/init.txt` | `wait blk.boot|blk.korin 30000`, puis `vfs / blk.boot|blk.korin fatfs`. `blk.korin` reste le repli : démarrage depuis le dossier `esp` de QEMU, ou un chargeur ancien. |
| `src/svc/nvme/nvme.c` | le journal dit « nvme.txt (local.tar ou initrd) ». |

Essais sous QEMU q35 + OVMF de WSL (le QEMU de Windows n'est plus installé) :

* image de `mkkorindisk.py`, démarrée depuis KORIN OS par `\EFI\BOOT\BOOTX64.EFI` (l'OVMF de WSL n'a pas de shell
  pour `startup.nsh`), avec `boot.cfg` et un `local.tar` qui renomme l'alias. Résultats :
  `boot: partition de demarrage ed4a955b-6054-59b5-ab7e-07a530c8dcd0` (le GUID de p5 dans la GPT),
  `nvme : règle 2 : p5 aussi servie en blk.locale` (la règle vient de `local.tar`),
  `init : attente de blk.boot|blk.korin : blk.boot -> 7`, `vfs : montage / -> 0`, `/ pret` ;
* dossier `esp` (volume vvfat, `bootpart` = `be1afdfa-01`, absent du NVMe) et image de `mkgpt.py` :
  `attente … : blk.korin`, `montage / -> 0` ;
* services `init`, `imgblk`, `usbstorage`, `nvme`, `sdhci`, `rtsx` construits pour aarch64 et x86_64 sans
  avertissement.

Pas encore essayé sur le ZBook. Avec ce changement, le ZBook monte `/` sur `blk.boot` (la partition KORIN OS d'où
démarre `korin.efi`). Un montage de `mounts.txt` qui viserait la même partition avec un `rootpath` doit utiliser
**le même nom que `/`** : vfs ne partage le service que pour une même source.

### 9.2 Étapes 2 à 4 : la clé, la GPT, le mode installation (16/11/2026, QEMU)

* **La clé** : `tools/pc/installkey.py build` fait `build/pc/installkey.img` (GPT, une partition EFI FAT32
  « KORIN INST » : korin.efi en `\EFI\BOOT\BOOTX64.EFI`, le noyau et l'initrd du système, le système « live »,
  `etc/install.ini`). Son `korin/local.tar` remplace `init.txt` (hwmanager et pcid d'abord : pcid annonce le PCIe,
  hwmanager lance usbd puis usbstorage depuis l'initrd, `/` sur `blk.boot`) et `nvme.txt` (aucune règle,
  `install = yes`). `installkey.py run` la démarre sous QEMU (usb-storage amorçable) avec le faux ZBook en
  NVMe. init2 ne relance pas hwmanager ; usbstorage ne monte pas une seconde fois la partition de démarrage.
* **La GPT** : `src/lib/gpt` (lecture avec copie de secours, ajout, retrait, espace libre aligné, écriture),
  dans korinrt ; commande `gpt` ; `src/lib/gpt/test/run.sh` la juge par `sgdisk -v` sous Linux. Elle servira aussi
  à un outil de partitionnement dans Korin (demande de l'utilisateur).
* **Le mode installation de nvme** : `install = yes` dans `nvme.txt` (la clé seulement). `BLK_PLAN` sur le disque
  entier (drapeaux, somme de l'en-tête GPT sur lequel le plan a été fait, plages à ouvrir), vérifié par le pilote :
  demandeur en uid 0, disque qui ne porte pas le système démarré, table inchangée, plages dans la zone utile, ni
  EFI ni MSR ni récupération sauf disque entier (`PLAN_WIPE`). Accepté : la GPT et les plages s'ajoutent à la
  garde, à part des règles ; `BLK_RESCAN` relit la table et sert les nouvelles partitions. `gpt disktest` (clé
  `--disktest`) : écriture refusée avant le plan, plans faux refusés, plan juste accepté, nouvelle partition
  écrite, partition de Windows toujours refusée.

### 9.3 mkfs ext4 sur la nouvelle partition (16/11/2026, QEMU)

* `mkfs -t ext4` (`src/lib/ext4/mkext4.c`) prend aussi une partition sans volume (`blk.nvme0p3`) : c'est ce que
  fera l'installateur. `gpt disktest` le lance sur la partition qu'il vient d'ajouter.
* Essai : `installkey.py build --disktest`, puis `installkey.py run --target build/pc/target512-run.img --write`
  (une copie du faux ZBook de 512 Mo : EFI, Windows, 312 Mo libres). Tous les résultats attendus ; puis, sous
  Linux : `sgdisk -v` « No problems found », `e2fsck -fn` sur la partition 3 sans erreur (KORINTEST, 311 Mo,
  journal, `metadata_csum`), les secteurs de l'EFI et de Windows identiques à ceux d'avant.
* À reprendre dans l'installateur :
  * la fin de la nouvelle partition n'est pas alignée sur 2048 secteurs (`gpt_free_space` rend toute la fin
    de l'espace libre ; sgdisk le signale sans erreur) : l'arrondir vers le bas ;
  * le type de partition : `gpt disktest` met « Microsoft basic data » (0700). Pour de l'ext4, mettre
    « Linux filesystem » (0FC63DAF-8483-4772-8E79-3D69D8477DE4) : sinon Windows voit une partition de données
    qu'il ne sait pas lire et propose de la formater. Garder 0700 pour FAT32 et exFAT.

### 9.4 mkfs exFAT (16/11/2026, QEMU)

* `src/lib/exfat/mkexfat.c` (autonome, comme mkext4) : région de démarrage et sa copie, une FAT au secteur 2048,
  le tas aligné sur 1 Mio, le bitmap, la table des majuscules (`upcase.inc`, compressée, tirée des données Unicode
  par `gen_upcase.py`), la racine (étiquette, bitmap, majuscules). Clusters par défaut comme Windows (4 Kio jusqu'à
  256 Mio, 32 Kio jusqu'à 32 Gio, 128 Kio au-delà), `-c` jusqu'à 32 Mio. `mkfs -t exfat` ; un volume monté est
  remonté par fatfs après le formatage.
* `src/lib/exfat/test/run.sh` : 8 Mio à 600 Gio, clusters de 512 o à 4 Mio, tous jugés sans erreur par
  `fsck.exfat -n` (exfatprogs 1.3.2, installé dans WSL le 16/11/2026) ; l'étiquette UTF-8 relue par `exfatlabel`.
  Le noyau de WSL n'a pas de pilote exFAT : pas d'essai d'écriture par Linux.
* `installkey.py build --disktest exfat` : `gpt disktest blk.nvme0 exfat` formate la nouvelle partition, la monte
  par fatfs, y écrit un dossier, un petit fichier et une copie de `/bin/bash`, démonte, remonte, relit (identique),
  démonte. Puis sous Linux : `sgdisk -v` sans remarque, `fsck.exfat` « clean » (2 dossiers, 2 fichiers).
* Repris de §9.3 dans `gpt disktest` : la fin de la partition est arrondie à 1 Mio, et le type est « Linux
  filesystem » pour l'ext4, « Microsoft basic data » pour l'exFAT et le FAT32.

### 9.5 ext4fs en lecture seule (16/11/2026, QEMU)

* Le service `src/svc/ext4fs` (sur `src/lib/ext4/ext4r.c`) monte une partition ext4 : `mount /media/data
  blk.nvme0p3 ext4fs`. Lecture seule pour l'instant (SYSTEME-FICHIERS.md §6, où on en est).
* Essai : la partition 3 du faux ZBook remplie sous Linux par `mke2fs -d` (l'arbre de `src/lib/ext4/test/mktree.py
  --no-links`), puis `installkey.py build --disktest ext4read` : montée par ext4fs, les 3 032 entrées relues
  identiques à l'original (noms, tailles, crc32), une écriture refusée (-30), un chemin trouvé sans la casse.
* L'écriture (16/11/2026) : `installkey.py --disktest ext4write` ; en mode installation, le plan (`BLK_PLAN`) puis
  `BLK_RESCAN` ouvrent la partition à ext4fs, qui sinon la monte en lecture seule. Des commandes du système
  (`/etc/e4test.sh`) écrivent dessus ; `e2fsck -fn` sans erreur ensuite.

## 10. Réponses de l'utilisateur (15/11/2026)

1. **Système de fichiers** : FAT32. Si Korin en sait d'autres plus tard, on pourra choisir lequel utiliser (§3.2 :
   liste tirée d'une table).
2. **Cas B** : une partition EFI **séparée** de `/` (§3.1, §6.1).
3. **Composants** : système de base et quelques accessoires sur la clé. Avec une connexion pendant l'installation,
   on choisit les paquets dans le dépôt en ligne ; sinon, parmi ceux de la clé (§2.1, §4).
4. **Élévation** : possible pour un ou plusieurs utilisateurs ou groupes d'utilisateurs, pas forcément `admin`
   (§5.2, `elevate.ini`).
5. **Réparer / mettre à jour** : oui, éventuellement (étape 9, plus tard).
6. **Disques** : le NVMe de l'ordinateur et les disques USB externes pour l'instant (§3.5). IDE, SATA, PATA :
   peut-être plus tard.

**Décision du 16/11/2026** : le noyau et l'initrd vont dans la **partition EFI**, montée sur **`/boot`** (et non
plus sur la partition racine). L'UEFI ne lit que le FAT32, mais `/` n'a plus besoin d'être lu par lui : l'installateur
propose donc **FAT32 ou exFAT** pour `/` (puis les systèmes de l'étude SYSTEME-FICHIERS.md, s'il y en a). À faire :
`korin.efi` charge `\korin\kernel.elf` et `initrd.tar` depuis son propre volume (l'ESP), `mkfs` apprend l'exFAT, `fatfs`
est essayé comme racine en exFAT (index `.exec`, paquets), `pkg` écrit le noyau dans `/boot`.

**Décision du 16/11/2026 (soir)** : il y a **toujours une partition EFI en premier** : celle qui existe (celle de
Windows) est réutilisée, sinon une neuve est créée (512 Mo, au début de la place de Korin). Le chargeur va dans
`\EFI\Korin\korin.efi`, le noyau, l'initrd, `local.tar` et `boot.cfg` dans `\korin\` ; rien d'autre n'est touché dans
une EFI gardée (ni `\EFI\Microsoft`, ni `\EFI\BOOT`), elle n'est jamais formatée. Ce qui suit :
* le plan du pilote reçoit `PLAN_ESP` : une plage peut être exactement la partition EFI gardée (les autres types
  protégés restent refusés) ;
* le système installé a `write = guid=<EFI>, type=efi` dans `nvme.txt` (`local.tar`) : la seule règle qui ouvre un
  type EFI (`policy.c`), pour que `pkg` mette le noyau à jour dans `/boot` (l'EFI, `blk.boot`) ;
* l'étape Disque est la liste à la Windows 7 (maquette `docs/maquettes/installateur-disque.html`) : Actualiser,
  Supprimer, Nouveau (taille au curseur ou au clavier) ; la ligne choisie est formatée (partition) ou reçoit Korin
  (espace libre) ; puis la taille de `/home` (0 à la place moins 16 Go pour le système).

## Annexe : à quoi sert `/etc/nvme.txt`

C'est la liste des **règles d'écriture du pilote NVMe** (`src/svc/nvme/policy.c`). Le disque NVMe du ZBook porte
Windows. Par défaut, le pilote n'y écrit **rien**. `nvme.txt` dit quelle partition peut être écrite
(`write = type=basic, label=KORIN OS`) et sous quel alias elle est servie (`name = korin, …`, d'où `blk.korin` que
`init.txt` monte comme `/`).

Le fichier de `/etc` n'est pas celui qui sert au démarrage. Le pilote démarre **avant** `/` (qui est sur ce disque
même). `tools/pc/build.sh` copie donc `skel/sd0/etc/nvme.txt` dans l'**initrd** (module `nvme.txt`), et c'est cette
copie qui est lue. Celle de `/etc` sert de référence lisible, et à `nvme -c /etc/nvme.txt` si l'on relance le pilote
à la main.

Aucune règle ne peut lever les refus fixes : disque entier, GPT au CRC faux, ESP, MSR, récupération, NTFS,
BitLocker, ReFS, attributs « requise par la plateforme ». Avec l'installateur, ce fichier deviendrait propre à
chaque machine : il serait écrit par l'installateur dans `\korin\local.tar`, avec des règles par GUID (§5.3).
