; ============================================================================
;  inc/netd.inc - service " net " (netd) : ops des programmes (par NetKit), des
;  pilotes d'interface (netif.inc), anneau de trames (docs/RESEAU.md 2.2-3)
;
;  Message (512 o) : +0 op / resultat, +8 a, +16 b, +24 c, +32 d,
;  +40 longueur des donnees (32 bits), +44 pid (noyau), +48 donnees (464 o)
; ============================================================================

; --- ops des programmes (NetKit) ---------------------------------------------
NET_OP_OPEN		= 1		; -> b = numero du tampon du process (128 Ko, buf_map)
NET_OP_SOCKET		= 2		; a type (1 TCP, 2 UDP) -> sd
NET_OP_CONNECT		= 3		; a sd, b ip, c port -> 0
NET_OP_BIND		= 4		; a sd, b port
NET_OP_LISTEN		= 5		; a sd, b file d'attente
NET_OP_ACCEPT		= 6		; a sd, b delai ms (<= 25 s) -> sd ; E_AGAIN
NET_OP_SEND		= 7		; a sd, b decalage dans le tampon (-1 : donnees du
					;   message), c octets -> octets
NET_OP_RECV		= 8		; a sd, b decalage (-1 : message), c octets, d delai
					;   -> octets (0 : fermee), E_AGAIN
NET_OP_CLOSE		= 9		; a sd
NET_OP_SENDTO		= 10		; a sd, b decalage, c octets, d ip | port << 32
NET_OP_RECVFROM		= 11		; comme 8 -> c ip, d port de la source
NET_OP_RESOLVE		= 12		; donnees : nom -> ip
NET_OP_PING		= 13		; a ip, b delai ms -> aller-retour ms
NET_OP_INFO		= 14		; -> donnees : netinfo (32 o), nom (8 o, +32), type (+40)
NET_OP_IFACES		= 15		; -> a nombre ; donnees : NIF_ENT o par interface
NET_OP_TIME		= 16		; -> a heure UTC (s depuis 1970, 0 : inconnue), b 1 si
					;   donnee par NTP, c instant de la synchro (ms du demarrage)
NET_OP_NTP		= 17		; a 1 : synchroniser maintenant (reponse a la fin, 10 s)

; --- ops des pilotes d'interface (netif.inc) -----------------------------------
NET_OP_IF_REGISTER	= 20		; a type, b MAC, c priorite ; donnees : nom
					;   -> n d'interface ; b = numero du tampon de l'anneau
NET_OP_IF_STATE		= 21		; a interface, b lien (0/1), c debit Mb/s ;
					;   donnees : SSID (Wi-Fi)
NET_OP_IF_DOORBELL	= 22		; a interface (accepte, inutile : netd scrute)

NIF_ETHERNET		= 1		; types d'interface
NIF_WIFI		= 2
NIF_USB			= 3
NIF_PRIO_GENET		= 30		; route par defaut : la plus haute avec un bail
NIF_PRIO_WIFI		= 20
NIF_PRIO_USB		= 10

; interface (NET_OP_IFACES) : entree de NIF_ENT octets
NIF_E_NAME		= 0		; 8 o
NIF_E_TYPE		= 8		; 32 bits
NIF_E_LINK		= 12
NIF_E_IP		= 16		; adresse si c'est l'interface active, sinon 0
NIF_E_PRIO		= 20
NIF_E_SPEED		= 24
NIF_E_ACTIVE		= 28
NIF_E_SSID		= 32		; 32 o
NIF_ENT			= 64

; --- tampon d'un process (NET_OP_OPEN) ------------------------------------------
NET_CLI_BUF		= 131072	; 16 zones de 8 Ko
NET_ZONE		= 8192
NET_ZONES		= 16
NET_INLINE		= 448		; au plus dans le message

; --- anneau de trames d'une interface (cree par netd, mappe par le pilote) -----
NR_RX_HEAD		= 0		; compteurs monotones : ecrit par le pilote
NR_RX_TAIL		= 8		;   netd
NR_TX_HEAD		= 16		;   netd
NIF_SIG_TX		= 2		; bit leve chez le pilote : une trame a emettre
NR_TX_TAIL		= 24		;   pilote
NET_OP_STATS		= 23		; -> donnees NST_SIZE : compteurs (tcpbench, net_stats)
NET_OP_SHUTDOWN		= 24		; a sd : plus rien a envoyer, FIN apres la file ;
					;   la reception continue (TCP, sans effet sur UDP)
NST_BYTES		= 136		; 17 mots : 8 du pilote (NR_STATS), 9 de la pile
NR_STATS		= 64		; compteurs du pilote dans l'en-tete de l'anneau
NST_RX_N		= 0		; trames lues sur le bus
NST_RX_T		= 8		;   temps (CNTPCT)
NST_TX_N		= 16		; trames ecrites
NST_TX_T		= 24		;   temps
NST_POLLS		= 32		; tours du pilote
NST_EMPTY		= 40		;   sans rien de la puce
NST_NOCRED		= 48		; emission retenue : plus de credit de la puce
NST_LINK		= 56		; debit radio (500 kb/s) | economie d'energie << 16
NST_STACK		= 64		; (pile) trames traitees
NST_RTX			= 72		; retransmissions par echeance
NST_OOO			= 80		; segments hors ordre
NST_OOO_FULL		= 88		;   sans place : perdus
NST_RX_DROPS		= 96		; anneau de reception plein
NST_TX_DROPS		= 104		; anneau d'emission plein
NST_FREQ		= 112		; frequence de CNTPCT
NST_FRTX		= 120		; retransmissions rapides
NST_PROBES		= 128		; sondes de fin de rafale
NST_RXB			= 136		; (apres NST_BYTES, le rapport de tcpbench) octets recus
NST_TXB			= 144		;   emis, trames Ethernet entieres
NST_SIZE		= 152
NR_MTU			= 40
NR_SLEEP		= 2048		; 1 : netd dort, le pilote le reveille (netif_rx_put)
NR_WAKE			= 2056		; canal + 1 de son service net.rx
NR_SIG_RX		= 1		; bit leve chez netd : une trame dans l'anneau
NR_RX_DROPS		= 48
NR_TX_DROPS		= 56
NR_SLOTS		= 64		; par sens
NR_SLOT			= 2048		; +0 longueur (8 o), +8 trame
NR_FRAME_MAX		= 1544
NR_RX			= 4096
NR_TX			= 4096 + NR_SLOTS * NR_SLOT
NR_SIZE			= 4096 + 2 * NR_SLOTS * NR_SLOT

; --- service " wifi " (pilote wifi, docs/RESEAU.md 4.3) -------------------------
WIFI_OP_STATUS		= 1		; -> donnees 64 o : etat, RSSI (+4), MAC (+8), SSID (+16), long. (+48)
WIFI_OP_SCAN		= 2		; a premiere entree, b 1 : resultat du dernier balayage
					;   -> a nombre ; donnees : <= 9 entrees de 48 o (appel 36)
WIFI_OP_JOIN		= 3		; donnees : SSID, 0, cle, 0 ; a 1 = memoriser ; b priorite
					;   -> 0, E_NOENT, E_ACCES, E_TIMEDOUT
WIFI_OP_LEAVE		= 4		; plus de reconnexion jusqu'au prochain JOIN
WIFI_OP_FORGET		= 5		; donnees : SSID (retire de wifi.txt)
WIFI_OP_KNOWN		= 6		; a premiere -> a nombre ; donnees : <= 9 entrees WK_*
WIFI_OP_PRIORITY	= 7		; donnees : SSID ; a priorite
WIFI_OP_COUNTRY		= 8		; donnees : " FR " (reapplique tout de suite)
WIFI_SCAN_ENT		= 48
WIFI_PER_MSG		= 9
WK_SSID			= 0		; reseau connu : SSID (33 o), securite (+36), priorite (+40)
WK_SECU			= 36
WK_PRIO			= 40
WK_ENT			= 48
WIFI_ST_ABSENT		= 0		; etats (WIFI_OP_STATUS)
WIFI_ST_START		= 1
WIFI_ST_READY		= 2
WIFI_ST_JOINING		= 3
WIFI_ST_CONNECTED	= 4
