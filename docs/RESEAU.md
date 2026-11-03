# Réseau en services — netd, ses pilotes, le Wi-Fi, l'accès à distance

Section 5 de la feuille de route. Comme pour l'USB (docs/USB.md), on sépare **la mécanique de
la politique**, et la pile quitte le noyau (docs/NOYAU-HYBRIDE.md, 3.2) :

```
 pilotes d'interface                    netd (la pile)                     programmes
 genet   (SoC, Ethernet)  ─┐ anneau   · ARP, IPv4, ICMP, UDP, TCP,   ◀──▶ NetKit (netkit.so) :
 wifi    (SoC, CYW43455)  ─┼─ de ───▶   DHCP, DNS ; sockets              socket, connect, send...
 usbnet  (usbd, ECM)      ─┘ trames   · choix de l'interface active       wifi_* (service « wifi »)
     ▲                                · service « net »
 hwmanager : lance genet / wifi (carte), usbnet (annonce de usbd) ; drivers.txt
```

## 1. Ce qui existe

| Fichier | Lignes | Contenu | Dépend du noyau par |
|---|---:|---|---|
| `src/kernel/net.asm` | 3 447 | pile IPv4 complète, 32 sockets, appels 40–52, file `net_rxq`, `netif_set` (priorité GENET > Wi-Fi > ECM) | `pmm_alloc_n`, `net_lock` (spinlock), `thread_sleep`, `time_ms`, `check_user` |
| `src/kernel/frames.asm` | 131 | canal de trames 90–92 (`trame_lire/ecrire`, `netif_info`) : un netd peut déjà lire/envoyer | `net_rxq`, `netif_send` |
| `src/kernel/wifi.asm` | 2 406 | CYW43455 : backplane, firmware, SDPCM/BCDC, WPA2 (supplicant de la puce), un seul réseau dans `wifi.txt`, appels 36–39 | `kload_file`, `mbox_*`, `netq_push`, `netif_set` |
| `src/kernel/arch/pi4/sdio.asm` | 483 | hôte Arasan (EMMC1, `0xFE300000`), GPIO 34–39 ALT3 | `gpio_fsel/pull`, `mbox_get_clock` |
| `src/kernel/arch/pi4/genet.asm` | 820 | GENET v5 (`0xFD580000`) + PHY, 1 anneau RX/TX, lien sondé toutes les 2 s | `pmm_alloc_n`, `dcache_flush_range`, `mbox_get_board_mac` |

Constats :
* **Sous QEMU il n'y a plus de réseau** : l'ECM USB est parti avec la pile USB (USB.md,
  « Transition ») ; GENET et Wi-Fi ne démarrent pas sous QEMU (`board_is_emulated`).
* Le Wi-Fi n'a **jamais tourné** sur un vrai Pi (docs/WIFI.md) ; il sera mis au point en service.
* `dev_map` accepte déjà tout ce qu'il faut : GENET `0xFD580000`, Arasan `0xFE300000`, GPIO
  `0xFE200000`, PM `0xFE100000` sont dans `DEV_ZONE1` (`0xFC000000–0xFF800000`).
* Programmes concernés : `browser`, `httpget`, `httpd`, `ping`, `dltest`, `ultest`, `wifi`,
  `menubar` (appel 51 pour l'icône), NetKit (`wait_dhcp`). NetKit n'a que 3 fonctions : les
  programmes font les appels eux-mêmes.
* `futex_wake` ne réveille que les threads **du même process** : pas de futex entre process.

## 2. Architecture (décisions)

### 2.1 Un service par interface, netd à part

| Service | Rôle | Lancé par |
|---|---|---|
| `SD0:/sys/svc/netd` | la pile, service « net » ; aucune ligne de pilote | `services.txt` (init2, relance ×3) |
| `SD0:/sys/svc/genet` | Ethernet du Pi 4 (`genet.asm` inclus) | hwmanager, clé `soc:genet` |
| `SD0:/sys/svc/wifi` | CYW43455 (`wifi.asm` + `sdio.asm` inclus), service « wifi » | hwmanager, clé `soc:wifi` |
| `SD0:/sys/svc/usbnet` | pilote de classe ECM (repris de `usb_ecm.asm`, historique git), client de usbd | hwmanager, `usb:02/06` |

**Un process par interface** (et non un « netdrv » unique) : c'est le modèle de usbd ; le Wi-Fi,
encore instable, peut planter et être relancé sans couper l'Ethernet ; usbnet est de toute façon
un pilote de usbd. Le code commun (anneau, enregistrement) est `src/svc/inc/netif.inc`. Le nom
« netdrv » désigne cette famille.

### 2.2 L'anneau de trames

À l'enregistrement d'un pilote, netd crée un tampon partagé (appel 70), l'autorise au pilote
(71) ; chacun le mappe (72). Un producteur, un consommateur par sens : ni verrou ni appel système.

| Décalage | Contenu |
|---|---|
| +0 | en-tête 4 Ko : `rx_tete` (pilote), `rx_queue` (netd), `tx_tete` (netd), `tx_queue` (pilote) — compteurs monotones ; `netd_dort` ; MTU ; compteurs de pertes |
| +4 Ko | RX : 64 emplacements de 2 Ko (longueur sur 8 octets + trame ≤ 1 544) |
| +132 Ko | TX : 64 emplacements de 2 Ko |

Barrières `dmb ish` entre la trame et le compteur. **Réveils** :
* pilote → netd : le pilote écrit la trame ; si `netd_dort` vaut 1, il envoie un **avis**
  `IF_SONNETTE` au service « net » (svc_call sans attente) ; netd, endormi dans `svc_next`, se
  réveille aussitôt. Sous charge, netd ne dort pas : aucun message.
* netd → pilote : rien ; les pilotes scrutent déjà leur matériel toutes les 1 ms et regardent
  l'anneau TX à chaque tour.
* Plus tard (optimisation) : un `futex_wake` entre process pour la fenêtre des tampons partagés
  (`0x68_0000_0000`), qui remplacerait l'avis.

Une copie par trame côté pilote (anneau ↔ DMA), comme aujourd'hui (`netq_push`).

### 2.3 Enregistrement des interfaces, choix de l'interface active

| Op (service « net ») | Entrée | Réponse |
|---|---|---|
| 20 `IF_ENREGISTRER` | a type (1 Ethernet, 2 Wi-Fi, 3 USB), b MAC, c priorité ; données : nom (`eth0`, `wlan0`, `usb0`) | a = n° d'interface, b = n° du tampon de l'anneau |
| 21 `IF_ETAT` | a interface, b lien (0/1), c débit Mb/s ; données : SSID (Wi-Fi) | 0 |
| 22 `IF_SONNETTE` | a interface | (avis) |

netd est enregistré avec `SVF_FINS` : un pilote qui meurt fait disparaître son interface.
**Chaque interface a son DHCP** (variables `net_ip`, `dhcp_*`... rangées dans une structure
d'interface) ; la **route par défaut** est l'interface de plus haute priorité qui a un lien et
un bail : GENET (30) > Wi-Fi (20) > usbnet (10), comme `netif_set`. Les connexions TCP d'une
interface perdue sont remises à zéro.

### 2.4 hwmanager et le SoC

Au démarrage, hwmanager lit la **révision de la carte** (`fw_call`, tag `0x00010002`, comme
`hwtest`) et la version du firmware (< `0x50000000` : QEMU, rien du SoC) :

| Type (bits 4–11) | Carte | Clés retenues |
|---|---|---|
| `0x11` | Pi 4 B (PocketTerm) | `soc:genet`, `soc:wifi` |
| `0x13` | Pi 400 | `soc:genet`, `soc:wifi` |
| `0x14` | CM4 | `soc:genet`, `soc:wifi` (le pilote dira si la puce est là) |

Le device tree n'est pas utile (la révision suffit). Chaque pilote **vérifie lui-même** :
`wifi` sort avec le code 0 (pas de relance) si l'Arasan ou la puce SDIO ne répondent pas ;
`genet` sort avec 0 si `GE_SYS_REV` n'est pas un GENET v5 ; sans câble, il reste et sonde le
lien toutes les 2 s (coût nul). hwmanager relance un pilote du SoC qui sort en erreur (3 fois,
comme init2). Une ligne retirée de `drivers.txt` désactive un pilote.

## 3. Sockets : NetKit ↔ service « net »

Les programmes appellent **NetKit** (ABI en ajout seul) ; mêmes arguments que les appels
40–52, pour une conversion mécanique (`sys SYS_SEND` → `bl net_send`).

| Fonction NetKit | Op | Entrée (message) | Réponse | Différée |
|---|---|---|---|---|
| (première utilisation) | 1 `OUVRIR` | — | b = n° du tampon du process | |
| `net_socket` | 2 | a type | sd | |
| `net_connect` | 3 | a sd, b ip, c port | 0 / erreur | oui (SYN) |
| `net_bind`, `net_listen` | 4, 5 | a sd, b port / file | 0 | |
| `net_accept` | 6 | a sd, b délai | sd | oui |
| `net_send` | 7 | a sd, b zone, c n | n | si l'anneau est plein |
| `net_recv` | 8 | a sd, b zone, c n, d délai | n (0 : fermée) | oui |
| `net_close` | 9 | a sd | (avis) | |
| `net_shutdown` | 24 | a sd | 0 | FIN après ce qui reste en file ; le socket reste au process et reçoit toujours |
| `net_sendto`, `net_recvfrom` | 10, 11 | + ip, port | + c ip, d port | 11 oui |
| `net_resolve` | 12 | données : nom | ip | oui (DNS) |
| `net_ping` | 13 | a ip, b délai | RTT ms | oui (plusieurs à la fois) |
| `net_info` | 14 | — | données : 32 o de l'appel 51 + nom et type de l'interface active | |
| `net_ifaces` | 15 | — | données : interfaces (nom, type, lien, ip) | |

* **Réponses différées** : netd garde le n° de requête dans la socket et appelle `svc_reply`
  quand les données, la connexion ou la réponse DNS arrivent (comme `USB_OP_INTR_LIRE`).
  `svc_call` abandonne à 30 s (`SVC_CALL_MS`) : netd répond `E_AGAIN` à 25 s et NetKit
  recommence tant que le délai demandé n'est pas écoulé (délai infini compris).
* **Données** : ≤ 448 octets dans le message ; au-delà, par le **tampon du process** (128 Ko,
  créé par netd à `OUVRIR`, autorisé à ce pid) découpé en 16 zones de 8 Ko ; NetKit prend une
  zone libre par appel (bitmap, `ldaxr/stlxr`) : plusieurs threads d'un programme peuvent
  appeler en même temps. Un `send` de 64 Ko = 8 allers-retours (≈ 6 µs chacun sur le Pi 4).
* **Sockets** : 64 (au lieu de 32), numérotées dans netd, `SK_OWNER` = pid (vérifié à chaque op).
* **Fin d'un process** (`SVC_OP_FIN`) : `net_close_all(pid)` (FIN envoyé, orphelins finis par
  le protocole), tampon libéré, réponses différées de ce pid abandonnées.
* **Threads de netd** : la pile dans **un seul thread** (ni verrou, ni course : les réponses
  différées rendent l'attente gratuite) qui boucle sur `svc_next` (échéance = prochaine
  minuterie, ≤ 10 ms), les anneaux et les minuteries ; un **second thread** pour ce qui peut
  bloquer : lecture de `SD0:/etc/net.txt` (nom d'hôte, IP fixe éventuelle), notifications à
  notifyd. C'est la lecture de « plusieurs threads » retenue.
* `net_lock` disparaît ; `pmm_alloc_n` → `sbrk` ; `thread_sleep` → l'échéance de `svc_next`.

## 4. Wi-Fi

### 4.1 Réseaux connus : `SD0:/etc/wifi.txt`

```
country=FR                 # ancien réglage : seulement si options.txt n'a pas de pays=
ssid="Maison"              # chaque ssid= ouvre un réseau
key="phrase secrète"       # absente : réseau ouvert
priority=10                # défaut 0 ; le plus grand d'abord
ssid=MonReseau
key=MotDePasse
proto=RSN                  # facultatif : RSN (WPA2), WPA
key_mgmt=WPA-PSK           # facultatif : WPA-PSK, NONE
priority=5
```

Au plus 16 réseaux ; guillemets acceptés (déjà le cas) ; `#` commentaire.

**Pays** (décision de l'utilisateur) : il ne se règle pas dans l'applet Wi-Fi mais dans l'applet
**« Région et langue »** du panneau (l'applet clavier `keyboard.inc`, étendu : disposition,
langue d'affichage, pays ; feuille de route, « Localisation »). Proposé : rangé dans
`SD0:/etc/options.txt` à côté de `keyb=`, en **`pays=FR`** et **`fuseau=Europe/Paris`** (pas de
mot anglais dans les options) ; le pilote `wifi` le lit au démarrage ; `country=` de `wifi.txt`
ne sert plus que de repli (anciennes cartes).

### 4.2 Connexion

1. Mise en route (firmware lu par AppKit dans `SD0:/firmware/`), pays (`pays=` d'`options.txt`).
2. **Balayage** ; on garde les réseaux connus présents, triés par **priorité puis signal**.
3. On essaie chacun ; clé refusée (`EACCES`) ou délai : le suivant.
4. Associé : `IF_ETAT` lien 1 → netd lance le DHCP de `wlan0`.
5. **Perte** (événement DEAUTH / LINK) : `IF_ETAT` lien 0 ; on retente le même réseau après
   2 s, puis le cycle 2–3 ; ensuite un balayage toutes les 5 s, 10 s... jusqu'à 60 s.
6. Un `REJOINDRE` explicite (applet, menu) passe avant la liste.

### 4.3 Service « wifi » (servi par le pilote `wifi`)

| Op | Entrée | Réponse |
|---|---|---|
| 1 `ETAT` | — | données 64 o (format de l'appel 38 : état, RSSI, MAC, SSID) |
| 2 `BALAYER` | a première entrée | **différée** (≈ 3 s) ; a = nombre ; données : ≤ 9 entrées de 48 o (format de l'appel 36) ; b ≠ 0 : résultat du dernier balayage sans en refaire |
| 3 `REJOINDRE` | données : SSID, 0, clé, 0 ; a 1 = mémoriser ; b priorité | **différée** : 0, `ENOENT`, `EACCES`, `ETIMEDOUT` |
| 4 `QUITTER` | — | 0 (et pas de reconnexion automatique jusqu'au prochain `REJOINDRE`) |
| 5 `OUBLIER` | données : SSID | 0 (retiré de `wifi.txt`) |
| 6 `CONNUS` | a première | données : (SSID, priorité, sécurité) — **jamais les clés** |
| 7 `PRIORITE` | données : SSID ; a priorité | 0 |
| 8 `PAYS` | données : « FR » | 0 : réappliqué tout de suite (appelé par l'applet Région et langue après avoir écrit `pays=`) |

C'est le pilote qui réécrit `wifi.txt` (programme de `SD0:/sys`). NetKit : `wifi_scan`,
`wifi_join`, `wifi_status`, `wifi_leave` (les appels 36–39), plus `wifi_forget`, `wifi_known`,
`wifi_priority`, `wifi_country` (pour l'applet Région et langue).

### 4.4 Notifications (`inc/notify.inc`, seulement aux changements)

| Qui | Titre | Texte |
|---|---|---|
| netd | Réseau connecté | « Wi-Fi « Maison » — 192.168.1.20 » / « Ethernet — 10.0.0.5 » |
| netd | Réseau perdu | « Wi-Fi « Maison » » |
| wifi | Wi-Fi | « Clé refusée pour « X » » ; « Aucun réseau connu » (une fois par démarrage) |

### 4.5 Applet Wi-Fi du panneau (`src/apps/control/applets/wifi.inc`)

Dessin propre (`ap_dessin` / `ap_clic`, comme le Dock) :
* **État** : réseau, adresse, passerelle, signal (`ETAT` + `net_info`), bouton Déconnecter.
* **Réseaux visibles** (`BALAYER`, bouton Actualiser) : SSID, barres de signal, cadenas si
  sécurisé, coche sur le réseau courant ; un clic déplie la ligne : champ **clé** (masqué) si le
  réseau est inconnu et sécurisé, **Se connecter** (`REJOINDRE`, mémoriser = 1).
* **Réseaux connus** (`CONNUS`) : priorité (± → `PRIORITE`), **Oublier** (`OUBLIER`).
* Rafraîchi chaque seconde tant que l'applet est ouvert.

### 4.6 Menu Wi-Fi de la barre de menus (`SD0:/sys/wifimenu`)

Comme `wifimenu` d'Onyx : la barre (`menubar`) dessine une icône **Wi-Fi** (barres selon le RSSI,
croix si non connecté ; l'icône filaire si l'interface active est Ethernet / USB) ; un clic lance
(ou ramène devant) **`wifimenu`**, une petite fenêtre sans cadre sous l'icône :
* les réseaux visibles, du plus fort au plus faible (`BALAYER` ; « Recherche... » pendant le
  balayage), signal, cadenas, **coche** sur le réseau courant ;
* un clic : connu ou ouvert → `REJOINDRE` aussitôt ; inconnu et sécurisé → la ligne s'ouvre avec
  un champ clé et **Se connecter** ; « Connexion... » puis fermeture une fois connecté ;
* en bas, **« Paramètres Wi-Fi... »** : lance `control` sur l'applet Wi-Fi ;
* se ferme sur Échap, ou quand une autre fenêtre prend le clavier.
La barre de menus passe de l'appel 51 à `net_info` + `wifi_status` (une fois par seconde).

## 5. Étapes

Chaque étape démarre, se teste sous QEMU (`-device usb-net`, déjà dans `build.ps1` avec
`hostfwd 8080:80`) et/ou sur le PocketTerm, et se défait en retirant une ligne de
`services.txt` / `drivers.txt` (ou en revenant à l'ancien programme).

| # | Étape | QEMU | PocketTerm | Retour arrière |
|---|---|---|---|---|
| 1 | **usbd** : lecture bulk **non bloquante** (NAK → 0 octet, comme `INTR_LIRE`) ; **usbnet** (ECM : `SET_INTERFACE` alt 1, MAC par la chaîne `iMACAddress`, filtre de paquets ; état du lien par l'extrémité d'interruption) | oui : première interface testable | avec un adaptateur USB-Ethernet ECM (RTL8153...) s'il y en a un | ligne `usb:02/06` |
| 2 | **netd** + `netif.inc` + NetKit (op 1–15) ; `ping`, `httpget`, `httpd`, `dltest`, `ultest` convertis | `ping 10.0.2.2`, `httpd` vu de l'hôte, débits de dltest/ultest vs l'ancien noyau | idem si étape 1 possible | ancienne version des programmes (le noyau garde 40–52) |
| 3 | `browser`, `menubar`, `wifi` (programme) passent à NetKit ; `wait_dhcp` sur `net_info` | bureau complet | — | idem |
| 4 | **genet** en service (hwmanager `soc:genet`) | rien (pas émulé) : hwmanager ne le lance pas | seulement si le RJ45 est accessible ; sinon un Pi 4 nu | ligne `soc:genet` |
| 5 | **wifi** en service : mise en route (WIFI.md), un réseau, puis plusieurs (§4.1–4.2), service « wifi », notifications | rien | **la mise au point du Wi-Fi** (journal `SD0:/journal.txt`) | ligne `soc:wifi` |
| 6 | **Élagage** : `net.asm`, `frames.asm`, `wifi.asm`, `sdio.asm`, `genet.asm` quittent le noyau ; appels 36–52 et 90–92 → `ENOSYS` (numéros réservés) | non-régression | non-régression | branche git **`reseau-noyau`** posée avant |
| 7 | Applet Wi-Fi, `wifimenu`, icône Wi-Fi de la barre ; pays dans l'applet Région et langue | applet sans Wi-Fi (« pas de Wi-Fi ») | tout | applet retiré de `app.txt` |
| 7b | **NTP** : client SNTP (UDP 123, `pool.ntp.org` ou `ntp=` d'`options.txt`) dans le second thread de netd, au premier bail DHCP puis toutes les 6 h ; heure locale par `fuseau=` | oui (le NAT de QEMU atteint `pool.ntp.org`) | horloge de la barre juste | ligne `ntp=non` |

Ce qui sort du noyau à l'étape 6 : **≈ 7 290 lignes** (3 447 + 131 + 2 406 + 483 + 820), 20
appels système, 2 threads noyau, ≈ 200 Ko de files et de sockets alloués au démarrage.
L'élagage peut avancer avant l'étape 5 si l'on accepte d'abandonner un Wi-Fi noyau qui n'a
jamais fonctionné (question 3).

### 5.1 Accès à distance (après l'étape 7) : tester le vrai Pi depuis le PC

But (utilisateur) : **automatiser les essais sur le PocketTerm** — lancer des commandes, envoyer
les binaires fraîchement assemblés, redémarrer sur un nouveau noyau, prendre des captures,
injecter touches et clics, rapatrier journaux et fichiers.

| # | Étape | Contenu |
|---|---|---|
| 8 | **telnetd** (port 23) | un interpréteur de commandes par connexion (8 au plus), son entrée et sa sortie par des tubes ; Ctrl-C, NOP toutes les 5 min, connexion perdue = session finie (comme Onyx) |
| 9 | **ftpd** (port 21) | transfert de fichiers (§5.2) |
| 10 | **`reboot`** et **`screenshot`** | §5.3 ; `screenshot RAM0:/screen.bmp` (op Elegant `WS_OP_SCREEN`) |
| 11 | **vncd** (port 5900, RFB 3.8, codage Raw + CopyRect, sans mot de passe) | écran composé + injection ; tout client VNC, et `vncdotool` (Python) pour les scripts |
| 12 | **rdpd** (port 3390, protocole d'Onyx) | fenêtres une à une vers **Onyx Remote** (`pc/OnyxRemote` d'Onyx), §5.4 |
| 13 | **TLS : mbedTLS** (retiré le 30/10/2026 au profit d'OpenSSL, voir 10.4 ; Apache-2.0, `third_party/mbedtls-3.6.3` d'Onyx, sa glue `user/Libs/tls/onyx_tls.hpp`) | compilé par la **chaîne d'outils d'Onyx** (`aarch64-onyx-elf`, WSL) en code libre de tout OS, enveloppé dans un kit (`tlskit`) dont la table `kit_fn` appelle le C ; transport par NetKit ; **entropie** : dans Onyx, tout accès au RNG200 du BCM2711 figeait le bus (« not enabled/clocked », `kernel/sys/kapi.cpp`), d'où un simple PRNG amorcé sur l'horloge, trop faible pour TLS. À l'essai dans ARMPI : vérifier l'adresse et la mise en route du RNG200 (registres propres au RNG200, pas ceux de l'ancien RNG du BCM2835), sur le PocketTerm, par un service isolé ; sinon un mélange de sources. **Générateur dans le noyau** (décision de l'utilisateur, 29/10/2026) : il ne dépend d'aucune valeur venue d'un service (ni fichier de graine, ni données de paquets ou de touches transmises par un pilote), seulement de ce que le noyau voit lui-même : le RNG200 (s'il marche), la gigue du compteur de cycles et du compteur générique, l'instant de chaque interruption reçue (le noyau les reçoit toutes, y compris celles des pilotes en service), les commutations ; le Cortex-A72 n'a pas l'instruction `RNDR`. Appel « octets aléatoires » pour mbedTLS et les autres ; HTTPS dans le client HTTP de NetKit. Le C n'est pas réassemblable sous ARMPI (binaire précompilé) : **les programmes tiers portés (mbedTLS, plus tard WebKit…) ne sont pas auto-hébergés** ; les apps natives d'Onyx, réécrites en assembleur, le sont (décision de l'utilisateur, 29/10/2026) |

**Ordre** : telnetd + ftpd + `reboot`/`screenshot` d'abord (ils suffisent à la boucle complète,
sans rien sur le PC qu'un script Python) ; vncd ensuite (clics et touches scriptables, client
standard) ; rdpd enfin (confort d'usage, compatibilité Onyx, plus de travail dans Elegant).

**Décision (utilisateur) : pas de mot de passe** pour telnetd, ftpd, vncd, rdpd : ce sont des
outils de développement pour les essais automatiques. Ils sont lancés par des lignes de
`services.txt` (la carte de développement de `build.ps1 sd` les a). **L'authentification vient à
la toute fin du projet** (dernier point, hors des étapes de cette section).

### 5.2 Transfert de fichiers : ftpd (recommandé)

Un **ftpd minimal** plutôt qu'un protocole maison : le PC n'a besoin de rien (Python `ftplib`,
`curl`, l'Explorateur Windows, WinSCP) ; Onyx en a un ; la connaissance du protocole (réponses,
mode passif) resservira à **FtpFS** (le client). Commandes : `USER` (tout nom accepté), `PASV`,
`TYPE I`, `RETR`, `STOR`, `LIST`, `NLST`, `SIZE`, `MKD`, `DELE`, `CWD`, `PWD`, `QUIT`, plus
`XCRC` (CRC32 d'un fichier, pour n'envoyer que ce qui a changé). Les chemins sont ceux d'ARMPI
(`/SD0/sys/svc/netd` ↔ `SD0:/sys/svc/netd`). Écrire `kernel8.img` ou un `.mod` sur `SD0:`
(la partition de démarrage, FAT32) suffit pour le prochain démarrage.

### 5.3 Redémarrage à distance

Il n'existe aujourd'hui aucun redémarrage dans le noyau. Le Pi 4 redémarre par le bloc PM
(`0xFE100000`, dans `DEV_ZONE1`) : `PM_WDOG` (+0x24) = `0x5A000000 | 10`, puis `PM_RSTC` (+0x1C)
= `0x5A000020` (réinitialisation complète). **Un service suffit** (pas d'appel noyau) :
* **init2** sert l'op `REDEMARRER` du service « systeme » : demande à `vfs` de vider tous les
  volumes (op de synchronisation, à ajouter au protocole FS), puis `dev_map(0xFE100000)` et les
  deux écritures ; commande `SD0:/bin/reboot` ;
* **essai d'un noyau sans risque** (à vérifier sur le Pi 4) : le firmware connaît **`tryboot`** —
  un drapeau de redémarrage posé par `fw_call` (tag `0x00038064`, SET_REBOOT_FLAGS) fait lire
  `tryboot.txt` (`kernel=kernel8-essai.img`) **une seule fois** ; si le noyau d'essai fige,
  couper/remettre l'alimentation relance l'ancien. On promeut ensuite l'essai en `kernel8.img` ;
* **chien de garde** (proposé) : le noyau arme `PM_WDOG` (option `chien=15` de `options.txt`)
  et le nourrit depuis le minuteur ; un Pi figé redémarre seul (comme `hangreboot` d'Onyx).

### 5.4 Ce que demandent vncd et rdpd

**Elegant** (nouvelles ops, réservées aux programmes de `SD0:/sys` : le noyau marque le message
de ce droit) :

| Op | Pour | Rôle |
|---|---|---|
| `WS_OP_SCREEN` | vncd, `screenshot` | composer l'écran (fenêtres, fond, curseur) dans un tampon partagé de l'appelant ; rend une **génération** (inchangée : rien copié) et la liste des rectangles changés |
| `WS_OP_LIST` (existe) étendu | rdpd | + génération du contenu, du cadre, état (réduite, autre bureau), fenêtre au clavier |
| `WS_OP_WIN_LIRE` | rdpd | un rectangle du contenu d'une fenêtre (ou de son cadre actif/inactif) dans le tampon de l'appelant |
| `WS_OP_DEPLACER`, `WS_OP_FERMER` (d'une autre app), `WS_OP_RAISE` (existe) | rdpd | déplacer, fermer comme la case de fermeture, passer devant |
| `WS_OP_CURSEUR` | rdpd | forme du pointeur (message 11 d'Onyx) |

**HIDManager** : vncd et rdpd sont des **pilotes virtuels** (op `HID_OP_PILOTE`, anneau du
pilote) : pointeur **absolu** (comme le GT911), boutons, molette ; touches : soit un usage HID,
soit un **caractère déjà traduit** (drapeau dans l'événement : la disposition du PC a déjà été
appliquée ; RFB envoie des keysyms X11, rdpd des caractères et touches spéciales).

**Protocole de rdpd (Onyx, `user/BinUtils/rdpd.c`, client `pc/OnyxRemote/Connection.cs`)** :
TCP 3390 ; le serveur salue par `ONYXRDP1` + largeur, hauteur, DPI (u16) ; le client répond
`ONYXRDP1` + un octet d'options (1 : 16 bits, 2 : sans cadres, 4 : rondes en pipeline, 8 : COPY).
Puis des messages `type (u8) + longueur (u32) + charge`, petit-boutistes :
* serveur → client : 1 `WIN` (id, place, taille, cadre, drapeaux, alpha, titre), 2 `GONE`,
  3 `ZORDER`, 4 `PIXELS` (id, partie 0 contenu / 1-2 cadre, 16/32 bits, LZ4 ou brut, rectangle),
  5 `END` (fin de ronde), 8 `SCREEN` (nouvelle taille), 9 `CAPS`, 10 `PING`, 11 curseur,
  12 `COPY` (rectangle déplacé) ;
* client → serveur : 1 `READY` (ronde suivante ; jusqu'à 3 en vol), 2 `PTR` (id, x, y,
  boutons, molette), 3 `KEY`, 4 `RAISE`, 5 `CLOSE`, 6 `CHAR`, 7 `DESKTOP`, 8 `PONG`, 9 `MOVE`.
Le bureau est la pseudo-fenêtre `0xFFFFFFFF`. rdpd ARMPI fait une ronde à chaque `READY` :
`WS_OP_LIST`, pour chaque fenêtre dont la génération a bougé `WS_OP_WIN_LIRE` des bandes
changées, compression **LZ4** (codeur simple, en assembleur), `END`. Les essais Python d'Onyx
(`tools/tests/rdpd/`, `fake_rdpd.py`) servent de référence.

### 5.5 Une séance d'essai automatique (script PC `tools/armpi_distant.py`)

1. `.\build.ps1` sur le PC.
2. **Envoi** par FTP des fichiers changés (`XCRC` comparé) : services, applications, kits,
   `.mod`, `kernel8.img`.
3. Noyau ou module changé : `reboot` (ou `tryboot`) par telnet ; attendre que le port 23
   réponde de nouveau (60 s ; sinon : « couper l'alimentation »).
4. **Commandes** par telnet ; chaque commande suivie d'un repère (`echo @FIN <code>`) pour lire
   sa sortie et son code de sortie.
5. **Interface** : touches et clics par VNC (`vncdotool`), captures (`screenshot` puis FTP `RETR`,
   ou une image VNC), comparées à des images de référence.
6. **Rapatrier** : `SD0:/journal.txt`, `klog > RAM0:/klog.txt`, captures, fichiers produits.

## 6. Dépendances

**Section 4 (fichiers)** :
* les pilotes lisent leur firmware (`brcmfmac43455-sdio.bin`, `.txt`, `.clm_blob`, ≈ 600 Ko)
  par AppKit (`open`/`read` : relais 12–22 aujourd'hui, descripteurs dans appkit ensuite —
  même interface) ; le mappage sans copie d'un fichier entier serait bienvenu ;
* `wifi` réécrit `wifi.txt` : sans `rename` dans le protocole FS, une coupure pendant
  l'écriture abîme le fichier → demander `rename` (ou écriture atomique) à la section 4 ;
  ftpd en profiterait (`RNFR/RNTO`) ;
* `reboot` : une op « tout vider » de `vfs` vers les services FS ;
* telnetd : un processus fils avec **entrée et sortie** en tubes (aujourd'hui `spawn_pipe` ne
  donne que la sortie) — « descripteurs transmis au fils » de la section 4 — et un
  interpréteur de commandes en programme (le « terminal virtuel » demandé) ;
* aucun blocage au démarrage : netd et ses pilotes se chargent après `vfs` ; netd lit ses
  fichiers dans son second thread (pas d'interblocage avec un futur FtpFS client de netd).

**usbd** : lecture bulk non bloquante (étape 1) — aujourd'hui `xx_bulk` bloque jusqu'à 5 s dans
l'unique thread de usbd ; DWC2 : un NAK rend 0 ; xHCI : un TRB IN laissé en attente et regardé
dans `xx_scruter`. Débit de usbnet limité par les allers-retours vers usbd (sans gravité).

**Noyau** : `SR_MAX` (64 requêtes en cours pour tout le système) est trop bas avec des réponses
différées (une par `recv` bloqué) → 256 ; le droit `SD0:/sys` visible des services dans le
message (pour Elegant) ; plus tard le futex entre process.

## 7. Risques

* **Wi-Fi** : jamais validé ; SDIO en PIO par l'Arasan depuis l'espace utilisateur (attentes
  après écriture : à mesurer) ; firmware de 600 Ko poussé en CMD53.
* **QEMU** : la carte SD y est sur l'**Arasan**, le contrôleur du Wi-Fi sur le vrai Pi : le
  pilote `wifi` ne doit jamais y être lancé (hwmanager s'en charge) ; un `dev_map` **exclusif**
  (refus d'une zone déjà mappée par un autre process vivant) protégerait de cette erreur.
* **GPIO partagés** : `gt911` (GPIO 2/3) et `wifi` (34–39, 48–53) écrivent des registres
  `GPFSEL`/pull différents : pas de conflit aujourd'hui, mais aucun arbitre.
* **Mémoire** : 128 Ko par process client + 260 Ko par interface + 64 sockets × 32 Ko ; tout
  dans le premier Go tant que le noyau n'en gère pas plus.
* **Accès à distance sans mot de passe** : un shell, l'écriture de `SD0:` et le redémarrage pour
  quiconque atteint le Pi ; acceptable seulement sur le réseau de la maison (décision assumée).
* Noyau poussé par FTP qui ne démarre pas : il faut retirer la carte (d'où `tryboot`).

## 8. Questions ouvertes (pour l'utilisateur)

1. **Le RJ45 du PocketTerm est-il accessible ?** Sinon l'étape 4 (GENET) se teste sur un Pi 4
   nu ou attend ; et avez-vous un **adaptateur USB-Ethernet ECM** pour l'étape 1 sur le Pi ?
2. Faut-il un **pilote par interface** (proposé) ou un seul `netdrv` comme l'écrit la feuille
   de route ?
3. **Élaguer le Wi-Fi noyau** avant de l'avoir fait marcher en service (il n'a jamais marché) ?
4. Les **clés Wi-Fi** en clair dans `wifi.txt`, lisible par tout programme : acceptable (comme
   Onyx), ou un fichier lisible des seuls services ?
5. `tryboot` et le **chien de garde du noyau** : les mettre en place avec l'étape 10 ?
6. Serveurs d'accès à distance : **lancés par défaut** sur la carte de développement, ou
   seulement à la demande (`services.txt` à éditer) ? Limités au **réseau local** (refus des
   adresses hors du sous-réseau) en attendant l'authentification ?
7. vncd avant rdpd (proposé, pour `vncdotool`) — ou rdpd d'abord pour Onyx Remote ?
8. IP fixe, nom d'hôte (`SD0:/etc/net.txt`) : utile maintenant, ou DHCP seul ?
9. **Où vit l'heure** donnée par NTP (le Pi n'a pas d'horloge sauvegardée ; l'ABI n'a que
   `time_ms`) : un appel noyau « heure UTC » (régler / lire), ou un petit service ? Et
   `pays=` / `fuseau=` dans `options.txt` (proposé) ou un `SD0:/etc/region.ini` à part ?

## Dernier point du projet (hors section 5)

* **Authentification** des serveurs d'accès à distance (telnetd, ftpd, vncd, rdpd) — à la toute
  fin du projet, une fois les phases d'essai validées (décision de l'utilisateur).

## 9. Réponses de l'utilisateur (29/10/2026)

1. Le PocketTerm a le **RJ45 du Pi 4** accessible : l'étape 4 (GENET) se teste directement dessus.
   **Pas d'adaptateur USB-Ethernet** : l'étape 1 (usbnet) se valide seulement dans QEMU ; sur le
   PocketTerm, le réseau se teste à partir de l'étape 4 (GENET).

## 10. Tel qu'écrit (29-30/10/2026, branche de travail ; QEMU, PocketTerm pas encore essayé)

Retour arrière : la branche **`reseau-noyau`** est posée sur le dernier commit avant
l'élagage (noyau avec `net.asm`, `wifi.asm`... et les services déjà présents).

### 10.1 Ce qui existe

| Pièce | Fichiers | État |
|---|---|---|
| **usbd** : op 7 `USB_OP_BULK_NB` (lecture bulk IN sans attente) | `src/svc/usbd/dwc2.inc` (canal 6 armé en permanence, le contrôleur relance sur NAK), `usbd.asm` | DWC2 : fait ; xHCI : `E_NOSYS` (aucun adaptateur USB-Ethernet sur le PocketTerm) |
| **usbnet** (ECM) | `src/svc/usbnet/usbnet.asm` | QEMU : OK. Clés `usb:02/06` et `usb:02/02/ff` : QEMU `usb-net` présente d'abord RNDIS, usbnet lit chaque configuration et choisit l'ECM (`SET_CONFIGURATION`). Lien supposé monté (l'extrémité d'interruption n'est pas lue) |
| **netd** | `src/svc/netd/` : `netd.asm` (threads, interfaces, tampons des process), `stack.inc` + `macros.inc` + `stackdata.inc` (pile de `net.asm`, générée une fois puis éditée), `ops.inc` (ops 2-15, reprises des appels 40-52), `background.inc` (notifications, NTP, fuseau) | QEMU : DHCP, ARP, ping, DNS, TCP (httpd, navigateur, dltest 4 Mo, ultest 300 Ko), UDP, NTP |
| **anneau de trames** | `src/inc/netd.inc` (ops, disposition), `src/svc/inc/netif.inc` (côté pilote) | 64 + 64 emplacements de 2 Ko, compteurs monotones, `dmb`/`ldar`/`stlr` ; le pilote redit son état toutes les 2 s et se réinscrit si netd a été relancé |
| **NetKit** version 30 | `src/kits/netkit/` | `net_*` (sockets, DNS, ping, `net_info`, `net_active`, `net_ifaces`, `net_time`, `net_ntp_sync`), `wifi_*` ; macro `netcall` (garde x30) ; tous les registres sauf x0 gardés (conversion mécanique `sys SYS_x` -> `netcall net_x`) |
| Programmes convertis | `ping`, `httpget`, `httpd`, `dltest`, `ultest`, `wifi`, `browser`, `menubar` | QEMU : OK (navigateur : info.cern.ch) |
| **genet** | `src/svc/genet/genet.asm` (repris de `arch/pi4/genet.asm`) | QEMU : sort aussitôt (code 0). **Pi : à essayer** |
| **hwmanager** : SoC | `src/svc/hwmanager/hwmanager.asm` | révision de la carte (Pi 4 B 0x11, Pi 400 0x13, CM4 0x14) ; `soc:genet`, `soc:wifi` ; relance ×3 d'un pilote sorti en erreur ; options `no_wifi`, `no_ethernet` |
| **wifi** | `src/svc/wifi/` : `wifi.asm` (service, réseaux connus, connexion automatique, simulation), `chip.inc` + `sdio.inc` (repris de `wifi.asm` / `sdio.asm` du noyau), `sdhci.inc` | QEMU : sort aussitôt ; `wifi test` (simulation) pour l'applet et le menu. Puce : fonctionne sur le PocketTerm (performances : plus bas) |
| **Élagage** | noyau sans `net.asm`, `frames.asm`, `wifi.asm`, `sdio.asm`, `genet.asm` ; appels 36-52, 90-92 -> `E_NOSYS` ; `bf_noms` = `options.txt` seul | noyau Pi 4 : 88 968 -> 57 696 octets (avec `random.asm`) |
| Applet **Wi-Fi**, **Région** (pays, fuseau), **wifimenu**, icône de la barre | `src/apps/control/applets/wifi.inc`, `keyboard.inc`, `src/sys/wifimenu/`, `src/sys/menubar/`, `src/inc/wifidraw.inc` | QEMU avec `wifi test` |
| **NTP** | `background.inc` de netd | QEMU : heure juste, `clock.cfg` écrit (la barre et l'Horloge la montrent) |
| **sh**, **telnetd**, **ftpd** | `src/bin/sh/`, `src/svc/telnetd/`, `src/svc/ftpd/` | QEMU : OK (Python) |
| **reboot**, **screenshot** | `src/bin/reboot/`, `src/bin/screenshot/`, init2 op 3, Elegant op 13 (`snapshot.inc`) | QEMU : OK (QEMU redémarre ; BMP juste) |
| **vncd** | `src/svc/vncd/vncd.asm` | QEMU : image (8 et 32 bits), clics, touches |
| **Aléa du noyau** | `src/kernel/random.asm`, `src/inc/chacha20.inc`, appel 101 `getrandom`, `src/bin/rngtest` | QEMU : ChaCha20 conforme à la RFC 8439, 1 Mo en 214 ms |
| **rdpd** | `src/svc/rdpd/rdpd.asm`, Elegant `remote.inc` (ops 14-17) | QEMU : protocole d'Onyx Remote (voir 10.7) |
| **TLS** | OpenSSL ([OPENSSL.md](OPENSSL.md)), dans curl, WebKit et Jet | QEMU et Pi : HTTPS dans Jet (voir 10.4) |

### 10.2 Choix faits (questions de la section 8 sans réponse : la proposition du document)

1. (répondu) RJ45 accessible ; pas d'adaptateur USB-Ethernet.
2. **Un pilote par interface** (`usbnet`, `genet`, `wifi`), comme proposé.
3. **Wi-Fi du noyau élagué** avant d'avoir marché en service (il n'avait jamais marché).
4. **Clés en clair** dans `wifi.txt` (comme Onyx) ; jamais écrites au journal ni rendues par
   le service (`CONNUS` : SSID, priorité, sécurité seulement).
5. **`tryboot`** : `reboot tryboot` (drapeau `SET_REBOOT_FLAGS` du firmware) ; **chien de garde
   du noyau : pas fait**.
6. Serveurs d'accès à distance **lancés par défaut** (lignes de `services.txt` ; `build.ps1 sd`
   les ajoute à la carte) ; **pas** de limite au réseau local (pas de mot de passe non plus :
   décision de l'utilisateur).
7. **vncd avant rdpd** (rdpd fait ensuite : 10.7).
8. **DHCP seul** : pas de `SD0:/etc/net.txt` (ni IP fixe, ni nom d'hôte).
9. **L'heure vit dans netd** (op 16 `NET_OP_TIME`, `net_time` de NetKit) et dans
   `SD0:/etc/clock.cfg` (le décalage déjà lu par la barre et l'Horloge, écrit après chaque
   synchronisation) : pas d'appel noyau. **`pays=`, `fuseau=`, `ntp=` dans `options.txt`**.

### 10.3 Écarts avec le document

* **netd n'a pas un seul thread** : la pile tourne dans un thread (anneaux, minuteries), mais
  les requêtes sont servies par **24 threads** qui attendent eux-mêmes (connect, recv, accept,
  DNS, ping), sans le verrou de la pile, comme les appels 40-52 du noyau : la pile du noyau a
  été reprise presque telle quelle. Les « réponses différées » sont remplacées par cette attente
  bornée à 25 s (`E_AGAIN` : NetKit redemande tant que le délai demandé n'est pas écoulé). Un
  thread de fond lit les fichiers et parle à notifyd.
* **Pas d'avis `IF_SONNETTE`** (l'op 22 est acceptée, sans effet) : le thread de la pile dort dans le
  service `net.rx` quand tout est vide, après avoir posé `NR_SLEEP` dans chaque anneau ; le pilote qui
  pose une trame voit ce drapeau et lève `NR_SIG_RX` (`netif_rx_put`). Sous charge, aucun appel. Elle
  dort jusqu'au prochain temporisateur (`net_wait_ms`) ; pendant une longue attente, `net_long` fait
  réveiller la pile par chaque requête, qui a pu armer un temporisateur plus proche.
* **Boucle locale** : `ip_output` rend à `netd` lui-même ce qui va vers 127.0.0.0/8 ou sa propre adresse
  (`ip_is_local`), par `lo_ring` (256 trames de 2 Ko, sans passer par une interface ni par ARP),
  vidée en tête de boucle par `lo_drain`. L'adresse source suit (`ip_src`) : 127.x vers 127.x, `net_ip`
  sinon. `dns_resolve` rend 127.0.0.1 pour `localhost`. `connect`, `bind`, `accept` et `ping` y
  passent ; `posixtest net` y fait passer 2 Mo vérifiés. Le chemin ne dépend pas d'une interface, mais
  ce n'est pas essayé sans interface active.
* **Une seule adresse à la fois** : l'interface active (lien monté, plus haute priorité) a le
  DHCP ; quand elle change, l'adresse est oubliée et le DHCP relancé (comme `netif_set`). Pas
  de DHCP par interface, pas de renouvellement du bail (comme avant).
* `WS_OP_SCREEN` existait déjà (taille de l'écran) : l'op de capture s'appelle
  **`WS_OP_SNAPSHOT`** (13) ; elle n'est pas réservée aux programmes de `SD0:/sys` (le noyau ne
  marque pas le message).
* vncd : **Raw seulement** (pas de CopyRect).
* Wi-Fi : `proto=` et `key_mgmt=` sont lus et ignorés (le firmware choisit WPA/WPA2 d'après le
  balayage) ; les commentaires de `wifi.txt` sont perdus quand le service le réécrit ; une
  copie `wifi.bak` est écrite d'abord (pas de renommage dans le protocole des fichiers).
* Les sous-commandes de `wifi` passent à l'anglais (`join`, `leave`, `known`, `forget`,
  `priority`, `country` ; `connecter` et `quitter` restent acceptés).

### 10.4 TLS (étape 13) : OpenSSL

Le TLS du système est **OpenSSL** ([OPENSSL.md](OPENSSL.md)), lié en statique dans curl, WebKit et
Jet, avec les certificats vérifiés par `/etc/ca-bundle`. TLSKit (mbedTLS, certificats non vérifiés)
et son essai `httpsget` ont été retirés le 30/10/2026. **Les programmes en assembleur n'ont plus de
TLS**.

### 10.5 Essais sous QEMU (`tools/qemu_drive.py --fwd`, `tools/armpi_remote.py`)

* `ping` sans argument (passerelle et `example.com`) ; `ping HOTE [N]` (N pings, 4 par défaut, et le
  bilan), `ping -t HOTE` (jusqu'à Ctrl+C) ; `httpget`, `httpd` vu de l'hôte, navigateur,
  `dltest` 4 Mo (somme juste), `ultest` 300 Ko (motif juste) ; NTP ; démarrage complet après
  l'élagage.
* telnet : `pwd`, `ls`, `hello un deux`, `cd`, `mem`, une commande inconnue (code −2) ;
  ftp (`ftplib`) : racine (volumes), `CWD` relatifs et absolus, `LIST`, `NLST`, `SIZE`,
  `RETR`, `STOR` de 300 Ko relu identique, `XCRC` égal au CRC32 du PC, `MKD`, `DELE` ;
  `screenshot` puis `RETR` ; `reboot` (QEMU redémarre).
* VNC : capture 1024×768 en 8 et 32 bits, clics (fermer Bienvenue, ouvrir le Terminal),
  touches (`ls` + Entrée).
* Débit d'émission sous QEMU : **≈ 50-80 Ko/s** (le DWC2 émulé envoie `usb-net` en pleine
  vitesse) : une image VNC entière prend 18 s en 8 bits. La réception va à ≈ 800 Ko/s. Sur le
  Pi, l'Ethernet (GENET) n'a pas cette limite.
* Wi-Fi simulé (`SD0:/sys/svc/wifi test` dans `startup.txt`) : menu de la barre, connexion à
  un réseau connu, clé saisie pour un inconnu, applet (état, listes, priorité, oubli),
  lecture de `wifi.txt` avec les anciennes clés, commentaires, guillemets, `#` dans une clé.

### 10.6 À essayer sur le PocketTerm

1. `.uild.ps1 sd E:` (ajoute à la carte les lignes de `services.txt` et `drivers.txt` sans
   toucher aux réglages) ; retirer `no_wifi` / `no_ethernet` d'`options.txt` s'ils y sont ;
   RNG200 : actif par défaut depuis l'essai sur le PocketTerm (il répond) ; `no_rng200` le coupe.
2. Journal (`SD0:/journal.txt`, ou `cat` par telnet) : `hwmanager : carte, révision ...`,
   `hwmanager : soc:genet -> pid`, `genet : Ethernet GENET v5 prêt, MAC ...`, `netd :
   interface eth0`, `genet : lien 1000 Mb/s ...`, `NET : DHCP ok, adresse ...`, `netd : NTP
   ...`, `Aléa : ...` (RNG200), `wifi : N réseau(x) connu(s)`, `WIFI : puce 00004345 ...`
   (docs/WIFI.md pour la suite), `wifi : connexion automatique à ...`.
3. Câble RJ45 : `ping`, puis depuis le PC `python -I tools/armpi_remote.py telnet IP "mem"`,
   `... ftp IP list /SD0/etc`, `... vnc IP capture.png`, `SD0:/bin/curltest https://example.com/`.
4. Sans câble : le Wi-Fi seul (`wifi`, `wifi scan`, l'applet, le menu de la barre).
5. Client telnet de Windows : `telnet` puis `set localecho`, `open IP` (le serveur n'a pas
   d'écho ni d'édition de ligne) ; PuTTY en Telnet fonctionne tel quel.
6. **rdpd** (10.7) : `python -I tools/rdp_test.py IP --capture ...` puis le vrai client
   **Onyx Remote** (`pc/OnyxRemote` d'Onyx) vers `IP`, port 3390 : fenêtres avec et sans
   cadres Onyx, 16 et 32 bits, bureau coché ; clic, saisie (Bienvenue, Terminal : `ls`,
   Ctrl-C), Entrée, flèches ; fermer une fenêtre depuis le PC ; la déplacer ; la passer devant
   (clic sur une fenêtre couverte sur le Pi) ; débrancher le câble 20 s puis le rebrancher
   (le client se reconnecte, rdpd prend le nouveau client) ; débit d'une fenêtre animée.

### 10.7 rdpd (étape 12)

* **Serveur** `SD0:/sys/svc/rdpd` (ligne de `services.txt`, ajoutée aussi par `build.ps1 sd`),
  port 3390, protocole d'Onyx (`user/BinUtils/rdpd.c`) **sans rien changer au client** Onyx
  Remote : salut `ONYXRDP1` + largeur, hauteur, **96** (le client y lit la version de l'ABI
  d'Onyx et veut au moins 56 : 96 convient aux deux lectures, « DPI » du §5.4) ; options du
  client 1 (16 bits), 2 (sans cadres), 4 (pipeline : `CAPS` protocole 2, 3 rondes en vol) ;
  messages `WIN`, `GONE`, `ZORDER`, `PIXELS`, `END`, `SCREEN`, `CAPS`, `PING` (client en
  pipeline silencieux 2 s ; fin de session après 12 s sans rien), `CURSOR` (la flèche, une
  fois) ; entrées `READY`, `PTR`, `KEY`, `CHAR`, `RAISE`, `CLOSE`, `DESKTOP`, `PONG`, `MOVE`.
* **Rondes** : une par crédit, seulement si quelque chose a changé ; `WS_OP_WIN_LIST`, puis pour
  chaque fenêtre dont la génération du contenu a bougé, lecture par bandes (`WS_OP_WIN_READ`
  dans le tampon partagé d'Elegant), comparaison par carrés de 64 × 64 avec la copie du client,
  les carrés changés côte à côte envoyés en un rectangle ; le cadre (deux copies, 32 bits) quand
  sa génération bouge. **LZ4** (codeur d'Onyx, en assembleur) quand il raccourcit, sinon brut ;
  16 bits RGB565 si demandé, sauf cadres et fenêtres `WF_ALPHA`. Drapeaux d'Onyx : sans cadre
  1, fond 2, dessus 4 (les mêmes bits que `WF_*`), `ALPHA` 32 ; état 1 (au clavier), 4
  (réduite).
* **Entrées** : pilote virtuel de HIDManager (comme vncd) ; le pointeur revient à sa place
  (la fenêtre + le point ; au clic, `WS_OP_RAISE` d'abord si elle n'a pas le clavier) ;
  `CHAR` = caractère déjà traduit (appui + relâche ; Ctrl + lettre : 1..26 → la lettre, Ctrl
  tenu) ; `KEY` = touches spéciales et modificateurs (table de vncd).
* **Écarts** :
  - **Pas d'op curseur** (`WS_OP_CURSEUR`) : Elegant n'a que la flèche, rdpd l'annonce une fois.
  - Ops nommées en anglais : `WS_OP_WIN_LIST` (14 ; `WS_OP_LIST` 7 reste tel quel pour le
    dock), `WS_OP_WIN_READ` (15), `WS_OP_WIN_MOVE` (16), `WS_OP_WIN_CLOSE` (17) ; **pas
    réservées à `SD0:/sys`** (comme `WS_OP_SNAPSHOT` : le noyau ne marque pas le message).
  - Les fenêtres `WF_BACK` (fond d'écran) ne sont pas dites une à une (le client ne les montre
    pas) : elles sont dans le **bureau** (`0xFFFFFFFF`, composé par Elegant : fond + couche du
    fond), envoyé seulement si le client le demande.
  - Pas de `COPY` (option 8 ignorée : le client envoie les pixels), pas de sondes `PING`
    rapprochées après une perte (seulement le `PING` d'inactivité), pas de mode PocketUI ni
    plein écran (Elegant n'en a pas), pas de journal de statistiques.
  - Un nouveau client prend la place de l'ancien (`net_accept` sans attente toutes les
    500 ms), sans thread d'acceptation.
  - `malloc` d'AppKit n'a pas de `free` : la copie du contenu d'une fenêtre est gardée par sa
    place (34) et réutilisée ; elle n'est remplacée (l'ancienne perdue) que pour une fenêtre
    plus grande.
* **Essais (QEMU)** : `tools/rdp_test.py` (poignée de main, une ronde, contenu de la fenêtre
  de devant comparé à une capture : 47 564 / 47 600 points égaux pour Bienvenue (le pointeur),
  91 200 / 91 200 pour le Terminal ; clic + texte + Entrée : `ls` tapé dans le Terminal, ses
  pixels reviennent ; 16 bits) ; **`Connection.cs` et `Lz4.cs` du vrai client**, copiés tels
  quels dans un petit programme console (.NET 4.8, hors du dépôt) : 0 message abîmé, pipeline
  (3 en vol, protocole 2), `PONG`, bureau, saisie dans Bienvenue, `MOVE` (la fenêtre bouge sur
  le Pi, `WIN` revient avec la nouvelle place), `CLOSE` (Bienvenue se ferme, `GONE`), 32 et
  16 bits. La première ronde (3 fenêtres et leurs cadres) part en 1,7 s sous QEMU. Le client
  graphique Onyx Remote se compile (SDK .NET 10) mais n'a pas été lancé (interface à piloter à
  la main).

## Performances du Wi-Fi (30/10/2026)

Mesures sur le PocketTerm, `SD0:/bin/tcpbench` et `tools/tcpbench.py` : aller-retour et débit TCP
sans disque, avec les compteurs de `NET_OP_STATS` (temps par trame, tours du pilote, retransmissions,
état de la liaison). Le Wi-Fi varie beaucoup : plusieurs tours, la médiane.

| Étape | Aller-retour | Le Pi envoie | Le Pi reçoit |
|---|---|---|---|
| Référence | 10,5 ms | 0,72 Mo/s | 0,64 Mo/s |
| Écritures SDIO : 2 cycles SD, sans les 2 µs de plus | 6,1 ms | 1,60 | 1,64 |
| + 5 GHz d'abord, sondage par `Intpend`, bus à 50 MHz, `ampdu_tx` 0, *frame bursting* | 2,1 ms | 2,49 | 2,37 |
| + tampons et fenêtre TCP de 64 Ko | 2,4 ms | 4,77 | 3,72 |
| + RTO minimal de 1 s | 3,0 ms | 5,07 | 4,10 |
| + NewReno, retransmission rapide, accusés retardés | 3,0 ms | 4,63 | 3,80 |
| + *window scaling*, tampons de 256 Ko (deux séries de 6 tours) | 2,1 à 2,6 ms | 4,55 à 5,80 (8,75 au mieux) | 6,66 à 7,83 (8,76 au mieux) |
| + cœur 0 libéré (`hdmiaudio` y prenait 84 %, ABI.md, 8 tours) | 3,0 ms | 7,56 (10,02 au mieux) | 5,84 (9,19 au mieux) |
| Essai du 31/10, sans les sondes (16 tours, en alternance avec la ligne suivante) | 2,3 ms | 1,21 (0,03 à 6,69 ; 2 à 3 RTO par tour) | 2,29 |
| + sondes de fin de rafale (16 tours) | 2,2 ms | 5,97 (1,02 à 8,29 ; moins de 0,5 RTO par tour) | 2,62 |
| + banc corrigé (8 tours) | **3,0 ms** | **6,60** (1,92 à 7,77) | **5,18** (1,38 à 6,32) |
| Sans sondage, interruption SDIO (quatre séries de 6 tours, médianes) | 2,0 à 3,0 ms | 6,3 à 8,9 | 4,8 à 8,6 |

Le même jour, 40 pings et 5 connexions FTP après chaque redémarrage (A/B, option `no_irq`) : 7 et 2
pings perdus, FTP 5/5 avec l'interruption ; 31 et 28 perdus, FTP 3/5 et 5/5 sans elle. Une série de
6 tours a donné 9,63 et 9,29 Mo/s, mais sur une version en sondage installée par erreur (`reboot` non
exécuté) : état non contrôlé, à ne pas retenir sans refaire la mesure.

* **L'économie d'énergie de la puce** : `WLC_SET_PM` donné avant la connexion ne suffisait pas : la
  puce y revenait selon les démarrages (aller-retour de 130 à 400 ms, l'intervalle des balises, avec
  un Pi inoccupé). Elle est coupée à nouveau après la connexion, et lue toutes les 2 s dans les
  compteurs. C'était la cause des coupures et des mesures incohérentes de la journée.
* **La puce est remise à zéro au démarrage** (`WL_ON`, ligne 129 du firmware) : `reboot` ne l'atteint pas.
* **5 GHz d'abord** : même SSID sur les deux bandes, `WLC_SET_BAND` à 5 GHz pour le premier essai ;
  un échec rend le choix à la puce. Le canal est au journal.
* **Pas de cœur réservé** : essayé (thread de la puce seul sur le cœur 3), aucun gain mesurable sur
  six tours avec et sans, donc retiré. `netd` et les pilotes restent des process séparés.
* Le sondage serré doit passer par `Intpend` (une commande courte) : sans cela, marteler `INTSTATUS`
  sur le bus interne fait tomber le débit.
* **TCP de `netd`** : contrôle de congestion NewReno, retransmission rapide, RTO minimal de 1 s (200 ms
  provoquait de fausses expirations avec les accusés retardés de Windows), un accusé pour 8 segments
  pleins (20 ms au plus), *window scaling* (décalage 3), sondes de fin de rafale (plus bas).
* Le débit suivait la fenêtre : 16, 32, 64 Ko donnaient 2,5, 3,2, 4,8 Mo/s. Le pilote passe 99 % de
  ses tours à vide : le passage par l'anneau entre pilote et `netd` n'est pas la limite.
* **Pourquoi le débit était irrégulier (31/10) : les accusés du PC, pas nos données.** Trace de chaque
  segment dans `netd` : au moment du silence, 130 Ko en vol (la fenêtre du PC) ; 1 s plus tard le RTO
  renvoie le premier segment et, 7 ms après, l'accusé couvre **tout** : le PC avait tout reçu. Le PC
  ne parle plus (il n'a plus rien à accuser), le Pi attend : c'est une coupure de quelques dizaines de
  millisecondes qui coûte 1 s, et aucun accusé dupliqué ne peut exister, d'où `rapides` à 0.
* **Ces coupures ne viennent ni de `netd` ni du pilote `wifi`.** Le sens PC vers Pi s'interrompt par
  tranches de 100 à 200 ms, environ une par seconde, 2 à 55 % du temps selon le moment, au repos comme
  en charge. Sondes ICMP numérotées : sur 184 perdues, aucune n'a atteint `netd`. Le pilote lit tout ce
  que donne la puce (`rx_n` égale le compte de `netd`). Pendant ces coupures l'UDP du Pi vers le PC passe
  (5253 datagrammes sur 5254) et les balises arrivent (98 sur 100). Sans effet : `roam_off`, `ampdu_rx` 0,
  `txbf_bfe_cap` 0, ni plus aucune lecture périodique de la puce. Depuis le Pi : 12 pings sur 12 vers
  la box et chaque appareil de la maison, mais 9 sur 12 vers le PC. Depuis le PC : 0 % vers la box et
  le décodeur (sans doute en câble), 5 à 65 % vers ce qui est en Wi-Fi, même Pi éteint ; un
  second PC identique fait pareil. Restent en cause le relais de la box entre deux appareils en Wi-Fi,
  ou le couple Intel AX201 et Windows de ces deux PC : non tranché, il faut un autre appareil que ces PC.
  Une télé ou une imprimante en veille répond mal : seule la cible Pi (`PM` 0) est probante.
* **Sondes de fin de rafale** (idée de RFC 8985, TLP) : pour ne plus payer 1 s, `tcp_timers` renvoie le
  dernier segment au bout de max(2 × SRTT, 60 ms), puis du double, quatre fois au plus avant le RTO. Un
  segment déjà reçu force un accusé immédiat ; la fenêtre de congestion n'est pas touchée (rien n'est
  perdu), et aucune sonde pendant une récupération. Compteur `sondes` de `NET_OP_STATS` : 0 à 29 par tour.
  Le RTO minimal reste à 1 s.
* **Réception** : le PC perd ses propres segments dans les mêmes coupures et se rattrape seul, à son
  rythme : on n'y peut rien côté Pi (`netd` ne propose pas SACK, à essayer si le besoin revient).
* **Banc** : `tcpbench` ne servait qu'une connexion à la fois et s'arrêtait sur le RST du client. Ce RST
  perdu dans une coupure, le tour suivant restait en attente : 0,06 Mo/s en réception (les 256 Ko de
  fenêtre en 5 s) et « compteurs illisibles » n'étaient pas des mesures. `S` prend maintenant une durée.

## Le mot de passe de l'accès à distance

telnet, FTP, VNC et RDP demandent le même mot de passe, choisi au premier démarrage (Setup) ou dans Réglages, entrée
Accès à distance. Il est dans `SD0:/etc/remote.txt` (`password = ...`), relu à chaque connexion (`src/inc/remotepw.inc`),
et n'entre dans aucun paquet. Sans lui, toute connexion est refusée. Les décisions plus haut, « sans mot de passe », sont
remplacées par celle-ci (3 novembre 2026).

| Service | Comment |
|---|---|
| telnet (23) | `Mot de passe :`, écho coupé par IAC WILL ECHO ; 3 essais, 2 s entre chacun |
| FTP (21) | `USER` libre, `PASS` vérifié ; avant, seules USER, PASS, SYST, FEAT, OPTS, QUIT et AUTH répondent |
| VNC (5900) | l'authentification VNC (type 2) : défi DES, `src/svc/vncd/des.inc` ; seuls les 8 premiers caractères comptent |
| RDP (3390) | salut `ONYXRDP2`, défi de 16 octets, réponse HMAC-SHA256 (`src/svc/rdpd/sha256.inc`) ; client : Korin Remote (`tools/remote`) |

Il n'y a pas de chiffrement : telnet et FTP font passer le mot de passe en clair. À garder sur un réseau de confiance.

