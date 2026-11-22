# vfs et fatfs : rendre le volume système rapide sous charge (à faire avant la reconstruction de WebKit)

Note de l'agent local (ZBook, 22/11/2026), pour la session du cloud. Décision de l'utilisateur : les points **1, 2, 4 et 5** ci-dessous sont à faire **avant** la reconstruction de WebKit
de `docs/agents/JET-LOCAL-BUILD.md` (le point 3 est facultatif ; 4 et 5 vont ensemble).

## Ce qui a été vu

Recompiler WebKit dans Korin (10 compilations en parallèle, sources et SDK sur le volume système FAT32) : après une
bonne première demi-heure, le rythme tombe à **≈ 20 objets par quart d'heure**. Le service `fatfs` est à **99 % d'un
cœur**, les compilateurs à **2-3 %** (≈ 1000 à 3000 appels système par seconde chacun : ils attendent leurs fichiers).
Copier le SDK (≈ 53 000 fichiers, des dossiers de plusieurs milliers) du FAT32 vers `/home` (ext4) : **62 Ko/s**. Sur
le même ZBook, Windows (même disque, FAT32) ne s'effondre pas ainsi.

## Pourquoi (lu dans le code)

1. **Pas de cache des noms ; une recherche linéaire par dossier.** `fat_lookup` (`src/svc/fatfs/fat32.c:464`) résout
   un chemin composant par composant depuis la racine ; pour chacun, `dir_find` relit le dossier entrée par entrée,
   reconstruit chaque nom long (LFN, UTF-16 → UTF-8) et compare. Rien n'est retenu, ni dans `fatfs` ni dans `vfs`
   (`src/svc/vfs` n'a pas de cache des chemins). Un échec parcourt tout le dossier. Un compilateur cumule les deux
   cas : les commandes de WebKit ont ≈ 150 chemins `-I`, chaque `#include` est essayé dans des dizaines de dossiers
   avant d'être trouvé, et `webcore/WebCore/PrivateHeaders/WebCore` (≈ 3 000 noms, 3 à 4 entrées de 32 o par nom long)
   est relu en entier à chaque essai raté. Le coût ≈ (taille du dossier) × (nombre de recherches).
2. **Un seul fil pour tout le volume** : toutes les requêtes de tous les process passent l'une après l'autre.
3. **Des caches petits** : celui des secteurs (où sont les entrées de dossier) fait `BC_N` = 256 secteurs = **128 Ko**,
   en correspondance directe (`fatfs.c:27`) ; un dossier de 3 000 noms longs (≈ 300 Ko) n'y tient pas et se relit à
   chaque parcours. Celui des données (`cc_*`) est plafonné à 256 Mo pour `fs.root` (`CC_BLOCKS_MAX`, `fatfs.c:34`),
   64 Mo pour les autres volumes, même sur 32 Go.
4. **Le pilote NVMe sérialise tout.** `blkserv` (la couche commune des périphériques de blocs) a « un thread par
   périphérique, un appel au pilote à la fois » (`src/svc/common/blkserv.c:9`) ; `nvme` prend un verrou global pour
   toute une requête (`lock()` … `unlock()` autour de `io()`, `src/svc/nvme/nvme.c`) ; **une seule paire de files
   d'E/S**, au plus `QD_MAX` = 16 commandes en vol, et seulement les morceaux d'**une même** requête : deux requêtes
   de clients différents ne sont jamais en vol ensemble. Et chaque requête attend sa fin en tournant (`wait_slots` :
   boucle active puis `SYS_IRQ_WAIT`) : le fil `nvme` était à 91 % d'un cœur pendant la copie du SDK. Pour de petites
   lectures dispersées (entrées de dossier, en-têtes), le débit est plafonné par la latence d'une commande à la fois,
   alors que le SSD en traiterait des dizaines en parallèle sur plusieurs files.
5. **Le coût des services en espace utilisateur** (programme → `vfs` → `fatfs` → `nvme`, des allers-retours de
   messages) est réel mais fixe par opération : il devient grave parce que 1 et 2 multiplient les opérations et les
   mettent en file. Avec un cache des noms, la plupart des `open` / `stat` n'atteindraient plus `fatfs`.

Windows : le pilote FAT garde l'arbre des dossiers et fichiers déjà vus (une table des noms : une recherche, même
ratée, ne relit pas le dossier), un cache de fichiers à la taille de la mémoire libre avec lecture anticipée, et des
requêtes servies en parallèle sous des verrous fins.

## Ce qu'il faut faire (1, 2, 4 : demandés ; 3 : si le temps le permet)

1. **Un cache des noms dans `vfs`** (indépendant du système de fichiers : FAT32, exFAT, ext4, ramfs en profitent) :
   chemin → (volume, identité du fichier, attributs) pour les chemins trouvés, **et les chemins absents** (le cas des
   `-I` du compilateur : un `open` ou `stat` raté n'interroge plus le service). Invalidation par `vfs` lui-même à
   chaque création, renommage, effacement, `mkdir`, `rmdir` et démontage (ce sont des requêtes qui passent par lui) ;
   taille bornée (LRU), proportionnée à la mémoire. Attention aux volumes qu'un autre que `vfs` peut changer (réseau :
   `gdrivefs`, `smbfs`) : ne pas mettre leurs absents en cache, ou avec un délai court.
2. **Un index par dossier dans `fatfs`** (FAT32 et exFAT) : une table de hachage des noms (minuscules pour FAT32,
   insensible à la casse comme `name_equal`) construite au premier parcours d'un dossier, gardée pour les dossiers
   récemment utilisés, tenue à jour par les créations, renommages et effacements du service ; `dir_find` devient
   O(1). Les entrées de dossier changées ailleurs n'existent pas (un seul `fatfs` par volume, `vfs.c:24`).
3. *(facultatif)* **Des caches proportionnés** : le cache des secteurs associatif et de plusieurs Mo (les entrées de
   dossier y vivent), le cache des données au-delà de 256 Mo quand la machine a 32 Go (il rend déjà de la mémoire sur
   `SVC_SIG_LOWMEM`).
4. **Les lectures en parallèle dans `fatfs`** : plusieurs fils servant les lectures (`open` sans création, `read`,
   `stat`, `readdir`), un verrou pour les écritures et la FAT ; le cache et l'index protégés. Penser au tampon partagé
   avec le périphérique bloc (`PARTAGE`, `fatfs.c:30`) : un par fil.

5. **Plusieurs requêtes en vol dans `blkserv` et `nvme`** (sans quoi 4 ne sert qu'aux lectures servies par le
   cache) : accepter les requêtes de plusieurs clients en même temps (plusieurs fils dans `blkserv` pour un même
   disque, ou une file de requêtes asynchrone) ; dans `nvme`, remplacer le verrou global par des emplacements de
   commande pris dans un ensemble commun (plus de 16), les fins de commande rendues à leur demandeur par leur `cid` ;
   idéalement **une paire de files NVMe par fil** (comme Linux, une par cœur : pas de verrou à l'envoi) ; l'attente par
   interruption plutôt qu'en tournant quand plusieurs commandes sont en vol. Les écritures restent revérifiées par
   `policy_write_ok` au plus bas, une par une (la garde des partitions ne doit pas faiblir). Les autres pilotes de
   `blkserv` (`sdhci`, `rtsx`, `usbstorage`, `imgblk`) gardent un appel à la fois tant qu'ils ne le déclarent pas.

Ordre conseillé : 1 et 2 d'abord (ils retirent la plupart des accès au disque, quel que soit le pilote), puis 4 et 5
ensemble.

## Comment vérifier

- Un banc : N process qui ouvrent / `stat` des fichiers d'un dossier de 3 000 noms (présents et absents), mesurer
  les opérations par seconde avant et après (`tools/…`, à écrire), et la charge de `fatfs` et de `nvme`
  (`mcp__korin__cpu`). Pour `nvme` : N process qui lisent des blocs de 4 Ko dispersés (IOPS), avant et après.
- Puis, sur le ZBook : `tools/webkit/local/rebuild-sdk.py 10 "…"` avec le SDK dans `/devtools` (FAT32) : le rythme ne
  doit plus s'effondrer ; et la copie d'un dossier de milliers de fichiers.
- Ces services sont dans `/sys` et `vfs` sert tout le système : construire et essayer sous QEMU d'abord, garder
  l'ancien binaire, et **demander à l'utilisateur avant d'installer** sur le ZBook (CLAUDE.md, « Le système
  lui-même »).

## Fait (session du cloud, 22-23/11/2026) : points 1, 2, 4 et 5

Branche `claude/serene-newton-051kkg`. Essayé sous QEMU seulement (q35, 4 cœurs, **sans KVM**, NVMe de QEMU, machine
chargée par la construction de WebKit) : **à essayer sur le ZBook**, en gardant les anciens `vfs`, `fatfs`, `nvme`.

| Point | Ce qui est fait | Fichiers |
|---|---|---|
| 1. cache des noms dans `vfs` | Chemin du service (rootpath compris, minuscules pour `fatfs`) → présent, ou absent avec son erreur. Un `open` en lecture ou un `stat` d'un absent connu est répondu par `vfs` ; un `open` d'un présent connu est transmis comme avant (`SVC_FORWARD`) ; un `open` d'un inconnu est fait par un appel (au nom du programme, `d` = son pid) pour en apprendre le résultat. Seulement `fatfs`, `ext4fs`, `ramfs` (pas `gdrivefs` ni `smbfs`). Création, `mkdir`, effacement : l'entrée du chemin retirée ; renommage, montage, démontage, fin d'un service : tout oublié (génération). Un résultat appris pendant qu'un changement passait n'est pas gardé (compteur). 2048 × 4 entrées. | `src/svc/vfs/ncache.c`, `path.c` (`vers_volume`), `vfs.c`, `exec.c` |
| 2. index par dossier dans `fatfs` | Table de hachage des noms (minuscules ASCII) de chaque répertoire lu, construite à la première recherche, tenue à jour par les créations, réécritures (`vol_maj`) et effacements de `fatfs` (FAT32 : en place ; exFAT : l'index du dossier changé est jeté et relu). 1024 dossiers, 96 Mo au plus sur le PC (le plus ancien rendu). `vol_chercher` descend le chemin par les index (`fat_lookup`/`exfat_lookup` ne servent plus). | `src/svc/fatfs/dirindex.c`, `fatfs.c` |
| 4. lectures en parallèle dans `fatfs` | 8 fils sur le PC (1 sur le Pi). Lecteurs (open sans écriture, read, stat, readdir, seek, info, close sans écriture) en parallèle, le verrou relâché pendant leurs lectures du périphérique, chacun dans sa zone du tampon partagé ; écrivains seuls (ils attendent la fin des lecteurs). Une op tient son fichier ouvert (`fo->busy`) ; une fermeture qui doit écrire repasse en écrivain ; les avis de fork et de fin de process passent dans l'ordre (`recv`), la fin en écrivain seulement si un de ses fichiers doit écrire. | `src/svc/fatfs/fatfs.c`, `src/svc/common/fsowner.c` |
| 5. plusieurs requêtes en vol (`blkserv`, `nvme`) | `struct blk_driver` : champ `concurrency` ajouté à la fin (`BLK_DRIVER_V3`) ; `blkserv` sert alors chaque périphérique par autant de fils et appelle `read`/`read_sg` sans son verrou (écritures et le reste : une à la fois, comme avant ; les autres pilotes ne changent pas). `nvme` : jusqu'à 8 paires de files d'E/S (ce que le contrôleur accorde), chacune avec ses places, ses listes PRP, son tampon et son verrou ; une requête prend une file libre ; toutes sur le vecteur 0. La garde `policy_write_ok` est inchangée. | `src/svc/common/blkserv.[ch]`, `src/svc/nvme/nvme.c` |

Le point 3 (caches proportionnés) n'est pas fait.

### Mesures (`fsbench`, nouvelle commande `src/bin/fsbench`)

Dossier de 3 000 fichiers vides (`Present_header_file_N.h`) sur le FAT32 de `/`, chaque process fait 50 % d'`open`
et 50 % de `stat`, moitié présents, moitié absents, pendant 5 s :

| Process | Avant (main) | Après | |
|---|---|---|---|
| 1 | 200 op/s | 947 op/s | × 4,7 |
| 4 | 4 × 51 = 204 op/s | ≈ 4 130 op/s | × 20 |
| 8 | 8 × 25 = 200 op/s | ≈ 4 810 op/s | × 24 |

Aucun résultat faux. Avant, le total reste à 200/s quel que soit le nombre de process (tout passe par un fil de
`fatfs` qui relit le dossier) ; après, il monte avec eux (sous QEMU sans KVM, le coût des messages domine).
Écritures en parallèle : 4 process créent chacun 400 fichiers dans 4 dossiers, puis tous sont retrouvés par
l'index (`fsbench run /w1 400 4`, 0 faux), un effacement est vu aussitôt.

Lectures de 4 Ko à des positions au hasard dans un fichier de 64 Mo : dans le cache de `fatfs`, rien ne change
(≈ 200/s, limité ailleurs : `FS_READ_DIRECT` et la projection des pages de l'appelant ?) ; cache coupé (`fscache
off`) : 8 process finissent ensemble en 5,7 s au lieu de 20,8 s (l'ancien `nvme` les sert l'un après l'autre),
mais une lecture prend ≈ 250 ms sous QEMU dans les deux cas : **à regarder** (la marche de la chaîne FAT d'un gros
fichier sans le cache ?). Un vrai banc NVMe (IOPS de blocs dispersés sur le ZBook) reste à faire.

### À essayer sur le ZBook (par l'utilisateur)

1. Garder les anciens `/sys/svc/vfs`, `fatfs`, `nvme` (et l'initrd : `vfs`, `fatfs`, `nvme` y sont).
2. Installer `pc` construit depuis cette branche (noyau du 22/11 avec le compteur des pages, 917b76a, compris).
3. `fsbench mk /fsb 3000`, `fsbench run /fsb 3000 8 5000` avant et après ; puis `tools/webkit/local/rebuild-sdk.py
   10 "…"` avec le SDK sur le FAT32 : le rythme ne doit plus s'effondrer.
4. Le journal : `nvme : N file(s) d'E/S`, et pour `fatfs` `fscache` (succès, défauts).
