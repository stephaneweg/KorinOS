# ARMPI OS — interface des applications

Exécutables **ELF64 AArch64** (`format ELF64 executable 3 at 0x4000000000`, voir
`src/inc/armpi.inc`). Point d'entrée : `x0` = ligne d'arguments (chaîne UTF-8) ou 0.

Appel système : `svc 0`, numéro dans `x8`, arguments `x0..x5`, résultat dans `x0`
(négatif = erreur : -1 EPERM, -2 ENOENT, -3 ESRCH, -9 EBADF, -11 EAGAIN, -12 ENOMEM, -14 EFAULT,
-17 EEXIST, -20 ENOTDIR, -21 EISDIR, -22 EINVAL, -28 ENOSPC, -38 ENOSYS, -39 ENOTEMPTY,
-110 ETIMEDOUT).
Tous les registres sauf `x0` sont préservés ; les registres FP/NEON ne sont jamais touchés.

## Numérotation (chantier 6 bis)

Les numéros sont **groupés par domaine sur le chiffre hexadécimal de tête**, sans trou dans
chaque plage ; la place libre en fin de plage est réservée aux appels à venir (`E_NOSYS`). Seule
source : **`src/inc/syscalls.inc`** (constantes `SYS_*`), inclus par `armpi.inc` et par le noyau
(`src/kernel/syscall.asm` : la macro `sysent` construit la table et refuse un numéro qui recule).
Un programme n'écrit jamais un numéro en dur : `sys SYS_xxx` (ou `mov x8,SYS_xxx` / `svc 0`).

| Plage | Domaine | Occupés | Libres |
|---|---|---|---|
| `0x00`–`0x0F` | mémoire : tas, tampons partagés, DMA, régions (mmap) | `0x00`–`0x0D` | `0x0E`–`0x0F` |
| `0x10`–`0x1F` | process et temps | `0x10`–`0x1A` | `0x1B`–`0x1F` |
| `0x20`–`0x2F` | threads, futex, signaux | `0x20`–`0x2F` | – |
| `0x30`–`0x3F` | IPC : services nommés | `0x30`–`0x35` | `0x36`–`0x3F` |
| `0x40`–`0x4F` | matériel et démarrage (programmes de `SD0:/sys`) | `0x40`–`0x4C` | `0x4D`–`0x4F` |
| `0x50`–`0x5F` | serveur graphique (ce qu'il reste à Elegant) | `0x50`–`0x5B` | `0x5C`–`0x5F` |
| `0x60`–`0x6F` | système : console, journal, état, aléa, kits, images des exécutables, membarrier | `0x60`–`0x68` | `0x69`–`0x6F` |
| `0x70`–`0x7F` | IPC entre process : poignées, canaux locaux, mémoire partagée (couche POSIX, WebKit2) | `0x70`–`0x7D` | `0x7E`–`0x7F` |
| `0x80`–`0x8F` | **fichiers et lancement : AppKit**, pas le noyau | `0x80`–`0x8E` | `0x8F` |

Le noyau ne sert que `0x00`–`0x7F` (`SYSCALL_COUNT` = `0x7E`) : tout autre numéro rend `E_NOSYS`.
Les anciens numéros (avant la renumérotation : 0–101, avec des trous laissés par les appels partis
dans les services) ne sont **pas** gardés ; les documents historiques (FICHIERS.md, RESEAU.md,
SON.md, NOYAU-HYBRIDE.md) les citent encore — correspondance en fin de section.

**Appels de fichiers (`0x80`–`0x8E`) : par AppKit.** Dans un programme qui importe AppKit
(`kit_use '…/appkit.abi'`, donc presque tous), la macro `sys` d'`armpi.inc` envoie ces numéros
(et `SYS_PRINT`) au client de fichiers d'AppKit, `fichier_sys` (`str x30,[sp,-16]!` /
`bl fichier_sys` / `ldr x30,[sp],16`), qui parle à vfs et aux services de fichiers sans passer par le
noyau ([FICHIERS.md](FICHIERS.md)). Même résultat et mêmes registres rendus qu'un `svc 0`
(x1–x15, x17, x18, x29, NZCV ; x30 gardé par la macro), sauf **x16** (relais du kit). Les
descripteurs d'AppKit sont **64 à 127** (plus 0–2 : entrée, sortie, erreur standard : un tube de vfs
donné par le père, sinon la console) ; les autres (3–63) n'existent pas (`E_BADF`). Un pointeur
invalide donne une faute (le process s'arrête) au lieu d'`E_FAULT`. Un tube est un descripteur
d'AppKit (`O_NONBLOCK` = 32 : `E_AGAIN` au lieu d'attendre). AppKit offre aussi ces appels en
fonctions AAPCS : `f_ouvrir`, `f_lire`, `f_ecrire`, `f_position`, `f_fermer`, `f_stat`,
`f_lire_rep`, `f_mkdir`, `f_unlink`, `f_chdir`, `f_getcwd`, `f_print`, `f_spawn`, `f_spawn_pipe`,
`f_spawn_stdio`, `f_spawn_file`, `f_pipe` ([FICHIERS.md](FICHIERS.md), 5.2). Par `svc 0` direct,
ces numéros rendent `E_NOSYS`. Les kits sont lus par AppKit (`f_kit_load`) et inscrits par
`kit_register` ; options et fichiers de démarrage sont donnés au noyau par init (`boot_file`)
([FICHIERS.md](FICHIERS.md), 5.3).

### 0x00 — mémoire

| N° | Nom (`SYS_*`) | Arguments | Résultat |
|---|---|---|---|
| `0x00` | `SBRK` | incrément | ancienne limite du tas |
| `0x01` | `BUF_CREATE` | taille | numéro (mappé à `0x68_0000_0000 + n × 64 Mo` ; 128 tampons dans tout le système, `E_NOMEM` au-delà) ; bit 62 de la taille (`BUF_LOW`) : pages sous 1 Go (sinon zone haute de préférence) ; bit 61 (`BUF_NOCACHE`) : vue non cachée de partout, pour un tampon que le GPU lit et écrit sans maintenance du cache (docs/MESA.md) |
| `0x02` | `BUF_GRANT` | numéro, pid | 0 (le créateur autorise ce process ; pid -1 : tous les process, depuis le 30/10/2026 — les surfaces de Jet, mappées par le process web et par Elegant) |
| `0x03` | `BUF_MAP` | numéro | adresse |
| `0x04` | `BUF_FREE` | numéro | 0 (libéré au dernier) |
| `0x05` | `DMA_ALLOC` | taille, &adresse physique | adresse d'une mémoire contiguë, à zéro, non cachée (`SD0:/sys`) ; bit 63 de la taille (`DMA_CACHED`) : **cachée**, à nettoyer par le programme (`DC CVAC`, permis en EL0 : `SCTLR_EL1.UCI`) avant qu'un DMA ne la lise (tampon arrière d'Elegant) ; pages **sous 1 Go** (zone basse) |
| `0x06` | `VM_MAP` | adresse, taille, droits `PROT_*`, drapeaux `VM_*` | adresse d'une **région à zéro peuplée à la demande** dans l'arène `0x58_0000_0000`–`0x68_0000_0000` (64 Go) ; taille arrondie à 4 Ko ; adresse : indication, sauf `VM_FIXED` (0x10 : à cette adresse, remplace ce qui y est) et `VM_FIXED_NOREPLACE` (0x100000 : `E_EXIST` si occupé) ; `VM_POPULATE` (0x8000) : peuplée tout de suite ; `VM_NORESERVE` (0x4000) : sans effet (rien n'est réservé d'avance) ; `PROT_NONE` (0) : simple réservation (de plusieurs Go si l'on veut) ; `PROT_READ` 1, `PROT_WRITE` 2, `PROT_EXEC` 4 ; `E_INVAL` (taille 0, droits inconnus, fixe hors de l'arène ou non alignée), `E_NOMEM` (plus de trou, plus de 4096 régions) |
| `0x07` | `VM_UNMAP` | adresse, taille | 0 ; les régions de `VM_MAP` de la plage sont retirées (coupées aux bords, trous permis), leurs pages rendues après invalidation des TLB sur tous les cœurs ; `E_INVAL` hors de l'arène ou adresse non alignée |
| `0x08` | `VM_PROTECT` | adresse, taille, droits | 0 ; la plage doit être couverte sans trou par des régions de `VM_MAP` (`E_INVAL` sinon, rien ne change) ; régions coupées aux bords, pages présentes reprotégées, TLB invalidées |
| `0x09` | `VM_ADVISE` | adresse, taille, conseil | 0 ; `MADV_WILLNEED` (3) : peuple (`E_NOMEM`) ; `MADV_DONTNEED` (4), `MADV_FREE` (8) : pages rendues, à zéro au prochain accès (régions paresseuses seulement : `E_INVAL` sinon) ; 0–2 : rien |
| `0x0A` | `VM_QUERY` | adresse, sortie 32 o | 0 : la région qui contient l'adresse, 1 : la suivante au-dessus, `E_NOMEM` : aucune ; sortie : +0 début, +8 fin, +16 droits (32 bits), +20 sorte (`VMK_ANON` 1 : `VM_MAP`, `VMK_BSS` 2 : `.bss` d'un exécutable, `VMK_STACK` 3 : pile du thread principal, `VMK_SHARED` 4 : mémoire partagée, `SHM_MAP`), +24 pages présentes (32 bits) |
| `0x0B` | `VM_STATS` | pid (0 : soi), sortie 64 o (`VS_*`) | 0 ; en pages de 4 Ko : +0 à lui (copies sur écriture comprises), +8 partagées (images préchargées, kits, tampons), +16 octets réservés dans les régions paresseuses, +24 pages remplies à la demande, +32 pages copiées sur écriture, +40 tables de pages, +48 pages libres en zone basse, +56 en zone haute |
| `0x0C` | `VM_PAGEINFO` | adresse | bits `PI_*` de la page : 1 présente, 2 partagée (`PTE_SHARED`), 4 en copie sur écriture, 8 inscriptible, 16 exécutable, 32 dans une région paresseuse, 64 lisible (EL0) |
| `0x0D` | `BUF_INFO` | numéro, sortie 32 o (`BI_*`) | 0 : +0 adresse physique (un tampon partagé est d'un seul bloc de pages ; sous 1 Go avec `BUF_LOW`), +8 octets, +16 pid du créateur, +24 pid autorisé ; ouvert à tous ; pour un pilote qui donne le tampon au matériel sans le mapper (service `v3d`, GpuKit : docs/V3D.md) |

**Régions paresseuses et fautes de page** (`src/kernel/vmspace.asm`). Une région est réservée
sans être peuplée ; la première touche d'une page (lecture, écriture, exécution permise par ses
droits) la remplit d'une page à zéro (zone haute de préférence) et l'accès reprend. Le **`.bss`**
de tout exécutable (la partie d'un segment au-delà du fichier) et la **pile du thread principal**
(256 Ko, sauf ses 16 Ko du haut) sont aussi des régions paresseuses ; le code, les données lues du
fichier, le tas de `SBRK` (borné à `0x50_0000_0000`), les kits, tampons, DMA et registres sont
peuplés d'avance comme avant. Un accès que la région ne permet pas (écriture après `PROT_READ`,
lecture `PROT_NONE`, hors de toute région) tue le process (code −11), comme avant ; faute de
mémoire libre : de même, avec « plus de mémoire pour une page » au journal. **Le noyau** qui lit ou
écrit un tampon du process le vérifie (`check_user`) : les pages absentes d'une région y sont
peuplées d'abord (un `READ` dans une région neuve marche) ; un tampon `PROT_NONE` rend `E_FAULT` ;
une faute EL1 restante sur une page paresseuse ou en copie sur écriture est résolue de même
(filet de sécurité), jamais fatale au noyau. Verrou par process (`P_VMLOCK`, spinlock : la faute
n'attend jamais, ne fait aucune entrée / sortie) ; table des régions triée, 4096 au plus, les régions `VM_MAP` voisines de mêmes droits recollées après `VM_MAP` et `VM_PROTECT` (un tas qui grandit par `mprotect` n'en ajoute pas une à chaque fois ; `VM_QUERY` voit la région recollée) ;
invalidation TLB par ASID sur tous les cœurs (`TLBI ASIDE1IS`) avant de rendre une page ; tables
de pages créées par échange atomique. Les tables L3 vidées par `VM_UNMAP` restent (une par 2 Mo
touchés), rendues à la fin du process. Plus de page libre : le noyau demande aux services `SVF_MEMORY` d'en rendre et attend jusqu'à 2 s avant de refuser (faute d'EL0, `check_user`, `VM_POPULATE`, `SBRK` ; pas `MADV_WILLNEED`) ; ils sont aussi prévenus sous 64 Mo libres. Pas encore : pages « épinglées » pendant un appel (un
`VM_UNMAP` d'un tampon qu'un autre thread du même process passe au même moment au noyau peut faire
écrire le noyau dans une page rendue), limite de mémoire par process.

### 0x10 — process et temps

| N° | Nom (`SYS_*`) | Arguments | Résultat |
|---|---|---|---|
| `0x10` | `EXIT` | code | — (tout le process) |
| `0x11` | `GETPID` | — | pid |
| `0x12` | `SPAWN_IMAGE` | image, taille, params 48 o {nom, arguments, options (bit 0 : droits), entrée, sortie, erreur (poignées de tubes de vfs, 0 : celles du père, −1 : console)} | pid ; droits seulement si l'appelant les a (`E_PERM` sinon) ; par AppKit (`spawn`) |
| `0x13` | `WAIT` | pid | code de sortie |
| `0x14` | `KILL` | pid | 0 |
| `0x15` | `PROCLIST` | tampon, max | nombre (entrées de 64 o : +0 pid, +8 threads, +16 Ko de tas, +24 nom (32 o), +56 état) |
| `0x16` | `PROC_FLAGS` | – | bits 0–2 : entrée, sortie, erreur dans un tube de vfs ; bit 8 : droits de `SD0:/sys` |
| `0x17` | `SLEEP` | ms | 0 |
| `0x18` | `YIELD` | — | 0 |
| `0x19` | `TIME_MS` | — | ms depuis le démarrage |
| `0x1B` | `CLOCK_GET` | 0 : secondes UTC, 1 : décalage local (s), 2 : secondes locales | la valeur ; l'horloge du système ([FICHIERS.md](FICHIERS.md), dates) |
| `0x1C` | `CLOCK_SET` | secondes UTC, décalage | 0 ; `E_PERM` sans les droits de `SD0:/sys` ; elle ne recule jamais (le décalage est toujours pris) |
| `0x1D` | `PROC_RIGHTS` | pid | 1 : programme de `SD0:/sys`, 0 : non ; `E_SRCH` |
| `0x1A` | `SPAWN_PRELOADED` | chemin résolu (« `SD0:/bin/fasmarm` »), taille du fichier, params 48 o (comme `SPAWN_IMAGE`) | pid ; depuis une **image préchargée** de ce chemin (majuscules ou minuscules) et de cette taille : code et lecture seule partagés, données en copie sur écriture, `.bss` à la demande ; `E_NOENT` s'il n'y en a pas (AppKit lit alors le fichier : `SPAWN_IMAGE`) ; par AppKit (`spawn`, après `V_EXEC`) |

### 0x20 — threads et futex

| N° | Nom (`SYS_*`) | Arguments | Résultat |
|---|---|---|---|
| `0x20` | `THREAD_CREATE` | entrée, sommet de pile, argument (x0 du thread) | tid ; `TPIDR_EL0` = 0, priorité normale, nom du process |
| `0x21` | `THREAD_EXIT` | code | — (le thread seul ; le process finit avec son dernier thread) |
| `0x22` | `FUTEX_WAIT` | adresse (alignée sur 4), valeur attendue, attente ms (0 : sans fin) | 0 (réveillé, ou réveil parasite) / `EAGAIN` (la valeur a déjà changé) / `ETIMEDOUT` / `EINTR` (thread tué) |
| `0x23` | `FUTEX_WAKE` | adresse, nombre | réveillés |
| `0x24` | `THREAD_CREATE_EX` | entrée, sommet de pile, argument, `TPIDR_EL0` initial, priorité, nom (0 : celui du process ; 23 caractères au plus, tronqué) | tid ; `EINVAL` (priorité inconnue), `EPERM` (temps réel sans droits) |
| `0x25` | `THREAD_JOIN` | tid (du même process) | code de `THREAD_EXIT` (ou de `THREAD_KILL`) ; attend la fin ; `ESRCH` : inconnu, ou fin plus ancienne que les 16 dernières du process ; `EDEADLK` : soi-même ; `EINTR` : l'appelant est tué. Un thread fini se rejoint autant de fois qu'on veut (tant qu'il est dans les 16 dernières fins) |
| `0x26` | `THREAD_SELF` | — | tid |
| `0x27` | `THREAD_SET_PRIORITY` | tid (0 : soi), priorité | ancienne priorité ; `ESRCH`, `EINVAL`, `EPERM` |
| `0x28` | `THREAD_SET_NAME` | tid (0 : soi), nom | 0 (23 caractères au plus, tronqué) |
| `0x29` | `THREAD_KILL` | tid (du même process ; 0 ou soi : comme `THREAD_EXIT`), code | 0 ; **différé** : la cible meurt à son prochain retour en EL0 (jamais au milieu d'un appel système) ; bloquée dans `FUTEX_WAIT` ou `THREAD_JOIN`, elle est réveillée ; endormie (`SLEEP`, `SVC_CALL`...), elle meurt au réveil |
| `0x2A` | `THREAD_LIST` | pid (0 : soi), tampon, max (≤ 64) | nombre ; entrées de 64 o (`THL_*`) : +0 tid, +8 état (0 prêt, 1 endormi, 2 bloqué, 3 mort), +16 priorité, +24 cœur, +32 temps consommé (ms), +40 nom (24 o) |

**Threads.** Le thread démarre en EL0 à l'entrée, `sp` = sommet de pile (aligné sur 16),
`x0` = argument, les autres registres à 0 ; il ne doit pas revenir de sa fonction (`x30` = 0) :
il finit par `THREAD_EXIT` (ou `EXIT` pour tout le process). Les threads d'un process sont
répartis sur les 4 cœurs (tourniquet à la création), puis restent sur leur cœur.

* **`TPIDR_EL0`** (registre de thread, TLS : la convention AArch64 « variante 1 ») est sauvé et
  rechargé à chaque commutation ; EL0 l'écrit lui-même (`msr tpidr_el0`), le noyau jamais. Valeur
  initiale : 0 pour le thread principal et `THREAD_CREATE`, celle donnée à `THREAD_CREATE_EX`.
* **Priorités** (`THREAD_PRIO_*` de `syscalls.inc`) : 0 normale, 1 haute, 2 temps réel souple
  (programmes de `SD0:/sys` seulement). À chaque passage de l'ordonnanceur, chaque thread prêt
  gagne un point ; on prend la plus grande somme (avance de la priorité : 0, 4 ou 16) + points, et
  le thread choisi repart de 0 : un thread prioritaire passe devant, mais un thread normal prêt
  finit toujours par passer (pas de famine ; mesuré par `threadtest` : sur un cœur partagé, un
  thread haut a environ 2 fois le temps d'un normal). Le service `sound` (thread de mélange) et
  ses pilotes (`pwmaudio`, `hdmiaudio`, `i2smic`, `usbaudio`) sont en priorité haute.
* **Verrous** : `FUTEX_WAIT` / `FUTEX_WAKE` sur un mot de 32 bits ; AppKit en fait des mutex et
  des variables de condition (`mtx_*`, `cnd_*` : [KITS.md](KITS.md)).
* Essai : `SD0:/bin/threadtest` (`src/bin/threadtest`).

| `0x2B` | `SIG_ACTION` | signal, gestionnaire (0 : défaut, 1 : ignoré), `SA_*`, masque, retour | 0 ; retour : l'adresse qui fait `SIG_RETURN`, gardée pour le process. `SIGKILL`, `SIGSTOP` : `E_INVAL` |
| `0x2C` | `SIG_SEND` | pid (0 : soi), tid (0 : un thread qui ne le masque pas), signal | 0 ; signal 0 : existe-t-il ? ; `SIGKILL` : le process finit ; `ESRCH` (-3) |
| `0x2D` | `SIG_RETURN` | — | ne rend rien : reprend x0–x30, sp, pc, NZCV, masque et FP/SIMD de l'ucontext de la trame, éventuellement changés par le gestionnaire |
| `0x2E` | `SIG_MASK` | `SIGM_*`, ensemble | `SIGM_SET` 0, `SIGM_BLOCK` 1, `SIGM_UNBLOCK` 2 : ancien masque **du thread** ; `SIGM_PENDING` 3 : signaux en attente ; `SIGM_SUSPEND` 4 : ce masque le temps de recevoir un signal, `E_INTR` ; `SIGM_WAIT` 5 : un signal de l'ensemble, retiré de l'attente |
| `0x2F` | `SIG_ALTSTACK` | sp (-1 : lire), taille (0 : aucune), sortie 16 o (sp, taille) ou 0 | 0 ; la pile de signaux **du thread** |

**Signaux** (`src/kernel/signal.asm`, 30/10/2026). Numéros de newlib, 1 à 63. Livrés au retour en
EL0 : le gestionnaire reçoit (signal, siginfo, ucontext) sur sa pile, ou sur celle de `SIG_ALTSTACK`
avec `SA_ONSTACK` ; la trame a la forme de celle de Linux arm64 (`SGF_*`, `UCX_*` dans
`syscalls.inc`, `<sys/ucontext.h>`), FP/SIMD compris (`fpsimd_context`). Le signal livré est
masqué le temps du gestionnaire (sauf `SA_NODEFER`), avec le masque donné à `SIG_ACTION` ;
`SA_RESETHAND` rend l'action par défaut. Défaut : URG, CONT, CHLD et WINCH ignorés, les autres
finissent le process avec le code -signal.

Une **faute** d'EL0 va au gestionnaire s'il y en a un et que le signal n'est pas masqué : `SIGSEGV`
(`si_code` 1 non mappé, 2 droits), `SIGBUS` (alignement), `SIGILL`, `SIGTRAP` (`brk`), `SIGFPE`,
avec `si_addr` (`SIGILL`, `SIGTRAP` : le pc) ; sinon le process est tué, comme avant. Pile
illisible en écrivant la trame : tué.

Un thread endormi ou bloqué est réveillé s'il ne masque pas le signal : `SLEEP`, `CHAN_*` et
`HANDLE_WAIT` rendent `E_INTR`, `EV_WAIT` 0, un futex 0. `SVC_CALL` et `THREAD_JOIN` ne sont pas
interrompus : le signal arrive à leur retour. Le faucheur envoie `SIGCHLD` au père si celui-ci a un
gestionnaire, code de sortie déjà rangé. Un thread neuf hérite du masque de son créateur. Essais : `SD0:/bin/sigtest`, `posixtest signal`.

### 0x30 — IPC : services nommés

| N° | Nom (`SYS_*`) | Arguments | Résultat |
|---|---|---|---|
| `0x30` | `SVC_REGISTER` | nom, drapeaux (1 `SVF_FINS` : prévenu des fins de process : avis 0xFFFF, a = pid, b = code de sortie ; 4 `SVF_MEMORY` : il garde de la mémoire qu'il peut rendre, le noyau lui lève `SVC_SIG_LOWMEM` quand elle manque, une moitié rendue par signal ; 2 `SVF_NAISSANCES` : des créations : avis 0xFFFE, a = fils, b = père (0 : le noyau), posté avant le premier thread du fils) | n° de service ; programmes de `SD0:/sys` seulement |
| `0x31` | `SVC_OPEN` | nom, attente ms | n° de service (canal) ; `E_NOENT` si absent au bout du délai |
| `0x32` | `SVC_CALL` | canal, message 512 o | résultat (réponse dans le message) ; `E_PIPE` si le service s'arrête |
| `0x33` | `SVC_NEXT` | canal, message, attente ms | n° de requête (le service ; pid de l'appelant en +44) |
| `0x34` | `SVC_REPLY` | n° de requête, message | 0 (le service) |
| `0x35` | `SVC_SIGNAL` | canal, bits | 0 : OU dans le mot de signaux du service, sans bloquer ni allouer, sans débordement ; le service n'est réveillé que si le mot était à 0. Son `SVC_NEXT` rend l'op `SVC_OP_SIGNAL` (0xFFFD, a = bits, remis à 0), sans réponse, avant les requêtes en file |
| `0x36` | `SVC_MAP_CALLER` | requête prise, adresse, taille (1 Mo au plus), `MC_WRITE` | adresse dans le service : mémoire de l'appelant, pages amenées et épinglées (`src/kernel/mapcaller.asm`) ; `E_FAULT` (lecture seule, pas de page), `E_PIPE` (appelant parti), `E_BUSY` (16 mappages au plus) ; programmes de `SD0:/sys` |
| `0x37` | `SVC_UNMAP_CALLER` | adresse rendue | 0 ; fait aussi par `SVC_REPLY` de la requête et à la fin du service |
| `0x38` | `SVC_CALLER_PHYS` | adresse rendue, tableau, places | pages ; adresses physiques, pour le DMA |

### 0x40 — matériel et démarrage (programmes de `SD0:/sys`)

| N° | Nom (`SYS_*`) | Arguments | Résultat |
|---|---|---|---|
| `0x40` | `DEV_MAP` | adresse physique, taille | adresse des registres mappés (zones de périphériques de la carte seulement) |
| `0x41` | `FW_CALL` | message « property tags » | 0 (réponse dans le message) |
| `0x42` | `MOD_LANCER` | nom, arguments | pid d'un module de initramfs |
| `0x43` | `MOD_LIRE` | nom, tampon, max | octets d'un module texte |
| `0x44` | `DEMARRAGE_PRET` | – | 0 : init lève le verrou du démarrage |
| `0x45` | `BOOT_FILE` | nom, image, taille | 0 : un fichier donné au noyau (`SD0:/etc/options.txt`) ; nom = 0 : liste des noms attendus (chaînes terminées par 0, puis 0) → octets ; donner : droits de `SD0:/sys` (init) |
| `0x46` | `KIT_REGISTER` | nom, image, taille | adresse de la table : l'image (`SD0:/lib/<nom>.so`, lue par AppKit) est recopiée, chargée et mappée dans l'appelant ; un nom déjà inscrit : l'existant est mappé, rien n'est rechargé ; `EPERM` sans droits de `SD0:/sys` (AppKit passe alors par init2, `INIT_OP_KIT_LOAD`) |
| `0x47` | `IMAGE_PRELOAD` | chemin résolu, image, taille | (ancien chemin, gardé pour un AppKit d'avant les images : AppKit passe maintenant par `IMAGE_CREATE` / `IMAGE_SEAL`) 0 : l'exécutable est chargé une fois pour toutes (un modèle : espace d'adressage sans thread) et repris par `SPAWN_PRELOADED` ; une image du même chemin est remplacée ; image = 0 : 0 si le préchargement est actif, `E_NOSYS` avec l'option `no_preload` ; droits de `SD0:/sys` (init2, par `SYS_PRELOAD` d'AppKit) ; au journal : `Préchargé : chemin (N Ko : P pages partagées, dont W en copie sur écriture ; .bss B Ko à la demande)` |
| `0x48` | `IMAGE_FORGET` | chemin résolu | nombre d'images oubliées : le fichier change, ses images ne sont plus prises (les process déjà lancés gardent la leur) ; ouvert à tous (oublier une image ne fait que la relire au lancement suivant ; la commande `unload`) (vfs l'appelle à chaque ouverture en écriture, création, troncature ou ajout, et à chaque suppression) |

| `0x49` | `IRQ_BIND` | ligne du GIC (32–255), canal (-1 : `IRQ_WAIT`), bits (`IRQ_SHARED`, bit 63 : ligne partagée) | 0 ; canal : un service du process ; `E_BUSY` (ligne d'un autre, sauf si les deux ont demandé `IRQ_SHARED`), `E_PERM`, `E_NOSYS` (option `no_irq`) |
| `0x4A` | `IRQ_ACK` | ligne | 0 : ligne démasquée (mode service) |
| `0x4B` | `IRQ_WAIT` | ligne, attente ms | 1 levée (ligne masquée), 0 délai, `E_INTR` (signal) ; la ligne est démasquée à l'appel |
| `0x4C` | `IRQ_UNBIND` | ligne | 0 : masquée et rendue |

**Interruptions des pilotes** (`src/kernel/irq.asm`, 30/10/2026). Quand une ligne se lève, le noyau
la masque, puis lève les bits donnés dans le mot de signaux du service (vus par `SVC_NEXT`, rendus
par `IRQ_ACK`), ou réveille le thread dans `IRQ_WAIT`. Le pilote calme son matériel avant de
démasquer. Lignes rendues à la fin du process. Essai : `SD0:/sys/svc/irqtest`, sur le canal DMA 4.

| Pilote | Ligne | Attente |
|---|---|---|
| Elegant, DMA 2D | 112 + canal | `IRQ_WAIT` ; sans interruption : `SYS_YIELD` en boucle |
| `v3d` | 106 | 2 ms sondées, puis `IRQ_WAIT` de 4 ms, relu ; sans interruption : `SYS_YIELD` |
| `pwmaudio`, `hdmiaudio` | table de `sndif.inc` : 119 pour les canaux 7-8, 120 pour 9-10, 121 à 124 pour 11 à 14 | `IRQ_WAIT` de 40 ms par tampon après une première interruption ; 5 ms avant |
| `sd` | 158 | sonde la fin du DMA 8 ms (`SD_SPIN_US`), puis `IRQ_WAIT`, l'interruption armée seulement pour dormir ; `no_sd_dma`, `no_irq` : PIO |
| `gt911` | 145 (GPIO 0 à 27 ; INT sur le GPIO 4, front descendant) | au repos, `IRQ_WAIT` de 1 s puis une lecture ; doigt posé, lecture toutes les 16 ms ; 3 touchers sans interruption : sondage |
| `genet` | 189 (réception) | service `genet`, 50 ms après une première interruption ; 1 ms avant, et de nouveau si une trame arrive sans elle. `netd` lève `NIF_SIG_TX` quand il met une trame à émettre (`nif_wake`) |
| `wifi` | 158 (`IRQ_SHARED`, avec `sd`) | interruption SDIO (bit `CARD` de l'Arasan), `IRQ_WAIT` de 200 ms au repos, 1 ms si la puce a du travail ; sans interruption : sondage |

Ces lignes sont celles du Pi 4. **Une ligne de DMA partagée n'est tenue que par un pilote**, et lui seul
efface son canal : l'interruption d'un autre canal resterait levée et le réveillerait sans fin
(`hdmiaudio` à 84 % d'un cœur avec `pwmaudio` sur le canal voisin). Donc `sound` donne d'abord un canal
seul sur sa ligne, et un pilote ne met `TI_INTEN` dans ses blocs que s'il tient la sienne.

La ligne 158 est de l'autre sorte : EMMC2 (`sd`) et l'Arasan (`wifi`) la partagent, chacun avec
`IRQ_SHARED`. Le noyau la masque et réveille les deux, chacun regarde son matériel et la démasque à
son appel suivant, sans attendre l'autre : un réveil pour rien, sans plus. Le bit `CARD` reste levé tant que les lignes DAT sont hautes : il s'efface en coupant son activation
(`SD_IRPT_MASK`), et comme il monte sur un front, le pilote relit `CCCR_INTPEND` après l'avoir réarmé.

L'USB sonde encore : DWC2 doit rejouer lui-même ses transferts d'interruption, donc une IRQ n'y
retirerait pas le réveil de 2 ms de `usbd`.

### 0x50 — serveur graphique

| N° | Nom (`SYS_*`) | Arguments | Résultat |
|---|---|---|---|
| `0x50` | `WS_REGISTER` | – | 0 : l'appelant devient le serveur graphique |
| `0x51` | `WS_CALL` | message 512 o | résultat (réponse dans le message) |
| `0x52` | `WS_NEXT` | message, attente ms | n° de requête (serveur) |
| `0x53` | `WS_REPLY` | n° de requête, message | 0 (serveur) |
| `0x54` | `WS_POST` | pid, événement 32 o | 0 (serveur) |
| `0x55` | `WS_DISPLAY` | tampon 40 o {adresse, largeur, hauteur, pas, adresse physique (ARM) du framebuffer} | 0 (serveur) ; l'adresse physique sert au DMA 2D d'Elegant |
| `0x56` | `EV_WAIT` | événement 32 o, attente ms | type (0 : aucun) |
| `0x57` | `SCREEN_INFO` | tampon {largeur, hauteur, barre} | 0 |
| `0x58` | `CLIP_SET` | tampon, longueur | 0 |
| `0x59` | `CLIP_GET` | tampon, max | longueur |
| `0x5A` | `WS_SIGNAL` | bits | `SVC_SIGNAL` vers le serveur graphique (`E_NODEV` : aucun) ; bit `WS_SIG_DAMAGE` |
| `0x5B` | `WS_MODE` | largeur, hauteur, infos (40 o, comme `WS_DISPLAY`) | serveur seulement : nouvelle résolution, 640 x 480 à 2560 x 1600, largeur paire (`E_INVAL`) ; refusée par le firmware : la précédente est redemandée, infos remplies, `E_IO` |

### 0x60 — système

| N° | Nom (`SYS_*`) | Arguments | Résultat |
|---|---|---|---|
| `0x60` | `PRINT` | chaîne, longueur | 0 : console du noyau (avec AppKit : sortie standard, console ou tube) |
| `0x61` | `KLOG` | tampon, max | octets du journal du noyau (64 Ko au plus ; copie : min(n, max)) |
| `0x62` | `SYSINFO` | tampon 80 o | 0 (+0 cœurs, +8 Ko libres, +16 Ko total (RAM gérée : zones basse et haute), +24 ms, +32 process, +40 charges des 4 cœurs, +72 commutations) |
| `0x63` | `GETRANDOM` | tampon, octets (≤ 65 536) | octets aléatoires : ChaCha20 dont la clé est remêlée à chaque appel par l'aléa que voit le noyau (gigue du compteur, instant de chaque interruption, commutations, RNG200 sauf avec l'option `no_rng200`) ; `src/kernel/random.asm` |
| `0x64` | `LIB_OPEN` (= kit_find) | nom, version minimale | adresse de la table du kit, mappé dans l'appelant ; `ENOENT` s'il n'est pas en mémoire (sauf `appkit` : module d'initramfs) : `kits_open` le fait alors lire par AppKit (`f_kit_load`) puis rappelle `LIB_OPEN` |
| `0x65` | `IMAGE_LIST` | tampon, max (≤ 128) | nombre d'images valides, les épinglées d'abord ; entrées de 128 o (`IML_*`) : +0 taille du fichier, +8 pages chargées, +16 dont inscriptibles (copie sur écriture), +24 références (1 pour la table + 1 par process lancé d'elle), +32 octets de `.bss` à la demande, +40 sorte (`IML_PIN` 1 : épinglée, `IML_SYS` 8 : construite avec les droits de `SD0:/sys`), +48 chemin (80 o) |
| `0x66` | `IMAGE_CREATE` | chemin résolu, taille du fichier (64 o à 4 Go), drapeaux (`IMG_PIN` 1 : épinglée) | adresse de la **fenêtre de chargement** : les pages physiques du fichier (à zéro) mappées en écriture chez l'appelant à `0x6C_0000_0000 + n × 4 Go` (4 chargements à la fois par process) ; l'appelant y lit le fichier puis appelle `IMAGE_SEAL` ; `E_NOSYS` (option `no_preload`), `E_NOSPC` (table de 128 images pleine, ou 4 chargements en cours), `E_NOMEM` ; par AppKit (`fc_image_load`) |
| `0x67` | `IMAGE_SEAL` | fenêtre, drapeaux (`IMS_ABORT` 1 : lecture échouée, tout est rendu) | 0 : fenêtre retirée de l'appelant, en-têtes ELF vérifiés, image mise en place (ci-dessous) ; `E_INVAL` : pas de chargement à cette fenêtre, ou exécutable refusé (image rendue) |
| `0x68` | `MEMBARRIER` | commande, drapeaux | `MB_QUERY` 0 : commandes connues ; `MB_PRIVATE` 8 et `MB_PRIVATE_SYNC` 32 : tout cœur qui fait tourner ce process passe une barrière, par une interruption attendue (10 ms au plus) ; `MB_REGISTER` 16, `MB_REGISTER_SYNC` 64 : 0, rien à faire ; mêmes valeurs que Linux |

**Images des exécutables** (`src/kernel/preload.asm`, 29/10/2026 ; comme les « program images »
d'Onyx). **Tout lancement passe par une image** : un modèle (espace d'adressage qui ne tourne
jamais) dont les process lancés **mappent directement les pages** — code et lecture seule partagés
tels quels (`PTE_SHARED`, jamais copiés ni rendus par eux), segment lecture / écriture en lecture
seule + `PTE_COW` (la première écriture copie la page : rupture avant construction, `TLBI VAE1IS`
de l'ASID), `.bss` en région paresseuse. **Clé** : le chemin résolu par vfs (« `SD0:/bin/jsc` »,
comparé aux majuscules près) et la taille du fichier : un second lancement du même programme ne
relit pas la carte.

*Construction, sans tampon ni copie* (le noyau ne lit toujours aucun fichier) : `spawn` d'AppKit
(`f_spawn_file`, après `V_EXEC`) essaie `SPAWN_PRELOADED` ; `E_NOENT` → `fc_image_load` :
`IMAGE_CREATE` (le noyau prend les pages du fichier, les mappe dans le modèle — zone de travail —
et chez l'appelant, à la fenêtre), lecture du fichier **directement dans ces pages** (vfs, fatfs),
`IMAGE_SEAL` (fenêtre retirée, en-têtes vérifiés : la table des segments doit tenir dans la
première page ; chaque page d'un segment est **la page du fichier elle-même** quand elle a le même
décalage dans la page, est entière dans la partie lue et n'est pas déjà prise, sinon une page à soi
où la partie lue est recopiée ; les pages du fichier hors segment — en-têtes de sections, symboles
— sont rendues), puis `SPAWN_PRELOADED` à nouveau. Pas de mémoire en double pendant le chargement,
pas de limite de taille autre que la mémoire (4 Go par fichier : la fenêtre). Un chargeur qui
meurt en route : ses chargements sont rendus (`vm_destroy`). Un lancement concurrent du même
programme qui finit second rend son image et prend la première.

*Deux sortes.* **Épinglée** (`IMG_PIN` : `SD0:/etc/preload.txt` lu par init2 au démarrage —
`init2 preload`, un process à part —, la commande `preload <chemin>`) : gardée tant que le fichier
ne change pas. **Passagère** (tous les autres lancements) : rendue quand son dernier process finit
(*à faire* : la garder après la fin, et l'éviction sous pression mémoire, comme l'étape d d'Onyx).
Comptage : une référence pour la table, une par process lancé d'elle. **Droits** : une image
construite par un process qui a les droits de `SD0:/sys` est marquée `IML_SYS` ; un lancement avec
droits n'accepte qu'une telle image (un process sans droits ne peut pas fabriquer l'image d'un
service). **Fichier changé** : vfs appelle `IMAGE_FORGET` à toute ouverture en écriture et à toute
suppression du chemin ; la taille doit aussi correspondre ; `unload <chemin>` en fait autant.
Limites : un fichier modifié sans passer par vfs n'est pas vu ; un nom court 8.3 n'est pas reconnu
comme le nom long ; 128 images (au-delà, AppKit retombe sur l'ancien chemin). **Option
`no_preload`** : `IMAGE_CREATE` / `IMAGE_PRELOAD` rendent `E_NOSYS`, AppKit lit le fichier dans un
tampon partagé (16 Mo au plus) et passe par `SPAWN_IMAGE`, comme avant les images.

*Journal*, une ligne par lancement : `image SD0:/bin/jsc : chargée en N ms` (premier lancement
d'une image neuve, depuis `IMAGE_CREATE`) ou `partagée en N ms` ; et `Préchargé : …` pour une
image épinglée. *Commandes* : `preload` (liste : sorte, droits, Ko, pages, process, chemin),
`preload <chemin>...`, `unload <chemin>...`.

*Mesures sous QEMU* : `memtest bench` (`SD0:/bin/fasmarm`, 454 Ko) : 21 ms par lancement depuis
l'image contre 454 ms lu sur la carte ; `SD0:/bin/jsc` (30,8 Mo, 7688 pages, dont 6 en copie sur
écriture) : chargé en 12 à 28 s (lecture de la carte émulée), puis **partagé en 0 à 1 ms**. Essais :
`SD0:/bin/memtest` (40 PASS), `preload`, `unload`.

### 0x70 — IPC entre process : poignées, canaux locaux, mémoire partagée

`src/kernel/ipc.asm` (30/10/2026) : ce qu'il faut à WebKit2 (`socketpair (AF_UNIX,
SOCK_SEQPACKET)`, `SCM_RIGHTS`, `memfd_create` / `shm_open` + `mmap (MAP_SHARED)`, l'héritage par
`posix_spawn`), servi par la couche POSIX ([POSIX.md](POSIX.md), « IPC entre process »). Une
**poignée** (1..1023) est une entrée de la table du process (4 pages, créée au premier besoin) qui
tient une référence sur un objet du noyau : un **bout de canal** (`IPC_KIND_CHAN`) ou un **objet
de mémoire partagée** (`IPC_KIND_SHM`). Constantes : `IPC_*`, `IPCM_*`, `IPCI_*`, `IPCS_*` de
`syscalls.inc`.

| N° | Nom (`SYS_*`) | Arguments | Résultat |
|---|---|---|---|
| `0x70` | `HANDLE_CLOSE` | poignée | 0 ; `E_BADF` |
| `0x71` | `HANDLE_INFO` | poignée, sortie 64 o | 0 : `IPCI_KIND`, `IPCI_TYPE` (canal : `IPC_SOCK_*`), `IPCI_SIZE` (canal : octets en attente ; mémoire : taille), `IPCI_STATE` (canal : `IPCS_PEER_CLOSED` 1, `IPCS_SHUT_RD` 2, `IPCS_SHUT_WR` 4, `IPCS_PEER_SHUT_WR` 8 ; mémoire : sceaux `IPC_SEAL_*`), `IPCI_FLAGS` (mémoire : `IPC_SHM_SEALABLE`, `IPC_SHM_PINNED` 2), `IPCI_PID` (canal : pid du créateur de la paire, `SO_PEERCRED` ; mémoire : numéro de l'objet), `IPCI_CAP` (canal : 65 536, l'anneau) |
| `0x72` | `HANDLE_WAIT` | entrées de 8 o {poignée 32 bits, événements 16, prêts 16}, n ≤ 64, attente ms (< 0 : sans fin, 0 : regarder) | nombre d'entrées prêtes (`IPC_POLLIN` 1, `IPC_POLLOUT` 4, `IPC_POLLERR` 8, `IPC_POLLHUP` 16, `IPC_POLLNVAL` 32 : les valeurs de `poll`) ; 0 : délai ; `E_INTR` (thread tué). **Attente dans le noyau**, réveillée par tout changement d'un des canaux ; un objet partagé est toujours prêt |
| `0x73` | `HANDLE_LIST` | sortie (entrées de 16 o {poignée, marque, sorte \| type << 8, 0}), max, 1 : marques remises à 0 | nombre ; la couche POSIX le lit au démarrage : les poignées données par le père (marque = numéro de descripteur + 1) |
| `0x74` | `HANDLE_INHERIT` | entrées de 8 o {poignée, marque}, n ≤ 255 | 0 : les objets sont pris tout de suite ; le **prochain process lancé** par ce process (`SPAWN_IMAGE`, `SPAWN_PRELOADED`) les reçoit dans sa table avec leurs marques ; n = 0 : la liste est rendue (lancement échoué) ; `E_BADF` |
| `0x75` | `CHAN_CREATE` | `IPC_SOCK_STREAM` 1 / `IPC_SOCK_DGRAM` 2 / `IPC_SOCK_SEQPACKET` 5, sortie 2 × 32 bits | 0 : deux poignées, les deux bouts d'un canal ; `-93` (`EPROTONOSUPPORT`), `E_MFILE`, `E_NOMEM` |
| `0x76` | `CHAN_SEND` | poignée, message (`IPCM_*`, 32 o), `IPC_NOWAIT` 1, attente ms (0 : sans fin) | octets envoyés : les iovecs (≤ 64) rassemblées **en un message** dans l'anneau du pair, avec les poignées (≤ 256, prises à l'appelant : une référence de plus) ; paquet (SEQPACKET, DGRAM) entier ou rien, `-90` (`EMSGSIZE`) au-delà de l'anneau ; STREAM par morceaux (sans attente : ce qui tient) ; `E_AGAIN` (plein et `IPC_NOWAIT`, ou délai), `-32` (`EPIPE` : pair fermé ou `SHUT_RD`, soi `SHUT_WR` ; pas de signal), `E_BADF` (une poignée portée invalide : rien n'est envoyé), `E_INVAL` (le bout receveur par son propre canal), `-105` (`ENOBUFS` : 1 024 anneaux dans le système) |
| `0x77` | `CHAN_RECV` | poignée, message, `IPC_NOWAIT` \| `IPC_PEEK` 2, attente ms | octets ; 0 : la fin (pair fermé ou `SHUT_WR`, soi `SHUT_RD`, anneau vide). Paquet : un message, coupé à la place des iovecs (`IPC_TRUNC` dans `IPCM_FLAGS`) ; STREAM : plusieurs messages, sans passer un message qui porte des poignées (elles viennent avec son premier octet). Poignées reçues : ajoutées à la table, écrites dans `IPCM_HANDLES` {poignée, sorte \| type << 8} (`IPCM_NH` : place en entrée, nombre en sortie) ; sans place ou table pleine : fermées, `IPC_CTRUNC`. `IPC_PEEK` : les octets sans les retirer, ni poignées |
| `0x78` | `CHAN_SHUTDOWN` | poignée, `IPC_SHUT_RD` 1 \| `IPC_SHUT_WR` 2 | 0 ; tout ce qui attend sur le canal est réveillé (WebKit : la fin d'une connexion réveille son thread de surveillance) |
| `0x79` | `SHM_CREATE` | taille, `IPC_SHM_SEALABLE` 1 | poignée d'un objet anonyme (`memfd_create`) |
| `0x7A` | `SHM_OPEN` | nom (≤ 55 o, global), `IPC_SHM_CREAT` 1 \| `EXCL` 2 \| `TRUNC` 4 \| `WEAK` 8 | poignée ; `E_NOENT`, `E_EXIST`, `-36` (`ENAMETOOLONG`), `-23` (`ENFILE` : 256 noms). `IPC_SHM_WEAK`, à la création : le nom ne tient pas l'objet, qui part avec sa dernière référence, son nom avec lui |
| `0x7B` | `SHM_UNLINK` | nom | 0 : le nom part, l'objet vit tant qu'il a une référence ; un nom faible emporte l'épingle de son objet ; `E_NOENT` |
| `0x7C` | `SHM_CTL` | poignée, `IPC_SHM_SET_SIZE` 1 (`ftruncate`) / `IPC_SHM_ADD_SEALS` 2 (`F_ADD_SEALS`) / `IPC_SHM_PIN` 3 / `IPC_SHM_UNPIN` 4, valeur | 0 ; `PIN` : un objet à nom faible reste sans process, une seule épingle par objet ; `E_INVAL` (plus de 1 008 Mo, `PIN` ou `UNPIN` sans nom faible), `E_PERM` (sceau `GROW` / `SHRINK`, objet non scellable, déjà `SEAL`), `-16` (`EBUSY` : rétrécir un objet projeté). Rétrécir rend les pages au-delà et met à zéro la fin de la dernière |
| `0x7D` | `SHM_MAP` | poignée, adresse, taille, droits `PROT_*`, drapeaux (`VM_FIXED`, `VM_FIXED_NOREPLACE`), décalage (aligné sur la page) | adresse : une région **`VMK_SHARED`** dans l'arène de `VM_MAP`, peuplée à la demande avec **les pages de l'objet** (les mêmes dans chaque process) ; une page au-delà de la taille de l'objet fait une faute (le process est tué, comme `SIGBUS`) ; `E_PERM` (sceau `WRITE` et `PROT_WRITE`) ; `VM_COW` (0x200000) dans les drapeaux : les pages de l'objet en lecture, une écriture prend une copie privée. `VM_UNMAP`, `VM_PROTECT`, `VM_ADVISE` (`DONTNEED` : la projection part, les données restent), `VM_QUERY` y marchent |

*Objets.* Un **canal** = une page : son verrou, son type, deux bouts. Un bout a sa référence, son
pair, son état (fermé, `SHUT_RD`, `SHUT_WR`) et son **anneau de réception** (64 Ko, alloué au
premier message ; 1 024 dans le système) : un message y est un enregistrement contigu (en-tête :
octets, poignées, octets déjà lus, poignées remises ; les objets portés ; les données), un saut en
fin d'anneau quand il ne tient pas avant. Un bout sans référence est **fermé** : le pair voit la
fin, les messages jamais lus sont rendus avec ce qu'ils portaient (hors de tout verrou :
`ipc_dead`, `ipc_reap`). Un **objet partagé** = une page (en-tête, puis 504 pointeurs vers des
pages de 512 adresses de pages : 1 008 Mo au plus), ses pages créées à zéro au premier accès,
**jamais à un process** (`PTE_SHARED` : ni un zap ni la fin d'un process ne les rendent), rendues à
la dernière référence. Références : les poignées, les poignées en vol dans un message, la liste de
lancement, chaque région qui le projette (une coupe de région en ajoute une), le nom.

*Attente.* `HANDLE_WAIT`, un `CHAN_SEND` plein et un `CHAN_RECV` vide dorment dans le noyau
(`ipc_sleep` : le thread inscrit dans une table de 64 entrées avec les canaux qu'il attend, état
`TS_BLOCKED`, échéance) ; tout changement d'un canal (message posé ou lu, fermeture, `SHUTDOWN`)
réveille ceux qui l'attendent. Pas de réveil perdu sans tenir les verrous de tous les canaux :
`ipc_seq` avance à chaque réveil, l'appelant le lit avant de regarder l'état, `ipc_sleep` ne dort
pas s'il a changé. Le réveil passe par `P_LOCK` (comme `futex_wake`) : un process en fin de vie
n'est jamais touché. Un `send` / `recv` qui dort **rend son bout** et le reprend par sa poignée au
réveil : un thread tué en dormant ne garde rien.

*Fin d'un process* (`vm_destroy`) : ses poignées fermées, sa liste de lancement rendue, ses entrées
d'attente effacées, une référence de moins par région partagée. *Lancement* (`proc_spawn_elf`) :
la liste du père (`HANDLE_INHERIT`) passe dans la table du fils avant son premier thread.

*Verrous* (tous en `irqsave`) : canal → table de poignées (feuille) ; canal → `ipcw_lock` →
`P_LOCK` ; `P_VMLOCK` → objet partagé ; `P_VMLOCK` ou canal → noms → pmm. *Limites* : 1 023 poignées par process, 64
iovecs et 256 poignées par message, 64 poignées par attente, 64 threads qui attendent à la fois
(au-delà : attente de 2 ms puis nouveau regard), 1 024 anneaux de 64 Ko, 32 noms ; les cycles de
bouts en vol (un bout envoyé dans un canal dont il tient l'autre bout) ne sont pas ramassés —
envoyer le bout receveur par son propre canal est refusé (`E_INVAL`). Un process tué pendant qu'un
de ses threads est *dans* un appel d'IPC sans dormir (quelques microsecondes) peut y laisser une
référence : comme les autres appels du noyau, la mort coupe le thread où il est.


### 0x80 — fichiers et lancement (AppKit, pas le noyau)

| N° | Nom (`SYS_*`) | Arguments | Résultat |
|---|---|---|---|
| `0x80` | `OPEN` | chemin, drapeaux (1 R, 2 W, 4 CREATE, 8 TRUNC, 16 APPEND, 32 NONBLOCK) | fd |
| `0x81` | `READ` | fd, tampon, n | octets (tube : EAGAIN si vide et `O_NONBLOCK`, 0 = fin) |
| `0x82` | `WRITE` | fd, tampon, n | octets |
| `0x83` | `SEEK` | fd, position, origine 0/1/2 | position |
| `0x84` | `CLOSE` | fd | 0 |
| `0x85` | `STAT` | chemin, tampon {taille, attributs} | 0 |
| `0x86` | `READDIR` | fd (dossier), tampon {nom[256], taille, attributs} | 1 / 0 en fin |
| `0x87` | `MKDIR` | chemin | 0 |
| `0x88` | `UNLINK` | chemin (fichier ou dossier vide) | 0 |
| `0x89` | `CHDIR` | chemin | 0 |
| `0x8A` | `GETCWD` | tampon, 256 | 0 |
| `0x8B` | `SPAWN` | chemin, arguments | pid (hérite du dossier courant et de la sortie) |
| `0x8C` | `SPAWN_PIPE` | chemin, arguments, &fd | pid ; sortie du fils lisible sur fd (`E_AGAIN` si vide, 0 à la fin) ; **fd = −1** : programme **détaché** (de `SD0:/sys`, lancé par init2 pour un process sans droits : sortie au journal du noyau, ne pas l'attendre) |
| `0x8D` | `PRELOAD` | chemin (0 : question) | 0 : image **épinglée** du programme (`V_EXEC`, `IMAGE_CREATE` avec `IMG_PIN`, lecture, `IMAGE_SEAL`) ; chemin = 0 : 0 si le noyau a les images, `E_NOSYS` sinon (init2 preload ; la question demande les droits de `SD0:/sys`) |
| `0x8E` | `UNLOAD` | chemin | nombre d'images oubliées (`V_EXEC` pour le chemin résolu, puis `IMAGE_FORGET`) ; la commande `unload` |

### Anciens numéros (avant le chantier 6 bis)

| Ancien | Nouveau | | Ancien | Nouveau | | Ancien | Nouveau |
|---|---|---|---|---|---|---|---|
| 0 exit | `0x10` | | 24 wait | `0x13` | | 82–86 svc_* | `0x30`–`0x34` |
| 1 print | `0x60` | | 25 proclist | `0x15` | | 87 dev_map | `0x40` |
| 2 sleep | `0x17` | | 27 kill | `0x14` | | 88 dma_alloc | `0x05` |
| 3 yield | `0x18` | | 53 klog | `0x61` | | 89 fw_call | `0x41` |
| 4 time_ms | `0x19` | | 54 lib_open | `0x64` | | 94 mod_lancer | `0x42` |
| 5 thread_create | `0x20` | | 66 screen_info | `0x57` | | 95 mod_lire | `0x43` |
| 6 thread_exit | `0x21` | | 67–68 clip_* | `0x58`–`0x59` | | 96 demarrage_pret | `0x44` |
| 7 sbrk | `0x00` | | 70–73 buf_* | `0x01`–`0x04` | | 97 spawn_image | `0x12` |
| 8 getpid | `0x11` | | 74–78 ws_* | `0x50`–`0x54` | | 98 proc_flags | `0x16` |
| 9–10 futex_* | `0x22`–`0x23` | | 80 ws_display | `0x55` | | 99 kit_register | `0x46` |
| 11 sysinfo | `0x62` | | 81 ev_wait | `0x56` | | 100 boot_file | `0x45` |
| 12–23, 26 (AppKit) | `0x80`–`0x8C` | | | | | 101 getrandom | `0x63` |

Retirés sans successeur : 28–35 (son → AudioKit, service `sound`), 36–52 (Wi-Fi, sockets → NetKit,
`netd`, `wifi`), 60–65 (fenêtres → Elegant, UIKit), 69 (disposition → HIDManager), 79 (entrées →
HIDManager), 90–93 (trames et blocs → anneaux de netd, `usbstorage`).

Requêtes aux services : 128 en cours dans tout le système (`svc_call` rend `E_BUSY` au-delà ;
les avis de fin de process comptent).

**Services de fichiers en direct** (`fs.<volume>` : `fs.ram0`, `fs.img0`... ; protocole de
`src/inc/fs.inc`, [FICHIERS.md](FICHIERS.md) section 4.2) : un programme peut les appeler par
`svc_open` + `svc_call` ; un fichier ouvert appartient à son pid (`E_BADF` pour un autre) et le
service le ferme à la fin du process. Gros transferts : op 14 `FS_TAMPON` → a = numéro d'un
tampon partagé de 64 Ko accordé à l'appelant (`buf_map`), puis 15 `FS_LIRE_T` / 16
`FS_ECRIRE_T` (a numéro, b position, c octets ≤ 64 Ko, au début du tampon). C'est ce que fait
le client d'AppKit (appels `0x80`–`0x8C` d'un programme). Le noyau n'ouvre plus aucun fichier (étape 5) :
un fichier n'est accessible qu'à son propriétaire (et aux avis du noyau, pid 0).

**Chemins par vfs** (`svc_open("vfs")` + `svc_call` ; [FICHIERS.md](FICHIERS.md) section 4.1,
`src/svc/vfs/path.inc`) : vfs garde un dossier courant par pid (hérité du père par l'avis de
naissance, oublié à la fin) ; données = chemin tel que le programme l'écrit, résolu pour
l'appelant (`MSG_PID`).

| Op | Requête | Réponse |
|---|---|---|
| 30 `V_OUVRIR` | a = drapeaux O_* ; chemin | a canal du service FS, b numéro (appartient à l'appelant : lire, écrire, fermer directement chez ce service), c attributs (+ `ATTR_SYS` = 0x100 sous `SD0:/sys/`), d taille |
| 31 `V_STAT` | chemin | b taille, c attributs, d date (secondes UTC, 0 : aucune) |
| 44 `V_FILE_END` | a mode (1 lecture, 2 écriture) ; chemin | a poignée d'un bout-fichier ([FICHIERS.md](FICHIERS.md), redirections) |
| 45 `V_END_FILE` | a poignée | 1 : un fichier (données : chemin, b mode), 0 : un tube |
| 42 `V_UTIME` | b secondes UTC ; chemin | 0 ou erreur |
| 43 `V_RENAME` | données : ancien, 0, nouveau, 0 | 0 ou erreur ; `E_XDEV` : deux volumes |
| 32 `V_MKDIR`, 33 `V_UNLINK` | chemin | 0 ou erreur |
| 34 `V_CHDIR` | chemin (un répertoire) | 0, `E_NOTDIR`, `E_NOENT` |
| 35 `V_GETCWD` | – | données : « `SD0:/a` » |
| 36 `V_EXEC` | nom ou chemin (nom seul : `SD0:/bin/<nom>` puis `SD0:/apps/<nom>.app/main` ; « `.app` » → `/main`) | comme 30, ouvert en lecture (`E_ISDIR` pour un répertoire) ; données : nom du process (« `paint` » pour `paint.app/main`), 0, chemin résolu (« `SD0:/apps/paint.app/main` »), 0 |
| 37 `V_PIPE` | – | a bout de lecture, b bout d'écriture (poignées de l'appelant) |
| 38 `V_PIPE_READ` | a poignée, b octets (≤ 464), c bit 0 : sans attente | octets (données), 0 en fin, `E_AGAIN` ; sans le bit 0, la réponse attend des données (10 s au plus) |
| 39 `V_PIPE_WRITE` | a poignée ; données | octets acceptés (0 : plein), `E_PIPE` sans lecteur |
| 40 `V_PIPE_CLOSE` | a poignée | 0 |
| 41 `V_STDIO` | – | a, b, c : poignées de l'entrée, la sortie, l'erreur standard de l'appelant (0 : console) ; d : bits 0-2, celles qui sont des fichiers |
| 23 `VFS_PATHMAP` | données : chemin virtuel (`/gcc`), 0, chemin physique (`SD0:/gcc`), 0 | 0, `E_INVAL`, `E_NOSPC` (16 entrées) ; un chemin virtuel connu est remplacé ([FICHIERS.md](FICHIERS.md), « Chemins virtuels ») |
| 24 `VFS_PATHQ` | données : chemin | 1 s'il passe par un chemin virtuel, sinon 0 |

Pour ces ops, un chemin qui commence par `/` désigne la **racine du volume courant** (celui du
dossier courant, comme `\` sous DOS) : dossier `SD1:/docs`, « `/musique` » → `SD1:/musique`,
« `/` » → `SD1:/`. Les programmes y passent par AppKit (appels `0x80`–`0x8C`). Ops 1–4 (l'ancien relais
du noyau) : retirées (`E_INVAL`). Une ouverture (30, 36) d'un fichier sous `SD0:/sys/` porte
`ATTR_SYS` (0x100) dans c, sous `SD0:/lib/` `ATTR_LIB` (0x200 : un kit).

Chemins : des **volumes** — `SD0:/...` (1re partition de la carte, FAT32), `SD1:/...` (2e,
exFAT), `USB0:/...`, `USB1:/...` (cle USB, une par partition) ; `SD:` et `USB:` designent le
volume 0 ; majuscules ou minuscules. Pas de racine commune : un chemin qui commence par `/`
désigne la racine du **volume courant** (dossier `SD1:/docs` : « `/musique` » → `SD1:/musique`) ;
résolu par vfs (ops 30–36), donc pour tout programme qui passe par AppKit
([FICHIERS.md](FICHIERS.md)). Chemins relatifs au dossier courant (`getcwd` le rend sous cette forme) ; noms longs
UTF-8. Plus tard : dossiers FTP montes en `FTPx:` (espace utilisateur).

* `SD0:/apps/<nom>.app/` : une application — `main` (l'exécutable), `app.txt` (le manifeste,
  clés d'Onyx : `name` le nom affiché, `category` la catégorie ; plus tard ses capacités, pour
  l'agent IA), et sa configuration interne s'il en a une. Le noyau nomme le process d'après le
  paquet (`paint.app/main` → « paint »).
* `SD0:/bin/<nom>` : programmes en ligne de commande (trouvés par le terminal, comme
  `SD0:/apps/<nom>.app`).
* `SD0:/sys/<nom>` : le système (Elegant, la session du bureau) ; `SD0:/lib` : les kits.
* `SD0:/sys/svc/<nom>` : les services (`src/svc`, docs/NOYAU-HYBRIDE.md) ; les programmes de
  `SD0:/sys` ont seuls le droit d'enregistrer un service (et, plus tard, d'accéder au matériel).
* Démarrage : `init` (initramfs) lance `sd` et `vfs`, monte `SD0:`, donne au noyau les fichiers
  qu'il attend (`boot_file` : `options.txt`), puis lance `SD0:/sys/svc/init2`,
  qui lance `hwmanager`, Elegant, `SD0:/etc/services.txt` (services relancés s'ils
  s'arrêtent sur une erreur : code de sortie non nul), puis, quand Elegant répond,
  `SD0:/etc/startup.txt` ;
  `SD0:/etc/mounts.txt` : volumes servis en espace utilisateur (`RAM0 = ram:16M, ramfs`) :
  les programmes les atteignent par AppKit et vfs (même ABI pour les programmes).
* Service `init2` (`src/inc/armpi.inc`) : op 1 `INIT_OP_LAUNCH` (données : chemin
  `SD0:/sys/...`, 0, arguments, 0 → pid : un programme de `SD0:/sys` lancé pour un process sans
  droits) ; op 2 `INIT_OP_KIT_LOAD` (données : nom du kit → 0 : `SD0:/lib/<nom>.so` lu et
  inscrit par init2, le demandeur fait ensuite `lib_open`). Chaque demande est notée au journal.
* `SD0:/etc/drivers.txt` : table de `hwmanager`, « détection = pilote » (`i2c1:5d =
  SD0:/sys/svc/gt911`) ; un pilote de matériel non détectable va dans `services.txt`.
* `SD0:/etc` : **toute la configuration** (`system.ini`, `session`, `theme.txt`, `dock.ini`,
  `options.txt`, `startup.txt`, `wifi.txt`, `clock.cfg`) ; `SD0:/fonts`, `SD0:/res/keyb`.
  `options.txt` : `lang=fr` ou `en` (langue choisie dans Réglages → Région : chaque application
  la lit et se traduit elle-même, au fur et à mesure), `keyb=NOM` (disposition du clavier) et `ecran=LxH` (résolution, de
  640x480 à 1920x1080), lus par le noyau au démarrage ; écrits par le panneau de configuration.

## Elegant : menus des applications (barre de menus globale)

Requêtes `ws_call` (message de 512 o : +0 op / résultat, +8 a, +16 b, +24 c, +32 d,
+40 longueur des données (`WM_LEN`, 32 bits), +44 pid, +48 données de 464 o ; constantes
dans `src/inc/armpi.inc`). Comme dans Onyx (`EL_OP_MENU_SET`), chaque fenêtre peut
publier un **texte de menus** ; la barre de menus (`SD0:/sys/menubar`) montre celui de la
fenêtre active.

Texte : lignes séparées par `\n`, UTF-8, `WS_MENU_MAX` = 2048 octets au plus :
`M<titre>` ouvre un menu, `I<id>\t<libellé>\t<raccourci>` une entrée (id décimal ≥ 0 ;
raccourci facultatif, par ex. `Ctrl+N`, **seulement affiché** : le programme traite
lui-même ses touches), `-` un séparateur.

| Op | Nom | Arguments | Résultat |
|---|---|---|---|
| 10 | `WS_OP_MENU_SET` | a id de la fenêtre (de l'appelant), b décalage, c longueur totale, `WM_LEN` longueur du morceau (≤ `WS_MENU_PIECE` = 464), données : le morceau | 0 ; le morceau qui atteint la longueur totale publie le menu (c = 0 : plus de menu) ; un morceau au décalage 0 retire l'ancien en attendant |
| 11 | `WS_OP_MENU_GET` | a décalage | numéro de série (0 : pas de fenêtre active normale, ou pas de menu) ; b id de la fenêtre, c longueur totale, d longueur du morceau ; données : titre de la fenêtre (`WS_MENU_TITLE` = 128 o, NUL) puis le morceau (≤ `WS_MENU_GPIECE` = 336 o) |
| 12 | `WS_OP_MENU_COMMAND` | a id de l'entrée (-1 : Quitter), b numéro de série vu (0 : sans contrôle) | 0 ; `E_AGAIN` si le menu a changé ; `E_BADF` sans fenêtre active |

Le numéro de série change quand la fenêtre active change, quand son menu est republié ou
quand son titre change (chaque publication donne au menu une génération unique). Les
textes de plus de 464 octets voyagent en **morceaux** dans les deux sens : l'envoi par
décalages croissants ; la lecture recommence si le numéro change entre deux morceaux.

Événement **`WEV_MENU` = 7** : un choix dans la barre ; `EV_CODE` (+16) = id de l'entrée,
`EV_WIN` (+28) = la fenêtre. « Quitter » (menu au nom de l'application) arrive en `WEV_CLOSE`,
comme la case de fermeture.

UIKit (`kits/uikit/uikit.abi`, version 35) :

* `ui_menu` — x0 = texte des menus (0 ou vide : aucun) → 0 (`E_INVAL` au-delà de 2048 o) ;
  à appeler après `ui_window`, de nouveau pour changer de menus.
* `ui_menu_get` — (barre de menus) x0 = tampon du texte (2049 o), x1 = tampon du titre
  (128 o), x2 = numéro connu → x0 = numéro, x1 = id de la fenêtre, x2 = longueur ; les
  tampons ne sont remplis que si le numéro a changé.
* `ui_menu_command` — (barre de menus) x0 = id (-1 : Quitter), x1 = numéro montré → 0.
* `ui_popup_ex`, drapeau bit 4 : libellés `texte<tab>raccourci`, raccourci aligné à droite.

## Elegant : capture de l'écran (vncd, screenshot)

`WS_OP_SNAPSHOT` (13) : b = génération déjà vue (0 : tout l'écran) → 1 si une zone a été
recopiée (0 : rien de neuf) ; a largeur, b hauteur, c nouvelle génération, d numéro d'un tampon
partagé de la taille de l'écran (créé au premier appel, accordé à l'appelant : `buf_map`) ;
données : x0, y0, x1, y1 de la zone recopiée (64 bits chacun). L'écran tel qu'affiché (tampon
arrière d'Elegant, curseur compris), pixels `0x00RRGGBB`, pas = largeur × 4.

## Elegant : les fenêtres une à une (rdpd)

Ops pour le bureau à distance d'Onyx (`SD0:/sys/svc/rdpd`, RESEAU.md 5.4). Comme
`WS_OP_SNAPSHOT`, elles ne sont pas réservées aux programmes de `SD0:/sys` (le noyau ne marque
pas le message).

* `WS_OP_WIN_LIST` (14) : a = première entrée → nombre de fenêtres ; a = id de la fenêtre qui
  reçoit le clavier (celle qui capture, sinon l'active ; 0 : aucune) ; données : 7 entrées de
  64 octets au plus (`WE_*` d'`armpi.inc`), **dans l'ordre d'affichage** (couches fond,
  normale, dessus ; du fond vers l'avant), réduites et `WF_NOLIST` comprises :
  +0 id (32 bits), +4 pid, +8 x, +10 y (16 bits signés, le contenu en coordonnées de l'écran),
  +12 largeur, +14 hauteur (16 bits), +16 rôle `WF_*` (32), +20 état (`WES_KEYS` 1 au clavier,
  `WES_MINI` 2 réduite), +24 génération du contenu (+1 à chaque `WS_OP_UPDATE`), +28
  génération du cadre (+1 à chaque `WS_OP_FRAME`), +32 titre UTF-8 tronqué à 31 octets.
  `WS_OP_LIST` (7) reste tel quel (le dock).
* `WS_OP_WIN_READ` (15) : a = id (`WS_WIN_DESKTOP` = `0xFFFFFFFF` : le fond et les fenêtres
  `WF_BACK`, composés), b = partie (0 contenu avec les boutons du cadre, 3 contenu seul ; 1 / 2 cadre actif / inactif : image de
  (l + 2 × 12) × (h + 12 + 12), contenu transparent), c = x | y << 32, d = l | h << 32 → 0
  (`E_BADF`, `E_INVAL` : partie sans cadre, rectangle hors de l'image ou plus grand que le
  tampon) ; le rectangle est recopié dans le **tampon de l'appelant** (celui de
  `WS_OP_SNAPSHOT` : taille de l'écran, créé au premier appel ; un programme ne se sert pas des
  deux), lignes bout à bout (pas = l × 4) ; réponse : a = génération du contenu, b = du cadre,
  c = taille du tampon (octets), d = son numéro (`buf_map`). l = 0 : rien de copié (le tampon
  et les générations seulement).
* `WS_OP_WIN_MOVE` (16) : a = id, b = x, c = y (le contenu, à l'écran) → 0 ; fenêtres normales
  seulement (`E_INVAL` pour `WF_BACK` / `WF_TOP`) ; la barre de titre reste sous la barre de
  menus, comme en la tirant.
* `WS_OP_WIN_CLOSE` (17) : a = id (fenêtre de n'importe quel programme) → 0 ; son programme
  reçoit `WEV_CLOSE`, comme un clic sur la case de fermeture.
* `WS_OP_RAISE` (8, déjà là) : passer devant (et restaurer une fenêtre réduite).

## Elegant : present asynchrone

`ui_update` n'attend plus Elegant. `WS_OP_CREATE` et `WS_OP_RESIZE` rendent en d le décalage, dans
le tampon de la fenêtre, d'un en-tête `WD_*` de 16 octets :

| Champ | Rôle |
|---|---|
| `WD_DAMAGE` (+0) | zone salie du contenu, `x0 \| y0 << 16 \| x1 << 32 \| y1 << 48`, 0 : aucune ; le programme l'étend par échange atomique, et lève `WS_SIG_DAMAGE` (`WS_SIGNAL`) quand il la fait passer de 0 à une zone ; Elegant la reprend et la remet à 0 à son tour suivant |
| `WD_FRAME` (+8) | 1 : `WEV_FRAME` voulu ; remis à 0 par Elegant |

`ui_frame_request` (UIKit 175) demande un événement **`WEV_FRAME` = 10** : il arrive quand Elegant
a pris le contenu (le programme peut redessiner sans déchirer l'image en cours), au plus une fois
par `WS_FRAME_MS` (16 ms), y compris pour une fenêtre cachée. Boucle d'animation : `ui_frame_request`,
dessin, `ui_update`, attente de `WEV_FRAME`. `WS_OP_UPDATE` reste servi : infobulles, menus
surgissants et dialogues, qui ont leur propre fenêtre, passent encore par lui. Essai :
`SD0:/bin/presenttest`.

`EV_WAIT` dort réellement : `WS_POST` réveille les threads du programme qui attendent.

## Elegant : surface attachée à une fenêtre

Un programme donne à Elegant **ses propres pixels** pour le contenu d'une de ses fenêtres (ou une
zone de ce contenu) : un tampon partagé qu'il crée (`BUF_CREATE`) et dans lequel il écrit ses
images (vidéo, émulateur, programme SDL porté, Jet) ; Elegant les compose **directement** depuis
ce tampon, sans recopie par le programme dans le tampon de la fenêtre ni `WS_OP_UPDATE`
([DESIGN-ELEGANT.md](DESIGN-ELEGANT.md) 17 ; constantes dans `src/inc/ws_ext.inc`).

| Op | Nom | Arguments | Résultat |
|---|---|---|---|
| 25 | `WS_OP_SURFACE_ATTACH` | a id de la fenêtre (de l'appelant), b numéro du tampon (-1 : détacher), c x \| y << 32 (zone dans le contenu), d l \| h << 32 ; données : +0 format (`WS_SF_XRGB8888` 0, `WS_SF_RGB565` 1 ; 32 bits), +4 pas en octets (32 bits ; 0 : l × octets du pixel), +8 décalage du premier pixel dans le tampon (64 bits) | 0 ; réponse a = **pid d'Elegant** ; `E_PERM` : Elegant ne peut pas mapper le tampon → le programme l'y autorise (`BUF_GRANT(n, pid)`) et recommence ; `E_INVAL` : format, taille (1 à 4096 de côté), pas, ou tampon trop petit pour la zone ; `E_BADF` : fenêtre |
| 26 | `WS_OP_SURFACE_DAMAGE` | a id, b nombre de rectangles (0 : toute la surface), données : x, y, l, h (16 bits chacun, dans la surface) par rectangle, `WS_DAMAGE_MAX` = 58 au plus | 0 ; les rectangles (bornés à la surface et au contenu) deviennent des zones sales du compositeur ; `E_INVAL` sans surface |

Rattacher le même tampon ne change que la zone, le format, le pas et le décalage (sans
remappage) : un double tampon tient dans un seul tampon partagé (deux images, on rattache au
décalage de celle qui est prête, puis `WS_OP_SURFACE_DAMAGE`). Une seule surface par fenêtre ; la
zone est bornée au contenu (une fenêtre redimensionnée plus petite la coupe). La surface est
détachée à la fermeture de la fenêtre (le tampon reste au programme). rdpd (`WS_OP_WIN_READ`,
partie 0) reçoit le contenu avec la surface par-dessus ; chaque `ATTACH` / `DAMAGE` fait avancer
la génération du contenu.

UIKit (version 172, [KITS.md](KITS.md)) : `ui_surface(x, y, l, h, format)` crée le tampon (pas
aligné sur 16 octets) et l'attache à la fenêtre courante (`BUF_GRANT` compris),
`ui_surface_pixels`, `ui_surface_damage(s, x, y, l, h)`, `ui_surface_damage_list(s, rectangles,
n)`, `ui_surface_move(s, x, y, l, h)` (tampon neuf s'il le faut), `ui_surface_free`.
`ui_surface_source(s, tampon, pas, décalage)` fait composer la zone depuis un tampon écrit ailleurs,
déjà autorisé à Elegant (-1 : les pixels de la surface) : Jet montre ainsi l'image du GPU. Elle passe
`WS_SF_NOREDRAW` (256, ajouté au format) : si le tampon et la zone ne changent pas, Elegant ne
recompose rien et `WS_OP_SURFACE_DAMAGE` dit ce qui a changé ; sans ce drapeau, tout le contenu est
recomposé. Essai :
`SD0:/bin/surfacetest [secondes] [rgb565]`.

## init2 : redémarrage

`INIT_OP_REBOOT` (3), service `init2` : a bit 0 = `tryboot`, bit 1 = arrêt (`INIT_REBOOT_HALT`)
→ 0, puis 500 ms plus tard le bloc PM du Pi 4 (`PM_WDOG`, `PM_RSTC`) redémarre la carte
(`SD0:/bin/reboot [tryboot]`). Arrêt : la partition 63 dans `PM_RSTS` dit au firmware de ne pas
relancer le système. `SD0:/sys/shutdown`, le bouton d'alimentation du dock : le dialogue Redémarrer,
Éteindre, Annuler.
