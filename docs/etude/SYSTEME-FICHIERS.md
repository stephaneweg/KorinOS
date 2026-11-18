# Étude : un système de fichiers pour le disque NVMe (512 Go)

Demandée le 16/11/2026. Rien n'est fait : ce document compare, recommande et propose un plan. Il ne change aucun
code.

La demande vient de l'utilisateur, qui prépare l'installation de Korin sur le NVMe de 512 Go du ZBook (étude
[INSTALLATEUR.md](INSTALLATEUR.md)) et le multi-utilisateur ([MULTI-UTILISATEUR.md](MULTI-UTILISATEUR.md)). Il veut
savoir s'il existe un système de fichiers mieux adapté qu'exFAT/FAT32 :

- **performant et fiable** ;
- **économe** : le moins de place perdue possible (petits fichiers, fragmentation, métadonnées) ;
- qui permette une **indexation** : poser des **étiquettes** (tags) et des **couleurs** sur des fichiers, chercher
  par étiquette et par couleur, ou chercher vite par extension, etc. ;
- et **implémentable dans Korin**.

## 0. Résumé

- **FAT32 et exFAT ne conviennent pas à un disque de 512 Go qui sert de système et de `/home`.** Pas de journal,
  pas de sommes de contrôle, des répertoires lus en entier à chaque recherche, pas d'attributs étendus, pas de
  propriétaire. Et surtout de gros clusters : mesuré sur la carte construite (`build/sdroot`, 16 144 fichiers,
  354 Mo), des clusters de 128 Ko (le défaut d'exFAT au-delà de 32 Go) perdent **1,8 Go**, soit 5 fois les données.
  Avec 4 Ko (ext4), il se perd 36 Mo.
- **Recommandation : ext4**, écrit par nous en C comme un service de fichiers (`ext4fs`), d'après sa spécification
  publique. Il servira `/home` d'abord (la grande partition), puis éventuellement `/`. Les raisons :
  - c'est un format éprouvé : journal, extents, répertoires indexés, petits fichiers dans l'inode, sommes de
    contrôle des métadonnées, attributs étendus ;
  - Linux (WSL compris) lit les images d'essai et **`e2fsck` sert d'oracle** : il juge chaque essai de notre code.
    C'est la meilleure protection contre un système de fichiers bogué ;
  - `mke2fs`, `e2fsck`, `tune2fs` et `debugfs` (e2fsprogs) se portent comme programmes, ce qui donne le formatage
    et la réparation sans les écrire ;
  - un disque Korin reste lisible depuis un Linux de secours.
- **L'indexation ne vit pas dans le format, mais à côté** :
  - les étiquettes et la couleur sont des **attributs étendus** du fichier (`user.korin.tags`,
    `user.korin.color`) : ils sont la vérité, gardés par le système de fichiers et copiés avec le fichier ;
  - un service **`indexd`** tient par volume des **index** (nom, extension, taille, date, étiquette, couleur), à
    la manière du BFS de BeOS. Il les range dans une base **SQLite**, déjà portée dans Korin (§3.7 : ce qu'ont donné
    les systèmes de fichiers à base de données). Il les met à jour en direct, d'après un **journal des
    changements** que tient le service du volume ;
  - l'index n'est qu'un cache : on le reconstruit à partir des attributs à tout moment (après un passage sous
    Linux, une panne, une nouvelle version).
  - Les requêtes ressemblent à `tag=travail && color=red` ou `ext=pdf`, avec des **requêtes vivantes** pour l'app
    Fichiers (dossiers intelligents).
- **Pourquoi pas un système maison** (un BFS moderne, avec index intégrés) : il irait parfaitement, mais il n'y
  aurait ni oracle, ni fsck, ni lecteur ailleurs que dans Korin. Le travail serait 1,5 à 2 fois plus long, pour
  un risque plus grand sur les données de l'utilisateur. C'est l'alternative si la lisibilité hors de Korin ne
  compte pas (question 1).
- **Taille du travail** : environ **15 000 lignes de C**, dont 9 000 pour `ext4fs` (trois fois fatfs) et 2 000 pour
  `indexd`, plus le port d'e2fsprogs, l'API, l'app Fichiers et l'installateur. Plan en 10 étapes (§6), chacune
  utile seule ; on lit d'abord, on écrit seulement après validation par `e2fsck`.
- **Données existantes** : rien n'est converti sur place. Le `/home` FAT32 du ZBook est **copié** vers la
  nouvelle partition ext4, vérifié, et l'ancien reste en lecture seule jusqu'à ce que l'utilisateur l'efface. La
  partition exFAT du Pi (`/media/sd1`) **n'est jamais touchée** ; les clés USB restent en FAT/exFAT.

## 1. Ce qui existe dans Korin

### 1.1 L'architecture

| Élément | Où | Ce qu'il fait aujourd'hui |
|---|---|---|
| Micro-noyau | `src/kernel` | messages de 512 octets (op, a–d, pid, 464 octets de données), tampons partagés, `SVC_FORWARD`. Aucun fichier dans le noyau ([FICHIERS.md](../FICHIERS.md), [MICRO-NOYAU.md](../MICRO-NOYAU.md) §4) |
| vfs | `src/svc/vfs` (C : `vfs.c`, `path.c`, `exec.c`, `pipes.c`) | arbre unique (§4.5 de FICHIERS.md) : 32 points de montage, le plus long préfixe gagne, `HOME:/`, dossier courant par pid, tubes. Il **résout le chemin**, puis le programme parle **directement** au service du volume (lecture, écriture) |
| Index `.exec` | `src/svc/vfs/exec.c` | FAT n'a pas de bit x : il est déduit du chemin (`/bin`, `/sys`, `exec.txt`) ou d'un fichier `.exec` par volume (un chemin par ligne, 32 Ko au plus, lu une fois) |
| fatfs | `src/svc/fatfs` (`fatfs.c` 1 441 lignes, `fat32.c` 869, `exfat.c` 740) | FAT32 et exFAT, noms longs (255 caractères), nœuds partagés entre ouvertures, cache de lecture par blocs de 64 Ko (**écriture directe**, rien n'est gardé à écrire), FSInfo tenu à jour |
| ramfs | `src/svc/ramfs` (619 lignes) | `/tmp` : 4 096 nœuds, recherche par parcours de la table |
| Protocole FS | `src/inc/fs.inc`, `src/svc/common/fssvc.h` | `FS_OPEN`, `FS_STAT` (taille, attributs, date en secondes), `FS_MKDIR`, `FS_UNLINK`, `FS_UTIME`, `FS_RENAME`, lecture et écriture (message, tampon de 64 Ko ou directe jusqu'à 1 Mo), `FS_READDIR` (nom, taille, attributs), `FS_INFO`, `FS_SYNC`, `FS_SEEK`. **Ni attribut étendu, ni uid, ni mode, ni numéro d'inode** |
| vfs, ops `V_*` | `fs.inc` | `V_OPEN`, `V_STAT`, `V_STATX` (le bit x), `V_CHMOD` (le bit x seulement), `V_RENAME` (`E_XDEV` entre volumes), `V_UTIME`… |
| Disque | `src/svc/nvme/nvme.c`, `policy.c`, `src/svc/common/blkserv.c` | secteurs de 512 octets, lecture, écriture, `FLUSH` (par un signal, et par un thread une seconde après la dernière écriture). **Pas de TRIM** (Dataset Management), **pas d'op de vidage pour un client** du protocole `blk` (`src/inc/blk.inc`) |
| Outils | `src/bin/mkfs` (FAT32), `find`, `grep` | **aucun fsck** dans Korin : on vérifie sous Windows ou Linux (`fsck.fat`, `fsck.exfat`) |
| App Fichiers | `src/apps/files/main.c` (C) | volumes, accès rapides, tri par nom ; **pas de recherche** |

### 1.2 Ce que les études en cours attendent du système de fichiers

- **Multi-utilisateur** (§2.3) : « FAT32 et exFAT ne gardent pas de propriétaire ». Les droits y sont donc des
  **règles par chemin** dans vfs. `stat` invente `st_uid` et `st_mode`, et `chmod` ne touche que le bit x. Des
  droits par fichier sont renvoyés à « plus tard ». Un système avec uid, gid et mode par inode les rendrait
  possibles là où l'on veut.
- **Installateur** (§3.2, réponse 1) : `/` en FAT32 pour l'instant. Le système de fichiers est « un **choix dans la
  liste** » de chaque partition, tiré d'une table (`fs` → `mkfs -t`, service, points de montage permis). Une
  partition `/home` séparée permet de réinstaller sans toucher aux données. La racine est repérée par son GUID, et
  les règles d'écriture de `nvme` par GUID vont dans `\korin\local.tar`.
- **Démarrage** : `korin.efi` lit `\korin\…` sur **son propre volume**, par les services UEFI. Le volume du chargeur
  doit donc rester en **FAT** (la seule chose que l'UEFI sait lire à coup sûr). Le volume `/boot` à la demande
  (FICHIERS.md §4.4) existe déjà pour ce cas.

### 1.3 Ce que coûte FAT sur 512 Go (mesures et chiffres)

Place perdue en fin de fichier (la moitié d'un cluster en moyenne par fichier), mesurée sur la carte construite
`build/sdroot` (16 144 fichiers, 2 220 dossiers, 354 Mo de données ; 7 747 fichiers de moins de 4 Ko) :

| Taille du bloc | Où | Place perdue | Par rapport aux données |
|---|---|---|---|
| 512 o | (repère) | 4 Mo | 1 % |
| **4 Ko** | ext4, XFS, F2FS, btrfs | **36 Mo** | 10 % |
| 32 Ko | FAT32 sur un grand volume | 399 Mo | 113 % |
| **128 Ko** | **exFAT de 32 Go à 256 To** (défaut de Microsoft) | **1 832 Mo** | **517 %** |

Un `/home` de développeur compte vite **200 000 fichiers** : le dépôt KorinOS, par exemple, suit 5 347 fichiers sans
compter `.git`, et s'y ajoutent les sources de ports, les paquets Python et les caches. À cette échelle, les mêmes
proportions donnent environ **22 Go perdus en exFAT** (128 Ko), **5 Go en FAT32** (32 Ko) et **0,45 Go à 4 Ko**. Moins
encore quand les plus petits fichiers vont dans l'inode : 1 041 fichiers de la carte font 160 octets ou moins.

Les autres limites de FAT, sans les mesurer :

- **Fiabilité** : ni journal ni somme de contrôle. Une coupure pendant une écriture laisse des clusters perdus ou
  une taille fausse (fatfs réécrit l'entrée à la fermeture). On ne s'en aperçoit qu'au prochain `fsck`, qui n'existe
  pas dans Korin.
- **Répertoires** : une recherche lit le répertoire en entier. Le hachage du nom d'exFAT accélère la comparaison,
  pas le parcours. Un dossier de 10 000 entrées coûte des centaines de Ko lus pour chaque `open`.
- **Fragmentation** : FAT32 suit une chaîne de clusters (une lecture de la FAT par cluster, que le cache masque).
  exFAT a le drapeau « contigu », mais pas d'extents dès qu'un fichier se fragmente.
- **Métadonnées** : FAT32 limite un fichier à 4 Go, n'a pas de date de création fiable et compte les dates par
  pas de 2 s. Ni l'un ni l'autre n'a d'attribut étendu, de propriétaire, de lien ou de numéro d'inode stable. Le
  « numéro » d'un fichier change à chaque renommage, ce qui gêne un index.
- **Ce qui est bon** : la simplicité (3 000 lignes de C) et la lecture par tous, Windows compris. On garde FAT et
  exFAT pour les clés, les cartes SD, le volume de démarrage et la partition du Pi.

## 2. Les candidats

### 2.1 Les critères

| Critère | Pourquoi ici |
|---|---|
| Fiabilité | journal (on rejoue après une coupure) ou copie sur écriture (COW : jamais d'écrasement en place), sommes de contrôle (métadonnées, données) |
| Place perdue | taille du bloc, petits fichiers dans l'inode (*inline*), extents (une plage de blocs = un enregistrement), place fixe des métadonnées (tables d'inodes, journal, réserve) |
| NVMe | le disque fait déjà son nivellement d'usure. Ce qui compte : grosses requêtes, peu d'écritures synchrones, TRIM. Un format pensé pour la flash brute (F2FS, NILFS2) y gagne moins |
| Attributs étendus | pour les étiquettes, la couleur, l'uid et le mode du multi-utilisateur |
| Index et requêtes | dans le format (BFS) ou à côté (§3) |
| Complexité | lignes de code d'une implémentation connue, spécification publique ou non, licence (Korin réimplémente proprement : le code d'un tiers sert de carte, pas de source à copier, sauf pour des programmes portés comme gcc, git ou bash) |
| Vérification | un fsck existe-t-il, et peut-il tourner dans Korin ? Un oracle extérieur pour juger notre code ? |
| Lisibilité ailleurs | Linux, Windows : utile pour secourir les données, utile pour juger nos images |

### 2.2 Le tableau

Les tailles de code sont des ordres de grandeur (le pilote du noyau Linux, ou l'implémentation de référence), pour
comparer entre eux. L'implémentation Korin d'un sous-ensemble serait bien plus petite.

| Système | Fiabilité | Place perdue | Attributs, index | Complexité, spécification | fsck | Lu par |
|---|---|---|---|---|---|---|
| **FAT32 / exFAT** (aujourd'hui) | aucune (bit « sale ») | clusters de 32 Ko / 128 Ko : très mauvais | aucun | ~3 000 lignes (fatfs) ; exFAT publié par Microsoft (2019) | hors Korin | partout |
| **ext4** | journal jbd2 (métadonnées ; données en mode ordonné), sommes crc32c des métadonnées (`metadata_csum`), pas des données | blocs de 4 Ko, extents, `inline_data` (~60 à 150 o dans un inode de 256 o), mais tables d'inodes fixes (réglables) | xattrs (dans l'inode, puis un bloc par fichier) ; pas d'index d'attributs | ~45 000 lignes dans Linux (jbd2 compris) ; **format documenté** (kernel.org, « ext4 Data Structures and Algorithms ») ; GPL pour le code, le format est libre | **e2fsck**, mûr, portable (C, POSIX) | Linux nativement ; Windows : outils tiers (lecture) |
| **XFS** | journal des métadonnées, CRC des métadonnées (v5) | 4 Ko, extents, B+ arbres partout ; pas de fichiers dans l'inode (seulement les petits dossiers) | xattrs ; pas d'index | ~100 000 lignes ; documenté (« XFS Algorithms & Data Structures ») | xfs_repair | Linux |
| **btrfs** | COW, sommes des données et métadonnées, instantanés | excellent : petits fichiers dans l'arbre (jusqu'à 2 Ko), compression zstd, déduplication | xattrs ; pas d'index d'attributs | ~150 000 lignes ; format décrit sur le wiki, sans spécification complète ; GPL | `btrfs check`, longtemps le point faible | Linux ; Windows : WinBtrfs (tiers) |
| **F2FS** | points de contrôle (*checkpoints*), sommes des points de contrôle (inode en option) | inline jusqu'à ~3,4 Ko, compression en option | xattrs | ~40 000 lignes ; peu documenté hors du code | fsck.f2fs | Linux, Android |
| **ZFS** | COW, sommes de tout, auto-réparation en miroir, instantanés | bon (compression, blocs variables) mais gros appétit de mémoire | xattrs ; pas d'index | 300 000+ lignes ; licence CDDL ; format long et complexe | `zpool scrub` (pas de fsck) | Linux (module à part), BSD |
| **bcachefs** | COW, sommes, compression, chiffrement | très bon | xattrs | ~100 000 lignes ; **sorti du noyau Linux officiel fin 2025** (maintenu à part), encore jeune | intégré | Linux (module à part) |
| **HAMMER2** (DragonFly) | COW, sommes (xxhash), instantanés | bon : jusqu'à 512 o dans l'inode, compression lz4 | pas d'index | ~40 000 lignes ; licence BSD ; peu documenté hors du code | hammer2 | DragonFly seulement |
| **NILFS2** | journal structuré (*log-structured*), points de contrôle continus | moyen (nettoyage à faire en tâche de fond) | xattrs limités | ~25 000 lignes ; GPL | fsck limité | Linux |
| **BFS** (BeOS / Haiku) | journal des métadonnées ; **pas** de sommes de contrôle | blocs de 1 à 8 Ko, extents ; les petits attributs dans l'inode (*small data*) | **attributs typés, index B+ arbre par attribut (nom, taille, date par défaut ; d'autres par `mkindex`), requêtes et requêtes vivantes** : la référence | ~20 000 lignes dans Haiku, **licence MIT** ; livre de Giampaolo (1999) qui décrit le format | `checkfs` | Haiku ; Linux en lecture (ancien) |
| **APFS** | COW, sommes des métadonnées (pas des données), instantanés, clones | bon (pensé pour les SSD) | xattrs ; pas d'index (Spotlight est à côté) | « Apple File System Reference » publiée pour la lecture ; l'écriture n'est pas décrite entièrement | outils Apple | macOS ; Linux en lecture (tiers) |
| **NTFS** | journal des métadonnées (`$LogFile`) ; pas de sommes | petits fichiers dans la MFT (~700 o), extents (*runs*) | ADS, EA ; journal des changements (USN) mais pas d'index d'attributs | pas de spécification officielle ; ntfs3, ntfs-3g (GPL) faits par rétro-ingénierie | chkdsk (Windows) | **Windows nativement**, Linux |
| **Maison** (« KorFS », §2.4) | journal + crc32c, au choix | conçu pour : 4 Ko, inline, extents | index intégrés comme BFS | ~10 000 lignes + fsck à écrire ; pas d'oracle | à écrire | Korin seulement |

### 2.3 Lecture du tableau

- **ZFS, btrfs, bcachefs** offrent le plus (sommes des données, instantanés, compression), mais coûtent 100 000 à
  300 000 lignes et une gestion de l'espace par COW et compteurs de références. C'est hors de portée d'une
  réimplémentation propre fiable dans un temps raisonnable. On ne gagnerait sur ext4 que sur la place (compression)
  et les instantanés.
- **XFS** est excellent pour les gros fichiers, moins bon pour beaucoup de petits (pas d'inline), et plus complexe
  qu'ext4 pour le même service.
- **F2FS et NILFS2** sont faits pour la flash brute ou les instantanés continus. Sur un NVMe qui a son propre
  contrôleur, leur avantage fond et leur nettoyage en tâche de fond reste à écrire.
- **HAMMER2** est élégant et sous licence BSD, mais personne d'autre ne le lit et il est peu documenté : pas
  d'oracle, et un format à apprendre dans le code.
- **APFS et NTFS** n'ont pas de spécification d'écriture complète. NTFS serait le seul lu par Windows sans outil, ce
  qui comptait pour le double démarrage sur une partition commune, abandonné le 12/11 (mémoire
  `armpi-ntfs-dualboot`). C'est une fonction facultative pour plus tard, pas le système de Korin.
- **BFS** est exactement l'idée des étiquettes : attributs typés, index B+ arbre et requêtes. Mais il a 25 ans : ni
  somme de contrôle, ni COW, et des flux de données à blocs indirects de taille fixe qui supportent mal un fichier
  très fragmenté. On en reprend **le modèle d'index**, pas le format.
- **ext4** n'a rien d'original, et c'est sa force ici. Format figé et documenté, journal, extents, répertoires en
  arbre haché (*htree*), inline, xattrs, sommes des métadonnées. Surtout, un oracle (`e2fsck -fn`) et des outils
  portables existent. Il lui manque les sommes des données et les instantanés.

### 2.4 Le système maison, honnêtement

Ce qu'on écrirait : blocs de 4 Ko, inodes de 512 octets avec données et petits attributs intégrés (~300 o),
extents dans un B+ arbre, répertoires en B+ arbre (noms sans casse, comme FAT), **index d'attributs intégrés**
(comme BFS), journal des métadonnées avec crc32c partout, et peut-être des sommes de données par extent.

| Pour | Contre |
|---|---|
| fait pour Korin : étiquettes dans le format, noms sans casse comme aujourd'hui, pas de place fixe perdue | **pas d'oracle** : le seul juge de notre code est notre code |
| mise à jour des index dans la même transaction que le fichier (toujours cohérents) | fsck à écrire (3 000 à 4 000 lignes), et c'est le programme le plus difficile à rendre juste |
| aucune contrainte de compatibilité | données **prisonnières de Korin** : pas de secours depuis un Linux, sauvegardes seulement par smb, tar ou rsync |
| | 1,5 à 2 fois le travail d'ext4, et des années avant d'avoir la même confiance |

C'est un beau projet. Ce n'est pas celui qu'on veut pour le **seul exemplaire** des documents de l'utilisateur.

## 3. L'indexation

### 3.1 Ce qu'on veut stocker

| Attribut | Valeur | Où dans ext4 | Où sur FAT (clés, Pi) |
|---|---|---|---|
| Étiquettes | liste de mots UTF-8 (« travail », « factures 2026 ») | xattr `user.korin.tags` (mots séparés par `\n`) | fichier annexe (§3.5), au mieux |
| Couleur | **un nom**, pas une couleur : `red`, `orange`, `yellow`, `green`, `blue`, `purple`, `gray` (UIKit les traduit en jetons du thème : jamais de couleur en dur) | xattr `user.korin.color` | idem |
| Propriétaire, droits | uid, gid, mode | champs de l'inode (natifs) | règles par chemin de vfs (MULTI-UTILISATEUR §2.3) |
| Bit x | mode | `i_mode` (remplace `.exec` sur ce volume) | `.exec` comme aujourd'hui |
| Type (facultatif) | type MIME, comme BeOS | xattr `user.mime_type` (nom courant sous Linux) | déduit de l'extension |
| Commentaire, note (plus tard) | texte, entier | `user.korin.*` | — |

Les espaces de noms `user.` et `system.` d'ext4 sont ceux de Linux : `getfattr -d` y montre les étiquettes. Une
copie par `cp -a` ou `rsync -X` les garde. Une copie vers une clé FAT les perd, et Fichiers doit le dire (question
3).

### 3.2 Les index et les requêtes

Par volume, un index par clé, sur le modèle de BFS (des B+ arbres ; en pratique les index d'une base SQLite, §3.7) :

| Index | Clé | Sert à |
|---|---|---|
| `name` | nom en minuscules, numéro d'inode | `name=*rapport*`, recherche pendant la frappe |
| `ext` | extension en minuscules | `ext=pdf` |
| `size`, `mtime`, `ctime` | entier | `size>100M`, `mtime>2026-11-01` |
| `tag` | étiquette → liste d'inodes | `tag=travail` |
| `color` | couleur → liste d'inodes | `color=red` |
| `owner` | uid | « mes fichiers », quotas plus tard |
| (déclarés) | tout attribut `user.korin.*` qu'on déclare indexé | extensions futures |

- Les entrées désignent un **numéro d'inode**, stable sous ext4 : un renommage ne touche que l'index `name` (et
  `ext`), et le chemin se recompose par l'inode du parent. C'est une raison de plus de ne pas indexer finement les
  volumes FAT, où le « numéro » d'un fichier change avec son renommage.
- Une requête combine des conditions avec `&&`, `||`, `!` et des parenthèses. Chaque condition donne une liste
  d'inodes triée, qu'on intersecte ou réunit. `tag=travail && color=red && ext=pdf` coûte trois lectures d'arbre et
  une intersection : quelques millisecondes pour 200 000 fichiers.
- On peut limiter une requête à un dossier (`in=HOME:/projets`), en vérifiant les ancêtres. Les règles d'accès du
  multi-utilisateur filtrent le résultat : on ne voit que ce qu'on peut lire.
- Les **requêtes vivantes** : celui qui pose la requête reçoit ensuite les entrées et sorties au fil des
  changements. Fichiers les utilise pour les dossiers intelligents et la vue « Étiquette : travail ».

### 3.3 Deux façons de tenir l'index

| | **A. Dans le système de fichiers** (comme BFS) | **B. Un service à côté** (comme Spotlight), nourri par les changements |
|---|---|---|
| Cohérence | parfaite : l'index change dans la même transaction du journal | l'index peut retarder ; on le dit à jour par un numéro de séquence (ci-dessous) |
| Volumes | ce format seulement | tous : ext4, ramfs, et au mieux FAT, smbfs |
| Complexité du service FS | plus grande (index dans chaque op qui écrit) | presque rien : un journal des changements en mémoire |
| Panne de l'index | c'est une panne du système de fichiers | on efface et on reconstruit, le fichier ne craint rien |
| Données lues par Linux | Linux ne connaît pas l'index : il n'est pas mis à jour par ce qu'il écrit | idem, mais on sait reconstruire : c'est déjà le cas normal |
| Plus tard : contenu des fichiers (plein texte) | ne va pas dans un système de fichiers | naturel : `indexd` lit les fichiers changés |

**Choix proposé : B, avec les attributs dans le système de fichiers.** Les xattrs sont la vérité et l'index est un
cache reconstructible. On garde ainsi les avantages de A (les étiquettes suivent le fichier, survivent à tout) sans
lier l'index au format. Le micro-noyau s'y prête : tout ce qui change un volume passe par un seul service.

### 3.4 Comment `indexd` apprend les changements

Aujourd'hui, vfs voit `V_OPEN`, création, `V_UNLINK`, `V_RENAME`, `V_MKDIR` et `V_UTIME`. Mais la lecture et
l'écriture vont **directement** au service du volume, sans repasser par vfs. vfs ne voit donc pas une taille qui
change. La source fiable est le service du volume :

- **Journal des changements** (à la manière de l'USN de NTFS), tenu par `ext4fs` :
  - un anneau en mémoire de couples (numéro de séquence, inode, parent, genre : créé, effacé, renommé, contenu,
    attribut) ;
  - le contenu est noté à la **fermeture** d'un fichier modifié (fatfs a déjà ce moment : le nœud « sale ») ;
  - une op `FS_CHANGES (depuis n)` le rend à `indexd`.
- **Séquence durable** : le numéro de séquence courant est écrit dans le superbloc (champ réservé à nous, ou xattr
  de la racine) et l'index garde le dernier qu'il a vu. Au montage :
  - même séquence : index à jour ;
  - écart encore dans l'anneau : on rattrape ;
  - anneau débordé, `s_mtime` ou `s_mnt_count` changé par un autre système (Linux a monté le volume) : on
    reparcourt.
- **Reparcours** : lire les tables d'inodes en séquence est rapide sur NVMe (2 millions d'inodes de 256 o, c'est
  512 Mo à lire), puis les xattrs des seuls inodes qui en ont. Comptons une à quelques dizaines de secondes, en
  tâche de fond, avec des résultats marqués « incomplets » pendant ce temps.
- **Abonnements** : `indexd` ouvre un canal vers chaque service de volume, comme fatfs et ramfs le font déjà pour
  les avis de fin et de fork (`SVF_ENDS`, `SVF_BIRTHS`). Pas de sondage.
- **busd** n'annonce que les **événements** utiles à l'interface : volume indexé, reparcours fini. Le flot des
  changements ne passe pas par le bus.

### 3.5 Sur FAT et exFAT (clés, `/media/sd1` du Pi)

FAT n'a ni xattr ni inode stable. Deux choix :

1. **Rien** : la recherche par nom et par extension marche par parcours (comme `find`), sans étiquettes.
2. **Un fichier annexe** `.korin/attrs` sur le volume, de la forme `chemin = étiquettes ; couleur` : simple, mais un
   renommage fait sous Windows perd l'attribut. Il ne doit **jamais** être écrit sans demande sur `/media/sd1` du
   Pi (règle du projet), donc pas d'écriture automatique sur ce volume.

Recommandé : **1** par défaut, et **2** seulement quand l'utilisateur pose une étiquette sur un volume FAT, avec
un avertissement dans Fichiers. Question 3.

### 3.6 L'API

Ce qui sert à plusieurs programmes va dans le kit du domaine : les fichiers sont dans **AppKit**. On ajoute à la
fin de sa table `.abi`, sans rien déplacer.

| Couche | Ajout |
|---|---|
| Protocole FS (`fs.inc`, `fssvc.h`) | `FS_GETXATTR`, `FS_SETXATTR`, `FS_LISTXATTR`, `FS_REMOVEXATTR` (par chemin, et par numéro de fichier ouvert), `FS_STATX2` (inode, nlink, uid, gid, mode, dates en ns, date de création), `FS_CHANGES`, `FS_CHMOD`/`FS_CHOWN` pour les vrais droits ; un service qui ne les connaît pas rend `E_NOTSUP` |
| vfs | les mêmes en `V_*`, après les règles d'accès ; `V_STATX` complète uid et mode depuis le volume quand il les a, sinon par les règles de chemin comme prévu |
| libposix | `getxattr`, `setxattr`, `listxattr`, `removexattr` (signatures de Linux : utile aux ports, `tar`, `rsync`, `cp -a` de coreutils, git) ; `st_ino`, `st_nlink`, `st_mode`, `st_uid` vrais sur ext4 |
| AppKit | `f_tags_get`, `f_tags_set`, `f_color_get`, `f_color_set`, `f_query_open(texte)`, `f_query_next`, `f_query_close`, requête vivante par un canal d'événements |
| UIKit | les couleurs d'étiquette en **jetons du thème** (Givre, Onyx Noir), une pastille et un menu « Étiquettes » réutilisables (Fichiers, boîte Ouvrir, Studio…) |
| Commandes (`/bin`) | `tag` (`tag +travail -perso fichier`, `tag -c red fichier`, `tag -l fichier`) et `query` (`query 'tag=travail && ext=pdf'`) |
| App Fichiers | pastilles de couleur, colonne « Étiquettes », menu contextuel, panneau « Étiquettes » à gauche (sous les volumes), champ de recherche (nom pendant la frappe, puis requête), dossiers intelligents enregistrés dans `HOME:/etc` |

### 3.7 Des systèmes de fichiers avec un moteur SQL ?

Question de l'utilisateur (16/11) : existe-t-il des systèmes de fichiers qui contiennent un moteur SQL, ou quelque
chose de proche, et sont-ils performants ? Oui, plusieurs ont été essayés. Ils se rangent en trois familles.

**1. La base de données *est* le stockage**

| Système | Ce que c'était | Ce qui s'est passé |
|---|---|---|
| **WinFS** (Microsoft, 2003–2006) | « Windows Future Storage », prévu pour Longhorn (Vista) : des *éléments* typés (contact, document, photo), avec des relations, rangés par le moteur de SQL Server ; le contenu des fichiers restait dans NTFS | **abandonné** en juin 2006, après une bêta en 2005. Les causes connues : un service lourd (mémoire, disque) et lent face à NTFS seul ; des schémas qui n'ont jamais convergé (qu'est-ce qu'un « document » ?) ; il fallait que les applications l'adoptent ; et il fallait garder la cohérence avec tout ce qui écrit NTFS sans passer par WinFS. Des morceaux sont devenus FILESTREAM (SQL Server 2008) et Entity Framework. Le projet Cairo / OFS des années 1990 avait échoué de la même façon |
| **IBM i** (OS/400, AS/400, depuis 1988) | la base de données (DB2 for i) fait partie du système. Les « fichiers physiques » sont des tables, dans une mémoire à un seul niveau où disque et RAM forment un même espace d'objets | **a marché**, et marche encore pour la gestion (comptabilité, stocks, banques), parce que tout le système et ses applications sont pensés autour. Pour les fichiers ordinaires (documents, sources, Unix), IBM a dû ajouter un système de fichiers classique à côté (IFS, 1994) |
| **Oracle DBFS** | un système de fichiers rangé dans des LOB d'une base Oracle, monté par FUSE (`dbfs_client`) | **marche** pour un besoin précis : des fichiers sauvegardés, répliqués et transactionnels avec la base (zones d'échange sur Exadata). Les débits sur gros fichiers sont corrects, mais les opérations de métadonnées et les petits fichiers sont bien plus lents qu'un système de fichiers natif |
| **libsqlfs** et autres FUSE sur SQLite | tout le système de fichiers dans une seule base SQLite (IOCipher du Guardian Project l'utilise, avec SQLCipher, pour un stockage chiffré dans une app Android) | marche pour un **conteneur** : un seul fichier, chiffrable, transactionnel. Lent pour les gros fichiers (le contenu est découpé en blobs) et pour les accès concurrents (un seul écrivain) ; jamais utilisé comme système de fichiers principal d'un système d'exploitation |

**2. Des attributs indexés et un langage de requêtes dans le système de fichiers, sans SQL**

| Système | Ce qui s'est passé |
|---|---|
| **BFS** (BeOS 1997, Haiku) | **a marché**, et reste la référence (§2.2). BeMail rangeait chaque courriel comme un fichier avec ses attributs (expéditeur, sujet, statut), l'app People faisait de même pour les contacts, et le Tracker montrait des requêtes enregistrées comme des dossiers. Le succès venait de ce que **le système livrait les apps qui s'en servaient**. Les limites sont documentées par l'auteur : chaque index coûte à chaque écriture ; un index créé après coup n'indexe pas les fichiers existants (Haiku a ajouté une commande `reindex`) ; les attributs se perdent à la copie vers un autre système de fichiers, par le réseau ou dans une archive qui ne les connaît pas |
| ReiserFS 4 (les « plugins ») | l'ambition d'un système de fichiers sémantique, jamais aboutie avant l'arrêt du projet |

**3. Un index à part, nourri par les changements du système de fichiers**

| Système | Moteur | Ce qui s'est passé |
|---|---|---|
| **Spotlight** (macOS, 2005) | base de métadonnées par volume (`.Spotlight-V100`), alimentée par **FSEvents** et des « importeurs » par type de fichier | **a marché**, utilisé tous les jours par des millions de personnes. Défauts connus : le premier indexage est long et lourd, et l'index se corrompt parfois (`mdutil -E` le reconstruit) |
| Recherche Windows | base ESE (Jet Blue), alimentée par le journal USN de NTFS | marche ; même modèle |
| **GNOME Tracker** (renommé TinySPARQL et LocalSearch en 2024) | RDF et SPARQL **par-dessus SQLite**, « mineurs » nourris par inotify | marche, mais longtemps connu pour des pointes de processeur et de disque pendant l'indexage, et pour la limite des surveillances inotify sur les gros dossiers personnels : il n'avait pas de flux de changements par volume, seulement des surveillances par dossier |
| KDE Baloo | LMDB | marche, avec les mêmes reproches que Tracker à ses débuts |

**Les performances : « SQLite plus rapide que le système de fichiers »**

- La mesure de SQLite (sqlite.org, « 35 % Faster Than The Filesystem ») : lire des **petits blobs** d'environ 10 Ko
  dans une base est environ 35 % plus rapide que d'ouvrir et lire autant de fichiers séparés. La base prend aussi
  environ 20 % de place de moins, sans fin de bloc perdue par objet. Le gain vient du coût de `open` et `close` par
  fichier, qu'une base n'a pas.
- La même source montre que l'avantage **s'inverse au-delà d'environ 100 Ko** par objet : le système de fichiers
  gagne pour les gros contenus.
- Pour Korin, l'écart serait sans doute plus grand : un `open` y coûte un message à vfs, puis un message au service
  du volume. Mais c'est un choix **d'application** (ranger des milliers de vignettes, de messages ou d'icônes dans
  une base plutôt qu'en fichiers). Ce n'est pas une raison de mettre SQL dans le système de fichiers.

**Ce qu'il faut en retenir**

1. Remplacer le système de fichiers par une base généraliste a échoué chaque fois que l'on a voulu garder le monde
   des fichiers à côté (WinFS, Cairo). Ça n'a marché que là où **tout** le système est la base (IBM i), et même là,
   il a fallu ajouter un vrai système de fichiers.
2. Ce qui a marché le plus largement, c'est un **index à part**, nourri par un **flux de changements du volume**
   (Spotlight et FSEvents, Recherche Windows et USN). Les attributs indexés de BFS ont marché aussi, à petite échelle
   et avec des apps faites pour.
3. Un moteur SQL est **bon pour les requêtes** et **mauvais comme stockage d'octets** pour de gros fichiers. Sa
   transaction s'ajoute aussi au journal du système de fichiers qui le porte.

**Pour Korin**, c'est exactement le choix B du §3.3 : les attributs sont dans le fichier (xattrs ext4), et l'index
est dans un service à part, nourri par le journal des changements du service du volume (le pendant de FSEvents et
de l'USN, §3.4). Pour ranger cet index, **SQLite** remplace avantageusement des B+ arbres écrits à la main :

| Point | SQLite dans `indexd` |
|---|---|
| Disponible | déjà porté : `src/extra/sqlite-3.50.4` (CPython le lie, le port de WebKit aussi) ; il reste à installer `libsqlite3.a` pour les programmes du système |
| Schéma | `files(ino PRIMARY KEY, parent, name, ext, size, mtime, uid, color)`, `tags(tag, ino)`, avec un index sur `name`, `ext`, `size`, `mtime`, `color` et `tags(tag)` ; une requête `tag=travail && color=red && ext=pdf` devient un `SELECT` avec une jointure |
| Vitesse | quelques dixièmes de milliseconde par recherche sur 200 000 fichiers ; des dizaines de milliers d'insertions par seconde dans une transaction, donc un reparcours complet ne demande que quelques secondes à la base |
| Plein texte (question 7) | **FTS5** est déjà activé dans le port de WebKit : la recherche dans le contenu viendrait presque gratuitement |
| Verrous | libposix ne verrouille pas (`flock` et `F_SETLK` rendent 0, POSIX.md). Ce n'est pas grave ici : **seul `indexd` ouvre la base**, et les apps l'interrogent par son canal, jamais en ouvrant le fichier. Mode de journal par défaut (*rollback*), ou WAL avec `locking_mode=EXCLUSIVE` (pas de mémoire partagée) |
| Double journal | l'index est un cache : `synchronous=NORMAL` suffit, et une base abîmée est effacée puis reconstruite |
| Requêtes vivantes | SQLite n'en a pas. `indexd` garde les requêtes ouvertes et teste chaque inode changé contre elles (le même filtre, en mémoire) |
| Coût | ~1,5 Mo de code dans un service, contre ~1 500 lignes de B+ arbres et de pages fantômes à écrire et à rendre sûres |

Recommandation : **`indexd` sur SQLite**. Les B+ arbres faits main ne valent la peine que si l'on refuse SQLite dans
un service du système (question 10).

## 4. ext4 dans Korin : ce qu'on prend, ce qu'on laisse

### 4.1 Les fonctions

| Fonction d'ext4 | Prise | Remarque |
|---|---|---|
| `extents`, `flex_bg`, `64bit`, `huge_file`, `dir_nlink`, `extra_isize` | oui | le cœur d'un ext4 moderne |
| `dir_index` (htree) | oui | recherche en O(log n) dans un grand dossier |
| `metadata_csum` (crc32c) | oui | sommes de contrôle de toutes les métadonnées, journal compris (csum v3) |
| `has_journal` (jbd2) | oui | mode **ordonné** : les données sont écrites avant que le journal ne valide les métadonnées |
| `inline_data` | oui | petits fichiers et dossiers dans l'inode (~60 à 150 octets avec des inodes de 256 o) |
| xattrs dans l'inode et en bloc | oui | étiquettes, couleur, type MIME |
| `casefold` (noms sans casse, UTF-8) | **à décider** (question 4) | Korin vient de FAT, qui ignore la casse : des apps écrivent peut-être `HOME:/Documents` et lisent `documents`. `casefold` le garde, au prix de tables Unicode. Sans lui, vfs peut tenter une seconde recherche sans casse |
| liens symboliques, liens durs | lecture seule d'abord | Korin n'en a pas aujourd'hui ; un lien dur est simple ; un lien symbolique demande sa résolution dans vfs (plus tard) |
| `encrypt`, `verity`, `quota`, `mmp`, `bigalloc`, `meta_bg` | non | un volume qui les demande en *incompat* est refusé ; en *ro_compat*, monté en lecture seule (règle d'ext4) |
| TRIM en ligne (`discard`) | non ; `fstrim` hebdomadaire | demande l'op Dataset Management dans `nvme` (§6, étape 0) |

### 4.2 La place fixe, réglée pour 512 Go

Les défauts de `mke2fs` sont faits pour des serveurs, et l'outil de formatage de Korin les change :

| Poste | Défaut de mke2fs | Pour Korin | Gain |
|---|---|---|---|
| Inodes | 1 pour 16 Ko → 32 M inodes × 256 o = **8 Go** | 1 pour 64 Ko → 8 M inodes = **2 Go** (largement assez pour des millions de fichiers) | 6 Go |
| Réserve pour root | 5 % = **25 Go** | 1 % (≈ 5 Go), ou 0 sur `/home` | 20 à 25 Go |
| Journal | 1 Go | 256 Mo (suffisant pour un poste) | 0,75 Go |
| Bloc | 4 Ko | 4 Ko (taille de page, alignée sur le NVMe) | — |

On garde au total environ **2,5 Go de place fixe** pour 512 Go (0,5 %), contre 22 Go perdus en exFAT pour 200 000
fichiers, sans compter la réserve.

### 4.3 Le service `ext4fs`

- **Où** : `src/svc/ext4fs/`, en C, sur le modèle de fatfs, pour le Pi et le PC. Il réutilise `fsowner.h` (ouvertures,
  propriétaires, fork), le tampon partagé, `FS_READ_DIRECT` et le protocole `blk`.
- **Cache** : fatfs écrit directement ; ext4fs a besoin d'un cache **en écriture différée** pour les métadonnées
  (bitmaps, inodes, blocs d'extents, dossiers), lié au journal. Une transaction rassemble les blocs changés et se
  valide toutes les 5 s ou sur `fsync`/`FS_SYNC`. Les données des fichiers restent en écriture directe (mode
  ordonné : elles partent avant la validation).
- **Ordre des écritures** : le journal exige qu'un bloc de validation n'atteigne le disque qu'après les blocs qu'il
  couvre. Le cache du NVMe est volatil, donc il faut une **op `BLK_FLUSH` pour un client** (aujourd'hui seulement par
  signal et par un thread après une seconde), ou l'écriture FUA. C'est l'étape 0.
- **Concurrence** : un seul thread de service, comme fatfs, avec un thread de validation du journal. On verra plus
  tard pour paralléliser les lectures.
- **Limites du protocole** qui restent : chemins de 256 octets (`PATH_MAX` de vfs), 464 octets de données par
  message (au-delà, les xattrs passent par le tampon partagé), 64 fichiers ouverts par process.

### 4.4 Le formatage et la réparation

Deux voies (question 2) :

1. **Porter e2fsprogs** (`mke2fs`, `e2fsck`, `tune2fs`, `debugfs`, `resize2fs`) : du C portable, GPL pour les
   programmes et LGPL pour `libext2fs`. On le traite comme gcc, git ou bash : un port, pas du code intégré au
   système. Il faut un accès direct au périphérique `blk.*` démonté (comme `mkfs` aujourd'hui) par une petite
   couche `unix_io` de Korin. C'est la voie recommandée : l'outil de réparation le plus éprouvé qui soit, tout de
   suite.
2. **Écrire `mkfs -t ext4`** nous-mêmes (~600 lignes, facile) et **un fsck minimal** (lecture, contrôle des
   sommes, rejeu du journal, bitmaps recalculés, ~3 000 lignes). Plus long, moins sûr.

Dans les deux cas, `e2fsck -fn` sous WSL juge les images d'essai produites par Korin sous QEMU.

## 5. Les partitions sur le NVMe du ZBook

Ce que l'installateur proposerait (INSTALLATEUR.md §3.2 : une ligne de plus dans la table des systèmes de
fichiers) :

| Partition | Taille | Système | Remarque |
|---|---|---|---|
| ESP | existante (ou 512 Mo créée) | FAT32 | jamais écrite si elle est à Windows |
| Windows (NTFS, récupération) | inchangées | — | règles fixes de `nvme` |
| `/` Korin | 32 à 64 Go | **FAT32 d'abord** | contient le chargeur et `\korin\` ; le système fait ~4 Go avec `/devtools` |
| `/home` | le reste (~400 Go selon la place laissée à Windows) | **ext4** | les données, les étiquettes, l'index ; réinstaller sans y toucher |

Ensuite, une fois ext4 éprouvé, on pourra passer `/` en ext4 avec un petit **`/boot` en FAT32** (le volume à la
demande de FICHIERS.md §4.4) pour le chargeur, le noyau et l'initrd. Il faudra alors que l'initrd lance `ext4fs`
pour la racine (`init.txt` : `vfs / $bootpart ext4fs`). Ce n'est pas urgent : la place perdue se trouve surtout dans
`/home`, et la FAT32 de `/` ne contient que ce qu'on réinstalle.

L'index de `/home` vit dans `/home/.korin/index/` : sur le même volume, donc il suit le volume s'il est monté
ailleurs.

## 6. Plan par étapes

Chaque étape s'essaie sous QEMU, sur une image formatée par `mke2fs` sous WSL et vérifiée par `e2fsck -fn` après
chaque campagne. Rien n'écrit sur le NVMe du ZBook avant l'étape 8.

| # | Étape | Taille (C) | Fini quand |
|---|---|---|---|
| 0 | **Bloc** : op `BLK_FLUSH` (et FUA) pour un client ; `BLK_TRIM` par Dataset Management dans `nvme` ; mêmes ops dans `sdhci`, `usbstorage` et `imgblk` (rien ou repli) | ~400 | un client vide le cache du disque et sait quand c'est fait |
| 1 | **Protocole** : xattrs, `FS_STATX2`, `FS_CHMOD`/`FS_CHOWN`, `FS_CHANGES` dans `fs.inc`/`fssvc.h` ; vfs ; libposix ; AppKit. **ramfs les prend en premier** (xattrs en mémoire) pour essayer l'API sans ext4 | ~1 000 | `setfattr`/`getfattr` (ou `tag`) marchent sur `/tmp` |
| 2 | **`ext4fs` en lecture seule** : superbloc, groupes, inodes, extents, htree, inline, xattrs, contrôle des sommes, refus des fonctions inconnues ; journal propre exigé (sinon lecture seule sans rejeu) | ~3 000 | une image de Linux se lit entière ; `git status` sur un dépôt copié ; grands dossiers rapides |
| 3 | **Écriture sans journal**, derrière une option `rw-nojournal` réservée aux essais : allocation (bitmaps, flex_bg), extents (ajout, découpe, libération), htree (ajout, découpe), inline (sortie vers un bloc), xattrs, liens, renommage, troncature | ~4 000 | `fstest2`, `posixtest file`, `git clone`, une compilation gcc puis `e2fsck -fn` sans erreur, 100 fois de suite |
| 4 | **Journal jbd2** (transactions, validation, rejeu au montage) et sommes à l'écriture ; `fsync` ; cache différé des métadonnées | ~1 500 | essais de coupure : QEMU tué au hasard pendant des écritures, 500 fois ; `e2fsck -fn` sans erreur après chaque rejeu |
| 5 | **Outils** : `mkfs -t ext4` (fait) + **fsck maison validé contre `e2fsck`** (décision du 18/11, §9 question 2) ; réglages de §4.2 ; `fstrim` ; `df`, `mount` et `eject` connaissent le type | port + ~500 | formater, vérifier, réparer depuis Korin |
| 6 | **`indexd`** sur SQLite (§3.7 : `libsqlite3.a` installée pour les programmes du système, schéma, traduction des requêtes en SQL, base reconstruite si elle est abîmée) ; journal des changements dans ext4fs (et ramfs) ; reparcours ; requêtes, requêtes vivantes | ~2 000 (3 500 avec des B+ arbres faits main) | `query 'tag=x && ext=pdf'` en moins de 10 ms sur 200 000 fichiers ; un renommage apparaît en moins d'une seconde |
| 7 | **Interface** : commandes `tag` et `query` ; UIKit (pastilles en jetons du thème, menu Étiquettes) ; Fichiers (pastilles, panneau, recherche, dossiers intelligents) ; boîte Ouvrir ; aides (`help_fr.md`, guide de l'API C) | ~1 500 | l'utilisateur étiquette et retrouve ses fichiers sans ligne de commande |
| 8 | **Installateur et migration** : `ext4` dans la table des systèmes de fichiers, partition `/home` (cas A et C), outil de migration (§7), règles `nvme.txt` par GUID dans `local.tar` | ~600 | installation sur le disque d'essai de QEMU (`mkgpt.py`), puis sur le ZBook |
| 9 | **Multi-utilisateur** : sur ext4, l'uid et le mode de l'inode s'ajoutent aux règles par chemin de vfs (ou les remplacent, question 6) ; `chmod`, `chown` vrais | ~500 | `ls -l` montre de vrais propriétaires ; un home `0700` est refusé aux autres |

**Où on en est (16/11/2026)** :

- **`mkfs -t ext4`** (étape 5, une partie) : `src/lib/ext4/mkext4.c`, jugé par `e2fsck -fn` de 16 Mio à 600 Gio.
- **Étape 2, une première partie** : `src/lib/ext4/ext4r.c` (le lecteur, autonome) et `src/svc/ext4fs` (le service,
  lecture seule). Le lecteur prend les blocs de 1 à 64 Kio, les extents et les blocs indirects (ext2, ext3), 64bit,
  flex_bg, htree (en parcours linéaire), inline_data (i_block et `system.data`), les liens symboliques (suivis).
  `src/lib/ext4/test/run_read.sh` le juge contre des images de `mke2fs -d` : ext4 en blocs de 1, 4 et 64 Kio,
  inline_data, sans 64bit, ext3, ext2. Sous QEMU, ext4fs relit un arbre de 3 032 entrées identique à l'original
  (`installkey.py --disktest ext4read`). Écrire rend `E_ROFS` (-30). Les noms : la casse exacte, sinon sans la casse
  ASCII. `FS_INFO` rend `FS_T_EXT4` (5). Un volume monté que `mkfs -t ext4` formate est remonté par ext4fs.
- **Étape 3 (écriture sans journal), faite** : `src/lib/ext4/ext4w.c`. Créer, écrire, tronquer, effacer, renommer
  (fichiers et dossiers), utime ; extents de profondeur 0 ou 1 (le tableau des extents réécrit à chaque changement,
  ses blocs d'arbre repris), allocation par plages contiguës, metadata_csum, 64bit, flex_bg, BLOCK_UNINIT et
  INODE_UNINIT. Refusé : volume sans extents (ext2, ext3), journal à rejouer, gdt_csum sans metadata_csum, bigalloc,
  quota, mmp (lecture seule) ; un dossier indexé par htree, un fichier en blocs indirects ou en ligne (-38).
  `src/lib/ext4/test/run_write.sh` : 11 séries d'opérations au hasard, chacune relue contre un modèle puis jugée
  par `e2fsck -fn` sans erreur. ext4fs écrit par ext4w : le superbloc est « pas propre » pendant une opération et
  redevient « propre » après chacune (vfs arrête le service au démontage sans toujours envoyer FS_SYNC). Sous QEMU
  (`installkey.py --disktest ext4write`) : mkdir, cp, cp -r, mv, rm, rm -r et des redirections de bash sur la
  partition ; `e2fsck -fn` sans erreur ensuite, le contenu juste (debugfs).
- **Le journal (étape 4) est au plan (décision du 18/11/2026)**, avec l'étape 0 (`BLK_FLUSH`, pas encore dans `nvme`)
  qu'il exige. Il n'est pas fait : une coupure pendant une écriture peut laisser une incohérence, que
  seul e2fsck répare (pas encore dans Korin).
- **Pas encore fait de l'étape 2** : les sommes de contrôle ne sont pas vérifiées à la lecture, les xattrs ne sont
  pas rendus, une recherche dans un grand dossier est linéaire (pas par le hachage htree), les lectures passent
  par le cache bloc par bloc (pas de `BLK_READ_D` direct pour les gros fichiers). Pas encore dans la construction
  du Pi (`build.ps1`), ni dans la détection des clés USB (usbstorage monte tout par fatfs).

**Total** : environ **15 000 lignes de C**, dont 9 000 pour `ext4fs` (trois fois fatfs) et 2 000 pour `indexd`, plus
le port d'e2fsprogs. Les étapes 0 à 2 sont sans risque pour les données. Les étapes 6 et 7 peuvent commencer dès
l'étape 1 sur ramfs, en parallèle d'ext4fs.

## 7. Les données existantes

| Où | Aujourd'hui | Ce qui arrive |
|---|---|---|
| ZBook, `/` (partition `KORIN OS`, FAT32) | le système et le dossier `/home` | reste FAT32 ; le système se réinstalle par l'installateur |
| ZBook, `/home` (dossier de la même FAT32) | documents, données d'app | **copié** vers la nouvelle partition ext4 par un outil `homemigrate` (programme `system`) : copie de chaque fichier, contrôle par crc32 et taille, bit x repris de `.exec`, dates gardées (au pas de 2 s de FAT32). L'ancien `/home` est ensuite monté **en lecture seule** sur `/media/oldhome` jusqu'à ce que l'utilisateur l'efface lui-même |
| Pi, `/media/sd1` (exFAT) | la partition personnelle de l'utilisateur | **jamais touchée**, ni convertie, ni annotée (règle du projet). Ext4 sur le Pi serait une autre carte, si l'utilisateur le veut un jour |
| Clés USB, cartes | FAT32 / exFAT | inchangées ; lisibles partout, c'est leur rôle |

Une **conversion sur place** (exFAT vers ext4 sans copie) est possible en théorie, comme `btrfs-convert`. Elle est
écartée : c'est le genre d'outil dont un seul bug détruit tout, pour économiser une copie qu'un NVMe fait en
quelques minutes.

## 8. Risques

| Risque | Gravité | Parade |
|---|---|---|
| Un bug d'`ext4fs` abîme des données | haute | lecture seule d'abord ; `e2fsck` comme oracle à chaque étape ; essais de coupure ; ancien `/home` gardé ; option de montage en lecture seule par défaut jusqu'à la fin de l'étape 4 |
| Journal sans barrière (cache volatil du NVMe) | haute | étape 0 (`BLK_FLUSH`/FUA) avant toute écriture journalisée |
| Apps qui supposent des noms sans casse | moyenne | `casefold`, ou seconde recherche sans casse dans vfs, plus un mode « journal » pour repérer les apps fautives (comme l'introduction douce des règles d'accès) |
| Index en retard ou faux | basse | c'est un cache : séquence, reparcours, commande `query --rebuild` |
| Linux écrit sur le volume (secours) | basse | Linux respecte le format et les xattrs ; l'index se reparcourt au montage suivant |
| Lecture depuis Windows | à savoir | Windows ne lit pas ext4 seul. WSL (`wsl --mount`) ne peut pas monter une partition du disque où tourne Windows ; il faut un outil tiers (lecture : DiskInternals Linux Reader ; lecture et écriture : Paragon) ou une clé Linux. Si c'est important, il faut le dire (question 1) |
| Taille du chantier | moyenne | chaque étape est utile seule ; on peut s'arrêter après l'étape 2 (lire des disques Linux) ou l'étape 7 sans ext4 en écriture (étiquettes sur ramfs, recherche par nom partout) |
| Licence | basse | `ext4fs` réimplémenté d'après la documentation du format (règle du projet) ; e2fsprogs seulement comme programmes portés |

## 9. Questions pour l'utilisateur

1. **Lisibilité hors de Korin** : est-ce important de pouvoir lire `/home` depuis un Linux (secours, clé live), ou
   depuis Windows avec un outil tiers ? Si non, le système maison (§2.4) redevient possible ; ext4 reste la
   recommandation pour la sûreté (oracle, fsck).
2. **e2fsprogs** : accepte-t-on de porter `mke2fs` et `e2fsck` (GPL, comme gcc et git), ou faut-il tout écrire
   nous-mêmes (formatage et fsck minimal, plus long) ?
   **Réponse (18/11/2026)** : `mkfs -t ext4` est déjà à nous (`src/lib/ext4/mkext4.c`). Le **fsck est écrit par
   nous aussi** (un `fsck -t ext4` de Korin), mais **validé contre `e2fsck`** : sur des images saines et abîmées
   exprès (sous WSL : `mke2fs`, puis corruptions choisies — bitmaps faux, compteurs, inodes orphelins, entrées de
   dossier cassées, extents qui se chevauchent, sommes crc32c fausses, journal à rejouer), notre fsck doit trouver
   les mêmes défauts que `e2fsck -fn`, et après sa réparation `e2fsck -fn` ne doit plus rien trouver. Pas de port
   d'e2fsprogs.
3. **Étiquettes sur FAT** (clés, `/media/sd1` du Pi) : pas d'étiquettes du tout (proposé), ou un fichier annexe
   `.korin/attrs`, au risque de les perdre au premier renommage fait sous Windows ? Et quand un fichier étiqueté
   est copié sur une clé : simple avertissement, ou rien ?
4. **Casse des noms** : `/home` en ext4 doit-il ignorer la casse comme aujourd'hui (`casefold`), ou devenir
   sensible à la casse comme Linux ?
5. **Couleurs** : les sept couleurs nommées (rouge, orange, jaune, vert, bleu, violet, gris, comme macOS) plus des
   étiquettes libres, ou des étiquettes qui ont chacune leur couleur ?
6. **Droits** : sur ext4, les droits par fichier (uid, mode) **remplacent**-ils les règles par chemin du
   multi-utilisateur pour ce volume, ou s'y **ajoutent**-ils (la règle de chemin d'abord) ?
7. **Recherche dans le contenu** (plein texte, comme Spotlight) : à prévoir dans `indexd` plus tard, ou noms et
   attributs seulement ?
8. **`/` aussi en ext4** plus tard (avec un petit `/boot` FAT32), ou `/` reste en FAT32 ?
9. **Instantanés et compression** : en veut-on ? Si oui, ext4 n'en a pas, et il faudrait revoir le choix (btrfs
   ou un COW maison), pour un travail bien plus grand.
10. **SQLite dans un service du système** : `indexd` peut-il lier SQLite (domaine public, déjà porté pour CPython
    et WebKit, ~1,5 Mo), ou faut-il des B+ arbres écrits par nous (§3.7) ?

## 10. Les étiquettes telles que l'utilisateur les veut (16/11/2026)

**Décision** : `indexd` (étiquettes et couleurs) se fera **plus tard**, après ext4 et l'installateur. Le modèle
voulu par l'utilisateur :

* **Une couleur est une étiquette** (d'un autre type), pas un attribut à part.
* Deux tables (SQLite) :
  * `tags (id, type, name)` : la liste des étiquettes ; `type` dit étiquette ou couleur ;
  * `tag_files (tag, path)` : les fichiers de chaque étiquette, **indexée sur `tag`** (un entier), avec le
    chemin complet du fichier.
* **Ajouter une étiquette à un fichier** : l'étiquette entre dans `tags` si elle n'y est pas, puis le chemin
  complet entre dans `tag_files`.
* **Recherche** dans la barre de recherche (de Fichiers), avec plusieurs étiquettes et des parenthèses :

  | Écrit | Sens | SQL (sur `tag_files`) |
  |---|---|---|
  | `A + B` | A **ou** B | `path IN (… tag=A) OR path IN (… tag=B)` (ou `UNION`) |
  | `A * B` | A **et** B | `INTERSECT` |
  | `A !B` | A **et pas** B | `EXCEPT` |
  | `(A * (B !C)) + D` | composition | les mêmes, imbriqués |

* Un **petit analyseur d'expressions** traduit la saisie en requête SQL : précédence `!` (et pas) puis `*` (et)
  puis `+` (ou), parenthèses, noms d'étiquettes (entre guillemets s'ils ont des espaces), chaque nom remplacé
  par son `id` (paramètre lié, jamais collé dans le texte SQL).

Ce que l'étude ajoute à ce modèle quand on le fera : les étiquettes restent aussi dans les attributs étendus
du fichier (§3.1) pour survivre à une copie ou un renommage hors de Korin, et un renommage ou un effacement
met `tag_files` à jour par le journal des changements du volume (§3.4) ; sinon les chemins de `tag_files`
deviendraient faux.
