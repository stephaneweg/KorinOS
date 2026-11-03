# IPC : canaux, files par service, droits d'accès, notifications

> **Remplacé le 30/10/2026** par décision de l'utilisateur : ce plan n'est pas suivi. Ses étapes 1 à
> 6 ne sont pas sur `main`. À la place, le chantier perf (FEUILLE-DE-ROUTE, « Chantier perf ») :
> mot de signaux par service, present asynchrone, `WEV_FRAME`, signaux POSIX, `membarrier` (faits),
> puis IRQ des pilotes, mappage de la mémoire de l'appelant, DMA de la SD.

Conception d'un chantier du noyau (`src/kernel/svc.asm`, `threads.asm`, `sched.asm`), issue de la
comparaison avec les micronoyaux (QNX, Minix 3, seL4, Zircon, Genode) du 29/10/2026. Rien n'est
encore écrit ; les numéros d'appels et d'ops sont des propositions.

## 1. L'existant (ce que le code montre)

| Point | Où | Conséquence |
|---|---|---|
| 32 services (`SVC_MAX`), 128 requêtes en cours pour tout le système (`SR_MAX`) | `svc.asm` | un process qui multiplie les threads bloqués dans `SVC_CALL` épuise les emplacements : vfs, Elegant, le son reçoivent `E_BUSY` |
| Un seul verrou `svc_lock` (`irqsave`) pour tout l'IPC | `svc.asm` | les 4 cœurs se sérialisent sur chaque appel, chaque prise de requête, chaque réponse |
| Le canal est le numéro global du service ; `svc_call_k` ne vérifie que `< SVC_MAX` | `svc.asm`, `svc_call_k` | `SVC_OPEN` ne protège rien : on peut appeler 0–31 sans avoir ouvert |
| `SVC_OPEN` ouvert à tous | `sys_svc_open` | une application appelle `blk.*` ou `fs.*` directement |
| `blkserv.inc` ne regarde pas le pid de l'appelant | `src/svc/inc/blkserv.inc` | une application peut écrire des secteurs bruts sur `blk.sd0p2` (partition personnelle) |
| Requêtes prises dans l'ordre d'arrivée (`SR_ORDRE`), aucun héritage de priorité | `svc.asm`, `sched.asm` | un thread haute priorité (`sound`) attend derrière les requêtes d'une application |
| `SVC_OPEN` qui attend un service sonde toutes les 10 ms | `sys_svc_open` | délai et réveils inutiles au démarrage |
| Échéance fixe de 30 s par appel (`SVC_CALL_MS`) | `svc.asm` | un FTP ou un disque lent peut la dépasser légitimement |
| Futex propres au process (clé : adresse virtuelle ; `futex_wake` ne parcourt que les threads de l'appelant) | `threads.asm` | aucun réveil possible entre process : les anneaux partagés sont **sondés** |
| HIDManager dort 10 ms (50 sans pilote) entre deux lectures des anneaux ; Elegant attend ses requêtes 8 ms au plus puis lit son anneau | `hidmanager.asm`, `elegant.asm` | jusqu'à ≈ 18 ms de latence d'entrée ajoutée ; ≈ 225 réveils par seconde au repos |
| `ui_update` = `WS_OP_UPDATE` **synchrone** ; Elegant marque la zone et répond 0 | `uikit.asm`, `core.inc` | l'application attend qu'Elegant ait fini de composer (plusieurs ms en plein écran) pour une réponse vide |

Ce qui est bon et reste : messages de 512 o sans allocation (emplacements réservés d'avance), pid
de l'appelant écrit par le noyau (+44), avis de fins et de naissances (`SVF_FINS`,
`SVF_NAISSANCES`), anneaux partagés pour les flux (son, entrées, trames), tampons partagés pour
les gros transferts.

## 2. Décisions proposées

### 2.1 Canaux par process

* `SVC_MAX` passe à **64** ; le PCB reçoit un **masque de 64 bits** `P_CHANNELS` (bit n : le
  service n est ouvert). Le numéro de canal reste le numéro du service : **ABI inchangée**.
* `SVC_OPEN` met le bit (après le contrôle d'accès, 2.3) ; `SVC_CALL` et `SVC_POST` refusent un
  canal dont le bit est à 0 (`E_BADF`).
* Un service qui s'arrête : son bit est effacé dans tous les process (parcours rare, à sa fin) ;
  ses clients reçoivent `E_PIPE` comme aujourd'hui, puis rouvrent. Un autre service qui
  reprend la même place n'hérite donc d'aucun client.
* Nouvel appel `SVC_CLOSE` (canal) : efface le bit.
* `SVC_OPEN` qui attend : réveillé par `SVC_REGISTER` (liste d'attente) au lieu de sonder.
* Les threads du noyau (`SVC_PID_NOYAU`) ne sont pas contrôlés.

### 2.2 Une file et un verrou par service, quotas

* Chaque service a **sa file** de requêtes et **son verrou** ; `svc_lock` ne protège plus que la
  table des noms (enregistrement, ouverture, fin d'un service).
* Les emplacements de requêtes restent réservés d'avance, dans une **réserve commune** (portée à
  256) protégée par son propre verrou. Ordre des verrous : service, puis réserve, jamais
  l'inverse ; `svc_lock` avant celui d'un service.
* **Quota par process** (compteur `P_SVC_INFLIGHT` dans le PCB) : requêtes en cours (appels et
  avis) au plus **8** pour un process sans droits, **32** pour un programme de `SD0:/sys`, sans
  limite pour le noyau. Au-delà : `E_AGAIN`, sans toucher aux autres process.
* `svc_proc_exit` parcourt les files une à une (verrou de chacune).

### 2.3 Niveaux d'accès (qui peut ouvrir un service)

Drapeaux de `SVC_REGISTER` (noms anglais, comme les nouveaux identifiants) :

| Niveau | Drapeau | Qui peut ouvrir | Exemples |
|---|---|---|---|
| public | (aucun) | tout process | `vfs`, `ws`, `net`, `sound`, `notify`, `fs.*` (voir plus bas) |
| système | `SVF_SYSTEM` | programmes de `SD0:/sys` (`P_DROITS`) | `blk.*`, `usbd`, `hwmanager`, `init2` |
| privé | `SVF_PRIVATE` | process autorisés par le service | cas où un programme système ne doit pas tout ouvrir |

* `SVC_GRANT` (canal, pid) : le process qui tient le service autorise ce pid à l'ouvrir
  (comme `BUF_GRANT`) ; autorisations rendues à la fin du pid. Huit par service au plus.
* **Dans les services, le contrôle par opération et par objet** (le noyau ne voit pas les ops) :
  * FatFS, RamFS : les ops par **chemin** (ouvrir, `stat`, `mkdir`, `unlink`, `chdir`) ne sont
    acceptées que du pid de `vfs` ; les ops sur un **fichier ouvert** (lire, écrire, `readdir`,
    fermer) restent ouvertes aux applications, le fichier devant appartenir à l'appelant
    (`fsowner.inc` le fait déjà) : `fs.*` reste public, `read` / `write` restent directs ;
  * `blkserv.inc` : `SVF_SYSTEM` suffit dans un premier temps ;
  * HIDManager : l'op « pilote » (3) réservée aux programmes de `SD0:/sys` (`PROC_FLAGS` de
    l'appelant, ou une liste donnée par hwmanager).
* **Plus tard** (moindre privilège entre services, comme le routage de Genode) : vfs lance FatFS en
  lui donnant l'autorisation vers son seul `blk.<partition>` (`SVC_GRANT` fait par le pilote à la
  demande de vfs, ou un `SVC_GRANT` délégué) ; `blk.*` passe alors en `SVF_PRIVATE`.

### 2.4 Priorités : file triée et héritage

* `SVC_NEXT` prend la requête **la plus prioritaire** (priorité du thread appelant, puis ordre
  d'arrivée), au lieu de la plus ancienne.
* Le thread du service qui prend une requête est **élevé** à la priorité de l'appelant si elle
  est plus haute, jusqu'à son `SVC_REPLY` (ou au `SVC_NEXT` suivant) ; il retrouve alors la sienne.
  Comme QNX. Pas d'héritage transitif dans un premier temps (service qui appelle un service).
* Une requête plus prioritaire qui arrive pendant que le service travaille élève aussi le thread
  occupé (sinon elle attend derrière une requête normale).

### 2.5 Appels combinés et avis des programmes

* `SVC_REPLY_NEXT` (n° de requête, message, attente ms) : répond puis attend la suivante dans le
  même message, en un appel système (le `ReplyRecv` de L4) ; la boucle de chaque service passe à
  un appel par requête au lieu de deux.
* `SVC_POST` (canal, message) : avis sans réponse envoyé par un programme (le noyau a déjà
  `svc_poster_k`) ; compté dans le quota jusqu'à ce que le service l'ait pris.
* `SVC_CALL` garde son échéance de 30 s ; `SVC_CALL_EX` (canal, message, attente ms) pour les
  services lents.

### 2.6 Notifications (réveil entre process)

Un mot de 64 bits dans le noyau, comme la *Notification* de seL4, le `notify` de Minix 3 ou les
*pulses* de QNX :

| Appel | Arguments | Effet |
|---|---|---|
| `NOTIFY_CREATE` | – | numéro ; le créateur seul attend dessus ; 128 dans le système, rendues à sa fin |
| `NOTIFY_GRANT` | numéro, pid | ce pid peut signaler |
| `NOTIFY_SIGNAL` | numéro, bits | OU des bits, réveil de l'attente ; **ne bloque jamais, n'alloue rien, ne peut pas déborder** |
| `NOTIFY_WAIT` | numéro, attente ms | bits levés (remis à 0) ; 0 : délai écoulé |
| `SVC_BIND_NOTIFY` | canal (son service), numéro, bit | l'arrivée d'une requête lève ce bit : un thread attend **à la fois** ses requêtes et ses anneaux |

Règle des anneaux : l'écrivain ne signale que quand l'anneau passe de vide à non vide (un appel
par rafale, pas par événement).

Usages :
* **Entrées** : chaque pilote (gt911, usbkeyboard, usbmouse) signale HIDManager, qui lui donne le
  numéro à l'op 3 ; HIDManager signale Elegant (numéro donné à `HID_OP_ABONNER`). HIDManager
  n'attend plus que ses notifications (et l'échéance de la répétition des touches).
* **Elegant** : une seule attente, sans délai fixe — bit 0 : requêtes (`SVC_BIND_NOTIFY`), bit 1 :
  entrées, bit 2 : surfaces mises à jour, plus l'échéance de l'horloge du bureau.
* **Son** : `sound` et AudioKit peuvent remplacer leurs attentes par sondage des anneaux.

### 2.7 Mise à jour des fenêtres sans attente

* `ui_update` n'appelle plus Elegant : la zone salie s'ajoute à un **en-tête de la surface**
  (rectangle englobant, mis à jour par échange atomique) et l'application signale le bit 2
  d'Elegant. Elegant lit et remet à zéro les zones des fenêtres au tour suivant.
* Après chaque composition qui a pris une zone d'une fenêtre, Elegant poste à son programme un
  événement **« image affichée »** (`WEV_FRAME`, à numéroter) : l'application sait quand
  redessiner (cadence, comme le *frame callback* de Wayland) au lieu d'être freinée par l'appel.
* `WS_OP_UPDATE` reste accepté (anciens programmes).
* Plus tard, pour la vidéo et les jeux : **double tampon** par fenêtre (une seule surface
  aujourd'hui : l'application peut dessiner pendant qu'Elegant lit).

## 3. Appels (plage `0x30`–`0x3F`, libres : `0x35`–`0x3F`)

| N° | Nom | N° | Nom |
|---|---|---|---|
| `0x35` | `SVC_CLOSE` | `0x3A` | `NOTIFY_GRANT` |
| `0x36` | `SVC_GRANT` | `0x3B` | `NOTIFY_SIGNAL` |
| `0x37` | `SVC_POST` | `0x3C` | `NOTIFY_WAIT` |
| `0x38` | `SVC_REPLY_NEXT` | `0x3D` | `SVC_BIND_NOTIFY` |
| `0x39` | `NOTIFY_CREATE` | `0x3E` | `SVC_CALL_EX` |

Il reste `0x3F` ; la plage est pleine ensuite (la `0x70`–`0x7D` est prise depuis le 30/10/2026 par
l'IPC entre process de la couche POSIX — canaux locaux, poignées, mémoire partagée :
`src/kernel/ipc.asm`, ABI.md « 0x70 » ; il y reste `0x7E`–`0x7F`).

## 4. Étapes

Chacune se valide par `svctest` (aller-retour), `ramtest`, le bureau sous QEMU, puis le PocketTerm.

1. **Canaux par process** (2.1) : masque, `SVC_CLOSE`, réveil de `SVC_OPEN`. Rien à changer dans
   les programmes s'ils ouvrent bien leurs services avant de les appeler (à vérifier : un
   programme qui appelle un numéro reçu d'un autre sans `SVC_OPEN` casserait).
2. **Files et verrous par service, quotas** (2.2). Mesure : `svctest` à 1, 2 et 4 cœurs en
   parallèle, avant et après.
3. **Niveaux d'accès** (2.3) : `SVF_SYSTEM` pour `blk.*`, `usbd`, `hwmanager`, `init2` ;
   contrôle des ops par chemin dans FatFS et RamFS ; op 3 de HIDManager. Essai : un programme
   sans droits qui ouvre `blk.sd0p2` reçoit `E_PERM`.
4. **Priorités** (2.4). Essai : un thread haute priorité qui appelle un service occupé par des
   requêtes normales passe devant (variante de `threadtest`).
5. **`SVC_REPLY_NEXT`, `SVC_POST`, `SVC_CALL_EX`** (2.5) ; boucles des services passées à
   `SVC_REPLY_NEXT` au fil de l'eau.
6. **Notifications** (2.6), puis HIDManager et ses pilotes, puis Elegant (attente unique).
   Mesure : latence d'une touche (journal horodaté) et réveils au repos, avant et après.
7. **Mise à jour des fenêtres** (2.7) : en-tête de surface, `WEV_FRAME`, UIKit.

## 5. Risques

* **Verrous** : passer d'un verrou global à des verrous par service ouvre la porte aux
  interblocages (fin d'un process pendant un appel, service qui s'appelle lui-même, thread du
  noyau appelant). L'ordre des verrous (2.2) est à écrire en tête de `svc.asm` et à respecter
  partout.
* **Héritage de priorité** : la priorité rendue au `SVC_REPLY` doit être celle de base, pas la
  précédente (plusieurs élévations successives).
* **Démarrage** : `init`, `sd`, `vfs`, FatFS doivent s'ouvrir dans le bon ordre avec les niveaux
  d'accès ; un refus à ce stade empêche de démarrer (garder la branche de retour).
* **Programmes existants** : un service enregistré en `SVF_SYSTEM` à tort coupe une application
  (le navigateur de fichiers liste les volumes par vfs, pas par `fs.*` : à vérifier service par
  service).

## 6. Hors de ce chantier (axe sécurité)

* Les droits viennent du chemin `SD0:/sys`, et rien n'empêche d'y écrire ; init2 lance des
  programmes de `SD0:/sys` pour des process sans droits (`SPAWN_PIPE`, fd = −1). Une application
  peut donc déposer un exécutable dans `SD0:/sys/svc` et le faire lancer avec les droits.
  À fermer : FatFS refuse l'écriture sous `/sys` à un process sans droits.
* `fsowner.inc` prend pour propriétaire le pid donné en `d` par l'ouverture : avec le contrôle
  « chemins seulement de vfs » (2.3), seul vfs peut le donner.
