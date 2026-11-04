# USB en service — usbd et ses pilotes

L'USB sort du noyau (feuille de route, section 3). On sépare **la mécanique de la politique** :

```
 contrôleurs                usbd (le bus)                hwmanager (la politique)      pilotes de classe
 DWC2 (USB-C)   ─┐   · contrôleurs, énumération, hubs   · drivers.txt :                usbkeyboard ─┐
 xHCI (USB-A,    ├─▶ · transferts (contrôle,         ──▶   « usb:03/01/01 =       ──▶  usbmouse    ─┼─▶ HIDManager
  derrière PCIe) ┘     interruption ; plus tard bulk)      SD0:/sys/svc/usbkeyboard »  (usbstorage, │
                     · annonce chaque interface          · lance le pilote               usbnet...)  │
                       à hwmanager                         « <périphérique> <interface> »           │
```

* **usbd** (`SD0:/sys/svc/usbd`, lancé par `init2`) pilote les contrôleurs, énumère les
  périphériques (hubs compris) et fait les transferts pour les pilotes. Il ne connaît aucune classe.
* **hwmanager** reçoit l'annonce de chaque interface et lance le pilote de `drivers.txt`.
* **Pilotes de classe** : un process par interface, client du service « usb ».
* **Le bus d'événements** : usbd publie chaque périphérique sous `korin.usb.device.<n>`, retiré
  avec lui ; une clé montée apparaît sous `korin.storage.volume.usb0` (docs/BUS.md).

Aucune interruption : comme dans le noyau, tout est scruté (`usbd` : toutes les 1 à 4 ms).

## Fichiers

| Fichier | Rôle |
|---|---|
| `src/svc/usbd/usbd.asm` | le cœur : table des périphériques, service « usb », annonces, tuyaux, boucle |
| `src/svc/usbd/dwc2.inc` | contrôleur DWC2 (port USB-C) : repris de `src/kernel/arch/pi4/usb_dwc2.asm` |
| `src/svc/usbd/xhci.inc` | PCIe + contrôleur xHCI VL805 (ports USB-A) : repris de `pcie.asm` et `usb_xhci.asm` |
| `src/inc/usb.inc` | constantes communes (requêtes standard, descripteurs, ops du service « usb ») |
| `src/svc/inc/hid.inc` | analyse des descripteurs de rapport HID : reprise de `src/kernel/usb_hid.asm` |
| `src/svc/usbkeyboard/usbkeyboard.asm`, `src/svc/usbmouse/usbmouse.asm` | pilotes HID → anneau de HIDManager (op 3) |
| `src/svc/usbstorage/usbstorage.asm` | clé USB (Bulk-Only, SCSI) → périphérique bloc `blk.usb0` (src/svc/inc/blkserv.inc), FatFS : `USB0:` |

## Interface entre le cœur et un contrôleur (« backend »)

Conventions ARMPI : arguments dans x0–x7, résultat dans x0 (négatif : erreur), x19–x28 préservés,
`prologue` / `epilogue`. usbd n'a **qu'un thread** : pas de verrou. Préfixe `dw_` (DWC2) ou `xh_`
(xHCI). Mémoire DMA : `SYS_DMA_ALLOC` (non cachée : aucune opération de cache ; adresse physique
rendue, à laquelle le DWC2 ajoute `0xC0000000`, l'alias bus du VideoCore ; le xHCI l'utilise
telle quelle). Registres : `SYS_DEV_MAP`. Firmware : `SYS_FW_CALL`.

Chaque contrôleur fournit :

| Fonction | Entrée | Sortie |
|---|---|---|
| `xx_init` | – | x0 = 0, ou < 0 si absent (QEMU sans PCIe...) ; énumère tout ce qui est branché et appelle `usbd_attacher` pour chaque périphérique qui n'est pas un hub |
| `xx_control` | x0 = poignée, x1 = paquet SETUP (8 octets dans un registre, petit-boutiste), x2 = tampon (mémoire ordinaire du process), x3 = longueur | x0 = octets transférés, ou < 0 (bloquant, ≤ 500 ms) |
| `xx_intr_ouvrir` | x0 = poignée, x1 = adresse de l'extrémité (bit 7 : IN), x2 = taille max. de paquet, x3 = intervalle (ms) | x0 = numéro de tuyau ≥ 0, ou < 0 |
| `xx_intr_lire` | x0 = tuyau | x0 = octets reçus (> 0), 0 si rien, < 0 erreur ; x1 = adresse des données (valides jusqu'au prochain appel) — **non bloquant** : le DWC2 lance un transfert IN si l'intervalle est écoulé, le xHCI regarde ses événements |
| `xx_scruter` | – | travail périodique (le xHCI vide son anneau d'événements) ; plus tard, les branchements |
| `xx_intr_fermer` | x0 = tuyau | – (le pilote s'est arrêté) |
| `xx_bulk_raz` | x0 = poignée, x1 = extrémité | – : appelé par le cœur après un `CLEAR_FEATURE(ENDPOINT_HALT)` réussi ; le contrôleur remet l'état de cette extrémité à zéro (bascule DATA0 du DWC2 ; Reset Endpoint + Set TR Dequeue du xHCI si elle est arrêtée) |
| `xx_bulk` | x0 = poignée, x1 = extrémité (bit 7 : IN), x2 = taille max. de paquet, x3 = tampon DMA (adresse virtuelle), x4 = son adresse physique, x5 = longueur (≤ 64 Ko) | x0 = octets transférés, ou < 0 (bloquant, ≤ 5 s) ; le contrôleur garde lui-même l'état de chaque extrémité bulk (bascule DATA0/1 du DWC2, anneau du xHCI, configurée au premier usage) |

La **poignée** est propre au contrôleur (DWC2 : adresse | vitesse << 8 | MPS0 << 16 ; xHCI : slot).

Le cœur fournit aux contrôleurs :

| Fonction | Entrée | Sortie |
|---|---|---|
| `usbd_attacher` | x0 = contrôleur (0 DWC2, 1 xHCI), x1 = poignée, x2 = descripteur de périphérique (18 o), x3 = descripteur de configuration complet, x4 = sa longueur, x5 = vitesse (0 FS, 1 LS, 2 HS, 3 SS) | x0 = numéro de périphérique (1..) ; annonce ses interfaces à hwmanager |
| `dma_zone` | x0 = taille | x0 = adresse (non cachée), x1 = adresse physique, ou x0 < 0 |

Les contrôleurs gardent leur code de hubs ; ils ne signalent pas les hubs au cœur.

## Branchement à chaud

Le cœur fournit aussi `usbd_detacher` (x0 = numéro de périphérique rendu par `usbd_attacher`) :
le périphérique est retiré de la table, ses tuyaux sont fermés (une lecture en attente reçoit
`E_NODEV` : le pilote s'arrête), hwmanager est prévenu (`HW_OP_USB_RETRAIT`). usbstorage, à son
premier transfert refusé, fait démonter son volume par vfs (op 21) et s'arrête.

Chaque contrôleur tient l'arbre de ses périphériques (poignée, hub parent, port, numéro usbd,
hub ou non) et, dans `xx_scruter`, toutes les 200 ms au plus :
* ports racines : changement de connexion (DWC2 : `HPRT0` ; xHCI : `PORTSC`, ou ses événements
  Port Status Change) ;
* ports de chaque hub : `GET_STATUS(port)`, bit `C_PORT_CONNECTION` (effacé par
  `CLEAR_FEATURE`).

Un **retrait** : tout ce qui est derrière ce port est détaché (récursivement pour un hub),
`usbd_detacher` pour chaque périphérique, ressources du contrôleur rendues (xHCI : Disable Slot ;
DWC2 : l'adresse peut resservir). Un **branchement** : remise à zéro du port puis énumération,
comme au démarrage (`usbd_attacher`). Une scrutation ne bloque pas la boucle plus de quelques
centaines de ms (remise à zéro d'un port).

## Annonce à hwmanager

Service « hwmanager », op `HW_OP_USB` (1), message avis (sans attendre de réponse utile) :
`a` = périphérique, `b` = interface, `c` = classe << 16 | sous-classe << 8 | protocole,
`d` = vendeur << 16 | produit. hwmanager cherche dans `drivers.txt`, de la clé la plus précise à la
moins précise : `usb:VVVV:PPPP` (vendeur:produit, hexa), `usb:cc/ss/pp`, `usb:cc/ss`, `usb:cc`
(hexa sur 2 chiffres), et lance le pilote avec les arguments « `<périphérique> <interface>` ».

## Service « usb » (pour les pilotes)

| Op | Entrée | Réponse |
|---|---|---|
| 1 `DESCRIPTEURS` | a = périphérique | données : descripteur de configuration (≤ 464 o), b = sa longueur, c = vendeur << 16 \| produit, d = vitesse |
| 2 `CONTROLE` | a = périphérique, b = paquet SETUP, c = longueur ; données : ce qu'on envoie | résultat = octets ; données : ce qu'on reçoit |
| 3 `INTR_OUVRIR` | a = périphérique, b = extrémité, c = taille max., d = intervalle | résultat = tuyau |
| 4 `INTR_LIRE` | a = tuyau | **réponse différée** : quand un rapport arrive ; données = rapport, résultat = longueur |
| 5 `BULK_TAMPON` | a = taille (≤ 64 Ko + 4 Ko) | résultat = numéro d'un tampon partagé créé par usbd et autorisé au pilote (`buf_map`) |
| 6 `BULK` | a = périphérique, b = extrémité, c = taille max., d = décalage dans le tampon partagé ; données +0 : longueur (≤ 64 Ko) | résultat = octets ; usbd copie entre le tampon partagé et son tampon DMA |

| 8 `ISO_OPEN` | a = périphérique, b = extrémité (bit 7 : IN), c = taille max., d = octets par ms en 16.16 \| octets par trame << 32 | résultat = tuyau isochrone ; b = numéro d'un anneau partagé (`UI_*` de `usb.inc` : OUT, le pilote y écrit le PCM et usbd en envoie un paquet par ms ; IN, usbd y écrit ce qu'il reçoit ; `UI_STATE` = 1 au retrait) |
| 9 `ISO_CLOSE` | a = tuyau isochrone | 0 |

usbd est prévenu des fins de process (`SVF_FINS`) : les tuyaux d'un pilote arrêté sont fermés.
Isochrone (`src/svc/usbd/iso.inc`, docs/SON.md 5.1) : DWC2 par un second thread d'usbd (canaux 3
et 4, un paquet par transfert, cadencé sur l'horloge, 4 ms d'avance ; vérifié sous QEMU avec
`usb-audio`) ; xHCI par des TRB Isoch (SIA) gardés 12 ms en file par la boucle d'usbd (non testé :
pas de xHCI sous QEMU).

## Transition

La pile USB du noyau est partie (29/10/2026, après validation sur le PocketTerm ; branche de
retour `usb-noyau`) avec `input.asm` (appel 79) et le canal de blocs (appel 93). Le réseau USB
(ECM) et le son USB attendent `usbnet` et `usbaudio` (à reprendre de l'historique : `usb_ecm.asm`,
`usb_audio.asm`).
