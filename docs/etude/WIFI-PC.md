# Le Wi-Fi du PC : Intel AX201 (étude du 13/11)

But du chantier : le Wi-Fi du vrai ZBook branché sur netd **exactement comme le Wi-Fi du Pi** : interface `wlan0`,
service « wifi » (balayage, connexion WPA2/WPA3-personnel, réseaux connus de `SD0:/etc/wifi.txt`, même protocole :
`docs/RESEAU.md` 4.3), même volet Réglages, même `wifimenu`. Le pilote s'appelle **`iwl`** (`src/svc/iwl`, C, PC
seulement). Rien de tout cela n'existe sous QEMU : chaque étape est essayée par l'utilisateur sur le ZBook.

Sources : la documentation publique (802.11-2020, RFC 2104, 3394, 8018, FIPS 180-4, 197), les fichiers publics de
`linux-firmware`, et le pilote **iwlwifi** de Linux (v6.12) comme **référence des séquences et des valeurs** : GPL, il
est décrit ici et réécrit ; aucun code n'en est repris (les structures sont posées octet par octet dans `cmds.c`, puis
vérifiées par une construction indépendante, `tools/pc/iwltest`).

## 1. Le matériel

| | |
|---|---|
| Fonction | `0000:00:14.3`, **`8086:43f0`**, classe 02/80 (réseau, « autre »), MSI-X 16 (et MSI), BAR0 16 Ko |
| Nature | **CNVi** (« connectivity integration ») du PCH Tiger Lake-H : la partie MAC (et le processeur du micrologiciel) est dans le PCH, la radio est le module **CRF M.2 « Hr2 »** (AX201) branché par le bus CNVio. Le Bluetooth du même module est un périphérique USB à part. |
| Famille iwlwifi | **22000**, transport **« gen2 »** (context info, TFD longs, réception multi-files), configuration `iwl_qu_long_latency_trans_cfg` : puce intégrée, quartz lent (**12 ms**), LTR appliquée après **2,5 ms** |
| `CSR_HW_REV` | type du MAC bits 15:4 : **0x35 = QuZ** (Tiger Lake), 0x33 = Qu (Ice Lake ; étape B ou C dans les bits 3:0). Attendu sur le ZBook : `0x354` (journaux publics des machines Tiger Lake : « REV=0x354 ») |
| `CSR_HW_RF_ID` | type de RF bits 23:12 : **0x10A = Hr2** (AX201), 0x10C = Hr1 (AX101), 0x105/0x108 = Jf (9560) |
| Adresse MAC | dans les registres CSR 0x388/0x38C (« strap », valeur de l'OEM), sinon 0x380/0x384 (OTP) ; octets dans l'ordre 3,2,1,0 puis 1,0 du mot |

**SoftMAC.** Le CYW43455 du Pi est *FullMAC* : son micrologiciel fait tout le 802.11 (balayage, authentification,
association, WPA2 avec la phrase secrète), le pilote ne fait que des ioctls. L'AX201 est **SoftMAC** (« MVM ») : le
micrologiciel fait le temps réel (canaux, émission, accusés, agrégation, chiffrement CCMP en matériel, balayage
« offload »), **l'hôte fait le reste** : choisir le réseau, envoyer authentification et association (trames de
gestion), le **supplicant** WPA2 (EAPOL 4 voies, PMK, PTK, GTK) et installer les clés dans la puce ; WPA3-SAE
(Dragonfly) aussi. C'est l'essentiel du chantier (§ 7).

## 2. Le micrologiciel : lequel, d'où, comment il voyage

**Nom.** iwlwifi choisit `iwlwifi-<MAC>-<étape>-<RF>-<étape>-<API>.ucode` d'après les deux registres : QuZ + Hr →
**`iwlwifi-QuZ-a0-hr-b0`** ; Qu étape B → `iwlwifi-Qu-b0-hr-b0`, étape C → `iwlwifi-Qu-c0-hr-b0` (même règle dans
`fw.c : fw_name_for`, vérifiée par `iwltest`). L'API la plus haute que Linux demande pour la famille 22000 est **77**
(`IWL_22000_UCODE_API_MAX`, qui est aussi le minimum aujourd'hui) : on prend **`iwlwifi-QuZ-a0-hr-b0-77.ucode`**,
version **77.3cda2a2c.0** (« release/core74::3cda2a2c »), 1 406 720 octets, sha256 `8f1b9bb9…8dde77`.

**PNVM.** Pas de fichier `.pnvm` pour cette famille (le PNVM et l'IML sont ceux des AX210 et suivants, « So »,
« Ty », « Ma ») : le fichier ne contient pas de TLV IML, et Linux n'en cherche pas pour un Qu. Mais quand la
notification « vivant » donne un identifiant de SKU non nul, Linux sonne quand même la sonnette PNVM
(`UREG_DOORBELL_TO_ISR6`, bit 20) et attend `PNVM_INIT_COMPLETE` : `iwl` fait de même (et continue sans réponse).

**Licence et place : gardé dans le dépôt** (`firmware/pc/iwlwifi/`, comme le micrologiciel du CYW43455 dans
`firmware/wifi/`), avec `LICENCE.iwlwifi_firmware` et `SOURCE.txt` (origine, date, sommes) ; `docs/LICENCES.md` le
liste. Raisons : la licence d'Intel permet la redistribution binaire sans modification avec sa notice ; l'arbre du
PC se construit alors sans réseau, et chaque essai sur le ZBook porte exactement le fichier contre lequel les
structures des commandes ont été vérifiées (versions du TLV 48). Les deux autres variantes (Qu-b0, Qu-c0, 1,4 Mo
chacune) ne sont **pas** dans le dépôt : `tools/pc/iwlfw.sh Qu-c0-hr-b0` les télécharge de linux-firmware avec leur
somme sha256 fixée (vérifiées le 13/11 : mêmes versions des commandes). La licence interdit la rétro-ingénierie :
on ne lit que les en-têtes TLV publics.

**Voyage.** `tools/pc/sysroot.sh` copie le dossier dans `SD0:/lib/firmware/` de l'arbre du PC ; `iwl` le lit (1,4
Mo), en fait des sections, et donne leurs adresses à la puce : c'est la **ROM de la puce** qui copie les sections
depuis la mémoire de l'hôte, en vérifie la signature et lance les deux processeurs (aucune copie par l'hôte dans la
SRAM, contrairement aux anciennes familles).

## 3. Le fichier `.ucode` (format TLV)

En-tête de 88 octets : un mot nul, `IWL\n`, 64 octets de texte, version, numéro de construction, 8 octets ; puis des
TLV (type 32 bits, longueur 32 bits, données arrondies à 4 octets). Ce que `fw.c` en tire, sur le fichier 77 :

| TLV | Contenu |
|---|---|
| 19 `SEC_RT` | 51 sections, chacune avec son adresse dans la puce : **LMAC 14** (la première, 1 656 octets à `0x433000`, est l'en-tête CSS signé) · séparateur `0xFFFFCCCC` · **UMAC 15** (CSS à `0x80433000`) · séparateur `0xAAAABBBB` · **pages 20** (mémoire paginée du micrologiciel, 606 Ko). 1 388 928 octets en tout. Pas de section INIT (TLV 20) : image **unifiée** |
| 30 / 29 | capacités et changements d'API (bits) : LAR, balayage UMAC, latence du SoC, poignée de main d'arrêt, MCC 11ax ; NVM v4, configuration de balayage réduite, type de station, MCC Wi-Fi. Pas de `DQA_SUPPORT` |
| 48 `CMD_VERSIONS` | (commande, groupe, version, version de la notification) : **SCAN_REQ_UMAC 15**, SCAN_CFG 5, vivant 6, NVM_GET_INFO 4, MCC_UPDATE 1 (réponse v4 par la capacité 11ax), SOC_CONFIGURATION 2, BT_CONFIG 6, TX_ANT 1, ADD_STA 12, MAC_CONTEXT 5, PHY_CONTEXT 4, BINDING 2 |
| 23 `PHY_SKU` | `0x00330018` : antennes A+B en émission et réception, radio type 0 étape 2 tiret 1 |
| 22 `DEF_CALIB` | déclencheurs d'étalonnage de l'image normale (`0x5f1601d3`, `0x5b06958b`) |
| 36 | version 77.3cda2a2c.0 ; 31 : 67 canaux de balayage ; 0x100 : 15 stations |
| ≥ 0x1000000 | débogage « ini » (régions, déclencheurs) : ignorés |

`iwl` **vérifie ces versions avant d'envoyer** quoi que ce soit : une version qu'il ne connaît pas l'arrête
proprement (« versions non prises en charge ») au lieu d'envoyer une structure fausse au micrologiciel.

## 4. Le transport PCIe « gen2 »

**Registres.** BAR0 : les CSR (0x000–0x0FF), le pont HBUS (0x400–0x4FF : accès indirect aux registres internes
« prph » et à la SRAM, pointeur d'écriture des files), le RFH (0x1C80 : index des RBD libres), les causes MSI-X
(0x2800). Un registre interne se lit en écrivant `(adresse & 0xFFFFF) | 3 << 24` dans `HBUS_TARG_PRPH_RADDR` puis en
lisant `…_RDAT`, avec l'accès MAC tenu (`CSR_GP_CNTRL.MAC_ACCESS_REQ`, puis horloge prête et pas en sommeil).

**Mise en route** (`trans.c`, ordre de Linux, valeurs vérifiées) :
1. *start_hw* : la puce à l'hôte (`HW_IF_CONFIG.NIC_READY` posé et relu ; sinon `PREPARE` et 10 × 150 ms : le ME/AMT
   peut la tenir) ; bit de persistance (`HPM_DEBUG` bit 12) effacé ; remise à zéro logicielle (`CSR_RESET` bit 7, 6
   ms) ; pour une puce intégrée, porte d'alimentation forcée (`HPM_HIPM_GEN_CFG` : FORCE_ACTIVE, PG_EN|SLP_EN, puis
   FORCE_ACTIVE retiré) et nouvelle remise à zéro ; « APM » (L0s coupé, seuil HPET, réveil HAP, `INIT_DONE` puis
   horloge du MAC prête, 25 ms) ; causes d'interruption par `CSR_INT` (`UREG_CHICK` = MSI).
2. *start_fw* : de nouveau la puce prête ; interruptions coupées ; poignées de main rfkill effacées (`GP1_CLR`) ; APM ;
   configuration de la carte (étape du MAC, type/étape/tiret de la radio dans `HW_IF_CONFIG`) ; coalescence 0x40 ;
   **anneau de réception laissé vide** ; file de commandes ; registres fantômes (`0x800FFFFF`) ; **context info** ;
   son adresse dans `CSR_CTXT_INFO_BA` (64 bits) ; LTR de démarrage (~250 µs : `HPM_MAC_LTR_CSR` = 0xF,
   `HPM_UMAC_LTR` = `0x88FA88FA`, le contournement de Linux pour les CNVi qui échouaient au démarrage) ; enfin
   `UREG_CPU_INIT_RUN` = 1.
3. La puce lève **ALIVE** (`CSR_INT` bit 0) quand le micrologiciel a réglé son RFH : alors seulement l'anneau de
   réception est rempli (511 RB) et son index d'écriture donné ; la notification « vivant » arrive par cet anneau.
   (Si l'interruption n'est pas vue en 1,5 s, `iwl` remplit l'anneau quand même et le dit au journal.)

**Context info** (1 792 octets, `cmds.c : build_ctxt_info`) : version (mac_id = `HW_REV`, taille en mots) ; contrôle
(TFD longs, taille de l'anneau de réception en log2 = 9, RB de 4 Ko) ; adresses des RBD libres, utilisés et du
statut ; file des commandes (adresse, log2(32) − 3 = 2) ; puis trois tableaux de 64 adresses : **UMAC à +192, LMAC à
+704, pages à +1216** (chaque section dans sa propre zone DMA contiguë). L'ordre (UMAC avant LMAC) est un piège : la
construction est vérifiée contre la disposition publique (`check.py`).

**Réception** (une file, la n° 0) : 512 RBD libres de 64 bits (adresse du RB de 4 Ko | numéro 1…511 dans les 12 bits
du bas), 512 RBD utilisés de 32 bits (le numéro du RB rempli), un statut de 16 octets (`closed_rb_num`, 12 bits).
Un RB peut porter plusieurs paquets (en-tête : longueur 14 bits, commande, groupe, numéro ; alignés sur 64 octets ;
fin `0x55550000`). Le bit 15 du numéro distingue les notifications des réponses ; une réponse porte la file (0) et
l'index de la commande. Chaque RB traité retourne aussitôt dans l'anneau libre (index d'écriture par multiples de 8).

**Commandes** : 32 TFD de 256 octets (nombre de tampons, puis 25 × (longueur 16 bits, adresse 64 bits)) ; la commande
dans un tampon DMA par place : en-tête large de 8 octets (commande, **groupe**, numéro = file << 8 | index, longueur,
réservé, version), puis la charge ; le premier tampon du TFD fait 20 octets, le second le reste ; le pointeur
d'écriture (`HBUS_TARG_WRPTR`, file << 16) tourne sur **256** alors que la file a 32 places. Les commandes
« historiques » (< 256) partent dans le **groupe 1** (« LONG »), comme le fait iwlwifi.

**Interruptions** : MSI à un vecteur (`SYS_IRQ_MSI`, capacité MSI écrite par `iwl`, MSI-X coupé), causes dans
`CSR_INT` (acquittées en les réécrivant) ; à chaque réveil (interruption ou 5 ms) `iwl` relit aussi l'anneau de
réception : une MSI qui ne viendrait pas ne fait que ralentir. `-p` : sondage à la milliseconde, sans MSI. Le MSI-X à
plusieurs vecteurs (tables IVAR, `UREG_CHICK_MSIX_ENABLE`) viendra avec le trafic de données (étape 5).

**Erreurs** : `CSR_INT` bits 25 (`SW_ERR`, assertion du micrologiciel) et 29 (`HW_ERR`) ; les tables d'erreur de
l'UMAC et du LMAC (adresses données par « vivant ») sont lues dans la SRAM et mises au journal (identifiant de
l'erreur, liens de branchement, données, dernière commande), avec les registres utiles (`CSR_*`, état du démarrage
sécurisé `UMAG_SB_CPU_1/2_STATUS`, PC de l'UMAC et du LMAC, `RFH_GEN_STATUS`).

**Arrêt** : poignée de main de remise à zéro (`UREG_NIC_SET_NMI_DRIVER`, bits 24|25 ; `RESET_DONE` attendu 200 ms)
si le micrologiciel tourne ; interruptions coupées ; DMA de réception arrêté (`RFH_RXF_DMA_CFG` = 0, attente de
`RXF_DMA_IDLE`) ; maître du bus arrêté (`STOP_MASTER`, `MASTER_DISABLED`) ; remise à zéro ; `INIT_DONE` effacé ; la
puce reprise (`NIC_READY`). Puis la MSI rendue, la fonction rendue à pcid (qui coupe le maître du bus dans la
configuration PCI), et seulement alors la mémoire DMA est rendue (fin du process).

## 5. Le micrologiciel en mode MVM : de « vivant » au balayage

| Ordre | Commande (groupe.cmd) | Charge |
|---|---|---|
| 1 | — | « vivant » v6 (144 o) : statut `0xCAFE`, versions du LMAC et de l'UMAC, adresses des journaux d'erreur, SKU |
| 2 | (sonnette PNVM) | si SKU non nul ; attente de `PNVM_INIT_COMPLETE` (c.fe) |
| 3 | `INIT_EXTENDED_CFG` (2.03) | `1 << IWL_INIT_NVM` |
| 4 | `NVM_ACCESS_COMPLETE` (c.00) | 0 ; puis attente de `INIT_COMPLETE_NOTIF` (0.04) |
| 5 | `NVM_GET_INFO` (c.02) | réponse v4 (468 o) : drapeaux, version, SKU (bandes, n/ac/ax), antennes, LAR, 51 profils de canaux |
| 6 | `TX_ANT_CONFIGURATION` (1.98) | antennes valides (NVM ∩ PHY_SKU) |
| 7 | `BT_CONFIG` (1.9b) | coexistence : mode NW, `HIGH_BAND_RET` (+ `MPLUT`) |
| 8 | `SOC_CONFIGURATION` (2.01) | puce intégrée, LTR après 2,5 ms, quartz lent : drapeaux `0xA`, latence 12 000 µs |
| 9 | `MCC_UPDATE` (1.c8) | « ZZ », source `GET_CURRENT` : le pays courant et les canaux permis (LAR : **pas de balayage avant**, il peut faire tomber le micrologiciel) |
| 10 | `SCAN_CFG` (1.0c) v5 | forme réduite : antennes d'émission et de réception |
| 11 | `SCAN_REQ_UMAC` (1.0d) v15 | 1 940 octets (§ ci-dessous) ; trames reçues `REPLY_RX_MPDU` (0.c1) ; fin `SCAN_COMPLETE_UMAC` (0.0f) |

Pas de `PHY_CONFIGURATION` (image unifiée), pas de `DQA_ENABLE` (pas la capacité), pas de station auxiliaire (ADD_STA
v12 : le balayage n'en a plus besoin), pas de contexte MAC pour le balayage (il en faudra un pour l'association).

**Balayage v14 à 17** (`build_scan_req`) : paramètres généraux (attente active 10 ms, passive 110 ms, attente
adaptative, priorité EXT_6, drapeaux `PASS_ALL | ADAPTIVE_DWELL`, `FORCE_PASSIVE` en passif), canaux (ordre des
canaux, 1 tour chacun, bande dans un octet — dans les bits 31:30 des drapeaux en v17 —, bit 0 : sonde avec le SSID
n° 0), une période, gabarit de la requête de sonde (en-tête 802.11 avec notre adresse, élément SSID que le
micrologiciel remplit, débits 2,4 et 5 GHz), SSID dirigé n° 0 (vide : générique). Les canaux sont ceux que la
régulation permet. **Trames reçues** : descripteur de 48 octets (longueur, drapeaux dont le bourrage de 2 octets,
statut CRC/débordement, énergie des deux chaînes → dBm, canal), puis la trame 802.11. `ieee.c` lit balises et
réponses de sonde : SSID, canal (élément DS, sinon HT), capacités, normes (HT, VHT, HE), **sécurité** : RSN (version 1 ;
chiffrements de groupe et par paire, AKM : PSK, PSK-SHA256, SAE, 802.1X, OWE ; MFPC/MFPR) ou ancien WPA (élément
propriétaire 00:50:F2:01), sinon WEP par le bit « Privacy ».

## 6. Ce qui découle du SoftMAC : le supplicant et la suite

Le supplicant WPA2-personnel est **écrit dans `crypto.c`** et vérifié (étape 1, sans matériel) :
PMK = PBKDF2-HMAC-SHA1(phrase, SSID, 4 096, 32) ; PTK = PRF-384(PMK, « Pairwise key expansion », min/max des
adresses, min/max des nonces) → KCK, KEK, TK ; MIC des trames EAPOL-Key = HMAC-SHA1 tronqué à 16 octets ; GTK
déballée par AES-128 (RFC 3394). Ce qui reste, par étape (§ 7) :

* **Association** (réseau ouvert d'abord) : un contexte MAC station (`MAC_CONTEXT_CMD` v5 : adresse, type, débits,
  filtres), un contexte PHY (`PHY_CONTEXT_CMD` v4, canal sur 32 bits puisque `ULTRA_HB_CHANNELS`), une liaison
  (`BINDING_CONTEXT_CMD` v2), une station pour l'AP (`ADD_STA` v12), une file de données/gestion
  (`SCD_QUEUE_CFG` du groupe DATA_PATH : la file et sa table d'octets en mémoire de l'hôte), un « time event »
  (`TIME_EVENT_CMD`, pour rester sur le canal pendant l'authentification), puis les trames **Authentication**
  (système ouvert) et **Association Request** émises par `TX_CMD` (v9) et les réponses lues dans `REPLY_RX_MPDU`.
  Une fois associé : `MAC_CONTEXT` « associé » (AID, DTIM), et les trames de données.
* **Données vers netd** : netd attend des trames Ethernet (anneau de `src/svc/netif`, comme genet, wifi et usbnet).
  `iwl` convertit : 802.11 (en-tête QoS data, LLC/SNAP `AA AA 03 00 00 00` + type) ↔ Ethernet II, dans les deux
  sens ; `netif_register(NIF_WIFI, mac, …, "wlan0")`, `netif_link(1, débit, ssid)` à l'association.
* **WPA2-personnel** : EAPOL 4 voies dans l'hôte (message 1 : ANonce → SNonce, PTK, message 2 avec RSN et MIC ;
  message 3 : MIC vérifié, GTK déballée ; message 4), puis les clés dans la puce (`ADD_STA_KEY` ou `SEC_KEY_CMD`
  selon la version du TLV 48) : **CCMP en matériel**. Renouvellement de la GTK (EAPOL de groupe).
* **WPA3-personnel (SAE)** : Dragonfly sur la courbe P-256 (« hunting and pecking » et H2E), donc de l'arithmétique
  de grands nombres et des courbes elliptiques (à écrire, ou à prendre d'OpenSSL déjà porté) ; **PMF obligatoire**
  (MFP : trames de gestion protégées, IGTK et BIP-CMAC-128). Les réseaux de transition WPA2/WPA3 se joignent en
  WPA2 en attendant.
* **Le service « wifi »** : les 8 opérations de `docs/RESEAU.md` 4.3 (état, balayage en entrées de 48 o avec la
  sécurité codée 0-3 — `iwl` y ajoute 4 « WPA3 » et 5 « entreprise », à faire connaître au protocole —, rejoindre,
  quitter, oublier, connus, priorité, pays), `SD0:/etc/wifi.txt` (même format), reconnexion, notifications : le même
  comportement que `src/svc/wifi/wifi.asm`, écrit en C pour iwl ; le volet Réglages et `wifimenu` (déjà en C pour le
  PC) marchent alors tels quels, comme NetKit (`wifi_*`).
* **Pays** : `pays=` d'`options.txt` envoyé par `MCC_UPDATE` (source « ME »), réappliqué par l'opération 8.

## 7. Les étapes (chacune essayée sur le ZBook par l'utilisateur)

| Étape | Contenu | Ce que l'utilisateur voit |
|---|---|---|
| **1** (13/11, faite) | `iwl` à la main : fonction, micrologiciel lancé, NVM, MAC, régulation, **balayage** et liste des réseaux ; erreurs journalisées, puce arrêtée proprement. Supplicant WPA2 (calculs) vérifié hors matériel | `iwl scan` au Terminal : la liste des réseaux (signal, canal, sécurité, BSSID, norme, SSID) |
| 2 | association à un **réseau ouvert** (contextes MAC, PHY, liaison, station, file, time event, auth/assoc), `wlan0` inscrite chez netd, **DHCP** et ping, toujours à la main (`iwl join SSID`) | `iwl join MonReseauOuvert` puis `ping` |
| 3 | **WPA2-personnel** : EAPOL 4 voies, clés installées (CCMP matériel), renouvellement de la GTK (`iwl join SSID clé`) | Internet par la box de la maison |
| 4 | le **service « wifi »** (protocole du Pi, `wifi.txt`, connexion au démarrage, reconnexion), ligne `pci:8086:43f0` dans `drivers.txt` ; volet Réglages et `wifimenu` | le menu Wi-Fi de la barre comme sur le Pi |
| 5 | performances : MSI-X à plusieurs vecteurs, plusieurs files de réception, agrégation (BA), économie d'énergie, débit (`tcpbench`) | débits proches de Windows |
| 6 | **WPA3-SAE** et PMF | réseaux WPA3 seuls |
| 7 | pays (`pays=`), canaux DFS, itinérance entre AP d'un même réseau | — |

## 8. Ce qui peut casser au premier essai, et ce que le journal en dira

| Symptôme au journal | Cause probable |
|---|---|
| « la puce ne se dit pas prête (NIC_READY) » | le ME/AMT (vPro) tient la fonction ; ou la puce en D3 |
| « pas de « vivant » en 3 s », « démarrage sécurisé : CPU1 … » | context info faux, sections refusées (signature : `UMAG_SB_CPU_1_STATUS`), LTR ; le journal des registres permet de trancher |
| « ALIVE non vue en 1,5 s » puis « vivant » | l'interruption ne vient pas (MSI) mais le reste marche : à noter |
| « ERREUR du micrologiciel » + « erreur UMAC : … id … » | une commande mal formée : l'identifiant et la dernière commande disent laquelle |
| « pas de réponse à la commande 1.0d » | balayage refusé sans assertion (version, canaux) |
| « 0 réseau(x) vu(s) » avec « N trames reçues » > 0 | descripteur des trames reçues mal lu (taille) ; « rfkill » : interrupteur de la radio |
| « accès aux registres internes … : ÉCHEC », valeurs `A5A5A5A2` | l'accès MAC pas accordé (vu le 13/11 au premier essai : attente trop courte, puis la porte d'alimentation réécrite avec la valeur d'erreur ; corrigé, voir PC-PILOTES.md) ; couper le PC, sinon passer une fois par Windows |
