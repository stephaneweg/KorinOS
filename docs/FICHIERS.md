# Fichiers hors du noyau (comme QNX)

Conception du chantier 4 de la [feuille de route](FEUILLE-DE-ROUTE.md) : `files.asm` et
`remote.asm` (1 396 lignes) quittent le noyau, qui ne garde que la messagerie, les process et la
mémoire. Ce document fixe l'ordre et les décisions ; **le chantier est fait** (étapes 0 à 5,
section 6 ; étapes 2–3 : section 5.1 ; étape 4 : section 5.2 ; étape 5 : section 5.3). Le noyau
ne lit plus **aucun** fichier, kits compris (décision de l'utilisateur à l'étape 5, au lieu du
`kcharger` de D11).

```
 aujourd'hui                                     cible
 programme ─svc 12..26─▶ noyau ─svc_call_k─▶ vfs     programme (appkit : descripteurs, positions)
                          │ chemins, P_CWD,     │        │ open, stat, chdir...   │ read, write, readdir
                          │ descripteurs,       ▼        ▼  (chemin)              ▼  (numéro)
                          └─────read/write──▶ fatfs     vfs ── route ──▶ fatfs ◀─────┘ direct
                                                         (dossier courant par pid)
```

## 1. L'existant (ce que le code montre)

| Élément | Où | Rôle aujourd'hui |
|---|---|---|
| Chemins | `files.asm` : `path_from_volume`, `path_normalize`, `user_path`, `path_to_volume` | « `SD0:/a` » ↔ « `/sd0/a` », `.`/`..`, relatif depuis `P_CWD` (256 o dans le PCB) |
| Descripteurs | `files.asm` : `file_pool` (256 objets `FO_*`), `P_FILES` (32 par process) | type 4 tube, 5 fichier distant, 6 répertoire distant ; `FO_SVC`, `FO_RID`, `FO_POS`, `FO_SIZE`, `FO_FLAGS` |
| Relais | `remote.asm` : `dist_ouvrir`, `dist_rw`, `dist_lire_rep`, `dist_chemin`, `dist_fermer` | ops 1–4 vers `vfs`, 10–13 directement au service FS, **par morceaux de 464 o** recopiés deux fois |
| Lancement | `files.asm` : `spawn_common`, `kspawn_path`, `app_main_fix`, `spawn_name` | nom seul → `/sd0/bin/<nom>` puis `/sd0/apps/<nom>.app/main` ; fichier entier lu par `dist_charger`, puis `proc_spawn_elf` |
| Droits | `chemin_systeme` | `P_DROITS = 1` si le chemin commence par `/sd0/sys/` |
| Lectures du noyau | `kload_file` = `dist_charger` | `options.txt` (`kernel.asm`), kits (`kits.asm`, après les modules d'initramfs), firmware, nvram, CLM et `wifi.txt` (`wifi.asm`, 4 appels) |
| Fin d'un process | `proc_on_exit` → `files_close_all` | avis `FS_FERMER` : fatfs réécrit taille et cluster dans le répertoire |

Côté services : `vfs` reçoit des chemins internes (« `/ram0/a` ») et ajoute le canal du volume à
la réponse d'ouverture ; `fatfs` et `ramfs` numérotent leurs fichiers ouverts (table `ouverts`,
64) ; avant l'étape 0, **sans propriétaire** et sans `SVF_FINS` (fait depuis : section 4.2).

**Appels des programmes** (`sys SYS_x`, src/apps, bin, sys, svc, kits, inc) :

| Appel | Fichiers | Sites | Appel | Fichiers | Sites |
|---|---:|---:|---|---:|---:|
| OPEN | 27 | 72 | MKDIR | 6 | 8 |
| READ | 24 | 38 | UNLINK | 6 | 15 |
| WRITE | 14 | 29 | CHDIR | 6 | 9 |
| SEEK | 6 | 6 | GETCWD | 2 | 2 |
| CLOSE | 26 | 75 | SPAWN | 11 | 13 |
| STAT | 10 | 13 | SPAWN_PIPE | 2 | 2 |
| READDIR | 8 | 9 | WAIT | 4 | 5 (reste au noyau) |

Hors macro `sys` : **fasmarm** (`svc 0` avec `ARMPI_SYS_*`, fichier généré par
`tools/x86toa64.py`, sans kit) et **imgblk** (`mov x8,x21 ; svc 0`, lire ou écrire). Le kit
**UIKit** appelle OPEN/READ/CLOSE/STAT lui-même (`uikit.asm`, `uikit_theme.inc`).

**Registres à travers un appel système** (`vectors.asm` : `save_frame` / `exc_return`) : le
noyau rend **tout** sauf x0 — x1–x30, sp, **NZCV** (rendu par SPSR), x8 (le numéro) ; il ne
touche jamais v0–v31. Le code assembleur d'ARMPI s'en sert (valeurs gardées dans x1–x15 d'un
appel à l'autre) : tout remplaçant doit offrir la même garantie.

## 2. Décisions

| # | Décision |
|---|---|
| D1 | **vfs** tient chemins et dossiers courants : il reçoit le chemin tel que le programme l'écrit (« `SD0:/a` », « `sd:a` », relatif), le résout avec le dossier courant **du pid de l'appelant** (`MSG_PID`, rempli par le noyau). « `/...` » désigne la racine du **volume courant** (décision du 29/10/2026, comme `\` sous DOS : dossier `SD1:/docs`, « `/musique` » → `SD1:/musique`) ; le relais du noyau le refuse encore jusqu'aux étapes 2–3. Une fiche par process connu (`src/svc/vfs/pids.inc`, blocs de 32 fiches ajoutés au besoin) : plus de plafond depuis le 04/11/2026. |
| D2 | **Avis de naissance** : nouveau drapeau `SVF_NAISSANCES` ; `proc_spawn_elf` poste `SVC_OP_NAISSANCE` (0xFFFE, a = fils, b = père) **avant** de rendre le premier thread exécutable. La file d'un service étant dans l'ordre d'arrivée (`SR_ORDRE`), vfs a copié le dossier du père avant la première requête du fils. L'avis de fin (`SVC_OP_FIN`) l'efface. |
| D3 | **Descripteurs dans appkit** : table dans les variables du kit (recopiées par process, `kits.asm`), donc propre à chaque process et partagée par ses threads ; un verrou (futex) la protège. Numéros **64 à 127** : jamais confondus avec ceux du noyau (0–31) pendant la transition, jamais 0. |
| D4 | **read / write / readdir / close directs** au service FS, avec le numéro rendu par vfs ; position et taille gardées dans appkit (seek sans message). |
| D5 | **Gros transferts** : un tampon partagé par couple (process, service FS), créé **par le service** (il connaît `MSG_PID` : `buf_create` + `buf_grant`), 64 Ko ; au-delà de 464 o, appkit passe par lui ; sans tampon (table pleine), il retombe sur les messages de 464 o. |
| D6 | **Services FS** : chaque fichier ouvert porte le pid de son propriétaire ; seul lui (ou le noyau, `SVC_PID_NOYAU`) s'en sert ; inscription `SVF_FINS` : à la fin d'un pid, ses fichiers sont fermés (fatfs réécrit taille et cluster) et son tampon libéré. |
| D7 | **Programmes inchangés** : la macro `sys` d'`armpi.inc` envoie les numéros 12–23 et 26 vers une entrée unique d'appkit, `fichier_sys`, qui a la sémantique d'un `svc 0` (section 3). Réassembler suffit. |
| D8 | **Lancement** : appkit lit l'exécutable (résolu par vfs) et appelle un nouvel appel noyau `spawn_image` ; le noyau ne voit plus de chemin. |
| D9 | **Droits** : un fils reçoit `P_DROITS` seulement si **le père l'a** et le demande ; appkit le demande quand vfs signale un fichier de `SD0:/sys/` (`ATTR_SYS`). Un process sans droits ne peut plus jamais en créer un qui en a. |
| D10 | **Tubes au noyau** : objets inchangés (`pipe.asm`) ; `P_FILES` devient `P_TUBES` (8 poignées) ; la sortie standard reste `P_STDOUT` (lue par `sys_print`) : rien d'autre à transmettre au fils. |
| D11 | *(remplacée à l'étape 5 : le noyau ne lit aucun fichier, section 5.3)* **Le noyau garde un petit client de vfs** (`kcharger.asm`, ≈ 110 lignes) pour ses propres lectures : kits du disque, `options.txt`, Wi-Fi. Il envoie un chemin constant (« `SD0:/lib/uikit.so` ») sans l'analyser. Il partira avec le Wi-Fi (chantier 5) pour ce qui le concerne. |

## 3. Les programmes : la macro `sys` vers appkit

```asm
macro sys n* {
  if defined FICHIERS_APPKIT & FICHIERS_APPKIT & ((n >= SYS_OPEN & n <= SYS_SPAWN) | n = SYS_SPAWN_PIPE)
	mov	x8,n
	str	x30,[sp,-16]!		; un bl ecrase x30 : fonctions feuilles sans prologue
	bl	fichier_sys		; relais du kit : x16 seulement
	ldr	x30,[sp],16
  else
	mov	x8,n
	svc	0
  end if
}
```

`FICHIERS_APPKIT` : défini par le programme lui-même à l'étape 2 (passage un par un), puis par
`appkit.abi` inclus dans un programme (`kit_use`) à partir de l'étape 3 ; jamais dans un kit
(`KIT_BUILD`) ni dans un programme sans appkit.

**`fichier_sys`** (nouvelle ligne à la fin de `appkit.abi`, rang 13, version 14 ; les `f_*` suivent :
version 25) : x8 = numéro, x0–x5
arguments, comme le noyau. Elle sauve x1–x15, x17, x18 et NZCV (`mrs`/`msr nzcv`, permis en EL0),
aiguille sur x8, restaure, rend x0. Le code derrière suit l'AAPCS et **n'utilise pas les
registres FP/SIMD**. Écart restant avec un `svc 0` : **x16** (le relais du kit) — aucun usage
de x16/x17 dans src/apps, bin, sys, svc (vérifié) ; l'AAPCS le réserve déjà au relais.

| Vérification | Résultat |
|---|---|
| Registres | identiques sauf x16 ; x30 sauvé par la macro ; sp rendu tel quel (16 o empilés le temps de l'appel, alignement gardé) |
| Pile | `fichier_sys` utilise ≈ 800 o (message de 512 o sur la pile, pas de tampon statique : réentrant entre threads). Vérifier les petites piles (threads de vfs : 16 Ko, suffisant) |
| Programmes sans appkit | **UIKit** (un kit) : voir Q2 ; **fasmarm** : le gabarit de `x86toa64.py` reçoit `kit_use appkit` + `kits_open`, et `svc 0` des numéros de fichiers devient `bl fichier_sys` ; **imgblk** : `mov x8,x21 ; svc 0` → même séquence avec `bl fichier_sys` (2 lignes) |
| kits_open | tous les programmes liés à appkit l'appellent en tête : la table d'import est remplie avant le premier appel de fichier |
| `init` (initramfs) | lié à appkit, trouvé dans les modules d'initramfs (`module_find("appkit.so")`) ; ses appels de fichiers (`stat SD0:/` en boucle, `spawn init2`) échouent en `E_NOENT` tant que vfs n'est pas inscrit, exactement comme aujourd'hui |
| `vfs` lui-même | ne peut pas s'envoyer une requête depuis son thread principal (`monter` lance fatfs **depuis le disque**). appkit offre un **résolveur local** : `fichier_resolveur(fonction)` ; vfs y inscrit sa propre résolution ; ses ouvertures vont alors directement au service FS, sans message à lui-même. Le thread de `mounts.txt` en profite aussi |
| Services FS | fatfs et ramfs n'ouvrent aucun fichier ; imgblk ouvre son image par vfs (client ordinaire ; pas de cycle tant que l'image n'est pas sur le volume qu'il sert) |
| Ordre de démarrage | init → `mod_lancer` sd, vfs (« `SD0 blk.sd0p1 fatfs` »), fatfs → vfs sert `SD0:` → init lance init2 par appkit : inchangé |

Alternative écartée : réécrire ~300 sites à la main en appels nommés (`f_open`...). Les fonctions
nommées AAPCS existent quand même dans appkit (`f_ouvrir`, `f_lire`...) pour le code nouveau ;
`fichier_sys` les appelle.

## 4. Protocole

### 4.1 vfs (nouvelles ops ; 1–4 et 20–22 inchangées)

| Op | Requête | Réponse |
|---|---|---|
| 30 `V_OUVRIR` | a = drapeaux O_* ; données : chemin du programme | a canal du service FS, b numéro, c attributs (`ATTR_DIR`, `ATTR_SYS` = sous `SD0:/sys/`), d taille |
| 31 `V_STAT` | données : chemin | b taille, c attributs |
| 32 `V_MKDIR`, 33 `V_UNLINK` | données : chemin | 0 ou erreur |
| 34 `V_CHDIR` | données : chemin (doit être un répertoire) ; du noyau seulement : b = pid concerné (transition, étape 2) | 0, `E_NOTDIR`... |
| 35 `V_GETCWD` | – | données : « `SD0:/a` » (≤ 256 o) |
| 36 `V_EXEC` | données : nom ou chemin ; règles d'aujourd'hui : nom seul → `SD0:/bin/<nom>` puis `SD0:/apps/<nom>.app/main` ; « `.app` » → `/main` | comme 30 (lecture) + données : nom du process (`spawn_name` : « `paint` » pour `paint.app/main`) |
| 37–41 (étape 4) | tubes et entrées / sorties standard : section 5.2 | |
| 0xFFFE avis de naissance | a = fils, b = père ; étape 4 : c, d, données = entrée, sortie, erreur données au fils | – (dossier du père copié ; bouts de tubes recopiés pour le fils) |
| 0xFFFF avis de fin | a = pid | – (dossier oublié ; volume retiré si c'était un service FS, comme aujourd'hui) |

Le code de chemins de `files.asm` (≈ 300 lignes) passe tel quel dans vfs (`inc/path.inc`).

**Dates** (30/10/2026) : `make` compare les dates de fichiers, il faut donc qu'elles soient justes
et dans l'ordre des écritures.

| Point | Choix |
|---|---|
| Horloge | dans le noyau : `SYS_CLOCK_GET` (0 : secondes UTC, 1 : décalage local, 2 : secondes locales), `SYS_CLOCK_SET` (droits de `SD0:/sys`). Elle **ne recule jamais** |
| Sources | netd par NTP (avec le décalage de `fuseau=`) ; au démarrage, init2 la reprend de `SD0:/etc/lastclock` (« `secondes_UTC décalage` »), écrit toutes les 30 min et au redémarrage. `build.ps1` le remplit avec l'heure du PC |
| Sans réseau | l'heure repart de la dernière sauvegarde : en retard sur la réalité, mais jamais avant un fichier déjà écrit (à 30 min près après une coupure de courant) |
| FAT32, exFAT | date locale dans l'entrée du répertoire, comme Windows : les fichiers copiés du PC gardent une date cohérente. Écrite à la création et à la fermeture d'un fichier modifié. Précision de 2 s (`src/inc/dostime.inc`) |
| RamFS | secondes UTC dans le nœud |
| `FS_STAT`, `V_STAT` | **d** = date en secondes UTC (0 : aucune) |
| `FS_UTIME` 5, `V_UTIME` 42 | b = secondes UTC ; données : chemin |

**`rename`** (30/10/2026) : `V_RENAME` 43 (données : ancien, 0, nouveau, 0) résout les deux chemins
et rend `E_XDEV` s'ils sont sur deux volumes (libposix recopie alors un fichier ; un répertoire :
`EXDEV`). `FS_RENAME` 6 ne déplace aucune donnée. Dans fatfs : une entrée de répertoire neuve, mêmes
clusters et même date, puis l'ancienne effacée ; le nœud d'un fichier ouvert suit ; un fichier à
l'arrivée est remplacé ; le « `..` » d'un répertoire déplacé est corrigé (FAT32). Dans ramfs : le
nœud change de parent et de nom. Un répertoire ne va pas dans lui-même (`E_INVAL`), ni sur un
répertoire existant (`E_EXIST`).

**Redirections vers un fichier** (30/10/2026, `src/svc/vfs/pipes.inc`) : le lancement d'un programme
ne reçoit que des poignées de bouts de tube. Un bout peut donc aussi désigner un fichier :
`V_FILE_END` 44 (a = 1 lecture, 2 écriture ; chemin) crée un tube sans anneau qui garde le chemin
résolu. Il se donne et se compte comme un autre ; `V_STDIO` signale par les bits de **d** lesquels
sont des fichiers, `V_END_FILE` 45 en rend le chemin, et l'AppKit du fils **ouvre le fichier
lui-même**, au premier besoin, en `O_APPEND` pour l'écriture. Il n'y a donc ni copie ni thread, et les
fils du fils héritent de la redirection (ils reçoivent le même bout). Du côté du lanceur : `f_redir`
d'AppKit (programmes en assembleur, avec `f_spawn_stdio`), `adddup2` d'un fichier et `addopen` de
`posix_spawn`. Limite : la position du père n'est pas reprise.

**Chemins virtuels** (30/10/2026, `src/svc/vfs/path.inc`) : un chemin absolu sans volume passe
d'abord par une table de vfs, qui vaut pour tous les programmes, en assembleur comme en C. Elle
se règle dans **`SD0:/etc/paths.txt`**, lu par init2 avant de lancer quoi que ce soit (16 lignes au
plus, « `#` » : commentaire) :

```
/gcc = SD0:/gcc
/bin = SD0:/bin
```

« `/gcc/bin/gcc` » désigne alors `SD0:/gcc/bin/gcc` quel que soit le volume du dossier courant ;
la comparaison porte sur des composants entiers (« `/gccx` » ne correspond pas) et respecte la
casse. Un chemin absent de la table garde la règle ci-dessous (racine du volume courant). Ces chemins
n'ont pas de « `:` », qui sépare les listes de chemins de GCC et de `PATH` ; `realpath` les garde
donc tels quels (`VFS_PATHQ`). Ops 23 `VFS_PATHMAP` et 24 `VFS_PATHQ` : [ABI.md](ABI.md).

**Tel qu'écrit à l'étape 1** (`src/svc/vfs/path.inc`, détails que la conception laissait
ouverts) :

| Point | Choix |
|---|---|
| Fichier | `src/svc/vfs/path.inc` (à côté de `vfs.asm`) : `path_normalize`, `path_from_volume`, `path_to_volume`, `app_main_fix`, `spawn_name` copiés de `files.asm` (le noyau garde les siens pour le relais jusqu'à l'étape 5) |
| Dossiers | table `dossiers` : 64 × 256 o, forme interne « `/sd0/a` » ; entrée vide = « `/sd0` » (process nés avant vfs, ou fils du noyau) |
| « `/...` » | préfixe « `/vol` » pris dans le dossier courant de l'appelant, puis normalisation ; « `..` » au-dessus d'un volume donne « `/` » : `E_NOENT` (comme le noyau) |
| Propriétaire (30, 36) | vfs ouvre au nom du programme : il met son pid dans **d** de la requête `FS_OUVRIR` ; `fsowner.inc` (`proprietaire`) prend d quand l'appelant est vfs, `SVC_PID_NOYAU` si d = 0 (ops 1–4 du relais : vfs y met d = 0, le message du noyau n'initialise pas d). Seul écart avec « Services FS : – » de la section 6 |
| `ATTR_SYS` | 0x100 (au-dessus des attributs FAT), `armpi.inc` ; mis dans c pour 30 et 36 quand le chemin résolu commence par « `/sd0/sys/` » |
| 34 | `E_INVAL` pour un pid hors 1..64 ; « du noyau » = `MSG_PID` = `SVC_PID_NOYAU` |
| 36 | `FS_STAT` d'abord (`E_NOENT` sur `SD0:/bin/<nom>` → essai de `SD0:/apps/<nom>.app/main` ; un répertoire : `E_ISDIR`, sans rien ouvrir), puis ouverture `O_READ` pour l'appelant ; nom du process (≤ 31 o) dans les données |
| Avis | 0xFFFE : copie du dossier du père (père 0 ou sans dossier : entrée vide) ; 0xFFFF : entrée effacée, puis volume retiré si c'était un service FS. vfs s'inscrit `SVF_FINS \| SVF_NAISSANCES` |
| Noyau | `svc_naissance` (`svc.asm`, ≈ 40 lignes) appelé par `proc_spawn_elf` juste après `proc_register`, avant `proc_add_thread` ; père = process du thread courant (0 pour un thread du noyau) |
| Test | `vfstest` (`src/bin/vfstest`) : 37 PASS, 0 FAIL sur QEMU ; `fstest2` et `ramtest RAM0` passent (lancés seuls : lancés ensemble, `fstest2` voit « `FS_LIRE_T` après fermeture » réussir, car `ramtest` a repris le même numéro chez ramfs pour un fichier du relais, ouvert à tout pid pendant la transition — comportement de l'étape 0) ; bureau démarré (Elegant en service) |

### 4.2 Services FS (ops ajoutées ; 1–4, 10–13 inchangées)

| Op | Requête | Réponse |
|---|---|---|
| 12 `FS_FERMER` | a = numéro | devient un **appel** quand il vient d'appkit (réponse 0) ; reste un avis venant du noyau |
| 14 `FS_TAMPON` | – | a = numéro du tampon partagé (créé et accordé à `MSG_PID`, 64 Ko) ; l'appelant le mappe (`buf_map`) |
| 15 `FS_LIRE_T` | a numéro, b position, c octets (≤ 64 Ko) | octets, lus au début du tampon |
| 16 `FS_ECRIRE_T` | a numéro, b position, c octets (dans le tampon) | octets, b nouvelle taille |
| 17 `FS_CHARGER` (étape 6) | a numéro | a = tampon de la taille du fichier, rempli et accordé : chargement entier **sans copie** |
| 0xFFFF avis de fin | a = pid | fichiers du pid fermés, tampon libéré |

Contrôle de propriété dans 10–17 : `MSG_PID` = propriétaire, sinon `E_BADF`.

**Tel qu'écrit à l'étape 0** (`src/svc/inc/fsowner.inc`, commun à fatfs et ramfs ; détails
que la conception laissait ouverts) :

| Point | Choix |
|---|---|
| Propriétaire | `MSG_PID` de l'ouverture (op 1). Une ouverture **transmise par vfs** (le relais du noyau : ops 1–4 de vfs) a pour propriétaire `SVC_PID_NOYAU` : le service reconnaît vfs à son nom de process (« `vfs` », `proclist`, pid gardé jusqu'à son avis de fin) ; vfs et le noyau ne changent pas. Un appel venant d'un thread du noyau (`SVC_PID_NOYAU`) donne aussi `SVC_PID_NOYAU` |
| Accès (10–16, 12 en appel) | permis au propriétaire, au noyau (pid 0 = avis, ou `SVC_PID_NOYAU`) et, **pendant la transition**, à tout pid pour un fichier du relais : `svc_call_k` remplit `MSG_PID` avec le pid du programme pour qui le relais lit ou écrit. Sinon `E_BADF` ; numéro libre ou objet effacé : `E_INVAL` |
| 12 `FS_FERMER` | pid 0 (avis du noyau) : sans réponse, comme avant ; sinon appel, réponse 0 ou erreur. Un fichier du relais n'est fermé que par l'avis du noyau (jamais par une fin de pid) |
| 14 `FS_TAMPON` | un tampon par pid client, `FS_TAMPON_TAILLE` = 64 Ko ; redemandé : le même numéro ; 16 clients à la fois par service (`E_NOMEM` au-delà : repli sur les messages) ; `E_INVAL` pour le noyau (pid 0 / `SVC_PID_NOYAU`). Le client le mappe par `buf_map` (`buf_grant` fait par le service) |
| 15, 16 | `E_INVAL` sans tampon (op 14 d'abord), si c > 64 Ko ou si le numéro est un répertoire ; 16 : `E_PERM` sans `O_WRITE` ou volume en lecture seule. Lecture : bornée par la taille (0 à la fin) |
| Avis de fin (0xFFFF) | fichiers du pid fermés (fatfs réécrit taille et cluster), tampon rendu (`buf_free` chez le service ; pages libérées quand le client l'a aussi lâché) |
| ramfs | le numéro rendu est une entrée de la table `ouverts` (64 : nœud, propriétaire), plus le numéro du nœud ; `E_MFILE` quand elle est pleine. fatfs : champ `FO_PROPRIO` (+112) de l'objet |

**fatfs : cache de données** (01/11/2026, `cc_*` dans `src/svc/fatfs/fatfs.asm`, essayé sur le Pi) :

| Point | Choix |
|---|---|
| Où | sous le système de fichiers, donc commun à FAT32 et exFAT : blocs de 64 Ko du périphérique (secteur / 128), lectures de fichier et secteurs de `bc_get` (répertoires, FAT) |
| Mémoire | une région `VM_MAP` à la demande : 1/8 de la mémoire libre, revu en marche (au montage de `sd0`, la zone haute n'est pas encore là), 256 Mo au plus pour `sd0`, 64 Mo pour les autres volumes ; actif par défaut ; un bloc neuf est peuplé par `MADV_WILLNEED` (`E_NOMEM` plutôt qu'une faute mortelle) |
| Mémoire rare | fatfs s'enregistre avec `SVF_MEMORY`. Sous 64 Mo libres, le faucheur du noyau lui lève `SVC_SIG_LOWMEM` (`mem_watch`, toutes les 20 ms) ; un thread qui n'a plus de page en fait autant et attend jusqu'à 2 s que 16 Mo reviennent (`mem_wait` : faute d'EL0, `check_user`, `VM_POPULATE`, `SBRK`). Chaque signal rend la moitié du cache ; il ne regrandit qu'au-dessus de 128 Mo libres. Le service bloqué sur sa propre faute n'attend pas |
| Remplacement | horloge, table de hachage |
| Lecture d'un bloc | `BLK_LIRE` par la zone de transfert, un bloc par commande |
| Écriture | le cache ne garde rien à écrire ; `blk_requete` retire les blocs touchés avant tout `BLK_ECRIRE` : c'est le seul passage des écritures (`bc_write`, `bc_flush`, `bc_get`, `blk_zero`, `blk_write`) |
| Hors cache | un fichier de plus de 128 Mo (vidéo), le dernier bloc incomplet du volume |
| Op 18 `FS_CACHE` | a = 0 lire, `FS_CC_ON`, `FS_CC_OFF`, `FS_CC_DROP` → a succès, b défauts, c blocs en mémoire, d remplacés. Commande : `fscache [volume] [on\|off\|drop]` |
| Écrivains | seul fatfs écrit sur sa partition. Un autre écrivain direct sur `blk.sd0pN` rendrait le cache faux |
| Essais | `cachetest [chemin]` : écritures dans des blocs déjà lus, puis relecture par 4, 64 et 256 Ko |

**fatfs : fichiers ouverts partagés, noms longs** (29/10/2026, `src/svc/fatfs`) :

| Point | Choix |
|---|---|
| Nœud partagé | un objet de `ouverts` (numéro, propriétaire, drapeaux, position de l'appel) désigne un **nœud** de la table `noeuds` (64), commun à toutes les ouvertures du même fichier : clé = premier cluster du répertoire parent + index de son entrée (8.3 ; exFAT : l'entrée 0x85), compteur d'ouvertures, premier cluster, taille, curseur de chaîne. Avant chaque lecture / écriture, l'objet reprend l'état du nœud (`fo_charger`) et le lui rend après (`fo_ranger`) : les écritures d'un numéro (taille, nouveaux clusters) sont vues des autres, aucun premier cluster n'est alloué deux fois. FAT32 et exFAT |
| Taille vue de dehors | `FS_STAT`, `FS_LIRE_REP` et une nouvelle ouverture donnent la taille du nœud quand le fichier est ouvert (l'entrée du répertoire n'est réécrite qu'à la fermeture) |
| Réécriture de l'entrée | à chaque fermeture d'un numéro si le nœud a changé (plus seulement à la dernière) ; `O_TRUNC` : chaîne libérée et entrée réécrite **tout de suite**, fichier vide pour toutes ses ouvertures |
| `O_APPEND` | `FS_ECRIRE` / `FS_ECRIRE_T` d'un numéro ouvert avec `O_APPEND` écrivent à la fin **réelle** (taille du nœud), quelle que soit la position b ; b rendu = nouvelle taille |
| `FS_UNLINK` d'un fichier ouvert | ses entrées (8.3 et toutes les LFN ; exFAT : le jeu) sont effacées tout de suite (il disparaît du répertoire ; un nouveau fichier du même nom est un autre nœud) ; ses clusters restent lisibles et inscriptibles par les numéros ouverts et sont libérés à la **dernière** fermeture |
| Noms longs | **255 caractères UTF-16** (FAT32 : jusqu'à 20 entrées LFN ; exFAT : 17 entrées de nom), soit jusqu'à 765 octets UTF-8 (`DE_NAME` : 768 o ; zone UTF-16 de lecture `DE_LFN` : 528 o) ; caractères hors du plan de base (emoji) en paires de substitution ; création, recherche, `readdir`, effacement de toutes les entrées LFN. Au-delà : `E_NAMETOOLONG` (-36). Entrées LFN orphelines (numéro 0 ou > 20, suite ou somme de contrôle cassée) : ignorées, le nom 8.3 sert |
| `readdir` (op 13) | le protocole rend `nom[256]` : 255 octets UTF-8 au plus, coupés avant un caractère entier (un nom de 255 caractères ASCII passe en entier ; un nom accentué de plus de 255 octets est tronqué à la lecture seulement) |
| Chemins | un chemin tient dans les données d'un message (464 o) côté fatfs, mais **AppKit** (`FC_CHEMIN` = 255, chemin + 0) et **vfs** (`PATH_MAX` = 256, chemin interne résolu « `/sd0/...` ») le bornent à ≈ 254 octets : un nom de 255 caractères n'est donc pas joignable par un programme (« `SD0:/` » + nom > 254 o) ; il se lit dans `readdir` (écrit par un autre système) mais ne s'ouvre pas. En pratique : nom ≤ 249 octets à la racine de `SD0:`, moins dans un sous-dossier. Il n'y a pas d'op de renommage (`rename` de posix recopie puis efface) |
| FSInfo (FAT32) | nombre de clusters libres lu au montage, tenu à jour par `fat_alloc` / `fat_free_chain` et réécrit (avec le prochain libre) après chaque opération qui l'a changé : `fsck.fat` ne le trouve plus faux |
| Essais | `fstest2` (« `fs.sd0` », « `fs.sd1` » : 20 essais chacun, deux numéros sur `tmp/fstest2.nd`), `posixtest file SD0:/tmp` et `SD1:/tmp` ; image vérifiée par `fsck.fat`, `fsck.exfat`, `mtools` et `pyfatfs` |

### 4.3 Noyau (nouveaux appels)

| Appel | Rôle |
|---|---|
| 97 `spawn_image(image, taille, params)` → pid | params (48 o) : nom, ligne d'arguments, options (bit 0 : droits demandés, bit 1 : sortie dans un tube neuf), `&poignée`. Image recopiée depuis la mémoire de l'appelant (≤ 64 Mo), puis `proc_spawn_elf`. Sans tube : le fils hérite `P_STDOUT` du père (comme `sys_spawn`) |
| 98 `tube_lire(poignée, tampon, n)` | lecture non bloquante (`E_AGAIN`), 0 = fin |
| 99 `tube_fermer(poignée)` | un lecteur de moins |

`wait` (24) reste tel quel. `svc_register(nom, SVF_FINS \| SVF_NAISSANCES)`.

**Tel qu'écrit à l'étape 4** : les tubes sont dans vfs (réponse 8), donc 98–99 ne servent plus
à lire un tube du noyau : **98 = `proc_flags`**, 99 n'existe pas (section 5.2).

## 5. Descripteur dans appkit

| Champ | Contenu |
|---|---|
| type | 0 libre, 1 fichier, 2 répertoire, 3 tube (poignée noyau), 4 descripteur du noyau (transition) |
| canal, numéro | service FS et numéro chez lui (ou poignée de tube) |
| position, taille, drapeaux | comme `FO_POS`, `FO_SIZE`, `FO_FLAGS` ; `O_APPEND` : position = taille à l'ouverture |
| tampon | par canal (pas par descripteur) : petite table canal → tampon mappé |

**Héritage** : seule la sortie standard passe au fils, et elle reste au noyau (`P_STDOUT`) ;
`spawn_pipe` = `spawn_image` avec l'option tube, la poignée devient un descripteur de type 3
chez le père. Transmettre d'autres descripteurs (entrée standard, fichier) demandera une op
`FS_TRANSMETTRE(numéro, pid)` : pas maintenant.

**Lancement par appkit** (`spawn`, `spawn_pipe`) : `V_EXEC` → lecture du fichier entier
(`FS_LIRE_T`, dans un bloc de `malloc`) → fermeture → `spawn_image` (droits si `ATTR_SYS` et si
le process en a) → bloc rendu. Plus tard, un serveur exec (personnalités) fera la même chose.

### 5.1 Tel qu'écrit aux étapes 2 et 3 (29/10/2026)

| Point | Choix |
|---|---|
| Fichier | `src/kits/appkit/fileclient.inc` (≈ 700 lignes) ; variables dans le segment inscriptible d'AppKit (nouveau : une page par process) : `fc_desc` (64 × 48 o : type, canal, numéro, position, taille, drapeaux), `fc_tampons` / `fc_tnum` / `fc_tverrous` (par canal de service, 32), `fc_verrou` |
| `fichier_sys` | sauve x1–x15, x17, x18, x29 et NZCV (`mrs`/`msr nzcv`) dans 144 o de pile, aiguille sur x8 (appel terminal vers `f_*`), restaure, rend x0 ; x16 seul change (relais). Pile ≈ 800 o (message de 512 o sur la pile : réentrant entre threads). Vérifié par `fstest2` (« `fichier_sys : stat` / `read 4098 o` » : registres et NZCV, un FAIL si `msr nzcv` manque) |
| Descripteurs du noyau | un numéro **hors 64–127** (tubes de `spawn_pipe` : 0–31) part au noyau tel quel (`svc 0`), comme `spawn`, `spawn_pipe` et les numéros non fichiers : pas de descripteur de type 4 dans AppKit (plus simple, même effet ; les tubes passent dans vfs à l'étape 4) |
| Allocation | sous `fc_verrou` (ldaxr/stxr, `yield` en attente) : entrée réservée (type 3) avant `V_OUVRIR`, rendue sur erreur ; table pleine : `E_MFILE` sans rien ouvrir |
| read / write | ≤ 464 o : `FS_LIRE` / `FS_ECRIRE` par message ; au-delà, le tampon du canal (`FS_TAMPON` + `buf_map` au premier besoin, un transfert à la fois par tampon : `fc_tverrous`), morceaux de 64 Ko par `FS_LIRE_T` / `FS_ECRIRE_T`. Mêmes règles que `dist_rw` : une lecture s'arrête au premier morceau court, une écriture va au bout, une erreur après des octets rend ces octets. Tampon refusé (16 clients par service) : messages pour toute la vie du process. `E_INVAL` d'une op 15/16 avant tout octet (service relancé, tampon inconnu) : tampon oublié (`buf_free`), l'appel repasse par les messages, le suivant redemande un tampon |
| close | `FS_FERMER` en appel (réponse 0) ; rend toujours 0 pour un descripteur valide |
| Chemins | `V_OUVRIR`, `V_STAT`, `V_MKDIR`, `V_UNLINK`, `V_GETCWD` à vfs, trouvé par `svc_open("vfs", 0)` à chaque appel (`E_NOENT` sans vfs, comme le relais : `init` et sa boucle `stat SD0:/` inchangés) ; chemin ≤ 254 o (`E_INVAL` au-delà, comme le noyau) |
| chdir | `V_CHDIR` puis `V_GETCWD`, puis `chdir` du noyau avec ce chemin et **x1 = `FC_CHDIR_SYNC`** (`armpi.inc`) : le noyau recopie dans `P_CWD` sans vérifier ni prévenir vfs (pour `spawn`). Un `chdir` reçu sans cette marque (`svc 0` direct) prévient vfs (`V_CHDIR` au nom du programme, chemin « `SD0:/a` ») |
| Écarts avec le relais | pointeur invalide : faute au lieu d'`E_FAULT` (sauf 0 pour `stat` et `getcwd`) ; « `/...` » accepté (racine du volume courant, D1) ; `getcwd` copie la chaîne (pas 256 o) |
| Macro `sys` | `if defined FICHIERS_APPKIT` puis `if FICHIERS_APPKIT & (12–23 ou 26)` (deux `if` : fasm n'arrête pas l'évaluation sur `defined`). Étape 2 : défini par le programme ; étape 3 : par `appkit.abi` quand il est importé (`if ~KIT_BUILD`) ; retour arrière : `FICHIERS_APPKIT = 0` dans `appkit.abi` (essayé : tout repasse par le relais, compteur plein) |
| Kits | `kit_use` dans un kit importe (bascule `KIT_BUILD` le temps de l'`include`) ; `kit_data` en décalages depuis `kits_list` ; UIKit : `kit_use appkit`, `kit_data`, `uk_kits` (`kits_open` au premier `ui_load_font` / chargement du thème). Pas d'ordre de chargement dans `kits.asm` : `lib_open` reste paresseux |
| fasmarm | fichier plat (en-tête ELF écrit à la main, sans `kit.inc`) : le gabarit de `x86toa64.py` appelle `lib_open("appkit", 14)` au démarrage et garde l'adresse de `fichier_sys` (rang 13) ; open, read, write, seek, close passent par `armpi_fichier` (`br` vers elle ; le noyau si AppKit manque). Le générateur numérote ses tables dans un ordre qui dépend du hachage de Python : `fasmarm.asm` = l'ancien fichier + la seule partie du gabarit régénérée (diff de 36 lignes) ; une régénération complète donne un fichier équivalent aux tables permutées |
| imgblk | `mov x8,x21` / `bl fichier_sys` (si `FICHIERS_APPKIT`) |
| vfs | **pas de résolveur local** : son thread principal n'ouvre aucun fichier (`monter` : `mod_lancer` ou `spawn`, au noyau) ; seul le thread de `mounts.txt` lit un fichier, par AppKit, et c'est le thread principal de vfs qui le sert. À revoir si le thread principal doit un jour lire un fichier |
| Services FS, démarrage | fatfs, ramfs, sd, usbstorage n'appellent aucun fichier ; `init`, `init2`, `hwmanager`, `hidmanager`, `usbd`, `imgblk` : clients ordinaires de vfs |
| Compteur | `relais_compter` (`files.asm`) dans `open`, `stat`, `mkdir`, `unlink`, `chdir` (sans la marque), `getcwd` du noyau : « `relais fichiers : open par <nom> (n)` » au journal, une ligne d'un bloc |
| Faucheur (section 8) | `proc_on_exit` (avis de fin compris) **avant** de rendre l'emplacement de la table des process (`proc_tab`) : un pid ne peut plus être repris avant l'avis de fin |

**Tests (QEMU)** : `vfstest` 37 PASS ; `fstest2` 50 PASS (dont les registres) ; `fstest`,
`ramtest RAM0`, `ramtest IMG0`, `pipetest` (tube du noyau) passent ; `exfattest` : 5 échecs,
tous sur « `SD1:/Dossier été` », absent de `skel/sd1` (6 par le relais du noyau, qui refuse en plus
« `/` ») ; FASMARM sous ARMPI (`fasmarm hello.asm hello.elf` dans `SD0:/t/bin/hello`, depuis le
terminal) : `hello.elf` identique à celui de Windows. Bureau : Fichiers (SD0, docs, IMG0, RAM0,
USB0), éditeur (ouvrir `README.TXT`, enregistrer `RAM0:/note.txt`), terminal (`pwd`, `cd`,
`cd /bin`, `ls`, `cat`, `cp`, `echo > fichier`, `mkdir`, `rm`, programmes lancés), réglages
(thème appliqué, clavier), dock, moniteur, navigateur, dessin, cibles, notification (`notify`),
clé USB branchée à chaud (`USB0:` monté, lu) : **journal sans aucune ligne du compteur**.
Débit (`ramtest`, 256 Ko, relais → AppKit) : RAM0 écriture 71 → 11 ms, lecture 66 → 9 ms ;
SD0 écriture 633 → 274 ms, lecture 194 → 83 ms.

### 5.2 Tel qu'écrit à l'étape 4 (29/10/2026)

Lancement hors du noyau, tubes et entrées / sorties standard dans vfs et AppKit. Le noyau garde
son ancien `spawn` / `spawn_pipe` (23, 26) et `pipe.asm` pour l'étape 5, mais plus aucun programme
ne s'en sert (sinon : « `relais fichiers : spawn par <nom>` » au journal).

| Point | Choix |
|---|---|
| Appel 97 `spawn_image(image, taille, params)` | `src/kernel/spawn.asm` (nouveau, ≈ 120 lignes). params (48 o) : nom, arguments, options (bit 0 : droits), entrée, sortie, erreur (poignées de vfs : 0 = celle du père, −1 = aucune). Image ≤ 64 Mo, **recopiée dans des pages du noyau** (`proc_spawn_elf` ne lit pas l'ELF dans la mémoire de l'appelant, qu'un autre thread pourrait changer), dossier courant du noyau = celui de l'appelant (`P_CWD`, pour l'ancien relais) |
| Droits (D9) | le noyau donne `P_DROITS` seulement si **l'appelant l'a** et le demande ; demandé sans l'avoir : `E_PERM`. AppKit le demande quand vfs marque le fichier `ATTR_SYS` (chemin résolu sous `/sd0/sys/`). L'ancien `spawn` du noyau applique aussi D9 (chemin système **et** père qui a les droits) : un process sans droits ne peut plus en créer un qui en a, par aucun chemin |
| Appel 98 `proc_flags()` | bits 0–2 : entrée, sortie, erreur standard dans un tube de vfs (`P_STDIO`, PCB +784, calculé par `proc_spawn_elf` : donné par le père, ou hérité de son masque) ; bit 8 (`PROC_RIGHTS`) : `P_DROITS`. Écart avec la section 4.3 : pas de `tube_lire` / `tube_fermer` (les tubes sont dans vfs) ni de `P_TUBES` |
| `proc_spawn_elf` | x7 = entrées / sorties du fils (3 × 8 o, 0 : toutes héritées) ; `svc_naissance` les met dans l'avis (c = entrée, d = sortie, données = erreur) |
| Tubes (vfs, `src/svc/vfs/pipes.inc`) | 32 tubes de 4 Ko (anneau), 128 bouts ; un bout = une poignée qui appartient à un pid (`E_BADF` pour un autre). Ops : 37 `V_PIPE` (→ a lecture, b écriture), 38 `V_PIPE_READ` (a, b ≤ 464, c bit 0 `PIPE_NOWAIT`), 39 `V_PIPE_WRITE` (→ octets acceptés, 0 si plein, `E_PIPE` sans lecteur), 40 `V_PIPE_CLOSE`, 41 `V_STDIO` (→ a, b, c : poignées du pid). Fin (0) quand le dernier écrivain ferme |
| Lecture bloquante | **réponse différée** : vfs garde la requête (16 au plus) et répond à l'arrivée de données ou de la fin ; `svc_next` passe à 500 ms tant qu'une lecture attend ; au bout de 10 s, `E_AGAIN` (le client redemande : `svc_call` abandonne à 30 s, une réponse plus tardive serait perdue). Table pleine : `E_AGAIN` |
| Écriture pleine | pas d'attente chez vfs : AppKit dort 2 ms et réessaie (`O_NONBLOCK` : `E_AGAIN`) |
| Entrées / sorties d'un fils | l'avis de naissance porte celles que le père donne ; vfs vérifie que chaque bout **appartient au père** (pid du père donné par le noyau) et en fait une copie au fils (un lecteur ou un écrivain de plus). Par défaut (0) : celles du père. Avis de fin : bouts fermés, lectures en attente abandonnées |
| AppKit (version 31) | descripteur de type **4** = bout de tube (canal de vfs, poignée) ; descripteurs **0, 1, 2** = entrée, sortie, erreur standard : demandées au premier usage (`proc_flags`, puis `V_STDIO` seulement si un bit est mis : vfs et les services, sans tube, n'envoient aucune requête et ne peuvent pas s'interbloquer avec vfs) ; sans tube : la console du noyau (write 1/2 → print ; read 0 → fin). Nouvelles fonctions : `f_print`, `f_spawn`, `f_spawn_pipe`, `f_spawn_stdio` (descripteurs du fils : un tube, 0–2 du père, < 0 la console), `f_spawn_file` (lancement depuis une réponse de `V_EXEC` : sert à vfs), `f_pipe` (deux descripteurs, lecture bloquante) |
| `print` (appel 1) | la macro `sys` l'envoie aussi à `fichier_sys` (`f_print`) ; `puts` d'AppKit aussi : la sortie d'un programme arrive dans le tube de son père **pendant qu'il tourne**. Sans AppKit (kits sauf AppKit, fasmarm sans `appkit.so`) : le noyau, comme avant |
| `spawn` / `spawn_pipe` (23, 26) | dans AppKit : `V_EXEC` (données de la réponse : nom, 0, **chemin résolu « `SD0:/…` »**, 0), lecture entière dans un **tampon partagé temporaire** (`buf_create`, rendu après : le tas, jamais rendu, ne grossit pas à chaque lancement), `spawn_image`. `spawn_pipe` : tube neuf, sortie **et erreur** du fils dedans, bout de lecture du père en `O_NONBLOCK` (même usage qu'avant : `E_AGAIN`, 0 à la fin), le père ferme son bout d'écriture après le lancement |
| init2, op `INIT_OP_LAUNCH` (= 1) | quand AppKit voit `ATTR_SYS` sans avoir les droits (`proc_flags`), il ferme le fichier et demande à init2 (données : chemin résolu, 0, arguments, 0). init2 accepte **tout chemin sous `SD0:/sys/`** (sans composant « `..` »), sinon `E_PERM`, et le lance par son propre AppKit : les droits viennent de `ATTR_SYS` (vfs) **et** de la vérification du noyau (init2 a `P_DROITS`). **Plusieurs instances** sont permises (fatfs par volume, imgblk, usbkeyboard par appareil) : un second service du même nom échoue de lui-même (`svc_register` : `E_BUSY`). Le fils a init2 pour père : ses entrées / sorties sont **la console (journal du noyau)**, pas le tube du demandeur, et son dossier est celui d'init2 ; il survit au terminal. Chaque demande est notée : « `init2 : launch par 17 svctest : SD0:/sys/svc/echo -> 23` » (pid et nom du demandeur, chemin, pid ou erreur). Liste blanche et authentification : à la toute fin du projet |
| vfs lui-même | son thread principal ne s'envoie jamais de requête : `monter` (service hors d'initramfs, depuis la requête 20) lance par `spawn_local` : `ops_chemins` (`V_EXEC` au nom de vfs, `msg` sauvé puis rendu) puis `f_spawn_file` (lecture directe chez le service FS). Le thread de `mounts.txt` passe par AppKit (`spawn` → vfs, servi par le thread principal) |
| fasmarm | gabarit de `tools/x86toa64.py` : l'écriture sur 1 / 2 (Linux) passe par `armpi_fichier` (print d'AppKit : le tube du terminal) au lieu de `svc 0` (3 lignes ; `fasmarm.asm` = l'ancien fichier + ce changement, comme à l'étape 3) |
| Programme détaché | lancé par init2 (ci-dessus) : il n'est pas relié au demandeur. `spawn_pipe` rend alors le pid **et `*fd = −1`** (pas de tube : sortie au journal) ; `spawn` rend le pid comme d'habitude. Le terminal affiche « `[lancé par init2 : pid N, sortie dans le journal]` » et rend l'invite tout de suite, sans `wait` : un service qui ne s'arrête jamais (`SD0:/sys/svc/echo`) ne bloque plus le terminal, et il survit à sa fermeture |
| Ancien relais | `sys_spawn`, `sys_spawn_pipe` du noyau comptés ; `spawn_image` ne donne plus de `P_STDOUT` (0) |
| Noms | identifiants nouveaux en anglais (ops `V_PIPE*`, `V_STDIO`, `INIT_OP_LAUNCH`, `SYS_SPAWN_IMAGE`, `SYS_PROC_FLAGS`, fichiers `spawn.asm`, `pipes.inc`, fonctions `f_spawn*`, `f_pipe`, `f_print`) ; commentaires et messages en français |

**Tests (QEMU)** : `pipetest` réécrit : **31 PASS, 0 FAIL** (spawn_pipe de `hello`, descripteur ≥ 64,
code par `wait` ; « `pipetest child` » : 5 lignes à 200 ms, **≈ 1 s entre la première ligne reçue et
la fin** : sortie en continu, code 7 ; `f_pipe` + `f_spawn_stdio` en lecture bloquante jusqu'à 0 ;
entrée standard d'un fils (`cat`) ; petit-fils qui hérite de la sortie ; `E_BADF`, `E_PIPE`,
`E_AGAIN`, `E_NOENT`) ; `vfstest` 0 échec (dont `V_EXEC` et les fils lancés par AppKit) ; `fstest`,
`fstest2` (0 échec), `ramtest RAM0`, `ramtest IMG0` ; `svctest` (au démarrage et depuis le
terminal : `hwtest` et `echo` lancés par init2, droits vérifiés, service arrêté → −2). Bureau :
terminal (`pwd`, `ls`, `cat`, `hello un deux` affiché en continu, « `pipetest child` » puis
« `[code de sortie 7]` », `echo … > RAM0:/a.txt`, `cd`, `cd /`, `svctest`), **FASMARM sous ARMPI**
(`fasmarm hello.asm hello.elf` dans `SD0:/t/bin/hello` : sa sortie dans le terminal, `hello.elf`
identique à celui de Windows), éditeur (enregistrer `RAM0:/note.txt`, relu par `cat`), Fichiers,
dock, clé USB branchée à chaud (`usbstorage` lancé par hwmanager, `USB0:` monté, `ls USB0:/`) :
**journal sans aucune ligne « `relais fichiers` »**. Selfhost et PocketTerm pas encore essayés.

Fait à l'étape 5 (section 5.3) : `sys_spawn`, `sys_spawn_pipe`, `kspawn_path`, `spawn_cwd`,
`P_STDOUT`, `P_CWD`, `pipe.asm`, `files.asm`, `remote.asm` retirés.

### 5.3 Tel qu'écrit à l'étape 5 (29/10/2026)

Ménage du noyau, et **aucune lecture de fichier par le noyau** : pas de `kcharger` (D11
remplacée, décision de l'utilisateur) ; les kits sont lus hors du noyau comme les programmes à
l'étape 4.

| Point | Choix |
|---|---|
| Retirés | `src/kernel/files.asm` (1 169 lignes : chemins, descripteurs, relais 12–23, spawn, compteur « relais fichiers »), `remote.asm` (357 : le relais vers vfs et les services FS, `dist_charger` — rien d'autre : pas de fonction « distante » visible, aucun outil de `tools/` ne s'en servait), `pipe.asm` (151 : tubes du noyau) ; `P_FILES`, `P_CWD`, `P_STDOUT` du PCB (place laissée libre) ; `files_mutex`, `file_pool` (32 Ko de BSS) ; `relais_compter` |
| Appels | 12–23 et 26 → `E_NOSYS` (`sys_nosys`), numéros gardés dans `armpi.inc` avec un commentaire jusqu'à la renumérotation (chantier 6 bis) ; 24 `wait` passe dans `spawn.asm` ; nouveaux : **99 `kit_register`**, **100 `boot_file`** |
| Déplacés | `copy_str_from_user` → `syscall.asm` (à côté de `check_user`) ; constantes `E_*` → `syscall.asm` ; `sys_print` : la console seule (la sortie standard d'un programme est le tube de vfs, par AppKit) ; `proc_spawn_elf` : x4, x5 inutilisés (dossier courant et tube du noyau : chez vfs) ; `kits.asm` inclus après `syscall.asm` (macros `check_user`, `current_proc`) |
| Kits (`kits.asm`) | **54 `lib_open` = kit_find** : un kit déjà en mémoire (mappé dans l'appelant, version vérifiée), ou un module de démarrage `<nom>.so` (seulement `appkit.so` : le seul kit qui ne passe pas par vfs) ; sinon `E_NOENT`. **99 `kit_register(nom, image, taille)`** : droits de `SD0:/sys` (`E_PERM` sinon), image (64 o à 16 Mo) recopiée dans des pages du noyau, chargée (`kit_load_image`), mappée dans l'appelant, copie rendue ; un nom déjà inscrit : l'existant est mappé (deux chargements en même temps : idempotent, sous `kits_mutex`). Un kit chargé reste en mémoire |
| `kits_open` (`kit.inc`) | sur `E_NOENT` : `lib_open("appkit", 32)`, appel de `f_kit_load(nom)` par le **rang 31** de la table d'AppKit (`KIT_LOAD_RANK`, vérifié par `assert` dans `appkit.abi`), puis second `lib_open` ; x5–x7, x9–x15, x18, x30 sauvés autour de l'appel (la garantie « x0–x8, x16, x17 seulement » tient) |
| `f_kit_load(nom)` (AppKit 32) | nom de 1 à 23 caractères `a-z 0-9 _` (`E_INVAL`) ; **sans droits** (`proc_flags`) : requête `INIT_OP_KIT_LOAD` (= 2) à init2 (données : le nom) ; **avec droits** : `V_OUVRIR` de `SD0:/lib/<nom>.so`, refus sans `ATTR_LIB` (`E_PERM`), lecture entière dans un **tampon partagé temporaire** (`buf_create`, comme `spawn`), fermeture, `kit_register`, puis `buf_free` : rien ne reste chez l'appelant |
| vfs | `ATTR_LIB` = 0x200 (`armpi.inc`) dans c d'une ouverture (30, 36) sous `/sd0/lib/` (à côté d'`ATTR_SYS` : `marquer_sys`) ; **ops 1–4 retirées** (`transmettre` : `E_INVAL`) ; `V_CHDIR` venant du noyau (b = pid) retiré |
| init2 | op **`INIT_OP_KIT_LOAD`** : `f_kit_load(nom)` par son propre AppKit (init2 a les droits : `kit_register` direct), réponse 0 ou erreur, une ligne au journal : « `init2 : kit_load par 15 kittest : netkit -> 0` ». Le demandeur fait ensuite `lib_open` |
| Fichiers du noyau (`bootfiles.asm`) | options et WiFi : **100 `boot_file(nom, image, taille)`**. init (initramfs, droits) demande la liste (`nom = 0` : « `SD0:/etc/options.txt` », plus, sur le vrai Pi seulement, `SD0:/etc/wifi.txt` et les trois fichiers de `SD0:/firmware/brcmfmac43455-sdio.*`), lit chacun par AppKit (tampon partagé temporaire) et le donne **avant** `demarrage_pret` (« `init : pour le noyau SD0:/etc/options.txt -> 0` ») ; absent : passé. Le noyau en garde une copie (4 Mo au plus, droits exigés, noms de la liste seulement) ; `boot_file_take(nom)` remplace `kload_file` (`opts_load`, 4 appels de `wifi.asm`) : la copie passe à l'appelant. Option `no_wifi` : `boot_files_drop` rend les fichiers du WiFi. Les fichiers du WiFi partiront avec le WiFi du noyau (chantier 5), mais **le mécanisme reste** (décision de l'utilisateur, 29/10/2026) : le noyau demande à init, une fois `SD0:` monté, les fichiers dont il a besoin au démarrage ; un futur besoin = une ligne dans `bf_noms`. Écarté : les mettre dans l'initramfs (copie à refaire à chaque réglage) |
| AppKit | descripteurs 3–63 : `E_BADF` (plus de descripteurs du noyau) ; `chdir` : vfs seul (plus de `FC_CHDIR_SYNC`, retiré d'`armpi.inc`) |
| Services FS (`fsowner.inc`) | plus de fichier « du relais » ouvert à tout pid : un fichier n'est accessible qu'à son propriétaire et au noyau (avis, pid 0) |
| `diag.asm` | `files_mutex` retiré de la liste des verrous ; fonctions repères : `spawn_image`, `svc_call`, `kit_register`, `lib_open` |
| Entrées / sorties d'un programme lancé par init2 | **pas fait** (pas simple) : les poignées de tube du demandeur lui appartiennent, et vfs vérifie à la naissance que chaque bout donné appartient au **père** (init2). Il faudrait une op de vfs qui confie un bout à un autre pid (`V_PIPE_GIVE(poignée, pid)`, comme `FS_TRANSMETTRE` de la section 5) puis un passage des poignées dans `INIT_OP_LAUNCH`. Aujourd'hui : la console (journal), programme détaché |
| Test | **`kittest`** (`src/bin/kittest`, sans droits) : `lib_open(netkit)` → −2 avant, `f_kit_load` par init2 → 0, `lib_open` ensuite ; **150 chargements de suite** (au-delà des 128 tampons du système : un tampon perdu par chargement ferait échouer `buf_create`), mémoire libre perdue 0 Ko, tas d'init2 +0 Ko ; `E_NOENT`, `E_INVAL`, `E_PERM` (`kit_register`, `boot_file`), `E_NOSYS` (ancien `open`), `E_BADF` (`read(5)`) : 11 PASS |

**Tests (QEMU)** : démarrage jusqu'au bureau ; `kittest` 11 PASS (lancé depuis le terminal :
« mémoire libre perdue 0 Ko ») ; `vfstest` 0 échec ; `fstest` ; `fstest2` 0 échec ; `ramtest
RAM0`, `ramtest IMG0` ; `pipetest` 31 PASS, 0 FAIL ; `svctest` (au démarrage et depuis le
terminal : `hwtest`, `echo` lancés par init2) ; `exfattest` : les 5 échecs connus (« `SD1:/Dossier
été` » absent de `skel/sd1`) ; `options.txt` donné par init (`no_gpu` appliqué) ; journal écrit dans
`SD0:/journal.txt` ; terminal (`cat`, `cd`, `pwd`, `ls`, `hello`, `echo … > RAM0:/a.txt`, `cat`,
`SD0:/sys/svc/echo` → « lancé par init2 », invite rendue, `notify` : notification affichée) ;
**FASMARM sous ARMPI** (`fasmarm hello.asm hello.elf` dans `SD0:/t/bin/hello` : `hello.elf`
identique à celui de Windows) ; éditeur (ouvrir `SD0:/README.TXT`, enregistrer `RAM0:/note.txt`,
relu par `cat`) ; Fichiers ; moniteur ; réglages (thème Peach appliqué) ; dock ; clé USB branchée
à chaud (`usbstorage` lancé par hwmanager, `USB0:` monté, `ls USB0:/`). Noyau Pi 4 : **93 064 →
88 968 octets** (Pi 5 : 69 912 → 65 816), BSS : −32 Ko (`file_pool`). Selfhost et PocketTerm
(WiFi : fichiers donnés par init) pas encore essayés.

**Reste ouvert** :
* recharger un kit après une recompilation sous ARMPI (`kitreload`, anciennes versions gardées
  jusqu'au départ de leur dernier utilisateur) — plus tard, à la demande de l'utilisateur ; AppKit
  (module d'initramfs) en resterait exclu ;
* entrées / sorties du demandeur pour un programme lancé par init2 (ci-dessus) ;
* `FS_CHARGER` sans copie (étape 6).

## 6. Étapes

Chaque étape démarre et se teste seule (QEMU, puis le PocketTerm) ; la branche de retour est
prise avant chacune. Les tests écrivent sur `RAM0:` et `SD0:/tmp` (jamais sur la partition
personnelle du PocketTerm).

| Étape | Noyau | vfs | Services FS | appkit / programmes | Test | Retour |
|---|---|---|---|---|---|---|
| **0. Services FS prêts** (fait, 29/10/2026) | `BUF_MAX` et `SR_MAX` à 128 (`P_BUFS` : masque de 128 bits, PCB +832) | – | propriétaire par fichier (noyau = `SVC_PID_NOYAU`), `SVF_FINS`, ops 14–16, fermeture à la fin d'un pid | – | `fstest`, `ramtest`, `exfattest` (inchangés : tout passe encore par le noyau) ; nouveau `fstest2` : ops 14–16 en direct (RAM0, IMG0), fils qui laisse 60 fichiers ouverts et touche ceux du père (`E_BADF`) | fatfs/ramfs précédents |
| **1. vfs connaît chemins et dossiers** (fait, 29/10/2026) | avis de naissance (`SVF_NAISSANCES`, ≈ 35 lignes) | `path.inc`, table des dossiers, ops 30–36, avis 0xFFFE/0xFFFF ; ops 1–4 gardées pour le relais | – | – | `vfstest` (bin) : ops 30–36 par `SVC_CALL`, `..`, relatif, `SD:`, refus de « `/` » ; dossier hérité par un fils | vfs précédent ; l'avis est ignoré par qui ne s'y inscrit pas |
| **2. Client de fichiers dans appkit, programme par programme** (fait, 29/10/2026) | relais 21 (`chdir`) : prévient aussi vfs (op 34 avec b = pid) pour que les deux dossiers restent égaux | – | – | appkit v14 : table, `fichier_sys`, `f_*` ; `chdir` : vfs **et** noyau ; spawn / spawn_pipe / wait encore transmis au noyau (descripteur de type 4 pour le tube) ; macro `sys` active si le programme définit `FICHIERS_APPKIT = 1` : `fstest`, `ramtest`, puis `files`, `editor`, `terminal`... | chaque programme passé : ses tests et un usage réel ; débit (lecture d'un fichier de 4 Mo : avant/après) | retirer la définition dans le programme |
| **3. Tous les programmes** (fait, 29/10/2026, sur QEMU ; selfhost et PocketTerm pas encore essayés) | compteur « appel fichier du noyau par <nom> » (journal) pour trouver les retardataires | résolveur local d'appkit utilisé | – | macro active par défaut ; UIKit (Q2) ; fasmarm (gabarit) ; imgblk ; `selfhost` : 21 identiques | démarrage QEMU et PocketTerm, `selfhost`, `pipetest`, Fichiers, éditeur, terminal, clé USB ; journal sans compteur | `FICHIERS_APPKIT = 0` global |
| **4. Lancement hors du noyau** (fait, 29/10/2026, sur QEMU ; section 5.2) | appels 97–98 (`spawn.asm`), `P_STDIO` (pas de `P_TUBES`) | tubes, entrées / sorties standard (`pipes.inc`), lancement local | – | `spawn`, `spawn_pipe` par `V_EXEC` + `spawn_image` ; descripteurs 0–2, type 4 ; init2 : `INIT_OP_LAUNCH` | `pipetest`, terminal (commandes, sortie), init2 et ses services (droits), Elegant, dock, `svctest` (Q3) | appkit v14 de l'étape 3 (spawn par le noyau) |
| **5. Ménage** (fait, 29/10/2026, sur QEMU ; section 5.3) | 12–23 et 26 → `E_NOSYS` ; **suppression de `files.asm`, `remote.asm` et `pipe.asm`** ; pas de `kcharger` : kits par `kit_register` (99), options et WiFi par `boot_file` (100, `bootfiles.asm`) ; `P_CWD`, `P_FILES`, `P_STDOUT` retirés du PCB ; `diag.asm` mis à jour | ops 1–4 retirées ; `ATTR_LIB` | plus de fichier du relais | `f_kit_load` (AppKit 32), `kits_open` ; init2 : `INIT_OP_KIT_LOAD` ; init : `boot_file` | tout le parcours de l'étape 3 + `kittest` ; Wi-Fi sur le PocketTerm à faire | `git` : commit de l'étape 4 (1d74035) |
| **6. Plus tard** | `bootfiles.asm` part avec le Wi-Fi (chantier 5) | – | `FS_CHARGER` (sans copie) | `f_charger` ; images de `spawn` et des kits sans copie (depuis un tampon partagé) | lecture d'images et de polices | – |

## 7. Ce qui reste au noyau, ce qui part

| | Lignes |
|---|---:|
| Prévu — supprimé : `files.asm` (1 039), `remote.asm` (357), entrées de la table d'appels | ≈ −1 400 |
| Prévu — ajouté : `spawn_image` ≈ 90 ; tubes 98–99 + `P_TUBES` ≈ 70 ; avis de naissance ≈ 35 ; `kcharger.asm` ≈ 110 | ≈ +305 |
| **Fait (étape 5)** : supprimés `files.asm` (1 169), `remote.asm` (357), `pipe.asm` (151) ; ajoutés `spawn.asm` (146), `bootfiles.asm` (206), `kit_register` (+≈ 90) ; tout `src/kernel/*.asm` : 14 150 → 12 804 lignes | **−1 346** ; `kernel8.img` 93 064 → 88 968 o |
| vfs : chemins (≈ 300, repris), dossiers, ops 30–36, exec | ≈ +450 (464 → ≈ 900) |
| appkit : table, verrou, `fichier_sys`, `f_*`, tampons, spawn | ≈ +600 |
| fatfs, ramfs : propriétaire, fins, ops 14–16 | ≈ +80 chacun |

Le noyau garde (état final) : la messagerie (`svc.asm`), `sys_print` (la console seule),
`proc_spawn_elf` derrière `spawn_image`, `wait`, `proc_flags`, les modules d'initramfs
(`mod_lancer`, `mod_lire`), la table des kits et leur mappage par process (`lib_open` = kit_find,
`kit_register` ; `appkit.so` pris dans initramfs), et les fichiers que init lui donne
(`boot_file` : options, WiFi). Il ne lit aucun fichier, n'analyse aucun chemin, ne tient ni
descripteur ni tube ni dossier courant.

## 8. Risques

| Risque | Parade |
|---|---|
| Un programme comptait sur x16 après un appel, ou sur un drapeau NZCV | x16 : aucun usage trouvé ; NZCV sauvé par `fichier_sys` |
| Avis perdus : `svc.asm` abandonne un avis de fin si les requêtes (`SR_MAX`, 128 depuis l'étape 0) sont prises ; avec vfs, fatfs×n, ramfs, Elegant inscrits, plus d'avis par fin | `SR_MAX` à 128, ou une file d'avis séparée ; un fichier orphelin chez fatfs garderait sa taille non réécrite |
| Tampons partagés : 64 dans tout le système (`BUF_MAX`, 128 depuis l'étape 0), déjà pris par les fenêtres et fatfs↔bloc ; un par couple (process, service) en plus | tampon créé seulement au premier transfert > 464 o, rendu à la fin ; repli sur les messages ; passer `BUF_MAX` à 128 (zone virtuelle : 2 entrées L1 de plus) |
| Deux dossiers courants pendant la transition (noyau `P_CWD`, vfs) | `chdir` d'appkit écrit les deux ; le relais 21 du noyau prévient vfs (étape 2) |
| Interblocage : un service qui s'ouvre un fichier à lui-même (vfs, imgblk sur son propre volume) | résolveur local de vfs ; imgblk : refuser une image sur le volume qu'il sert |
| Pid réutilisé avant que vfs ou fatfs aient traité la fin | `vfstest` (QEMU) : le second fils reprend le pid du premier et reçoit bien le dossier du père. Mais le faucheur retire le pid de `proc_table` **avant** `proc_on_exit` (qui poste l'avis de fin) : un `spawn` sur un autre cœur dans cet intervalle (quelques µs) mettrait la naissance avant la fin, et vfs effacerait le dossier du nouveau (retour à « `/sd0` »). **Corrigé** (étape 2) : le faucheur appelle `proc_on_exit` (avis de fin) avant de libérer l'entrée |
| Sécurité inchangée : n'importe quel process peut parler à `fs.sd0` directement | comme aujourd'hui ; les droits d'accès aux fichiers sont un autre chantier |

## 9. Questions ouvertes

1. **Macro `sys`** (D7) : d'accord pour que `sys SYS_OPEN` devienne un appel d'appkit (seul écart :
   x16), plutôt que de réécrire les ~300 sites ?
2. **UIKit** ouvre des fichiers (polices, thème) mais un kit ne dépend d'aucun autre : (a) permettre
   à un kit d'importer appkit (`kit_use` dans un kit, `kits_open` dans `ui_init`), ou (b) une copie
   réduite du client (lecture seule) dans UIKit ? Proposé : (a).
3. **Droits** (D9) : un programme sans droits ne pourra plus lancer un programme de `SD0:/sys`
   avec droits — c'est le cas de `svctest` (`SD0:/sys/svc/echo`, `hwtest`) et d'un lancement à la
   main depuis le terminal. Accepter (déplacer `svctest` dans `SD0:/sys/bin`), ou ajouter à init2
   une op « lancer » qui le fait pour eux ?
4. **fasmarm** : modifier le gabarit de `tools/x86toa64.py` pour lier appkit (le fichier généré
   change, le contrôle bit à bit du selfhost aussi) ?
5. **Numéros des descripteurs** 64–127 au lieu de 0–31 : un programme qui suppose « petit
   numéro » ou range les descripteurs dans un octet ? Rien vu, à confirmer.
6. **`BUF_MAX`** à 128 et **`SR_MAX`** à 128 dès l'étape 0 ?
7. **`kcharger`** : garder ce client de vfs dans le noyau pour les kits jusqu'à nouvel ordre (le
   noyau mappe les kits dans chaque process), ou faire charger un kit par le process (appkit étant
   lui-même un kit, il resterait un cas à part) ?
8. **Tubes** : rester au noyau (D10) jusqu'à un service de tubes — quand ? (stdin des commandes,
   tubes entre deux fils).

## 10. Réponses de l'utilisateur (29/10/2026)

1. Macro `sys` vers appkit : **oui**.
2. UIKit : un kit peut **importer appkit** (a).
3. Droits : **init2 reçoit une op « lancer »**, acceptée seulement pour un chemin sous `SD0:/sys`
   (`svctest`, `hwtest`, lancement à la main depuis le terminal).
4. fasmarm : **oui**, le gabarit de `tools/x86toa64.py` change (contrôle par le selfhost).
5. Descripteurs 64–127 : rien à faire.
6. `BUF_MAX` et `SR_MAX` à **128** dès l'étape 0.
7. `kcharger` reste au noyau pour les kits, jusqu'à ce qu'appkit les charge. **Revu à l'étape
   5** : pas de `kcharger` du tout ; AppKit lit les kits, le noyau les inscrit (`kit_register`,
   droits ; sinon par init2), seul `appkit.so` vient d'initramfs ; un kit reste en mémoire une
   fois chargé (recharger après une recompilation : plus tard, `kitreload`).
8. **Les tubes vont dans vfs** (fait à l'étape 4, section 5.2) (pas dans le noyau, pas dans un service à part) : un tube est un
   fichier servi par vfs lui-même (création : deux descripteurs, lecture et écriture ; lecture
   bloquante par réponse différée ; fin quand le dernier écrivain ferme). Ils arrivent avec
   l'étape 4 (lancement hors du noyau : la sortie d'un fils dans un tube) ; `pipe.asm` part du
   noyau à l'étape 5.

**stdio** (29/10/2026) : appkit porte les trois descripteurs standard (entrée, sortie, erreur) et
les fonctions qui s'en servent ; vfs sert ce vers quoi ils pointent (un tube : le terminal, le front
end de FASMARM lisent ainsi la sortie d'un programme ; plus tard un pseudo-terminal ; sans parent à
l'écoute, le journal du noyau). Au lancement, le parent dit à quoi relier les trois descripteurs du
fils (par défaut : les siens). Avec l'étape 4.
