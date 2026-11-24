# Audit de sécurité de Korin OS, version PC

Audit du 24/11/2026, sur `main` (659b52bf). Périmètre : le noyau C du PC (`src/kernel/core`, `arch/x86_64`, `arch/pc`),
les pilotes du PC, les services (`src/svc`), les programmes système (`src/sys`) et les outils sensibles de `src/bin`.
Le noyau asm du Pi est hors du périmètre.

**Méthode.** Le code a seulement été lu, rien n'a été exécuté. Cinq zones ont été lues en parallèle, puis les
constats critiques ont été relus dans le code. Chaque constat porte une mention :

* **certain** : le chemin a été suivi dans le code de bout en bout ;
* **probable** : le constat dépend d'une course, du matériel ou d'un comportement qui n'a pas été essayé.

Les numéros de ligne sont ceux du 24/11.

**Deux passes.** Les §1 à §9 sont la première passe, corrigée là où la seconde l'a infirmée ou aggravée (mentions
« seconde passe »). Les §10 à §17 sont la seconde passe : chaînes d'attaque, Jet, inventaire des appels système,
démarrage et installation, services et kits restants, erreurs corrigées, et **le plan révisé (§17), qui remplace la
liste du §1 et le §9**.

Ce document complète le plan de [SECURITE.md](SECURITE.md) et l'étape 5 de
[MULTI-UTILISATEUR.md](MULTI-UTILISATEUR.md).

**Cadre.** La règle du 29/10 (« pas de sécurité contre un attaquant ») a été assouplie le 18/11 : la menace retenue
est une page web piégée dans Jet, avec un vrai compte Google, sur un PC à plusieurs comptes. Pour chaque constat, la
colonne « bugs » dit si la correction protège aussi d'une app boguée.

---

## 1. Synthèse

Le noyau du PC a de bonnes bases :

* le pid de l'appelant est écrit par le noyau ;
* `copy_from_user` est borné ;
* NX est actif ;
* la mémoire rendue aux process est toujours mise à zéro ;
* `SETUID` et `rights` sont bien gardés ;
* TLS est vérifié partout où il existe ;
* l'index des paquets est signé.

Le cloisonnement entre comptes et entre apps **n'existe pourtant pas encore en pratique**, pour cinq raisons de fond.

1. **`access = log` par défaut.** vfs ne refuse rien, sauf le coffre (`src/svc/vfs/homes.c:19`,
   `skel/pc/etc/system.ini:28`). N'importe quelle app peut écrire dans `/bin`, `/sys`, `/etc/shadow.ini` et
   `/etc/elevate.ini`.
2. **Les services ne contrôlent pas leur appelant.** Le noyau donne le pid sans qu'on puisse le falsifier, mais
   presque aucun service ne s'en sert. Sont ouverts à tous : vfs (montage, alias), `fs.*`, `blk.*`, pcid, usbd,
   hwmanager, hidmanager, netd, son, Wi-Fi, init2, Elegant, oauthd et clipd.
3. **Les avis du noyau peuvent être forgés.** `sys_svc_call` ne refuse pas les codes réservés (`SVC_OP_END` 0xFFFF,
   `SVC_OP_SETUID` 0xFFFA, `SVC_OP_SIGNAL` 0xFFFD…). Il suffit d'envoyer `{op=0xFFFA, a=mon pid, b=0}` à vfs pour qu'il
   traite l'appelant comme uid 0.
4. **Des identités sont tirées de fichiers que tout le monde peut écrire.** `/tmp/session.ini` décide de l'uid des
   shells de telnet et de la console. Les paquets passent par `/tmp/pkg`. La ligne `password =` de `/etc/remote.txt`
   est elle aussi lue.
5. **Le noyau a des failles de mémoire à la portée de toute app.** Une page rendue au pmm reste mappée en écriture
   (`SHM_TRUNC`, fenêtre d'image héritée par `fork`). Un ELF forgé lit la RAM physique. Le cache d'images peut être
   empoisonné.

Conséquence : aujourd'hui, **une app ordinaire de n'importe quel compte (ou Jet compromis) peut devenir uid 0 ou
noyau par au moins dix chemins indépendants.** Passer en `enforce` ne suffit pas : il faut d'abord fermer les
contournements des §3.2 à §3.4.

### Les 15 corrections les plus rentables

| # | Correction | Ferme | Taille | Bugs |
|---|---|---|---|---|
| 1 | Noyau : `sys_svc_call` et `sys_svc_forward` refusent `op >= 0xFFF0` ; les services ne traitent un avis que si `SVC_NEXT` rend `REQ_SIGNAL-1` | I1 | 5 lignes | oui |
| 2 | `SHM_TRUNC` traité comme `SET_SIZE(0)` (`E_BUSY` si l'objet est mappé) | N1 | 3 lignes | oui |
| 3 | elf.c, image.c, kits.c : `offset > size \|\| filesz > size - offset`, idem pour `phoff` ; `e_entry` vérifié | N3 | 10 lignes | oui |
| 4 | `MSR_FMASK = 0x44700` (NT) ; adresses canoniques vérifiées pour le gestionnaire de signal, `entry` et `tls` | N5, N6 | 15 lignes | oui |
| 5 | `IMAGE_CREATE`, `SEAL` et `FORGET` réservés à un process de confiance ; `fork` refusé pendant un chargement d'image | N2, N4 | petite | oui |
| 6 | vfs : `PATHMAP`, `MOUNT` et `UNMOUNT` réservés à l'uid 0 (sauf `/media`) ; nom de service filtré ; casse repliée dans `access.c` | F1-F3 | petite | oui |
| 7 | `fs.*` et `blk.*` n'acceptent que vfs, fatfs et mkfs élevé (pid ou drapeau du noyau) | F4, P1 | petite | oui |
| 8 | uid de session hors de `/tmp` (en mémoire dans init2 ou Elegant) ; uid 0 refusé | A1 | petite | oui |
| 9 | hidmanager : `ABONNER` réservé à Elegant, `PILOTE` aux process à droits ; boucle de lecture bornée | G1 | petite | oui |
| 10 | authd calcule lui-même la portée de « retenir » ; `AUTH_SUDO` lié à `/bin/sudo` et à un terminal | A2, A3 | petite | non |
| 11 | pcid : `CLAIM`, `CFG_WRITE`, `RESCAN` et `ANNOUNCE` réservés aux process à droits ; MSI, MSI-X et BAR protégés | P2 | petite | oui |
| 12 | Elegant : les deux tests de bornes qui débordent (`uikit_ops.c:31`, `snapshot.c:136`) | G2 | 2 lignes | oui |
| 13 | pkg : téléchargement dans `/var/pkg/dl` et extraction depuis le fd vérifié | K1 | petite | oui |
| 14 | Wi-Fi : machine d'états EAPOL (KRACK, rejeu) | P3, P4 | moyenne | non |
| 15 | puis `access = enforce` par défaut, et la suite de SECURITE.md (SMEP/SMAP, unveil, ASLR, pledge) | tout | — | oui |

---

## 2. Noyau (`src/kernel`)

### Critiques

**N1. `SHM_OPEN` avec `SHM_TRUNC` rend les pages d'un objet encore mappé (page libérée mais encore utilisée).**
*Certain, relu.*

* **Où :** `core/ipc.c:914-915`. `resize(s, 0)` fait `pmm_free` sur les pages sans regarder `s->mapped` ni les sceaux,
  alors que `SHM_SET_SIZE` vérifie les deux. Les pages projetées par `mm_fault` n'ont pas de référence (`pmm_ref`).
* **Scénario :** une app crée un objet, le mappe en lecture-écriture et touche les pages. Elle le rouvre ensuite avec
  `SHM_TRUNC` : les pages retournent au pmm mais restent inscriptibles chez elle. Elle fait réutiliser ces pages pour
  des tables de pages (en lançant des process), puis mappe n'importe quelle page physique. Elle tient alors le noyau.
* **Correction :** `E_BUSY` si l'objet est mappé, respecter `SEAL_SHRINK`. Mieux : un `pmm_ref` par page mappée.

**N2. Fenêtre d'`IMAGE_CREATE` héritée par `fork`.** *Certain.*

* **Où :** `core/image.c:316` mappe la fenêtre `VM_W|VM_SHARED`. `vm_fork` (`arch/x86_64/vm.c:252`) recopie l'entrée,
  et `sys_image_seal` ne retire la fenêtre que chez le chargeur.
* **Effet :** le fils garde des pages libérées après `IMS_ABORT`, comme dans N1. Il peut aussi réécrire le code
  partagé d'une image scellée (`/bin/x`) utilisée par tous les comptes, sans que l'empreinte retenue par le coffre
  (`im->hashed`) le remarque.
* **Correction :** refuser `fork` pendant un chargement, ou sauter les entrées `VM_SHARED` hors région ou BUF dans
  `vm_fork`.

**N3. ELF : `offset + filesz` déborde, ce qui permet de lire la RAM physique.** *Certain, relu.*

* **Où :** `core/elf.c:46`, puis la copie de `elf.c:80` lit `img - k`. Comme `img` est dans la carte physique, la
  lecture porte sur n'importe quelle mémoire sous le tampon.
* **Qui peut :** toute app, par `SPAWN_IMAGE` ou `EXEC`.
* **Effet :** le fils emporte des copies de la RAM : coffre, clés TLS, mots de passe en mémoire.
* **Même défaut :**
  * `phoff + phnum*phentsize` (`elf.c:38`) ;
  * `check_elf` de `image.c:168` (et `phoff`, `image.c:159`). Ce n'est pas une #GP, contrairement à ce que disait la
    première version : `image_map` lit `im->pages[fo/4096]` avec un index choisi (`image.c:205-207`) et **mappe la
    valeur lue comme une page physique** (`image.c:214`). L'index peut viser la mémoire de l'appelant (pas de SMAP), donc
    la page projetée est choisie : lecture de toute la RAM, sans course (seconde passe) ;
  * `kits.c:78`, qui demande `rights`.
* Le test d'`elf.c` est à la ligne 45, la copie aux lignes 80-81.

**N4. Empoisonnement du cache d'images : élévation entre comptes.** *Certain, relu.*

* **Où :** `sys_image_create` (`image.c:260`) n'exige aucun droit, et le chemin n'est qu'une déclaration. AppKit
  essaie d'abord `SPAWN_PRELOADED(path, size)` (`appkit.c:1332`), et `EXEC` passe par `image_get`.
* **Scénario :** le compte A scelle une fausse image `/bin/sudo` de la bonne taille. Quand B, ou un process d'uid 0,
  lance `sudo`, c'est le code de A qui s'exécute. A récupère le mot de passe, puis authd le passe en uid 0.
* **Autres défauts :** `IMAGE_FORGET` est ouvert à tous, et 128 images de 4 Go épuisent la RAM.
* **Correction :** création réservée à vfs ou à un chargeur système à droits, ou au minimum des images indexées par
  uid ; à terme, vfs atteste le lien entre le chemin et le contenu.

### Élevées

**N5. Une instruction suffit à planter le noyau : drapeau NT à l'entrée de `syscall`.** *Certain, relu.*

* **Où :** `arch/x86_64/cpu.c:164` (`FMASK 0x40700` ne masque pas le bit 14).
* **Mécanisme :** `popfq` avec NT, puis `syscall`. `iretq` lève alors une #GP en anneau 0 après `swapgs`, avec le GS
  de l'utilisateur. Les fautes s'enchaînent jusqu'à l'arrêt, et les autres cœurs se bloquent sur les invalidations de
  TLB.
* **Correction :** `0x44700`, et une reprise sur `iretq` (comme `bad_iret` de Linux).

**N6. Même panne avec une adresse non canonique.** *Certain.*

* **Où :** gestionnaire de `sys_sig_action` (`signal.c:50`), `e_entry` (`elf.c:89`, `image.c:241`), `tls` de
  `sys_thread_create` (`thread.c:45`, `wrmsr FS_BASE`).
* **Déjà fait :** `SIG_RETURN` et `SYSCALL_CAPTURE` vérifient. Il faut une fonction `user_canonical()` partout.

**N7. Faute pendant une copie sous un verrou global : la machine gèle.** *Probable : course facile.*

* **Où :** `sys_futex_wait` lit le mot sous `futex_lock`, interruptions coupées (`thread.c:206`). `deliver()` écrit la
  trame de signal sous `sig_lock` (`signal.c:255`).
* **Mécanisme :** un autre thread fait `munmap` entre `user_check` et la copie. La faute du noyau mène à
  `proc_exit(-11)` sans relâcher le verrou.
* **Correction :** copier hors verrou, puis une table d'exceptions (`E_FAULT` au lieu de tuer le process). Ce sera
  aussi nécessaire pour SMAP.

**N8. `BUF_FREE` concurrent : double `buf_put`.** *Probable.*

* **Où :** `mm.c:922` teste `buf_mapped()` sans verrou.
* **Effet :** double `pmm_free`, qui provoque une panique. Ou, avec un tampon accordé (`BUF_GRANT`), des pages rendues
  alors qu'elles restent mappées chez le destinataire, comme dans N1.
* **Correction :** tester et effacer le bit de façon atomique sous `buf_lock`.

**N9. Ni KPTI, ni mitigations Spectre ou MDS ; carte physique et texte du noyau en RWX.** *Certain ; l'exploitabilité
dépend du processeur.*

* **Où :** `paging.c:70-112`.
* **À faire en premier :** NX sur la carte physique, texte du noyau en lecture seule.
* **Ensuite :** vérifier le processeur du ZBook (Tiger Lake n'est pas sensible à Meltdown, mais l'est à certaines
  variantes de MDS et de Spectre v2).

### Moyennes

* **N10. Sceaux SHM contournables** *(certain)*. `sys_vm_protect` rend inscriptible une région partagée scellée
  (`mm.c:632`). `SEAL_WRITE` est accepté alors qu'une projection inscriptible existe déjà.
* **N11. Noms SHM sans contrôle d'accès** *(certain)*. La table est globale (`ipc.c:889`) : n'importe quel compte
  ouvre, tronque (voir N1), délie ou réserve à l'avance un nom.
* **N12. Presse-papier du noyau global** *(certain)*. `ws.c:228` : lisible et remplaçable par tout process, de tout
  uid.
* **N13. Aucun quota** *(certain)*.
  * Threads et process illimités (65 535 emplacements).
  * Allocations noyau sans limite : SHM d'1 Go ; 1 023 canaux, chacun avec un anneau de 64 Ko en tas noyau ;
    `SPAWN_IMAGE` qui alloue 256 Mo par `kmalloc`.
  * `BUF_MAX = 128` pour tout le système.
  * Effet : une app affame les services, et des avis de vfs sont perdus.
* **N14. Aléa faible sans RDRAND** *(certain sur le code)*.
  * La clé ChaCha démarre à zéro (`sys.c`), avec un réservoir de 64 bits mélangé par une multiplication.
  * Le retour de `rdrand` est ignoré, il n'y a ni RDSEED ni graine sauvée sur disque.
  * Sous QEMU (`qemu64`) et en début de démarrage, la sortie est prévisible. Cela touche TLS, OAuth, le sel des mots
    de passe et la future ASLR.
* **N15. `rights` équivaut à l'anneau 0** *(conception)*.
  * `DEV_MAP` accepte l'APIC, le HPET et l'ECAM PCI.
  * `DMA_ALLOC` rend l'adresse physique, et il n'y a pas d'IOMMU.
  * `IO_PORT` laisse passer 0xB2 (SMI).
  * « Jamais la RAM » n'est donc pas une frontière. Il faut garder très peu de process avec `rights`, et à terme
    donner des droits par fonction PCI et utiliser VT-d.
* **N16. `SVC_FORWARD` garde le pid d'origine** *(certain, services à droits seulement)*. Un service compromis fait
  passer une requête forgée pour celle de son appelant (`svc.c:523`).
* **N17. `SVC_SIGNAL` et `WS_SIGNAL` ouverts à tous** *(certain)*. `svc.c:482` : interruptions fantômes chez les
  pilotes, et `SVC_SIG_LOWMEM` forgé.

### Faibles

* Fuites d'informations sur tout pid :
  * `PROC_EXE`, `PROC_INFO`, `VM_STATS`, `THREAD_LIST`, `WAIT` ;
  * `BUF_INFO` rend des adresses physiques ;
  * `KLOG` est lisible par tous et contient des vidages de registres du noyau ;
  * `PRINT` permet d'écrire de fausses lignes au journal.
* W+X permis par `VM_MAP` et `VM_PROTECT`. C'est utile au JIT ; à borner avec pledge.
* `PROC_SET_SCRIPT` : le premier appel, venu de n'importe quel process, fixe l'identité du script (voir A6).
* Fenêtres NMI autour de `swapgs` ; #MC sans IST ; pas de page de garde sur les piles du noyau.

---

## 3. IPC, fichiers et identité

### 3.1 Transverse

**I1. Les avis du noyau peuvent être forgés par tous les process.** *Certain, relu (`svc.c:178-195` : seul le pid est
réécrit).*

Les avis `SVC_OP_END`, `BIRTH`, `FORK`, `EXEC`, `SETUID` et `SIGNAL` arrivent dans la même file que les appels, et
les services les reconnaissent au seul `msg.op`. Exemples :

| Cible | Message forgé | Effet |
|---|---|---|
| vfs | `SETUID {a=moi, b=0}` | uid 0 pour vfs : `/etc/shadow.ini`, coffre, homes, `/sys` |
| vfs | `END {a=pid d'un fs}` | le volume disparaît |
| vfs | `BIRTH`, `FORK`, `EXEC` | E/S ou dossier courant imposés à une victime ; fiches de pid sans plafond (épuisement) |
| pcid | `END {a=pid de nvme}` | NVMe libéré, bus master coupé : disque système perdu |
| usbd | `END {a=pid de usbkeyboard}` | tuyau libéré, rouvert par l'attaquant : enregistrement des frappes |
| hidmanager | `END {a=pid d'Elegant}` | plus de clavier ni de souris |
| hwmanager | `END` | relance de pilotes |
| acpid | `SIGNAL` répétés | GPE coupées : couvercle, bouton marche, batterie critique |
| wifisvc | `SIGNAL` bit 1 | radio arrêtée |
| authd, keyvault, busd | `END` | cartes, attentes ou abonnements d'un autre annulés |

**Correction :** une seule ligne dans le noyau (`op >= 0xFFF0` donne `E_INVAL` dans `SVC_CALL` et `SVC_FORWARD`),
plus `req == REQ_SIGNAL-1` dans chaque service.

**I2. Pas de contrôle d'appelant dans les services.** Le modèle existe : keyvault et authd lisent `SYS_PROC_UID` à
chaque requête. Il faut un en-tête commun `caller_uid()` et `caller_has_rights()` (ajouter au noyau `rights` par
pid). Les opérations à réserver sont listées service par service plus bas.

### 3.2 vfs et systèmes de fichiers

**F1. `VFS_PATHMAP` ouvert à tous : un alias contourne toutes les règles.** *Certain, relu (`path.c:384-397`).*

* **Mécanisme :** la cible est résolue sans règle, et `acces_verifier` juge le chemin externe de l'alias.
* **Exploitation :** `PATHMAP "/tmp/e" -> "/etc"`, puis écriture de `/tmp/e/shadow.ini`, puis `sudo`.
* **Alias inverse :** `/etc` vers un dossier de l'attaquant ; authd et keyvault lisent alors ses fichiers.
* **Correction :** réservé à l'uid 0, et jugement sur le chemin canonique.

**F2. `VFS_MOUNT` et `VFS_UNMOUNT` ouverts à tous ; nom de service non filtré.** *Certain pour l'absence de contrôle
(`vfs.c:1048-1058`) ; probable pour l'exécution.*

* `MOUNT "x src ../../home/moi/evil"` lance `/sys/svc/../../home/moi/evil` par vfs, en uid 0.
* Un montage par-dessus `/etc` est possible.
* `UNMOUNT root` provoque un déni de service global.

**F3. La casse FAT contourne les règles en lecture, même en `enforce`.** *Certain, relu (`access.c:28-34`,
`strncmp`).*

* `/ETC/SHADOW.INI`, `/Var/KeyVault/1000.db`, `/Etc/KeyVault/machine.key` et `/HOME/autre/…` échappent aux règles.
  Un chemin qui ne correspond à aucune règle tombe sur `return !ecrire` (`access.c:58`) : c'est un contournement **en
  lecture seulement**, l'écriture reste refusée (seconde passe).
* Avec le shadow, la clé de la machine (sans TPM) et le `.db`, le coffre s'attaque hors ligne.
* **Correction :** comparaison sans casse (déjà faite pour `/sbin`). Vérifier aussi les points et espaces de fin et
  les noms courts `XXXX~1`.

**F4. Les services `fs.*` s'appellent directement, sans passer par vfs.** *Certain.*

* **Où :** `common/fsowner.c:28-43`, fatfs, ext4fs, ramfs.
* **Exploitation :** `FS_OPEN "etc/shadow.ini"` envoyé directement au service du volume système.
* **Aggravé (seconde passe) :** `proprietaire()` reconnaît vfs **à son nom de process** (« vfs », `fsowner.c:38`),
  qui se choisit au lancement. Un faux « vfs » ouvre des fichiers au nom de n'importe quel pid.
* **Correction :** un drapeau du noyau « appelants de confiance seulement », ou bien `m->pid == pid de vfs`.

**F5. Images de disque piégées (clé USB).**

* **ext4fs, élevée, certain :** `bpg` et `ipg` ne sont pas bornés à `8*bs` au montage (`ext4r.c:73`, `ext4w.c:971`).
  Il en résulte une écriture de bits hors de `bm[65536]` dans le `.bss` (`ext4w.c:203-213`, `284-289`), dans un
  service qui a les droits matériel. **Nuance (seconde passe) :** usbstorage ne monte automatiquement que fatfs ou
  exfat (`usbstorage.c:46`, `299`) ; ext4 demande un montage explicite (qu'une app peut obtenir par F2 et imgblk).
  Par simple branchement d'une clé, l'attaque se limite donc au déni de service.
* **ramfs (`/tmp`), élevée, certain :**
  * `pos + len` déborde (`ramfs.c:380-399`) ;
  * une poignée sur un nœud réutilisé après `unlink` permet à A de lire le fichier de B ;
  * ni ramfs ni ext4fs ne vérifient `O_WRITE`.
* **Moyenne :** boucles sans fin sur des chaînes de clusters circulaires (`fat32.c:243-263`, `exfat.c:66-78`), table
  de majuscules exFAT non bornée, dossier ext4 creux géant, `descsz` non borné. Le service se fige sous le verrou
  `big`.
* **Moyenne :** les noms venus du disque (`/`, `\`, `..`, caractères de contrôle) ne sont pas filtrés
  (`fat32.c:406`, `exfat.c:245`, `ext4r.c:332`).

**F6. Homes** *(faible)*.

* Un uid sans compte reçoit le home de la session (`homes.c:209`).
* `/home/system` est lisible par tous.
* Un compte avec `home = /home` peut écrire tous les homes (`access.c:49-51`).
* Le refus d'exécuter `/sbin` se contourne par une copie dans `/tmp` (la vraie garde est `uf_need_root`).
* `HID_OP_KEYMAP` concatène `../` sous `/res/keyb/` (`hidmanager.c:319`).

### 3.3 Authentification et session

**A1. `/tmp/session.ini` (écrit par tous) choisit l'uid des shells.** *Certain, relu (`korinrt/users.c:115-133`).*

* **Lecteurs :** `spawn_session_next` (telnetd `telnetd.c:168`, console `console.c:109`) et `spawn_session` (Elegant
  `desktop.c:289`, init2 `init2.c:92`, `logout_request` `init2.c:579`).
* **Avec `uid = 0` :** pas de `SPAWN_AS`, donc un shell root à la prochaine connexion telnet ou console.
* **Avec `uid = 1001` :** un shell sous le compte d'un autre.
* **Autres fichiers :** `/tmp/login.halt` et `/tmp/login.manual` bloquent ou modifient la connexion.
* **Correction :** uid gardé en mémoire (init2 ou Elegant, rendu par login par IPC), uid 0 refusé. telnetd et la
  console prennent l'uid du compte **authentifié**.

**A2. « Retenir 5 minutes » : la portée est fournie par le client.** *Certain, relu (`authd.c:647`, `664`).*

* N'importe quelle app du même compte envoie `AUTH_SUDO` avec la portée 0 (session graphique) ou le pid du shell
  (énumérable). Elle devient uid 0 sans carte.
* Après une carte « retenue », `AUTH_ELEVATE` réussit en silence pour **toute** app du compte (`authd.c:547`).
* **Correction :** authd calcule la portée (pid parent réel lu au noyau), vérifie que l'appelant est `/bin/sudo`
  (`SYS_PROC_EXE`), et lie la portée graphique au pid de l'app élevée.

**A3. Compte sans mot de passe : `AUTH_SUDO ""` élève sans rien montrer.** *Certain (`authd.c:288-291`,
`659-679`).*

* **Où :** c'est la configuration par défaut d'une installation à un seul compte. `useradd` crée aussi des comptes
  sans mot de passe, et le compte `user` de `skel/pc/etc/users.ini` est dans `elevate.ini`.
* **Correction :** sans mot de passe, `AUTH_SUDO` exige un terminal (vterm) ou passe par la carte.

**A4. Le `startup.txt` du premier compte est lancé sous le compte qui se connecte.** *Probable fort (`homes.c:206-213`,
`init2.c:767-778`).*

init2 (uid 0) a pour `HOME:` celui de la connexion automatique ou du premier compte. Le compte A exécute donc du code
sous l'uid de B. **Correction :** lire `/home/<compte de session>/etc/startup.txt` par chemin explicite.

**A5. La carte d'élévation se laisse imiter et ne nomme pas le demandeur.** *Certain (`authd.c:555-568`,
`authcard.c:95-127`).*

* La raison affichée est un texte libre de l'app.
* La carte est un `ui_dialog` ordinaire. Toute app peut en dessiner une identique en `WF_TOP` plein écran (voir G3).
* « Autoriser » est le bouton par défaut, et l'élévation passe toute l'app en uid 0.
* **Correction :** afficher le chemin vérifié (`SYS_PROC_EXE`) ; une couche sécurisée dessinée par Elegant seul ;
  plus tard, des tickets par opération (pkgd) plutôt qu'un `SETUID` de l'app.

**A6. keyvault : l'identité d'un script se déclare elle-même.** *Certain pour le code (`keyvault.c:918-926`,
`proc.c:439-455`).*

* `python3 -c` ou un programme dotnet déclare `"/home/u/backup.py"` et lit ses secrets.
* `BASH_ENV`, `PYTHONPATH` ou `sitecustomize` font tourner du code dans un process dont l'identité est légitime.
* Sous `/apps` et `/bin`, aucune empreinte n'est retenue : tout contournement de vfs (F1 à F4, ou le mode log) donne
  la place de Jet dans le coffre, et son jeton Google.
* **Correction :** le noyau pose le script lui-même à l'`exec` d'un `#!` ; identité de script refusée si
  l'environnement contient `BASH_ENV`, `ENV`, `PYTHON*` ou `DOTNET_STARTUP_HOOKS` ; keyvault lit les fichiers par le
  volume interne.

**A7. Autres faiblesses (moyennes).**

* **authd tourne sur un seul fil et dort 1 s par échec dans sa boucle** (`authd.c:657`, `670`) :
  * essais illimités ;
  * quelques appels suffisent à bloquer l'authentification de tous ;
  * une carte laissée ouverte rend `E_BUSY` à toutes les autres.

  Correction : compteur d'échecs par uid avec délai croissant, sans dormir.
* **PBKDF2-SHA256 à 20 000 tours** (`authd.c:70`) : passer à Argon2id (déjà dans keyvault) ou autour de 600 000
  tours. Le format garde le nombre de tours, la migration est donc douce.
* **sudo** :
  * l'environnement : `SYS_SPAWN` de korinrt ne transmet aucun bloc d'environnement, donc `BASH_ENV` ne passe **pas**
    (corrigé par la seconde passe). Le fils prend les valeurs par défaut (`env.c:37-39`) et garde `HOME=/home` en
    uid 0 : `sudo -s` lit `/home/.bashrc`, qui n'est celui de l'utilisateur que si son home vaut `/home` (installations
    de développement) ;
  * les arguments sont recollés avec des espaces (`sudo.c:197-204`) ;
  * `find()` copie `argv[1]` sans borne dans `path[256]` (`sudo.c:116`).
* `ecrire_mdp` : nom non borné dans `ligne_cpt[200]` (`authd.c:330`, `348`). Seul l'uid 0 peut l'appeler.
* **TPM :** scellé sans PCR et avec une auth vide (`tpm.c`). C'est documenté.

### 3.4 init2

* **M1.** `INIT_OP_REBOOT` sans contrôle (`init2.c:539`, `669`) : toute app éteint la machine.
* **M2.** `INIT_OP_LAUNCH` (`init2.c:479-505`) :
  * il lance n'importe quel `/sys/...` avec des arguments choisis et les **droits matériel**, pour n'importe quel uid ;
  * si `SYS_PROC_UID(who)` échoue (demandeur mort), le lancement se fait en uid 0 (*probable*).

  Correction : refuser `uid < 0`, et une liste de programmes lançables par l'utilisateur.

---

## 4. Pilotes du PC

Les pilotes de l'image PC sont listés dans `skel/pc/etc/services.txt` et `drivers.txt`. intelgpu n'est pas lancé par
défaut.

### Critiques

**P1. N'importe quel process lit et écrit le disque brut.** *Certain, relu (`common/blkserv.c:588-625`).*

* **Mécanisme :** `op_attach` enregistre tout pid qui présente un tampon, et `op_rw` ne vérifie que ce pid.
* **Exploitation :** réécrire les secteurs de `/sys/svc/...` sur `blk.nvme0pN`, puis attendre le prochain démarrage :
  le code tourne en uid 0 avec droits. Le même accès permet de lire tout le disque, y compris la partition Windows et
  les autres comptes. `blk.usbN` est touché aussi.
* **Défaut annexe :** le propriétaire du tampon n'est pas vérifié (`BUF_INFO`). Un attaquant peut enregistrer le
  tampon de fatfs à son nom.
* **Bien pire (seconde passe, relu) :** `op_attach` borne la fenêtre à la taille **déclarée** par l'appelant
  (jusqu'à `BUF_SLOT`, 64 Mo), pas à la taille réelle du tampon (`blkserv.c:614`). `op_rw` calcule
  `phys = addr - start + c->phys` (`blkserv.c:646`) et nvme fait le DMA à cette adresse. Un tampon d'une page déclaré
  à 64 Mo donne un **DMA en lecture et en écriture sur la RAM physique qui suit** : le noyau tout de suite, sans
  redémarrage, même en `enforce` (chaîne C3 du §10). `policy_write_ok` de nvme ne limite que les secteurs écrits.
* **Correction :** réserver ces opérations à vfs, fatfs et mkfs élevé ; vérifier `info[2] == pid`.

**P2. pcid : prise d'une fonction PCIe et écriture de sa configuration sans droits.** *Certain pour la prise ;
probable pour l'écriture en RAM.*

* **Où :** `pcid.c:428-486`.
* **Scénario :**
  1. Un `END` forgé (voir I1) libère le NVMe.
  2. `CLAIM` donne la fonction à l'attaquant, avec le bus master.
  3. `CFG_WRITE` reprogramme MSI avec une adresse en RAM. Chaque interruption devient alors une écriture DMA choisie
     par l'attaquant, sans IOMMU pour l'arrêter : élévation jusqu'au noyau.
* **Correction :** ces opérations réservées aux process à droits ; refuser `CFG_WRITE` sur MSI, MSI-X, les BAR et les
  ponts.

### Élevées

* **P3. Wi-Fi : la clé est réinstallée sur un message 3 retransmis (KRACK, CVE-2017-13077/13080).** *Certain dans le
  code.*
  * **Où :** `iwl/mlme.c:292-310`, `assoc.c:746-757`. M3 est accepté en `SUP_DONE`, et TK et GTK sont réinstallées
    par `ADD_STA_KEY`.
  * **Effet :** le PN repart à zéro dans le micrologiciel, d'où une réutilisation de nonce CCMP (déchiffrement) et le
    rejeu des trames de groupe.
  * **Correction :** accepter M3 seulement en `WAIT_M3` ; ne jamais réinstaller une clé identique.
* **P4. Wi-Fi : un M1 en clair forgé fige le compteur de rejeu.** *Certain.*
  * **Où :** `assoc.c:807-811`, `mlme.c:281-290`.
  * **Mécanisme :** l'EAPOL non protégé est accepté après l'installation des clés, et M1 (sans MIC) inscrit
    `s->replay`.
  * **Exploitation :** à portée radio et sans clé, un compteur de `0xFFFF…F0` provoque un déni de service durable
    (renouvellements refusés, trafic de groupe coupé).
  * **Correction :** inscrire le compteur seulement après un MIC vérifié ; refuser l'EAPOL non protégé une fois
    `keys_on`.
* **P5. hidmanager.** Même constat que G1, vu du côté des pilotes.
  * Par ailleurs, la boucle de lecture d'un pilote (`hidmanager.c:941-946`) se fie au `head` qu'écrit le pilote.
  * Un faux pilote fait tourner hidmanager environ 2⁶⁴ fois, et toute l'entrée du système se bloque.
* **P6. usbd : USB brut ouvert à tous.** *Certain (`usbd.c:290-386`).*
  * **Opérations :** `CONTROL`, `BULK`, `INTR_OPEN` et `ISO_OPEN` sur n'importe quel périphérique.
  * **Effets :**
    * commandes SCSI à la main sur une clé, en contournant vfs ;
    * reflashage du micrologiciel d'un périphérique ;
    * écoute d'un micro USB ;
    * avec un `END` forgé, lecture des rapports du clavier ;
    * course avec hwmanager à l'arrivée d'un périphérique.
  * **Correction :** réserver ces opérations au pilote que hwmanager a lancé pour ce couple (périphérique, interface).
* **P7. hwmanager** *(certain, `hwmanager.c:432-446`)*. `HW_OP_PCI` et `HW_OP_USB` forgés font lancer **avec droits**
  le pilote de `drivers.txt` sur une adresse choisie. N'accepter ces opérations que de pcid et usbd.
* **P8. netd : tout process devient une interface réseau** *(certain, `netd.c:386-420`)*. Avec une priorité forte,
  il reçoit tout le trafic sortant (DNS, HTTP, jetons) et peut y répondre. Réserver aux process à droits.

### Moyennes

* **P9. usbhid** *(certain, `hidparse.c:200-268`, `hidpad.c:119-127`)*. Positions et tailles prises du descripteur
  sans borne. Une clé piégée fait lire jusqu'à 512 Mo plus loin (faute ou processeur occupé), ou fait fuir la mémoire
  voisine. i2chid (`hidrd.c`) fait déjà ce qu'il faut : c'est le modèle.
* **P10. nvme** *(probable, `nvme.c:370-378`)*. Après un délai dépassé, ni Abort ni remise à zéro. Un DMA tardif
  peut écrire dans des pages déjà rendues (`READ_D` écrit en DMA directement chez le client).
* **P11. Son** *(certain, `sound.c:1310-1320`)*. `REC_OPEN` (micro) et `DRV_OUTPUT` (recevoir le mélange) sont
  ouverts à tous : écoute du micro, écoute des appels.
* **P12. wifisvc** *(certain, `wifisvc.c:755-787`)*.
  * `OP_COUNTRY` change le domaine réglementaire.
  * `JOIN`, `LEAVE` et `FORGET` permettent de basculer sur un réseau de l'attaquant.
  * Un SSID contenant `"` ou un saut de ligne s'injecte dans `/etc/wifi.txt` (`wifisvc.c:316-322`).
* **P13. Nonces Wi-Fi** *(certain, `assoc.c:112-131`)*. Faits de `rdtsc`, de l'heure et d'un compteur, sans RDRAND
  ni aléa du noyau.

### Faibles

* Pas de 802.11w (PMF) : déconnexions forcées par deauth.
* Élément RSN de M3 non comparé à celui de la balise.
* Groupe TKIP accepté sans le MIC Michael.
* `mic_ok` compare par `memcmp`.
* Deux octets lus de trop avec `pad` (`mvm.c:52`).
* `IG_OP_STOP` d'intelgpu ouvert à tous.
* La chaîne INQUIRY de usbstorage va telle quelle à la console.

---

## 5. Réseau

### 5.1 Pile TCP/IP (`src/svc/netd`)

| # | Gravité | Constat | Où |
|---|---|---|---|
| R1 | élevée | **`bind(127.0.0.1)` écoute sur toutes les interfaces**, et `accept` ne donne pas le pair. Exposés au réseau local : `webd --dev` (avec la liste des dossiers), `python -m http.server`, Node, Jupyter, les retours OAuth sur 127.0.0.1 des programmes Linux | `ops.c:197-216`, `posix/socket.c:182` |
| R2 | élevée | **DNS falsifiable.** Identifiant = `time_ms() & 0xFFFF`, port source séquentiel, source de la réponse non vérifiée, question non comparée | `stack.c:1453`, `1491-1511`, `572-577` |
| R3 | moyenne | NTP : pas de lien entre requête et réponse ; l'heure peut reculer de plusieurs années, ce qui rend de nouveau valides des certificats expirés | `bg.c:456-529` |
| R4 | moyenne | ARP : cache mis à jour par tout paquet, MAC apprise depuis l'IP, 16 entrées. Une trame suffit pour l'homme du milieu | `stack.c:147-153`, `249` |
| R5 | moyenne | DHCP : `xid` prévisible, ACK accepté de n'importe quel serveur, options lues jusqu'à 4 octets hors limites | `stack.c:411`, `454-512` |
| R6 | moyenne | 64 sockets pour tout le système, 1 Mo alloué dès le SYN, pas de SYN cookies : une soixantaine de connexions coupent tout le réseau | `netd.h:122`, `stack.c:530-541` |
| R7 | moyenne | Numéro de séquence initial inversible depuis une connexion (`(x ^ rdtsc) * k`), ports séquentiels, RST accepté dans une fenêtre de 64 Ko | `stack.c:674-677`, `1008` |
| R8 | moyenne | Ports < 1024 non réservés : un compte ordinaire se fait passer pour telnetd ou ftpd pendant une relance, et récolte le mot de passe | `ops.c:197-216` |
| R9 | faible | Destination ou source 127/8 acceptées depuis le réseau, écho ICMP en diffusion, identifiant IP séquentiel | `stack.c:246-330` |

**Correction de R1 :** garder `laddr`, filtrer dans `sock_find_port` et `tcp_input`, ajouter `getpeername`.

**Correction de R2, R3, R5, R7 :** tout prendre à `getrandom` (identifiant DNS et port source, `xid`, origine NTP),
SipHash pour le numéro de séquence initial, RFC 5961 pour les RST.

### 5.2 Serveurs (éteints par défaut sur PC, `skel/pc/etc/services.txt`)

| # | Gravité si activé | Constat |
|---|---|---|
| S1 | critique | **Un seul mot de passe pour telnet, FTP, VNC et RDP ; telnet et FTP en clair** (`svc/inc/remotepw.c`). Aucune longueur minimale. |
| S2 | critique | **ftpd travaille en uid 0 sur tout l'arbre** (`ftpd.c:192-205`). `RETR /etc/shadow.ini` et `STOR /sys/svc/...` marchent : prise de contrôle persistante. `V_OPEN_AS` n'existe pas. smbd sert lui aussi en uid 0 (moyenne). |
| S3 | élevée | **Force brute sans frein.** ftpd : `PASS` illimité. vncd et rdpd : un essai par connexion, sans délai. telnetd : 3 essais puis on se reconnecte. |
| S4 | élevée | **VNC ne garde que 8 caractères** du secret commun, en DES. L'échange de RDP est un HMAC-SHA256 à une seule itération : un dictionnaire hors ligne passe vite. Écran et frappes de RDP circulent en clair. |
| S5 | élevée | **`password =` dans `/etc/remote.txt` remplace le secret du coffre** (`remotepw.c`, `rp_read`). En mode log, tout process pose son mot de passe et active ftpd. |
| S6 | moyenne | RDP : toute connexion, avant authentification, coupe la session ouverte (`rdpd.c:1073-1081`). |
| S7 | moyenne | FTP : rebond `PORT` (SSRF, balayage) ; vol de la connexion de données PASV (port fixe, premier venu accepté). |
| S8 | moyenne | smbd et smbfs : signature seulement « activée », pas exigée ; tout compte authentifié voit tous les partages. |
| S9 | moyenne | `webd --dev` visible sur le réseau (voir R1) ; `/tmp/webd.pid` lu par `webd stop` élevé, qui tue le pid qu'on y a écrit. |
| S10 | faible | ftpd : `cwd[256]` et `rep[640]` débordent dans la structure de session, après authentification (`ftpd.c:36-41`, `368`). |
| S11 | faible | webd : `%0d%0a` permet d'injecter des lignes au journal. vncd : `SetEncodings` non borné. telnetd et VNC : blocage par des connexions inactives. |

**Prérequis avant d'activer un serveur :**

* `enforce` ;
* A1 et S5 corrigés ;
* authentification par comptes (authd), pas par un secret commun ;
* ftpd sous l'uid du compte et enfermé dans son `HOME:` ;
* telnet et FTP retirés ou passés sous TLS ; à terme SSH/SFTP.

### 5.3 Clients et services en ligne

* **O1, élevée, certain : oauthd n'est pas cloisonné par uid** (`oauthd.c:1824`, `handle()`).
  * Tout process local obtient le jeton d'accès de n'importe quel compte (Drive en écriture, GitHub `repo workflow`).
  * Un second utilisateur reçoit les jetons du premier.
  * **Correction :** une instance par uid, ou un contrôle de `SYS_PROC_UID`.
* **O2, faible :** l'écouteur de retour OAuth est sur `INADDR_ANY`. Un tiers peut faire échouer la connexion, mais
  pas voler le code (PKCE et `state`).
* **O3 :** wget ne fait que du HTTP, sans intégrité. Le client ftp envoie le mot de passe en clair.
* Les `client_id` publics de `providers.ini` sont acceptables. Le secret Google est injecté à la construction et
  reste lisible dans l'image, ce que Google admet pour une application de bureau.

---

## 6. Interface graphique et entrées

**G1. hidmanager : n'importe quel process capte toutes les frappes ou en injecte.** *Certain
(`hidmanager.c:1004-1021`).*

* **Capter :** `HID_OP_ABONNER` reçoit tout : mot de passe de login, d'authcard, de `sudo`.
* **Injecter :** `HID_OP_PILOTE` injecte clavier et souris. Il appuie sur « Entrée » pendant la carte d'élévation
  (« Autoriser » est le bouton par défaut), ou tape dans un terminal `sudo -s`.
* **Occuper :** il n'y a que 4 abonnés ; les prendre empêche Elegant de revenir.
* **Correction :**
  * `ABONNER` réservé à Elegant ; `PILOTE` réservé aux process à droits ;
  * pendant authcard et login, ignorer les pilotes virtuels.

**G2. Elegant (uid 0) : deux écritures hors bornes par débordement d'entier.** *Certain, relu.*

* `uikit_ops.c:31` (`WS_OP_CLIP_SET`) : `off + n > total` déborde, ce qui écrit jusqu'à 464 octets choisis avant
  `clip_buf`, dans le tas d'Elegant.
* `snapshot.c:136` (`WS_OP_WIN_THUMB`) : `off + tw*th*4 > size` déborde, ce qui écrit jusqu'à 1 Mo de pixels avant le
  tampon.
* **Effet :** le plantage de la session est certain ; une exécution de code en uid 0 est plausible.
* **Correction :** `off > total || n > total - off`, comme dans `menu.c:57`.

**G3. Imitation de l'écran de connexion et de la carte d'élévation.** *Certain.*

* `req_create` (`core.c:1397-1435`) accepte de n'importe qui `WF_TOP`, `WF_CURTAIN`, `WF_GRAB` et `WF_BACK`, en plein
  écran.
* login est en couche normale (`login.c:503`).
* **Correction :** ces drapeaux réservés à l'uid 0, et une couche sécurisée pour login et authcard.

**G4. Capture de l'écran et des fenêtres des autres.** *Certain.*

* **Où :** `snapshot.c:53` (écran entier), `snapshot.c:109` (miniature), `remote.c:107` (contenu d'une fenêtre),
  `remote.c:19` (liste avec les pid).
* **Correction :** réserver ces opérations à rdpd, au switcher, et à screenshot par élévation.

**G5. Autres opérations ouvertes à tous (moyennes).**

* `WS_OP_MENU_COMMAND` (`menu.c:146`) : déclenche une entrée du menu de l'app au premier plan (Supprimer, Envoyer…).
* `WS_OP_WIN_CLOSE` (`remote.c:211`) : ferme la fenêtre d'une autre app, y compris la carte d'élévation.
* `WS_OP_CLIP_GET`, presse-papier du noyau (N12) et clipd (`clipd.c:452-482`, les 10 dernières copies) : lisibles
  par tous.
* notifyd : le champ `app` est libre, on peut se faire passer pour une autre app ou pour le système.
* busd : publication sur n'importe quel sujet.

---

## 7. Paquets et outils

**K1. Course dans `/tmp/pkg` : installation de fichiers arbitraires par pkgd (uid 0).** *Certain, en `enforce`.*

* **Où :** `src/lib/pkg/pkglib.h:85`, `1128-1163`. L'archive est vérifiée (taille, SHA-256 de l'index signé), puis
  **rouverte par son nom** pour l'extraction.
* **Exploitation :** un compte ordinaire remplace le fichier entre les deux, pendant le tour automatique de pkgd. Il
  obtient l'écriture de fichiers arbitraires sous `/`.
* **Correction :** télécharger dans `/var/pkg/dl` (écriture uid 0 seulement), extraire depuis le fd vérifié ou depuis
  la mémoire.

**K2. Pas de protection contre le rejeu de l'index** *(moyenne, `pkglib.h:1024-1042`)*. Un ancien index signé, venu
d'un miroir ou d'un dépôt local, gèle les mises à jour de sécurité. **Correction :** numéro de série et date signés,
refus de tout index plus ancien que le dernier, expiration.

**K3. Clé privée de signature** *(moyenne)*.

* `C:\Projects\onyx_pkg-key.pem.txt` est en clair, sans mot de passe (`tools/pkg/pub.py:40`, `mkrepo.py:227`).
* Elle est aussi acceptée par la variable d'environnement `OnyxPkgKey` (agents du cloud) et écrite dans un fichier
  temporaire.
* Une seule clé pour toutes les plateformes, sans rotation.
* **Correction :** clé chiffrée, sous-clé révocable pour le cloud, plusieurs clés publiques acceptées.

**K4. Faibles.**

* `.opk` local : `name` du manifeste non validé, donc `remove_tree("/var/pkg/stage/../..")` est possible
  (`pkglib.h:1190`, `1308`).
* pkgd tue et relance des process par **nom** (`pkglib.h:918-945`). Un process nommé `pkgd` bloque les mises à jour.
* ArchiveKit `name_unsafe` (`ar_read.c:1251`) ne rejette ni `\` ni `:`. Ce n'est pas une traversée (seconde
  passe) : vfs ne découpe que sur `/`, et `:` n'est un préfixe qu'en tête de chemin. Reste un nom invalide pour Windows
  sur la partition FAT partagée.
* korin-mcp écrit ses journaux et captures dans `/tmp/korin-mcp*`, lisibles par les autres comptes. Sa liste
  `no_launch` n'est qu'un garde-fou.
* Outils d'essai livrés dans l'image :
  * dans `/sbin` : `crash`, `svcfault`, `memhog`, `dmareach`, `gpuhang` ;
  * dans `/bin` : `maptest` (avec `mapsvc`), `ramtest`, `directtest`, `usbtest`… et des pilotes (`iwl`, `intelgpu`,
    `i2chid`), voir `tools/pc/sysroot.sh:50-57`.

  Les sortir dans un paquet « devtests ».

---

## 8. Ce qui est bien fait

* **Entrée dans le noyau :**
  * `syscall`, retour par `iretq` (pas de piège SYSRET), trame complète restaurée ;
  * IDT en DPL 0, IOPL 0, TSS sans bitmap d'E/S ;
  * XSAVE assaini, FPU sauvée à chaque bascule.
* **Mémoire :**
  * toute page rendue est mise à zéro ; aucune fuite de mémoire noyau non initialisée trouvée ;
  * copie sur écriture et invalidations de TLB faites sous verrou avant libération ;
  * `user_check` borne `addr+len`.
* **Identité et privilèges :**
  * pid écrit par le noyau ; pids à génération ;
  * `SETUID`, `PROC_SETUID` et `SPAWN_AS` réservés à l'uid 0 ;
  * `KILL` vérifie l'uid ;
  * `rights` ne se transmet qu'à un enfant, ni par fork ni par exec ;
  * `SVC_REGISTER`, `DEV_MAP`, les IRQ et `IO_PORT` exigent `rights` ;
  * `DEV_MAP` refuse la RAM ; les MSI sont liées au pid.
* **Analyse de données non fiables :**
  * Wi-Fi par l'air : éléments bornés, MIC vérifié avant le déballage de la clé, `aes_unwrap` contrôlé ;
  * énumération USB : `bLength`, `wTotalLength`, profondeur des hubs ;
  * usbstorage, GPT avec CRC, i2chid ;
  * entrées IP, UDP et TCP de netd ; chemins de webd et smbd ;
  * messages d'Elegant bornés, sauf les deux de G2.
* **Cryptographie et secrets :**
  * TLS partout : `SSL_VERIFY_PEER`, `SSL_set1_host`, TLS 1.2 au minimum (oauthd, gdrivefs, cdmhost, pkg) ;
  * OAuth avec PKCE S256, `state` aléatoire, jetons au coffre ;
  * comparaisons en temps constant (authd, vncd, telnetd, keyvault) ;
  * keyvault : AES-256-GCM avec données associées, Argon2id 64 Mio, identité par l'empreinte de l'image ;
  * index des paquets signé en ECDSA P-256, SHA-256 par archive, `forbidden()`, aucun script d'installation.
* **Choix par défaut :**
  * serveurs éteints par défaut sur PC ;
  * pas de liens symboliques ni de setuid sur fichier ;
  * le coffre est refusé même en mode log.

---

## 9. Plan de correction proposé (première passe, remplacé par le §17)

**Vague 1 : quelques heures, gain maximal.** La plupart de ces corrections protègent aussi des bugs.

1. I1 : refus des codes d'avis dans le noyau, et vérification de `REQ_SIGNAL-1` dans les services.
2. N1, N3, N5, N6 : `SHM_TRUNC`, bornes ELF, FMASK, adresses canoniques.
3. G2 : deux tests de bornes dans Elegant.
4. F3 : casse dans `access.c`.

**Vague 2 : les contrôles d'appelant (un à deux jours).**

1. Un en-tête commun : `caller_uid()`, `caller_has_rights()`.
2. vfs : `PATHMAP`, `MOUNT`, `UNMOUNT` (F1, F2).
3. `fs.*` et `blk.*` (F4, P1).
4. pcid, usbd, hwmanager, netd, hidmanager (P2, P6-P8, G1).
5. Son, wifisvc, init2 (P11, P12, M1, M2).
6. Elegant : capture, drapeaux de couche, menus (G3-G5).
7. oauthd par uid (O1) ; clipd.

**Vague 3 : identité et session.**

1. Session hors de `/tmp` (A1) ; `startup.txt` du bon compte (A4).
2. authd : portée calculée, `AUTH_SUDO` lié à sudo et au terminal, compteur d'échecs, Argon2id (A2, A3, A7).
3. Images réservées aux process de confiance (N2, N4) ; identité des scripts posée par le noyau (A6).
4. pkg : téléchargement hors de `/tmp` (K1), numéro de série de l'index (K2).
5. sudo : environnement nettoyé, argv.
6. Puis **`access = enforce` par défaut**, après l'essai en mode journal sur le ZBook prévu par l'étape 5.

**Vague 4 : robustesse et réseau.**

1. Parseurs : ext4 (`bpg`, `ipg`), ramfs, boucles FAT et exFAT, usbhid, noms venus du disque (F5, P9).
2. Wi-Fi : EAPOL et nonces (P3, P4, P13).
3. netd : `bind` sur une adresse et `getpeername` (R1) ; DNS, DHCP et NTP aléatoires et vérifiés (R2, R3, R5) ;
   ports < 1024 réservés (R8) ; quotas et SYN cookies (R6) ; numéro de séquence par SipHash (R7).
4. Noyau : courses N7 et N8, quotas par uid (N13), aléa (N14), NX sur la carte physique (N9).

**Vague 5 : le plan de SECURITE.md**, sur ces bases : SMEP, SMAP et UMIP (la table d'exceptions de N7 est un
prérequis), `unveil` pour WebContent, ASLR (après N14), `pledge`. À long terme : droits par fonction PCI et VT-d
(N15).

**Avant d'activer un serveur d'accès à distance :** S1 à S5 et A1.

**Vérification :** un programme d'essai `sectest`, prévu par SECURITE.md, qui tente chaque contournement de ce
document (avis forgé, `PATHMAP`, casse, `fs.*` direct, `blk.*` brut, abonnement HID, `session.ini`, ELF piégé,
`SHM_TRUNC`) et doit être refusé partout. Il servirait de test de non-régression.

---

# Seconde passe (24/11/2026)

Cinq lectures de plus, toujours sans rien exécuter :

* l'inventaire de tous les appels système ;
* le modèle de menace de Jet et du code étranger ;
* les services et kits que la première passe n'avait pas lus ;
* la chaîne de démarrage, d'installation et de mise à jour ;
* une contre-lecture adverse de la première passe.

Les constats nouveaux les plus graves ont été relus dans le code, comme pour la première passe :

* `CHUNK_FREE` d'intelgpu ;
* le nom des objets du cache de fichiers de libposix ;
* `CR0.WP` ;
* la règle d'ESP d'osinstall ;
* `sys_kill` ;
* `new_pid` ;
* les messages vides de `CHAN_SEND` ;
* la fenêtre d'`op_attach` ;
* `image_map` ;
* `fsowner.c` ;
* `op_slave` de vterm ;
* le PNG de UIKit ;
* `haspw` de login.

Numérotation des nouveaux constats :

| Préfixe | Domaine |
|---|---|
| N18 à N29 | noyau |
| J | Jet |
| X | transverse |
| L | code étranger |
| B | démarrage |
| D | services et kits |

## 10. Chaînes d'attaque

D'une app non privilégiée (ou de WebContent compromis) jusqu'au noyau ou à l'uid 0, par ordre de simplicité :

| # | Mode | Étapes | Résultat |
|---|---|---|---|
| C1 | `log` (défaut) | écrire `/sys/svc/x`, puis `INIT_OP_LAUNCH /sys/svc/x` (M2) : x a `rights`, puis `DEV_MAP` ou `DMA_ALLOC` sans IOMMU (N15) | anneau 0, sans course ni bug mémoire |
| C2 | `log` | compte sans mot de passe : `AUTH_SUDO ""` (A3) ; sinon ajouter son compte à `/etc/elevate.ini` | uid 0, puis C1 |
| C3 | **`enforce`** | `BUF_CREATE` 4 Ko, `BUF_GRANT` au pilote, `ATTACH` déclaré à 64 Mo, `BLK_READ` vers une adresse au-delà du tampon (P1) : DMA dans la RAM physique, à une adresse connue par `BUF_INFO` | anneau 0 en quatre appels |
| C4 | `enforce` | `VFS_MOUNT` avec `../` (F2) : code en uid 0 (sans droits, tué en 3 s : il lance un fils), puis il écrit `/sys/svc/y`, puis C1. Variante : `PATHMAP /sys` vers son dossier (F1) | uid 0, puis anneau 0 |
| C5 | `enforce` | `IMAGE_CREATE("/apps/jet.app/main")` avec le vrai contenu, `fork`, scellement, lancement ; keyvault hache une image conforme, puis le fils réécrit le code par la fenêtre héritée (N2, empreinte paresseuse `image.c:113-125`) | jetons du coffre au nom de Jet |
| C6 | entre comptes | faux `/bin/sudo` en cache (N4), ou `HID_OP_ABONNER` puis `HID_OP_PILOTE` (G1) | mot de passe d'un autre compte, puis uid 0 |
| C7 | noyau | `SHM_TRUNC` (N1), `BUF_FREE` en double (N8), fenêtre d'image (N2), `CHUNK_FREE` (J7), usage après libération d'un process (N18, N19) : pages libérées encore mappées, réutilisées comme tables de pages. Ou `image_map` (N3) : lecture de toute la RAM | anneau 0 |
| C8 | Jet | bug JSC, puis entrée dans le tas JIT RWX (J2), puis lecture de `HOME/jet/webkit/cookies.db` et `connect()` (J1, J3) | **compte Google volé sans aucune élévation**, en secondes |

**Persistance**, par ordre de discrétion :

1. `/etc/web-probe.js` (J4) ;
2. clé publique et dépôt de pkg dans `/etc/pkg` : pkgd (uid 0, chaque jour) installe les paquets de l'attaquant (L4) ;
3. `local.tar` sur l'ESP (B2) ;
4. `HOME:/etc/startup.txt` ;
5. le module Widevine dans `HOME` (J5).

**Gravités revues par la contre-lecture :**

* **Sous-estimées :** P1 (DMA direct), N8 (critique), N3 côté `image.c` (critique), N2 (contourne le coffre), M2
  (passerelle de C1, critique), F1 (remapper `/sys` ou `/bin`).
* **Surestimées :**
  * N7 : déni de service seulement, que N5 donne déjà ;
  * la variante `snapshot.c` de G2 : plantage, exécution peu probable ;
  * R7 : on ne tire de l'ISN que le TSC ; l'injection passe plutôt par la fenêtre RST de 64 Ko ;
  * le repli en uid 0 de `INIT_OP_LAUNCH` : les requêtes en file d'un process mort sont supprimées, et LAUNCH donne déjà
    les droits ;
  * F5 par simple branchement d'une clé.
* **Nuancés :**
  * F3 : contournement en lecture seulement ;
  * P2 : la prise et le déni de service sont certains ; l'écriture en RAM par MSI n'est pas tranchée pour nvme, MSI-X
    étant dans un BAR ;
  * N4 : keyvault compare l'empreinte au fichier (`keyvault.c:910-916`), donc N4 seul ne trompe pas le coffre. Mais une
    correction de A2 qui vérifierait `/bin/sudo` par `SYS_PROC_EXE` seul serait contournée par N4 : il faut l'empreinte.
* **N9, processeur :** le ZBook a un i7-11800H (Tiger Lake-H).
  * Ne s'appliquent pas : Meltdown, L1TF, MDS, TAA, Retbleed.
  * S'appliquent : Spectre v1, Spectre v2 et BHI (eIBRS existe, mais Korin n'écrit pas `SPEC_CTRL`), et peut-être GDS
    et ITS selon le microcode.
  * L'absence de SMEP compte davantage : tout bug du noyau devient un saut vers du code utilisateur.

## 11. Jet et le code étranger

**Comment Jet tourne.** Tous les process de Jet lancent `/apps/jet.app/main`, avec leur rôle en argument
(`ProcessLauncherOnyx.cpp:155`) :

| Process | Nombre | Rôle |
|---|---|---|
| UI | 1 | fenêtre, UIKit |
| Réseau | 1 | curl, OpenSSL, cookies, cache |
| WebContent | 1 par onglet et par site | JSC avec JIT DFG/FTL, mise en page, Skia, **ANGLE et Mesa iris dans le process**, décodeurs, son |
| cdmhost | à la demande | Widevine, lancé **par WebContent** |

Il n'y a pas de process GPU. Tous ont l'uid de l'utilisateur, aucun n'a `rights`, et aucun n'est cloisonné.

| # | Gravité | Constat | Où |
|---|---|---|---|
| J1 | critique | **WebContent n'est qu'une app ordinaire du compte** : tout `HOME` en lecture et en écriture, tous les services joignables, `socket()` direct vers netd (exfiltration sans passer par le process réseau), environnement et fd sans `CLOEXEC` hérités | `ProcessLauncherOnyx.cpp:155` |
| J2 | élevée | **JIT en RWX à adresse fixe** : `ENABLE_SEPARATED_WX_HEAP 0`, pages `PROT_WRITE\|PROT_EXEC`, pas d'ASLR. Une primitive d'écriture de JSC suffit pour exécuter du code, sans ROP | `PlatformEnable.h:540`, `OSAllocatorPOSIX.cpp:81` |
| J3 | critique | **`cookies.db` en clair** (SID, `__Secure-1PSID`…), avec localstorage, indexeddb et mediakeys, dans `HOME/jet/webkit`, dossiers en 0755, lisibles par WebContent lui-même : une lecture vole la session Google (pas liée à l'appareil) | `engine_webkit.cpp:890-916` |
| J4 | critique en `log` | **`/etc/web-probe.js` injecté dans toutes les pages et tous les cadres**, au début du document. Un fichier écrit donne une persistance invisible dans Gmail et la banque. De même, `/etc/web-console` envoie la console vers `KLOG`, et `JET_CDMHOST` choisit le programme lancé | `engine_webkit.cpp:971-980`, `1041-1053` |
| J5 | élevée | **Module Widevine chargé depuis `HOME/.jet/widevine`** sans empreinte au chargement (vérifiée seulement au téléchargement). Le module propriétaire tourne avec tout `HOME` | `0009-eme-widevine.patch`, `module.cpp:40-65` |
| J6 | élevée | **Surfaces des pages accordées à tous** (`BUF_GRANT n, -1`). 128 numéros à essayer : tout process lit l'écran de Gmail (codes 2FA) ou y écrit | `webkit2_armpi.c:254`, `mm.c:899` |
| J7 | critique | **intelgpu `IG_OP_CHUNK_FREE` libère le tampon sans délier la PPGTT** (relu, `serve.c:298-310`). Le GPU écrit alors dans des pages rendues au pmm : WebContent, déjà client pour WebGL, atteint la RAM entière, puis l'anneau 0 | `src/svc/intelgpu/serve.c:307` |
| J8 | moyenne | Téléchargements automatiques sans confirmation dans `~/downloads`. Le bit d'exécution n'est pas une frontière : `SPAWN_IMAGE` lance des octets lus n'importe où | `engine_webkit.cpp:495-504` |
| J9 | correct | Schémas : aucun programme externe lancé, `file:` fermé aux pages http, `xdg-open` limité à http(s) | `engine_webkit.cpp:644-691` |
| J10 | faible | EME accordé sans demander : toute page fait charger le module propriétaire | `engine_webkit.cpp:756-762` |
| J11 | faible | Tubes vers les aides sans `CLOEXEC`, hérités par les process web | `engine_webkit.cpp:796-822` |
| J12 | moyenne | keyvault prend WebContent pour Jet (même image) : le jour où Jet gardera un secret au coffre, WebContent le lira | — |
| X1 | critique | **Le cache de fichiers projetés de libposix empoisonne du code entre comptes.** Les objets SHM sont nommés `/file-<FNV(nom de base)>-<taille>-<mtime>` (relu, `ipc.c:413-419`) et ouverts sans contrôle (N11). Un compte pré-crée le nom avec son contenu, ou projette l'objet existant en écriture partagée. Tout process .NET ou Python de tout uid, root compris, exécute alors son `System.Private.CoreLib.dll` ou sa `.so` | `src/lib/posix/ipc.c:397-500` |
| X2 | info | La capture de syscall de linuxhost n'est pas une frontière : un JIT ou un saut vers un `syscall` de libposix fait des appels natifs, et le module peut couper la traduction | `signal.c:512-571` |
| L1 | élevée | **linuxhost** : le programme Linux tourne dans le process hôte, avec l'uid de l'utilisateur, sans filtre. Ses serveurs « 127.0.0.1 » sont exposés (R1). Claude Code en mode `bypassPermissions` (que l'app sait choisir, `background.rs:482`) peut, par une injection de consigne dans une page lue par WebFetch, lancer un shell : une entrée « du web vers du code » sans aucun bug du moteur | `src/sys/linuxhost`, `src/lib/linux` |
| L3 | moyenne | Les runtimes de `/devtools` sont inscriptibles par tous en `log`, et exposés à X1 et A6 | — |
| L4 | critique en `log` | **Apps et paquets.** Une app n'a aucun droit déclaré. pkgd écrit en uid 0 partout sauf dans `var/`, `etc/wifi*` et `etc/remote*` (donc aussi `sys/`, `etc/shadow.ini`, `/boot`). La seule racine de confiance, `/etc/pkg/onyx.pub`, est inscriptible par tous en `log` | `pkglib.h:530-539` |

**La chaîne la plus courte** (installation par défaut, un compte) :

1. un bug du moteur ;
2. vol du compte Google, par `cookies.db` (J3) ou par `OA_TOKEN` d'oauthd (O1, jeton Drive complet), sans élévation ;
3. une seule IPC pour l'uid 0 (A3, I1 ou G1) ;
4. le noyau par J7, la voie la plus naturelle depuis WebContent ;
5. la persistance par J4 ou par pkg.

**Il ne faut aucune évasion de bac à sable.**

**Ce que `unveil` (étape 3 de SECURITE.md) doit laisser à WebContent**, posé dans `WebProcessMainOnyx.cpp` avant le
premier script :

* **En lecture :**
  * `/apps/jet.app/` ;
  * les polices, ICU et `/etc/ca-bundle*` ;
  * `/lib` ;
  * `/etc/web-gpu-*`, lus avant le verrou.
* **En lecture et écriture :** un `/tmp/jet-<pid>/` créé par l'UI.
* **Rien d'autre**, en particulier :
  * ni `HOME/jet/webkit`, ni `HOME/jet/cache`, ni `HOME/.jet/widevine`, ni `HOME/downloads` : le stockage passe par
    le process réseau, à vérifier pour IndexedDB et `mediakeys` ;
  * ni `/etc/web-probe.js`.
* **Aucune exécution** : cdmhost doit être lancé par l'UI.
* **Prérequis :** F1, F3 et F4 fermés, sinon `unveil` se contourne.

**Ce que `pledge` (étape 5) doit poser sur WebContent :**

* **Permis :**
  * mémoire, threads, futex, horloge, `getrandom` ;
  * IPC à poignées : `CHAN_*`, `HANDLE_*`, `SHM_CREATE`, `SHM_MAP`, `SHM_CTL` sans `PIN`.
* **Interdits :**
  * `SHM_OPEN` et `SHM_UNLINK` nommés (couper le cache de X1 dans ce process) ;
  * le réseau ;
  * `SPAWN*`, `FORK`, `EXEC`, `IMAGE_*` ;
  * tout ce qui demande `rights`, et `BUF_GRANT -1` ;
  * `KLOG`, `PROC_*` et `KILL` sur d'autres pid ;
  * `SVC_SIGNAL`, `WS_SIGNAL`, le presse-papier du noyau.
* **`SVC_OPEN` limité à une liste :** vfs, intelgpu, son en sortie seulement.

## 12. Noyau : inventaire des appels système

Chaque structure utilisateur est copiée une seule fois, sans double lecture (sauf le futex, voulu). Il n'y a pas
d'appel de type ptrace. Le noyau ne porte ni environnement ni cwd : ils passent par vfs et libposix.

**Légende :**

* **Contrôle :**
  * ∅ : aucun ;
  * R : `rights` ;
  * U0 : uid 0 ;
  * MU : même uid ou uid 0 ;
  * P : propriétaire de l'objet ;
  * S : soi seulement.
* **Verdict :** « ok », ou le constat qui s'y rapporte. Seules les lignes qui ne sont pas « ok » sont détaillées
  ici. Les appels sûrs sont listés à la fin.

| N° | Appel | Gestionnaire | Contrôle | Verdict |
|---|---|---|---|---|
| 01 | BUF_CREATE | mm.c:842 | ∅ | 64 Mo contigus, 128 pour tout le système (N13) |
| 04 | BUF_FREE | mm.c:919 | S | N8 |
| 05 | DMA_ALLOC | mm.c:951 | R | taille non bornée (N24) |
| 08 | VM_PROTECT | mm.c:616 | S | N10 |
| 0B | VM_STATS | mm.c:743 | ∅, tout pid | **N18** |
| 0D | BUF_INFO | mm.c:932 | ∅ | adresse physique de tout tampon |
| 12 | SPAWN_IMAGE | proc.c:490 | ∅ (R, U0 selon options) | N3, 256 Mo de kmalloc |
| 13 | WAIT | proc.c:242 | ∅, tout pid | faible |
| 14 | KILL | proc.c:290 | MU | **N18**, N27 |
| 15, 1F | PROCLIST(_FROM) | proc.c:317 | ∅ | N21 |
| 1A | SPAWN_PRELOADED | proc.c:738 | ∅ | N4 |
| 1E | PROC_INFO | proc.c:337 | ∅ | faible |
| 20, 24 | THREAD_CREATE | thread.c:25 | S | N6, threads illimités |
| 22 | FUTEX_WAIT | thread.c:196 | S | N7 |
| 29 | THREAD_KILL | thread.c:116 | S | **N18** |
| 2A | THREAD_LIST | thread.c:130 | ∅, tout pid | faible |
| 2B | SIG_ACTION | signal.c:44 | S | N6 |
| 2C | SIG_SEND | signal.c:192 | MU | **N18**, N23, N27 |
| 35 | SVC_SIGNAL | svc.c:482 | ∅ | N17 |
| 36 | SVC_MAP_CALLER | svc.c:356 | R | **N19** ; 64 vues pour tout le système |
| 39 | SVC_FORWARD | svc.c:499 | P | N16 |
| 40 | DEV_MAP | hw.c:30 | R | N15 |
| 46 | KIT_REGISTER | kits.c:157 | R | N3, N29 |
| 48 | IMAGE_FORGET | image.c:362 | ∅ | N4 |
| 4F | IO_PORT | hw.c:207 | R | N15 |
| 54 | WS_POST | ws.c:93 | ws | **N22** |
| 58, 59 | CLIP_SET, CLIP_GET | ws.c:208, 228 | ∅ | N12 |
| 5A | WS_SIGNAL | ws.c:82 | ∅ | N17 |
| 60, 61 | PRINT, KLOG | syscall.c:28, sys.c:200 | ∅ | faible |
| 63 | GETRANDOM | sys.c:133 | ∅ | N14 |
| 64 | LIB_OPEN | kits.c:182 | ∅ | N29 |
| 65 | IMAGE_LIST | image.c:386 | ∅ | chemins de tous les comptes |
| 66 | IMAGE_CREATE | image.c:260 | ∅ | N4, N24 |
| 67 | IMAGE_SEAL | image.c:320 | P | N2 |
| 68 | MEMBARRIER | syscall.c:44 | S | N26 |
| 69 | PROC_STATS | syscall.c:64 | ∅, tout pid | N25 |
| 6A | FORK | proc.c:534 | S, refusé avec R | N2 |
| 6B | EXEC | proc.c:592 | S, refusé avec R | N3, N19 |
| 6D | SETPGID | signal.c:419 | soi ou un fils | N28 |
| 6F | SETITIMER | signal.c:460 | S | **N18** (fil des alarmes) |
| 76 | CHAN_SEND | ipc.c:532 | P | **N20** |
| 79 | SHM_CREATE | ipc.c:850 | ∅ | 1 Go, pas de quota (N13) |
| 7A, 7B | SHM_OPEN, SHM_UNLINK | ipc.c:879, 927 | ∅ | N1, N11, X1 |
| 7C, 7D | SHM_CTL, SHM_MAP | ipc.c:961, 1020 | P | N10, N11 |
| 94 | PROC_EXE | proc.c:405 | ∅ | faible ; SHA-256 jusqu'à 4 Go à la demande |
| 95 | PROC_SET_SCRIPT | proc.c:439 | S | A6 |

**Appels jugés sûrs :**

* **Mémoire et threads :** SBRK, BUF_GRANT, BUF_MAP, VM_MAP (W+X déjà noté), VM_UNMAP, VM_ADVISE, VM_QUERY,
  VM_PAGEINFO, TLS_SET, EXIT, GETPID, PROC_FLAGS, SLEEP, YIELD, TIME_MS, CLOCK_GET, PROC_RIGHTS, THREAD_EXIT,
  FUTEX_WAKE, THREAD_JOIN, THREAD_SELF, SET_PRIORITY, SET_NAME.
* **Signaux :** SIG_RETURN, SIG_MASK, SIG_ALTSTACK.
* **Services :** SVC_REGISTER (R), SVC_OPEN, SVC_CALL, SVC_NEXT, SVC_REPLY, SVC_UNMAP_CALLER, SVC_CALLER_PHYS.
* **Démarrage et matériel (avec R ou propriétaire) :** MOD_*, BOOT_*, IMAGE_PRELOAD, IRQ_*, ACPI_TABLE, IRQ_MSI,
  POWER, CLOCK_SET.
* **Fenêtres :** WS_REGISTER, WS_CALL, WS_NEXT, WS_REPLY, WS_DISPLAY, EV_WAIT, SCREEN_INFO, WS_MODE.
* **Divers :** SYSINFO, GETPPID, GETPGID, CPU_LOAD.
* **Poignées et canaux :** HANDLE_*, CHAN_CREATE, CHAN_RECV, CHAN_SHUTDOWN.
* **Identité :** GETUID, SETUID (U0), PROC_UID, PROC_SETUID (U0), SYSCALL_CAPTURE.
* FW_CALL n'existe pas (E_NOSYS).

### Nouveaux constats du noyau

**N18. Pointeurs de process et de thread utilisés hors du verrou, sans référence : usage après libération.**
*Élevée ; probable (course rejouable), motif relu dans `sys_kill`.*

* **Le motif :** `proc_get` ou `thread_find` sous `sched_lock`, puis le verrou est rendu, puis le pointeur sert
  encore.
* **Où :**
  * `proc.c:294-302` (KILL) ;
  * `signal.c:203-233` (SIG_SEND, envoi à un groupe) ;
  * `thread.c:121-126` (THREAD_KILL) ;
  * `mm.c:746-766` (VM_STATS) ;
  * `signal.c:64-67`, `492-509` (alarmes, fin d'un fils).
* **Aggravation :**
  * `proc_finished` (`proc.c:210-234`) démonte `mm`, `vm`, `evq` et les poignées **avant** de vider `slot_of` ;
  * `vm_destroy` ne remet pas `p->vm` à 0 ;
  * `sched_reap` tourne sur plusieurs cœurs.
* **Scénario :**
  1. THREAD_EXIT de T2 pendant THREAD_KILL(T2).
  2. Le thread libéré est réoccupé par un message de `CHAN_SEND` au contenu choisi.
  3. `wake_locked` met un faux thread dans la file, et le noyau bascule sur un `arch_sp` choisi.
* **Effet :** exécution en anneau 0.
* **Correction :** agir sous le verrou, ou compter les références ; vider le slot d'abord dans `proc_finished`.
* **Protège aussi des bugs :** oui (pannes aléatoires sous 8 cœurs).

**N19. `SVC_MAP_CALLER` sur un appelant qui finit ou qui fait EXEC.** *Élevée ; probable.*

* **Où :** `svc.c:389-409` épingle les pages de `caller->mm` et `caller->vm` hors verrou, alors que EXIT ou EXEC les
  libèrent (`proc.c:687-695`).
* **Scénario :**
  1. Un service (fatfs, clipd) épingle 1 Mo, avec 257 fautes : la fenêtre est large.
  2. Pendant ce temps, l'app fait EXEC.
  3. Le service traduit des adresses par des tables de pages libérées, que l'app a reprises : il épingle et écrit une
     page physique choisie.
* **Effet :** lecture et écriture de toute la RAM.
* **Correction :** une référence sur `mm` et `vm`, attendue par EXIT et EXEC.

**N20. `CHAN_SEND` : messages sans borne, cycles, récursion sans fin.** *Élevée ; certain, (a) relu (`ipc.c:596-597`).*

* **(a) Messages vides :** un message de 0 octet n'est pas compté dans l'anneau et peut porter 256 poignées. La file
  devient infinie et épuise le tas du noyau.
* **(b) Cycles :** des bouts de canaux envoyés l'un dans l'autre ne sont jamais libérés (pas de ramasse-miettes).
* **(c) Chaînes :** une chaîne de 2 000 canaux imbriqués fait descendre `put_locked` et `free_msg` en récursion, sous
  `ipc_lock`. La pile du noyau (16 Ko, sans page de garde) déborde sur le tas.
* **Correction :**
  * un coût minimal par message ;
  * un plafond de poignées en transit par process ;
  * une libération itérative ;
  * un refus des bouts qui en contiennent d'autres, ou un ramasse-miettes.

**N21. Débordement de la génération des pids.** *Moyenne à élevée ; certain (relu, `proc.c:62`), long à atteindre.*

* **Mécanisme :**
  * `generation * 65536 + s + 1` devient négatif quand la génération atteint 32 768 ;
  * `proc_create` libère alors `p` sans vider `slot_of[s]` ;
  * le slot reste pour toujours sur de la mémoire libérée, que PROCLIST copie chez l'appelant ;
  * une fois réoccupée, `proc_get` rend un faux process : lecture arbitraire par VM_STATS.
* **Coût :** environ 65 000 process vivants (pas de quota), puis quelques heures de lancements.
* **Correction :** des pids sur 64 bits ; un slot vidé en cas d'échec.
* Le point « pids à génération » du §8 est à nuancer.

**N22.** `ws_proc_exit` fait `kfree(p->evq)` sans `sched_lock` pendant que `WS_POST` y écrit (`ws.c:248` contre
`ws.c:101-113`). *Moyenne.* Corrigé avec N18.

**N23.** Signal 64 : `BIT(64)` est un comportement indéfini, et la livraison lit `p->act[-1]`, qui contient des données
du process (`signal.c:23`). *Faible*, le process reste dans son propre uid.

**N24.** Chevauchements de l'espace d'adresses (`image.c:29-31`, `ws.c:27`, `svc.c:42`) :

* les fenêtres d'image n° 1 et suivantes recouvrent `WS_FB_VA` et `MAPC_BASE` ;
* `dma_next` et `dev_next` croissent sans borne ;
* `vm_map_page` écrase une entrée présente sans rien dire.

*Faible* (process à droits seulement). Protège aussi des bugs.

**N25.** `PROC_STATS` sur tout pid donne un canal auxiliaire : on suit le rythme des lectures de login, authd ou sudo
pendant la saisie d'un mot de passe. *Faible.*

**N26.** `MEMBARRIER` envoie une interruption à tous les cœurs à chaque appel, sans limite de débit. *Faible.*

**N27.** L'envoi d'un signal à un groupe s'arrête à 64 process : un `kill -9 -pgid` laisse vivre une bombe de fork.
Un process d'uid 0 **sans droits** (app élevée, ftpd) peut envoyer SIGSTOP à vfs. *Faible.*

**N28.** `SETPGID` accepte un groupe qui n'existe pas dans la session (`signal.c:438-441`). *Faible.*

**N29.** Kits :

* `kits.c:78` : `vaddr + memsz` peut déborder, et l'écriture part loin dans la RAM (R ou initrd seulement) ;
* `map_into` hors verrou.

*Faible.*

## 13. Démarrage, installation et mises à jour

| # | Gravité | Constat | Où | Correction |
|---|---|---|---|---|
| B1 | critique | **Les partitions KORIN OS et KORIN HOME sont des « basic data » FAT32/exFAT, sans attribut** : tout compte Windows, même non admin, écrit `\korin\kernel.elf`, `\sys\svc\*`, `\etc\elevate.ini`, et lit `shadow.ini`, `machine.key` et tous les homes | `osinstall.c:1548-1549`, `gpt.h:28`, `korin.ps1:62-82` | attributs GPT « pas de lettre » et « cachée », ou un type GUID propre à Korin ; à terme, chiffrement |
| B2 | élevée | **Aucune intégrité au démarrage** : Secure Boot coupé, le chargeur ne vérifie que l'en-tête ELF. `local.tar` passe avant l'initrd et peut fournir `init`, `init.txt` ou `nvme.txt`, sans appartenir à aucun paquet. `/boot` se monte par `VFS_BOOT_BEGIN`, ouvert à tous | `bootx64.asm:217-330`, `menu.inc:369-380`, `initrd.c:97-107`, `vfs.c:1059` | empreintes signées vérifiées par le chargeur ; `local.tar` sans exécutables ; à terme shim et MOK, Secure Boot actif |
| B3 | élevée | **L'installation à côté de Windows ouvre en écriture l'ESP de Windows** (`write = guid=<ESP>, type=efi`, relu), contrairement au commentaire de `policy.c`. Avec P1 ou `VFS_BOOT_BEGIN`, on remplace `bootmgfw.efi` : bootkit Windows depuis Korin | `osinstall.c:1800`, `nvme/policy.c:18-21` | n'ouvrir que `\korin` et `\EFI\Korin`, ou une ESP propre ; `VFS_BOOT_BEGIN` réservé à l'uid 0 |
| B4 | élevée | **La console de secours (Elegant tombé 3 fois, ou ws muet 20 s) donne un shell sans mot de passe**, sous le premier compte (ou en uid 0 avec A1), sans regarder si une session a été ouverte | `init2.c:270-276`, `815-820`, `console.c:109`, `users.c:124-151` | demander compte et mot de passe (`AUTH_VERIFY`) ; jamais d'uid 0 |
| B5 | élevée | **Mise à jour du noyau ni atomique ni réversible** : `.old` puis copie d'un volume à l'autre (effacer, copier, effacer), `korin.efi` et `initrd.tar` effacés avant d'être recopiés, aucune entrée « version précédente », pas de nouvelle vérification SHA-256 au commit | `pkglib.h:1428-1446`, `stat.c:398-412` | `.new` puis renommage dans le volume ; A/B avec compteur d'essais ; SHA-256 au commit |
| B6 | moyenne à élevée | **`CR0.WP` n'est jamais posé** (relu, `cpu.c:167-170`) : il vaut 0 sur les cœurs secondaires. `copy_to_user` peut donc écrire, après une course, dans une page partagée en lecture seule (code d'un programme uid 0 préchargé) | `cpu.c:167-170`, `smp_trampoline.asm:52-54` | `cr0 \|= 1<<16`, prérequis de SMEP et SMAP |
| B7 | moyenne | Clé de machine scellée dans le TPM sans PCR ni autorisation : Windows (TBS) ou une clé USB la descelle | `tpm.c:322`, `359` | autorisation propre à l'objet ; PCR 7 après B2 |
| B8 | moyenne | osinstall installe les paquets de la clé sans `index.sig` ni SHA-256, contrairement à INSTALLATEUR.md | `osinstall.c:587-610`, `719-727` | passer par `pkg --root … add` |
| B9 | moyenne | Le moniteur du noyau (`run` avec droits, sans authentification) s'ouvre par l'option `monitor`, par un initrd sans `init` (B5), ou si `/` manque 45 s | `monitor.c:67-89`, `pc_main.c:303-312`, `412` | en secours, seulement `info`, `reboot` et `poweroff` |
| B10 | moyenne | Valeurs par défaut d'osinstall : `autologin = yes`, `admin = yes`, `passwords = none`. `access` reste en `log`. Le retour de `getrandom` est ignoré pour les GUID et les sels | `osinstall.c:1418-1425`, `258-261` | mot de passe imposé à un admin ; `enforce` ; vérifier `getrandom` |
| B11 | moyenne | **Pas d'IOMMU, même pas détectée** (aucune trace de DMAR). `PCI_OP_RESCAN` est ouvert à tous, et un périphérique Thunderbolt ou PCIe qui se dit NVMe obtient un pilote à droits et le bus master | `pcid.c:473` | VT-d avec domaines par fonction ; confirmation avant d'annoncer une fonction branchée à chaud |
| B12 | moyenne | Pas de journal d'audit persistant : un anneau de 64 Ko en RAM, `PRINT` ouvert à tous, un refus noté une seule fois sur 32 | `sys.c:169`, `access.c` | `/var/log/auth.log` en ajout seulement, tenu par un service uid 0 |
| B13 | faible | Le menu cherche « Windows » sur tous les volumes, clés USB comprises, ce qui contourne le verrou de démarrage USB du firmware | `menu.inc:818-824` | seulement le disque de démarrage |
| B14 | faible | `BLK_PLAN` sans contrôle : dans la session live, toute app peut effacer le disque interne | `blkserv.c:748-760` | réservé à l'uid 0 |
| B15 | faible | Ni bit MOR ni effacement des clés à l'arrêt (attaque de démarrage à froid) | — | MOR ; effacer les clés du coffre |
| B16 | info | Les pages du trampoline SMP (PML4 provisoire RWX) restent sous 1 Mo, atteignables seulement par DMA | `smp.c:174-186` | les effacer après `smp_start` |

**Ce qui est correct :**

* NX est actif avant le premier process ;
* la mémoire du firmware est rendue proprement ;
* `kernel_option` est ignorée avant `BOOT_READY`, et aucune option ne coupe une protection ;
* le menu ne permet pas d'éditer la ligne de commande ;
* les mots de passe ne passent jamais en clair à l'installation.

## 14. Services et kits restants

**Transverse (D2).** AppKit demande `rights` pour tout fichier de `/sys` (`appkit.c:1320`). Elegant, qui a `rights`,
lance la session : voronoy, menubar, panel, notifyd, quickmenu, soundmenu, stickies, clockd et printd tournent donc
**avec `rights` sous l'uid de l'utilisateur**. *Probable fort.* Toute faille mémoire de ces programmes est une faille
du noyau (N15), et keyvault croit sans empreinte le chemin d'un process à `rights` (`keyvault.c:910`). **Correction :**
`spawn_session` lance sans `rights` ; `rights` seulement pour une liste fermée de services et pilotes.

| # | Gravité | Constat | Où | Correction |
|---|---|---|---|---|
| D1 | élevée | **vterm : n'importe quel process, de n'importe quel compte, s'attache à l'esclave de n'importe quel pty** (relu) : il lit les lignes tapées (mot de passe de sudo), écrit un faux invite, rallume l'écho et occupe le pty | `vterm.c:831-841`, `posix/tty.c:341` | retenir l'uid du créateur ; `VT_SLAVE` au même uid ou aux process de la session |
| D2 | élevée | Programmes de session avec `rights` (ci-dessus), par exemple voronoy qui décode le fond d'écran de `HOME` | `voronoy.c:78-80` | voir ci-dessus |
| D3 | moyenne (élevée avec D2) | **Décodeur PNG de UIKit** : largeur et hauteur non bornées, `rsize = (pitch+1)*h` déborde (relu), puis `png_unfilter` écrit des données choisies hors du tas. Index de palette PNG et BMP non bornés, IHDR lu même si trop court. Touche Fichiers, les aperçus, voronoy et Courier | `uk_image.c:662-673`, `564`, `165` | IHDR de 13 octets, `bits` valides, `w` et `h` ≤ 16384, multiplication contrôlée, index < taille de la palette |
| D4 | moyenne | **vterm envoie des signaux en uid 0 à n'importe quel groupe** : `VT_SPGRP` accepte tout pgid, puis `^C`, `^Z` ou `VT_CLOSE` le signalent au nom de vterm, ce qui arrête par exemple un `sudo pkg` en cours | `vterm.c:973-979`, `286` | groupe de la session du pty seulement |
| D5 | moyenne | 16 pty pour tout le système : 16 `VT_OPEN` bloquent Terminal, telnet et la console | `vterm.c:97` | plafond par uid |
| D6 | moyenne | **printd est partagé et tourne sous l'uid du premier qui imprime** : `PD_SUBMIT` écrit un PDF à la place d'un fichier quelconque de ce compte, `PD_CANCEL` accepte tout id, `/var/spool/print` est en 0777, `PD_ADD` sert de SSRF et analyse IPP avec `rights` | `printerkit.c:370`, `main.cpp:392-613` | une instance par uid ; `output` limité au home de l'appelant |
| D7 | moyenne | **keyvault prend « illisible » pour « absent »** et crée un nouveau coffre en effaçant l'ancien. `KV_SET` n'a pas de plafond : une app fait grossir `<uid>.db` jusqu'à l'échec de lecture, et tous les secrets du compte disparaissent. L'écriture n'est pas atomique (`unlink` puis `rename`) | `keyvault.c:647`, `293` | créer seulement sur ENOENT ; plafonds ; renommage atomique |
| D8 | moyenne | **`KV_UNLOCK` sert d'oracle du mot de passe de session** (celui de sudo pour un admin), et Argon2id plus `SLEEP 1000` sur l'unique fil bloque le coffre de tous | `keyvault.c:1123-1131`, `1094`, `1268` | réservé à authd et à la carte ; compteur d'échecs sans dormir |
| D9 | moyenne | init2 `SVC_STOP` et `SVC_START` acceptent `uid <= 0` : un demandeur mort passe, et arrête vfs, authd ou netd | `init2.c:620` | `uid == 0` strict |
| D10 | moyenne | **login ouvre la session sans mot de passe si authd ne répond pas** (relu) : une erreur de `AUTH_HASPW` vaut « pas de mot de passe », et `shadow.ini` illisible rend 0 | `login.c:496-501`, `authd.c:719` | une erreur vaut « mot de passe demandé » |
| D11 | moyenne | `/tmp` est partagé sans propriétaire : `mkstemp` en 0600 n'a aucun effet, `O_EXCL` est émulé par `stat` puis `open`, `tempname.c` retombe sur `rand()` | `access.c:5`, `posix/file.c:84` | `TMPDIR=/tmp/<uid>` protégé ; `O_EXCL` atomique dans vfs |
| D12 | moyenne | smbfs ne filtre pas les noms du serveur (`a/../../.bashrc`, caractères de contrôle) | `smbfs.c:698` | comme `korin_name` de gdrivefs |
| D13 | faible | Le Mixer se lance **en uid 0 depuis l'écran de connexion** (réglages rapides, puis son, puis Mixer…) | `soundmenu.c:250` | ne rien lancer avant la connexion |
| D14 | faible | acpid `PROFILE` et `RELOAD` ouverts à tous (réécriture de `/etc/power.ini` par l'uid 0) ; i2chid `OP_STOP` ouvert à tous | `acpid.c:592`, `i2chid.c:690` | contrôle d'uid |
| D15 | faible | busd : `korin.power.button` forgé ouvre l'extinction, `korin.clock.offset` fausse les alarmes ; sujets et abonnés jamais rendus | `panel.c:596` | préfixes réservés, plafond par pid |
| D16 | faible | `WS_OP_DROP_GET` : le dernier glisser-déposer est lisible par tous | `uikit_ops.c:844` | réservé à la fenêtre cible |
| D17 | faible | keyvault : en-tête (`mkib`, `t`, `lanes`) lu sans borne ; pas d'anti-rejeu du `.db` ; mot de passe différé non effacé par `expire()` ; cache d'empreintes par (chemin, mtime à la seconde, taille) | — | bornes, HMAC de l'en-tête, `OPENSSL_cleanse` |
| D18 | faible | gdrivefs : `api_url` lu dans `HOME:/gdrive/drives.ini`, donc le jeton Bearer peut partir vers n'importe quel hôte | `gdrivefs.c:617` | seulement `*.googleapis.com` |
| D19 | correct | Le Terminal avale OSC, DCS, APC et PM, et ne répond qu'à DA et DSR avec un contenu fixe : pas d'injection d'entrée. À garder ainsi | `vt.c:450`, `772-815` | — |
| D20 | info | À l'arrêt, init2 épargne les process par **nom** | `init2_x86_64.c:37` | — |

**Correct :**

* Elegant ne décode aucune image ;
* la cryptographie de keyvault (IV GCM aléatoire, données associées, HKDF, `OPENSSL_cleanse`, `KVF_PARENT` et le
  gestionnaire vérifiés) ;
* libposix : `system()` et `popen()` sans shell, `mkstemp` par `getrandom` ;
* les noms de gdrivefs ;
* DocumentKit ;
* les bornes d'ArchiveKit ;
* mapsvc, imgblk, hda, ps2kbd et rtsx sans IPC à eux.

## 15. Erreurs de la première passe, corrigées ci-dessus

* **Gravité et portée :**
  * N3 côté `image.c` : projection d'une page choisie, pas une #GP ;
  * P1 : DMA direct en RAM ;
  * F3 : lecture seulement ;
  * F4 : usurpation de vfs par le nom du process.
* **Faits inexacts :**
  * sudo : pas de `BASH_ENV` ;
  * ArchiveKit : pas une traversée ;
  * F5 : ext4 n'est pas monté automatiquement.
* **Lignes :** `elf.c:45` (et non 46), `vm_fork` aux lignes `vm.c:243-259`, `deliver` à `signal.c:244`, appelé sous
  verrou aux lignes 314 et 327.

## 16. Pour `sectest`

En plus des essais du §9, chacun doit être refusé :

* **IPC et noyau :**
  * `ATTACH` déclaré plus grand que le tampon ;
  * `CHAN_SEND` de 0 octet en boucle ;
  * chaîne de 2 000 canaux imbriqués ;
  * THREAD_KILL pendant THREAD_EXIT ;
  * `CHUNK_FREE` d'un morceau encore lié ;
  * objet `/file-*` pré-créé ;
  * `BUF_MAP` d'une surface de Jet par un autre process.
* **Terminal :** `/dev/pts/N` d'un autre compte ; `VT_SPGRP` vers un pgid étranger.
* **Données :** PNG `2³¹+23170 × 2³¹−23170`.
* **Services :** `KV_UNLOCK` en boucle ; `SVC_STOP` suivi de la sortie immédiate du demandeur ; `AUTH_HASPW` avec
  authd arrêté.

## 17. Plan révisé (remplace le §9)

L'ordre suit la gravité, puis le coût. « Bugs » indique que la correction protège aussi des bugs.

**Vague 0 : avant de publier une version (un jour), ce qui ne coûte presque rien.**

1. **Ne jamais livrer `access = log` avec des données réelles.** Tant que les vagues 1 à 3 ne sont pas faites, le
   dire dans la documentation et à l'installation.
2. Corrections de quelques lignes :
   * B1 : attributs GPT ;
   * B3 : règle d'ESP restreinte, `VFS_BOOT_BEGIN` réservé à l'uid 0 ;
   * J4 : `web-probe.js` retiré de la version publiée ;
   * L4 : clé publique de pkg compilée dans pkglib ;
   * D10 : `HASPW` en erreur vaut « mot de passe demandé ».

**Vague 1 : quelques heures, une ligne à dix lignes chacune.**

| Correction | Constat | Bugs |
|---|---|---|
| refus de `op >= 0xFFF0` dans `SVC_CALL` et `SVC_FORWARD`, et `REQ_SIGNAL-1` vérifié dans les services | I1 | oui |
| `SHM_TRUNC` traité comme `SET_SIZE(0)` | N1 | oui |
| bornes de `elf.c`, `image.c` et `kits.c`, adresses canoniques | N3, N6, N29 | oui |
| `FMASK` avec NT, `CR0.WP` | N5, B6 | oui |
| taille réelle du tampon et propriétaire dans `op_attach` | P1 | oui |
| `CHUNK_FREE` qui délie la PPGTT | J7 | oui |
| messages vides comptés | N20a | oui |
| deux bornes d'Elegant | G2 | oui |
| bornes du PNG | D3 | oui |
| casse dans `access.c` | F3 | oui |
| `uid == 0` strict dans init2 | D9, M2 | oui |
| signal 64 | N23 | oui |

**Vague 2 : les contrôles d'appelant (un à deux jours).**

1. Un en-tête commun `caller_uid()` et `caller_has_rights()`, avec `rights` par pid dans le noyau.
2. vfs : `PATHMAP`, `MOUNT`, `UNMOUNT` (F1, F2) ; `fs.*` et `blk.*` liés au pid de vfs donné par le noyau, pas par
   son nom (F4, P1, B14).
3. Pilotes : pcid, usbd, hwmanager, netd, hidmanager (P2, P6-P8, G1, B11).
4. Son, wifisvc, acpid, i2chid (P11, P12, D14) ; init2 `REBOOT` et `LAUNCH` (M1, M2).
5. Elegant : capture, couches, menus, glisser-déposer (G3-G5, D16) ; presse-papiers (N12, clipd).
6. vterm : uid du créateur, pgid de la session, plafond par uid (D1, D4, D5).
7. Services par uid : oauthd (O1), printd (D6) ; keyvault `UNLOCK` (D8).
8. Surfaces de Jet accordées à deux pid (J6) ; noms SHM avec propriétaire, cache `/file-*` créé par vfs (N11, X1).

**Vague 3 : identité, session et noyau (quelques jours).**

1. **Noyau :**
   * références sur les process, les threads et les espaces d'adresses ;
   * ordre de `proc_finished` ;
   * N2 et N8 ;
   * pids sur 64 bits.

   Constats : N18, N19, N21, N22, N2, N8. Bugs : oui (pannes SMP).
2. **Images :** réservées aux process de confiance (N4) ; identité des scripts posée par le noyau (A6).
3. **Session :**
   * session hors de `/tmp` (A1) ;
   * `startup.txt` du bon compte (A4) ;
   * console de secours authentifiée (B4) ;
   * `rights` retiré aux programmes de session (D2) ;
   * Mixer avant la connexion (D13).
4. **authd :** portée calculée et liée à l'empreinte de `/bin/sudo` (A2) ; terminal exigé sans mot de passe (A3) ;
   compteur d'échecs ; Argon2id (A7).
5. **Paquets :** téléchargement hors de `/tmp` et extraction depuis le fd (K1) ; numéro de série de l'index (K2) ;
   `forbidden()` étendu (L4) ; vérification à l'installation (B8).
6. **keyvault :** pas de création sur une erreur, plafonds (D7).
7. **Installation :** valeurs par défaut sûres (B10).
8. **Puis `access = enforce` par défaut.**

**Vague 4 : robustesse, Wi-Fi, réseau, démarrage.**

* **Parseurs :** ext4 et ramfs, boucles FAT et exFAT, usbhid, noms venus des disques et des serveurs (F5, P9, D12).
* **Wi-Fi :** EAPOL et nonces (P3, P4, P13).
* **netd :** R1 à R9.
* **Noyau :**
  * quotas par uid (N13) ;
  * aléa (N14) ;
  * IPC (N20b, N20c) ;
  * NX sur la carte physique, `SPEC_CTRL` et eIBRS (N9).
* **Démarrage :** mise à jour A/B (B5) ; journal d'audit (B12) ; moniteur de secours (B9) ; `TMPDIR` par compte
  (D11).

**Vague 5 : le plan de SECURITE.md**, sur ces bases :

* SMEP, SMAP et UMIP (prérequis : B6 et la table d'exceptions de N7) ;
* `unveil` et `pledge` de WebContent selon le §11 ;
* ASLR (après N14), puis JIT W^X (J2) ;
* cookies chiffrés par le coffre (J3) ;
* cdmhost lancé par l'UI et vérifié (J5) ;
* intégrité du démarrage et Secure Boot (B2, B7) ;
* VT-d (N15, B11).

**Avant d'allumer un serveur d'accès à distance :** S1 à S5, A1 et R1.
