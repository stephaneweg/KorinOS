# vfs et fatfs : rendre le volume système rapide sous charge (à faire avant la reconstruction de WebKit)

Note de l'agent local (ZBook, 22/11/2026), pour la session du cloud. Décision de l'utilisateur : les points **1, 2 et
4** ci-dessous sont à faire **avant** la reconstruction de WebKit de `docs/agents/JET-LOCAL-BUILD.md` (le point 3 est
facultatif).

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
4. **Le coût des services en espace utilisateur** (programme → `vfs` → `fatfs` → `nvme`, des allers-retours de
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

## Comment vérifier

- Un banc : N process qui ouvrent / `stat` des fichiers d'un dossier de 3 000 noms (présents et absents), mesurer
  les opérations par seconde avant et après (`tools/…`, à écrire), et la charge de `fatfs` (`mcp__korin__cpu`).
- Puis, sur le ZBook : `tools/webkit/local/rebuild-sdk.py 10 "…"` avec le SDK dans `/devtools` (FAT32) : le rythme ne
  doit plus s'effondrer ; et la copie d'un dossier de milliers de fichiers.
- Ces services sont dans `/sys` et `vfs` sert tout le système : construire et essayer sous QEMU d'abord, garder
  l'ancien binaire, et **demander à l'utilisateur avant d'installer** sur le ZBook (CLAUDE.md, « Le système
  lui-même »).
