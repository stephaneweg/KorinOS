# Étude 04 — Pile TCP/IP, graphisme/GUI et modèle MenuetOS (RPi 4, AArch64, FASMARM)

Source de référence : `ref/circle` (Circle, C++ bare-metal, **GPLv3**). Tous les chemins ci-dessous sont relatifs à `./ref\circle`.

> **Licence** : une transposition fidèle de la logique de Circle en asm reste une œuvre dérivée (GPLv3). Si l'OS ne doit pas être GPLv3, utiliser Circle uniquement comme « carte » et réimplémenter d'après les RFC (791, 792, 826, 768, 793/9293, 2131, 1035, 6298, 1071).

---

## A. Pile TCP/IP

### A.1 Architecture en couches (Circle)

```
 CSocket (socket.cpp)                       API BSD-like : Bind/Connect/Listen/Accept/Send/Receive/SendTo/ReceiveFrom
   │ handle int
 CTransportLayer (transportlayer.cpp)      table de connexions (CPtrArray de CNetConnection*), démultiplexage linéaire
   ├─ CTCPConnection (tcpconnection.cpp)   TCB + machine d'état RFC 793 + RTO + congestion
   ├─ CUDPConnection (udpconnection.cpp)
   └─ CTCPRejector   (tcprejector.cpp)     envoie RST pour segment sans connexion
 CNetworkLayer (networklayer.cpp)          IPv4 : vérif header/checksum, routage (CRouteCache), files RX/ICMP/notif
   ├─ CICMPHandler (icmphandler.cpp)       echo reply, dest-unreach → notifications vers transport
   └─ CIGMPHandler (non requis)
 CLinkLayer (linklayer.cpp)                Ethernet II : dispatch EtherType 0x0800/0x0806, multicast
   └─ CARPHandler (arphandler.cpp)         cache 20 entrées, requêtes, file TX différée par entrée
 CNetDeviceLayer (netdevlayer.cpp)         files TX/RX de CNetBuffer ↔ driver CNetDevice (GENET = lib/bcm54213.cpp)
 CNetSubSystem (netsubsystem.cpp)          construit et relie les couches, lance CDHCPClient, CNetTask
```

**Modèle d'exécution** (`lib/net/nettask.cpp`, `netsubsystem.cpp::Process`) : une tâche coopérative `net` boucle sur
`NetDevLayer.Process → LinkLayer.Process → NetworkLayer.Process → TransportLayer.Process` puis `Yield()`. Chaque couche **tire** les paquets de la couche du dessous (`Receive()` = `Dequeue` d'une file) : pas de callbacks montants, pas d'interruptions dans la pile. C'est idéal pour une transposition procédurale : **une routine `net_poll` appelée périodiquement** (tick timer 1 ms ou IRQ RX GENET qui positionne un flag).

DHCP (`dhcpclient.cpp`) et DNS (`dnsclient.cpp`) sont des **clients au-dessus de `CSocket` UDP** (DHCP est une `CTask` bloquante, DNS est synchrone avec `MsSleep(1000)` entre essais). En asm sans threads noyau : DHCP → machine d'état pilotée par `net_poll`; DNS → bibliothèque côté application utilisant les syscalls UDP.

### A.2 Structures de données clés

| Structure | Fichier | Rôle | Transposition asm |
|---|---|---|---|
| `CNetBuffer` | `include/circle/net/netbuffer.h`, `lib/net/netbuffer.cpp` | buffer DMA de `FRAME_BUFFER_SIZE`=1600 (`include/circle/netdevice.h`) + `HeaderReserve` (ligne de cache). `m_pHead` est pré-avancé selon `TPurpose` (`TCPSend` : +14+20+20, `UDPSend` : +14+20+8, `ICMPSend` : +14+20…). `AddHeader(n)` = `head -= n` ; `RemoveHeader(n)` = `head += n`. 20 octets de *private data* (passage IP src/dst/proto entre couches, `TNetworkPrivateData`). Lien `m_pNext` intrusif. | Pool statique de N=64 blocs de 2048 o alignés 64 : `[0]next(8) [8]head(8) [16]len(4) [20]flags(4) [24]priv(24) [64..]data`. Free-list LIFO. `nb_alloc(purpose)` fixe `head = data+64+headroom[purpose]`. |
| `CNetBufferQueue` | `netbufferqueue.cpp` (217 l.) | FIFO simple chaînée `first/last/nEntries/ulBytes`, spinlock optionnel, `Peek/Rewind/MoveOn` (sert à la retransmission TCP), `Flush(nBytes)` (retire ce qui est acquitté). | struct 32 o `{first,last,count,bytes}` ; `q_put/q_get` ~15 instr. chacune ; IRQ masquées (`msr daifset,#2`) si accès depuis IRQ. |
| `CNetQueue` | `netqueue.cpp` | file générique (notifications ICMP). | réutiliser la file netbuf. |
| `TARPEntry[20]` | `include/circle/net/arphandler.h` | états `FreeSlot/RequestSent/RetryRequest/SendTxQueue/Valid`, IP, MAC, timer, tentatives, `nTicksLastUsed`, file TX différée. | 16 entrées × 32 o `{ip(4) mac(6) state(1) tries(1) tlast(4) tretry(4) pending(8)}`; 1 seul buffer en attente par entrée suffit. |
| `TIPHeader` | `networklayer.h` | header 20 o, pas d'options émises ; fragments : non réassemblés (min. 1er fragment). | offsets constants (`IP_TTL=8`, `IP_PROTO=9`, `IP_CSUM=10`, `IP_SRC=12`, `IP_DST=16`). |
| `CRouteCache` | `routecache.cpp` (102 l.) | routes apprises via ICMP redirect. | **à omettre** : `if ((dst^myip)&mask) next_hop=gw else dst`. |
| table connexions | `transportlayer.cpp` (`CPtrArray m_pConnection`) | scan linéaire, `PacketReceived()` retourne 0 (pas à moi)/1 (consommé)/-1 ; terminées supprimées dans `Process`. | tableau statique 16 slots × 256 o (TCB/UCB communs, champ `type`) ; handle = index. |
| TCB | `include/circle/net/tcpconnection.h` | `m_State` (11 états), TxQueue/RxQueue/ReassemblyQueue, `SND_UNA/NXT/WND/WL1/WL2/ISS`, `RCV_NXT/WND/IRS`, `SND_MSS`, RTO calc, `CWND/SSThresh/DupAck/FastRecovery`, 3 timers (user, retrans, time-wait), flags `bSendSYN/bFINQueued/bFINSent/bFINReceived/bRetransmit`. | voir A.4. |
| `CReassemblyQueue` | `reassemblyqueue.cpp` (177 l.) | segments hors ordre triés par SEQ. | **phase 2** : en minimal on jette le hors-ordre (l'émetteur retransmet). |
| `CRetransmissionTimeoutCalculator` | `retranstimeoutcalc.cpp` (235 l.) | RFC 6298 : SRTT/RTTVAR, Karn (pas de mesure sur retransmis), table de hachage de segments en vol. | une seule mesure à la fois (`rtt_seq`, `rtt_t0`) : ~60 instr. |
| `CChecksumCalculator` | `checksumcalculator.cpp` (114 l.) | somme 16 bits + pseudo-header {src,dst,0,proto,len BE} ; `FoldResult`. | routine A.6. |

### A.3 Chemins de données

**RX** : GENET RX ring → `netdev_poll` copie (ou échange) dans un netbuf → `link_rx` lit EtherType : `0x0806` → `arp_input` (répond aux requêtes pour notre IP, met à jour le cache, libère la file différée) ; `0x0800` → `ip_input` (version 4, IHL, checksum, `dst == myip || broadcast`, pas fragmenté, `RemoveHeader(ihl*4)`, `RemoveTrailer` au `TotalLength`) → proto 1 `icmp_input` (type 8 → réécrit en type 0, recalcule checksum, renvoie) / 17 `udp_input` / 6 `tcp_input` → demux table connexions → sinon UDP : drop (ICMP port-unreach optionnel), TCP : RST (`tcprejector.cpp`).

**TX** : socket `send` → copie données dans netbuf (`TCPSend`) → `tcp_output` (`AddHeader(20)`, checksum pseudo-header) → `ip_output` (`AddHeader(20)`, ID, TTL 64, checksum) → `link_output` : next-hop, `arp_resolve` ; si MAC inconnue → buffer mis en attente dans l'entrée ARP + requête ARP, sinon `AddHeader(14)` → `netdev_tx` (file TX → anneau GENET). Pad à 60 octets (`ETH_MIN_LEN`, `include/circle/net/sizes.h`).

### A.4 Machine d'état TCP

États (`tcpconnection.h`) : `CLOSED, LISTEN, SYN_SENT, SYN_RECEIVED, ESTABLISHED, FIN_WAIT_1, FIN_WAIT_2, CLOSE_WAIT, CLOSING, LAST_ACK, TIME_WAIT`. `PacketReceived` (`tcpconnection.cpp` l.735–~1500) suit la structure « SEGMENT ARRIVES » de RFC 793 : `case CLOSED` / `case LISTEN` (l.856) / `case SYN_SENT` (l.916) / bloc commun des états synchronisés (l.1032 : vérif. séquence → RST → SYN → ACK → URG → texte → FIN). `Process()` (l.593) gère émission (SYN, nouveaux segments, FIN différé `m_bFINQueued` → `m_StateAfterFIN`), retransmission, abandon après `MAX_RETRANSMISSIONS`=5. Constantes : MSS = 1514−14−20−20 = 1460, fenêtre RX = 10×MSS, `TIME_WAIT` 60 s, comparaisons modulo 2³² via `lt/le/gt/ge` (`(int)(x-y) < 0`).

**Client minimal (HTTP GET)** — sous-ensemble suffisant :
`CLOSED →(connect: SYN)→ SYN_SENT →(SYN+ACK: ACK)→ ESTABLISHED →(close: FIN)→ FIN_WAIT_1 →(ACK)→ FIN_WAIT_2 →(FIN: ACK)→ TIME_WAIT →(2 s au lieu de 60)→ CLOSED` ; et `ESTABLISHED →(FIN reçu)→ CLOSE_WAIT →(close: FIN)→ LAST_ACK →(ACK)→ CLOSED`. RST reçu → CLOSED + erreur. `LISTEN/SYN_RECEIVED` (serveur) en phase 2.

Layout TCB asm proposé (256 o) :
```
0  type(1) state(1) flags(2)  lport(2) rport(2)
8  rip(4) err(4)
16 snd_una snd_nxt snd_wnd iss          (4×4)
32 rcv_nxt rcv_wnd irs mss              (4×4)
48 rto srtt rttvar rtt_seq rtt_t0       (5×4)
68 t_retrans t_timewait t_user tries    (deadlines en ticks)
84 cwnd ssthresh dupack
96 txring_ptr(8) tx_head tx_tail (sz 16 KiB : [una..nxt) = en vol, [nxt..tail) = à envoyer)
120 rxring_ptr(8) rx_head rx_tail (sz 16–64 KiB)
144 wait_pid(4) evmask(4) ...           (réveil processus, événement NET)
```
Remplacer les chaînes de netbufs de Circle (TxQueue + Peek/Rewind) par des **anneaux d'octets** : la retransmission relit simplement `[snd_una, snd_nxt)` ; l'ACK avance `tx_head`. Beaucoup plus simple en asm.

Congestion minimale : slow start + évitement (cwnd += MSS / cwnd += MSS²/cwnd), retour à 1 MSS sur timeout ; fast retransmit (3 dup ACK) en option.

### A.5 Lignes de code Circle (`wc -l`)

Périmètre minimal ARP+IPv4+ICMP+UDP+DHCP+DNS+TCP client :

| Fichier `lib/net/` | l. | Fichier | l. |
|---|---|---|---|
| tcpconnection.cpp | 1980 | netbuffer.cpp | 242 |
| dhcpclient.cpp | 887 | retranstimeoutcalc.cpp | 235 |
| udpconnection.cpp | 600 | netbufferqueue.cpp | 217 |
| socket.cpp | 579 | tcprejector.cpp | 210 |
| transportlayer.cpp | 561 | ipaddress.cpp | 199 |
| arphandler.cpp | 450 | netdevlayer.cpp | 197 |
| networklayer.cpp | 434 | netsubsystem.cpp | 185 |
| linklayer.cpp | 355 | reassemblyqueue.cpp | 177 |
| dnsclient.cpp | 332 | netqueue.cpp | 150 |
| icmphandler.cpp | 272 | netconfig.cpp | 136 |
| checksumcalculator.cpp | 114 | routecache.cpp | 102 |
| netconnection.cpp | 100 | **Total .cpp minimal** | **≈ 8 700** |

+ headers `include/circle/net/*.h` (~1 500 l.), + `httpclient.cpp` 326 l. pour HTTP. Hors périmètre : mqtt*, mdns*, tftp, ntp, syslog, igmp, httpdaemon (≈ 5 000 l.). `lib/net` complet = 17 102 l. (cpp+h).
**Prérequis driver** : GENET/BCM54213 `lib/bcm54213.cpp` = 1 933 l. (+ `bcmmailbox.cpp` 100 l. pour la MAC via `PROPTAG_GET_MAC_ADDRESS`).

### A.6 Routine clé : checksum Internet (RFC 1071) AArch64

La somme en complément à 1 est indépendante de l'endianness si l'on somme des mots natifs et qu'on stocke le résultat tel quel (`strh`) : pas de `rev16` nécessaire.

```asm
; ip_csum_partial : x0 = ptr, x1 = len (octets), x2 = somme initiale (64 bits)
; retour x0 = somme 64 bits non pliée (chaînable : pseudo-header puis données)
ip_csum_partial:
        mov     x3, x2
.L32:   subs    x1, x1, #32
        b.lo    .Ltail
        ldp     x4, x5, [x0], #16
        ldp     x6, x7, [x0], #16
        adds    x3, x3, x4
        adcs    x3, x3, x5
        adcs    x3, x3, x6
        adcs    x3, x3, x7
        adc     x3, x3, xzr          ; replie la retenue à chaque tour (le subs écrase C)
        b       .L32
.Ltail: add     x1, x1, #32
        tbz     x1, #4, 1f
        ldp     x4, x5, [x0], #16
        adds    x3, x3, x4
        adcs    x3, x3, x5
        adc     x3, x3, xzr
1:      tbz     x1, #3, 2f
        ldr     x4, [x0], #8
        adds    x3, x3, x4
        adc     x3, x3, xzr
2:      tbz     x1, #2, 3f
        ldr     w4, [x0], #4
        adds    x3, x3, x4
        adc     x3, x3, xzr
3:      tbz     x1, #1, 4f
        ldrh    w4, [x0], #2
        add     x3, x3, x4
4:      tbz     x1, #0, 5f
        ldrb    w4, [x0]             ; octet impair = octet de poids faible en LE (cf. CalculateChunk)
        add     x3, x3, x4
5:      mov     x0, x3
        ret

; ip_csum_fold : x0 = somme 64 bits -> w0 = ~somme 16 bits (prête pour strh)
ip_csum_fold:
        lsr     x1, x0, #32
        adds    w0, w0, w1
        adc     w0, w0, wzr          ; 64 -> 32
        and     w1, w0, #0xFFFF
        add     w0, w1, w0, lsr #16  ; 32 -> 17
        add     w0, w0, w0, lsr #16  ; 17 -> 16
        mvn     w0, w0
        and     w0, w0, #0xFFFF
        ret
```
Pseudo-header TCP/UDP (équivalent de `CChecksumCalculator::Calculate`) : `init = src32 + dst32 + (proto << 8) + rev16(len)` (valeurs lues en LE depuis la mémoire), puis `ip_csum_partial(seg, len, init)` et `ip_csum_fold`. Vérification RX : le fold de l'ensemble (checksum inclus) doit donner 0 (`CHECKSUM_OK`). Débit attendu > 2 Go/s sur Cortex-A72 ; une version NEON (`uadalp v.4s`) est inutile à 1 Gb/s.

### A.7 Plan de transposition asm (ordre de bring-up)

1. **GENET** RX/TX polling (anneaux de descripteurs, MAC via mailbox) — test : dump de trames.
2. **netbuf pool + files** (`nb_alloc/nb_free/q_put/q_get`).
3. **Ethernet + ARP** (répondre aux requêtes ; résolution avec 1 trame en attente, 3 essais / 1 s).
4. **IPv4** (input/output, pas de fragmentation, IP statique d'abord) + **ICMP echo reply** → test `ping` depuis un PC.
5. **UDP** + table de connexions + syscalls socket.
6. **DHCP** (machine d'état : `INIT→SELECTING(DISCOVER)→REQUESTING(REQUEST)→BOUND→RENEWING(T1)→REBINDING(T2)`, options 1,3,6,51,53,54,58,59 comme `dhcpclient.h`), XID aléatoire (RNG `bcmrandom200.cpp`), flag broadcast.
7. **DNS** (requête A, RD=1, port src aléatoire, parse avec pointeurs de compression `0xC0`), lib applicative.
8. **TCP client** (sous-ensemble A.4, anneaux TX/RX, RTO 6298, MSS option dans SYN) → **HTTP GET** (`GET / HTTP/1.0\r\nHost: …\r\n\r\n`, lecture jusqu'à FIN).
9. Phase 2 : ICMP dest-unreach → erreurs socket, TCP listen/accept (serveur), reassembly hors-ordre, fast retransmit, ping sortant (raw ICMP).

Intégration noyau : `net_poll` appelé (a) par l'IRQ GENET via un flag consommé dans la boucle idle/scheduler, (b) à chaque tick (timers ARP/TCP/DHCP = comparaisons de *deadlines* en ticks, sans objets timer). Une seule entrée dans la pile à la fois (pas de réentrance) ⇒ pas de spinlocks si les syscalls réseau ne font que lire/écrire les anneaux et positionner des flags (IRQ masquées pendant ces copies).

### A.8 API socket en appels système (style MenuetOS fonction 53)

Convention : `x8 = 53` (net socket), `x0 = sous-fonction`, `x1..x5 = args`, retour `x0` (≥0 ok, <0 = -errno). `x8 = 52` : configuration réseau.

| x8 / x0 | Nom | Args | Retour |
|---|---|---|---|
| 52/0 | net_get_config | x1 = ptr struct {ip,mask,gw,dns,mac,flags} | 0 |
| 52/1 | net_set_static | x1=ip x2=mask x3=gw x4=dns | 0 |
| 52/2 | net_dhcp_status | – | 0=en cours, 1=bound |
| 53/0 | udp_open | x1=lport (0=auto) x2=rip x3=rport | handle |
| 53/1 | sock_close | x1=handle | 0 |
| 53/2 | sock_poll | x1=handle | octets dispo RX (UDP : taille datagramme suivant) |
| 53/3 | sock_read | x1=h x2=buf x3=len x4=flags (bit0 = non bloquant) | octets lus (0 + EOF flag si FIN) |
| 53/4 | sock_write | x1=h x2=buf x3=len | octets acceptés (peut être < len si anneau TX plein) |
| 53/5 | tcp_open | x1=lport(0) x2=rip x3=rport x4=mode(0 actif,1 passif) | handle |
| 53/6 | tcp_status | x1=h | état TCP (0..10, numérotation Circle) |
| 53/7 | udp_sendto | x1=h x2=buf x3=len x4=rip x5=rport | len |
| 53/8 | udp_recvfrom | x1=h x2=buf x3=len x4=ptr {rip,rport} | len |
| 53/9 | sock_setopt | x1=h x2=opt (broadcast, timeout) x3=val | 0 |
| 53/10 | icmp_ping | x1=ip x2=seq x3=timeout ms | RTT µs ou <0 |
| 53/11 | dns_resolve (option noyau) | x1=ptr nom x2=ptr ip_out | 0 / <0 |

Événement **NET (8)** posté au processus propriétaire quand un socket passe à « données dispo », « connecté » ou « fermé » (champ `wait_pid/evmask` du TCB) → l'application intègre le réseau dans sa boucle d'événements GUI sans thread.

### A.9 Estimation de taille asm (pile seule)

| Module | Lignes asm | Code |
|---|---|---|
| netbuf + files | 250 | 1 Ko |
| checksum | 70 | 0,3 Ko |
| Ethernet + ARP | 500 | 2 Ko |
| IPv4 + routage simple | 350 | 1,4 Ko |
| ICMP (echo reply + ping) | 200 | 0,8 Ko |
| UDP + table connexions | 350 | 1,4 Ko |
| TCP client minimal | 2 000–2 500 | 8–10 Ko |
| DHCP | 600 | 2,4 Ko |
| DNS | 400 | 1,6 Ko |
| couche syscalls socket | 450 | 1,8 Ko |
| **Total pile** | **≈ 5 200–5 700** | **≈ 21–23 Ko** |
| (GENET driver, séparé) | 1 500–2 000 | 6–8 Ko |
| (app HTTP GET) | 300 | 1,2 Ko |

Données : pool 64×2 Ko = 128 Ko + anneaux TCP 16×(16+32) Ko ≈ 768 Ko (BSS). Effort : 2–3 mois-homme pour un asm-iste expérimenté, TCP étant ~45 % du travail et du débogage (Wireshark indispensable).

---

## B. Graphique / GPU

### B.1 Framebuffer via mailbox (Circle)

- `lib/bcmpropertytags.cpp` (108 l.) : buffer `{size, code=0, tags…, END}` aligné 16 dans une page cohérente (`COHERENT_SLOT_PROP_MAILBOX`), `DataSyncBarrier`, écriture de l'adresse **bus** sur le canal 8 (`BCM_MAILBOX_PROP_OUT`, `lib/bcmmailbox.cpp` 100 l. ; base Pi 4 `0xFE00B880` en mode « low peri »), lecture de la réponse, `code == 0x80000000`.
- `lib/bcmframebuffer.cpp` (464 l.) : un seul message multi-tags (`m_InitTags`) :
  `SET_PHYS_WIDTH_HEIGHT 0x48003`, `SET_VIRT_WIDTH_HEIGHT 0x48004`, `SET_DEPTH 0x48005` (32), `SET_VIRTUAL_OFFSET 0x48009`, `ALLOCATE_BUFFER 0x40001` (align), `GET_PITCH 0x40008`. Adresse ARM = `base & 0x3FFFFFFF` (l.176). Autres : `GET_DISPLAY_DIMENSIONS 0x40003`, `SetVirtualOffset` (tag `0x48009`, l.386), `WaitForVerticalSync` (tag `PROPTAG_WAIT_FOR_VSYNC 0x4800E`, l.404), `SET_PALETTE` (8 bpp), backlight. Format 32 bpp = ARGB8888 (`CDisplay`, `bcmframebuffer.cpp` l.49) — vérifier l'ordre R/B (`framebuffer_swap` dans config.txt).
- **MMU** : mapper le framebuffer en *Normal Non-Cacheable* (ou Normal WT) et **pas en Device** : les `STP q`/accès non alignés fauteraient en Device-nGnRnE, et le write-combining NC donne le débit.

### B.2 Double buffering et vsync

`lib/2dgraphics.cpp` (567 l.) : `Initialize` (l.173) crée le FB avec **hauteur virtuelle = 2×H** (RASPPI ≤ 4), dessine dans un buffer RAM `m_pBuffer8` (cacheable), puis `UpdateDisplay` (l.540) : `WaitForVerticalSync()` → `SetArea()` (copie DMA 2D du buffer RAM vers la demi-page cachée) → `SetVirtualOffset(0, swapped ? H : 0)` → bascule. Primitives : `DrawRect`, `DrawRectOutline`, `DrawLine` (Bresenham), `DrawCircle`, `DrawImage[Transparent|Rect]`, `DrawPixel`, `DrawText` (via `CCharGenerator`).

Proposé pour l'OS :
1. **Shadow buffer** cacheable (W×H×4) où le compositeur écrit (lectures rapides pour blending/curseur logiciel).
2. FB physique 2 pages ; à chaque frame : copier les **rectangles sales** du shadow vers la page cachée (DMA4 2D ou NEON), `SET_VIRTUAL_OFFSET`, puis recopier les mêmes rects dans l'autre page au frame suivant (ou garder une liste de dirty par page).
3. vsync : le tag `0x4800E` bloque l'appelant jusqu'au vblank (appel mailbox synchrone ~16 ms max) ; à tester sur firmware Pi 4 (comportement dépendant de `dtoverlay=vc4-fkms/kms` — **ne pas** charger de KMS en bare-metal). Alternative : accepter un léger tearing et écrire directement dans une page unique (MenuetOS fait ainsi), ce qui supprime le flip.

### B.3 Texte, polices, écran console

- `lib/chargenerator.cpp` (74 l.) : `GetPixelLine(c, y)` = `font.data[(c - first)*height + y]`, stride 1–4 octets selon largeur, double largeur/hauteur. Polices en tables : `lib/font8x8.cpp`, `font8x16.cpp` (267 l. chacune), `font6x7.cpp`, `font12x22.cpp` (2 202 l.). En asm : `file 'font8x16.bin'` (4 Ko pour 256 car.) ; `draw_char` = 16 lignes × test de 8 bits → `str w` couleur (ou masque NEON : `tbl`/`cmtst` sur 8 pixels → `bsl` couleur fond/encre, 2 `stp`/ligne).
- `lib/screen.cpp` (290 l.) + `lib/terminal.cpp` (1 066 l.) : console VT100 sur FB — utile comme console de debug noyau ; une version asm « TTY sans escape » fait ~200 l.

### B.4 DMA (`lib/dma4channel.cpp`, 572 l.)

`SetupMemCopy2D` (l.363) : mode 2D (`TI4_TDMODE`), `XLENGTH` = octets par ligne, `YLENGTH` = lignes−1, `DEST4_STRIDE` = pitch − largeur, bursts 128 bits, adresses 40 bits (`ADDRESS4_LOW/HIGH`) ; canaux DMA4 = 11–14 sur Pi 4 (`include/circle/machineinfo.h`). `CBcmFrameBuffer::SetArea` (l.220) l'utilise si `SCREEN_DMA_BURST_LENGTH` est défini, avec attente/IRQ de fin.
Pour l'OS : **blit rect shadow → FB** en un seul control block (src stride possible via 2 CB ou une ligne = un CB si le shadow a le même pitch — le plus simple : shadow avec **même pitch** que le FB, alors src_stride = dest_stride). **Remplissage** : source non incrémentée (`SOURCE4_INC` à 0) pointant sur 16 o de couleur. Contraintes : `clean` D-cache sur le source (`dc cvac` par ligne de 64 o) avant DMA ; le DMA libère le CPU mais pour des rects < ~16 Ko la copie NEON est plus rapide que la mise en place. Gain réel : plein écran 1080p (8,3 Mo) pendant que le CPU fait autre chose.

### B.5 Curseur souris

`lib/input/mousebehaviour.cpp` (316 l.) : **curseur matériel** via `PROPTAG_SET_CURSOR_INFO 0x8010` (16×16 ARGB, adresse bus du bitmap, hotspot) et `PROPTAG_SET_CURSOR_STATE 0x8011` (x, y, visible) — activé pour RASPPI ≤ 4. Avantage énorme : pas de *save-under*, le compositeur ignore le curseur. Coût : un appel mailbox (~quelques dizaines de µs) par mouvement → limiter à 1 mise à jour par tick. Fallback : curseur logiciel dessiné en dernier dans le shadow (rect sale = ancienne + nouvelle position).

**Prérequis entrée** : clavier/souris USB sur Pi 4 = PCIe (`lib/bcmpciehostbridge.cpp`) + xHCI VL805 (`lib/usb/xhci*.cpp`) + HID : ≈ 4 900 l. C++ (+ `usbkeyboard.cpp` 272, `usbmouse.cpp`, `keyboardbehaviour.cpp` 193). C'est **le vrai goulot** de la GUI, à traiter dans une étude dédiée ; en attendant : entrée par UART (clavier) pour développer le WM.

### B.6 addon/vc4 (VCHIQ, V3D) — évaluation

- `addon/vc4/vchiq/` ≈ 9 200 l. C (portage du driver Linux VCHIQ, GPLv2, dépend de `addon/linux` émulation), `addon/vc4/interface/` ≈ 34 900 l. C (EGL/GLES/OpenVG/Dispmanx du *userland* Raspbian). Le README indique : « It does build with AARCH = 32 only » et support expérimental.
- Sur **Pi 4**, le GL firmware (pile VC4 « legacy ») n'est plus fourni : le V3D 4.2 du BCM2711 est piloté directement par l'ARM (Mesa `v3d`), ce qui demande un driver de command lists/tiling non documenté par Broadcom.
- **Verdict : hors de portée** pour un OS asm. Seul intérêt potentiel : Dispmanx (couches matérielles HVS, scaling, alpha) via VCHIQ — mais ~10 k l. de protocole à porter pour un gain modeste. Le HVS pourrait aussi être programmé directement (registres `0xFE400000`, display lists), non documenté officiellement → reporter.
- `addon/display/` (2 748 l.) : drivers SPI/I²C d'afficheurs externes (ILI9341, ST7789, SSD1306, HD44780) ; utile seulement pour une cible sans HDMI. `font6x8.h` réutilisable.

### B.7 Architecture GUI proposée (type MenuetOS, dans le noyau)

**Table de fenêtres** (max 64 × 128 o) :
```
0  pid(4) flags(4)         ; visible, focus, sans-cadre, skin, redraw_pending
8  x y w h (4×4)           ; position écran, cadre inclus
24 cx cy cw ch             ; zone client (relative)
40 surface_ptr(8) pitch(4) ; surface privée ARGB (compositing)
52 bg_color title_color
60 title_ptr(8)
68 btn_tbl_ptr(8) nbtn(4)  ; boutons {id, x,y,w,h}
80 dirty_x0 y0 x1 y1       ; rect sale accumulé
```
`zorder[64]` = indices du fond (0 = bureau) au premier plan ; fenêtre active = dernier.

**Compositing simple** (recommandé plutôt que la *window stack map* octet/pixel de MenuetOS) : chaque fenêtre possède sa surface (RAM abondante sur Pi 4 : 1 Mo pour 512×512). Les syscalls de dessin écrivent dans la surface (clip au seul rect client → pas de calcul d'occlusion). Le compositeur, une fois par tick (60 Hz) si dirty : pour chaque rect sale écran, parcourt `zorder` de bas en haut et blit l'intersection de chaque fenêtre (copie opaque, pas d'alpha au début) dans le shadow, puis push vers le FB (B.2/B.4). Déplacement de fenêtre = rect sale (ancienne ∪ nouvelle position), aucun redraw applicatif → plus robuste que MenuetOS (qui envoie un événement redraw aux fenêtres découvertes). Optimisation : ignorer les fenêtres entièrement couvertes par une fenêtre opaque au-dessus.

**Événements par processus** (comme MenuetOS : bits en attente + petits tampons) :
`pending` (bitmask), `evmask` (masque fonction 40), `keybuf[16]` (anneau), `btnbuf[8]`, état souris (x, y, boutons) lu à la demande. Types : 1 REDRAW, 2 KEY, 3 BUTTON (bouton défini cliqué, id), 6 MOUSE, 7 IPC, 8 NET. `wait_event` met le processus en état bloqué ; le WM le réveille en positionnant un bit. Clic : hit-test `zorder` du haut vers le bas → barre de titre (drag noyau), bouton fermer (BUTTON id 1), boutons applicatifs (BUTTON id), sinon MOUSE au propriétaire ; clic sur fenêtre inactive = passage au premier plan.

**Décorations** dessinées par le noyau dans la surface lors de `draw_window` (cadre, barre de titre, bouton ×), app = zone client uniquement.

### B.8 Routine clé : fill_rect 32 bpp (STP/NEON)

```asm
; fill_rect32 : x0 = base surface, x1 = pitch (octets), w2 = x, w3 = y, w4 = w, w5 = h, w6 = couleur ARGB
; préconditions : rect déjà clippé, w>0, h>0 ; mémoire Normal (cacheable ou NC)
fill_rect32:
        madd    x0, x3, x1, x0          ; base + y*pitch   (x3 = y zero-étendu par l'écriture w3)
        add     x0, x0, x2, lsl #2      ; + x*4
        dup     v0.4s, w6
        mov     v1.16b, v0.16b
.Lrow:  mov     x9, x0                  ; ptr ligne
        mov     w10, w4                 ; pixels restants
        ; aligner sur 16 octets (0..3 pixels de tête)
.Lhead: tst     x9, #15
        b.eq    .Lbody
        str     w6, [x9], #4
        subs    w10, w10, #1
        b.eq    .Lnext
        b       .Lhead
.Lbody: subs    w10, w10, #16           ; 16 px = 64 o = 1 ligne de cache
        b.lo    .Lt8
.L16:   stp     q0, q1, [x9]
        stp     q0, q1, [x9, #32]
        add     x9, x9, #64
        subs    w10, w10, #16
        b.hs    .L16
.Lt8:   tbz     w10, #3, 1f             ; w10+16 reste ∈ [0,15] : bits 3..0 valides
        stp     q0, q1, [x9], #32
1:      tbz     w10, #2, 2f
        str     q0, [x9], #16
2:      tbz     w10, #1, 3f
        str     d0, [x9], #8
3:      tbz     w10, #0, .Lnext
        str     w6, [x9]
.Lnext: add     x0, x0, x1
        subs    w5, w5, #1
        b.ne    .Lrow
        ret
```
(`tbz` sur w10 négatif fonctionne car les bits bas de `reste−16` en complément à 2 sont ceux de `reste`.) Débit attendu ≈ bande passante d'écriture DRAM (plusieurs Go/s) : plein écran 1080p ≈ 2–3 ms. `blit32` (copie rect) = même squelette avec `ldp q0,q1 / stp q0,q1`. Pour le zéro sur mémoire cacheable : `dc zva` (64 o) possible.

### B.9 Appels système GUI (AArch64, numéros MenuetOS conservés)

| x8 | Fonction | Args |
|---|---|---|
| -1 (ou 0xFFFF) | exit | – |
| 0 | draw_window | x0=x x1=y x2=w x3=h x4=couleur client \| style<<24 x5=ptr titre |
| 1 | put_pixel | x0=x x1=y x2=couleur |
| 2 | get_key | → x0 = code (ou -1) |
| 4 | draw_text | x0=x x1=y x2=couleur (bit31 = fond) x3=ptr x4=len |
| 5 | sleep | x0 = centièmes de s |
| 8 | define_button | x0=x x1=y x2=w x3=h x4=id (bit31 = supprimer) x5=couleur |
| 10 | wait_event | → x0 = type |
| 11 | check_event | → x0 = type ou 0 |
| 12 | redraw | x0 = 1 début / 2 fin (fin = marque la surface entière sale) |
| 13 | fill_rect | x0=x x1=y x2=w x3=h x4=couleur |
| 17 | get_button | → x0 = id |
| 23 | wait_event_timeout | x0 = centièmes |
| 37 | mouse | x0 = 0 pos écran / 1 pos fenêtre / 2 boutons |
| 38 | draw_line | x0=x1 x1=y1 x2=x2 x3=y2 x4=couleur |
| 40 | set_event_mask | x0 = masque |
| 7 | blit_image | x0=ptr ARGB x1=x x2=y x3=w x4=h |
| 52/53 | réseau | cf. A.8 |

Coordonnées relatives à la zone client (MenuetOS : relatives à la fenêtre) ; clipping noyau systématique. Contrairement à MenuetOS 32 bits (x<<16 | taille dans un registre), on passe un argument par registre : AArch64 en a assez.

### B.10 Estimation de taille asm (GUI)

| Module | Lignes asm | Code |
|---|---|---|
| mailbox + init FB + flip + vsync | 250 | 1 Ko |
| primitives (pixel, fill, h/vline, rect, line, blit, char/text 8×16) | 800 | 3 Ko |
| gestionnaire de fenêtres (table, z-order, décor, drag, focus, boutons, hit-test) | 1 500 | 6 Ko |
| compositeur (dirty rects, intersection, push FB) | 500 | 2 Ko |
| événements + dispatch syscalls GUI | 400 | 1,6 Ko |
| curseur (HW mailbox / SW) | 150 / 300 | 0,6–1,2 Ko |
| blit DMA4 | 250 | 1 Ko |
| **Total GUI noyau** | **≈ 3 900** | **≈ 15–16 Ko** |

Données : police 4 Ko, shadow 8,3 Mo (1080p) + 2 pages FB 16,6 Mo (côté VC), surfaces fenêtres.
Référence C++ : bcmframebuffer 464 + bcmpropertytags 108 + bcmmailbox 100 + 2dgraphics 567 + screen 290 + chargenerator 74 + dma4channel 572 + mousebehaviour 316 ≈ 2 500 l. (Circle n'a pas de gestionnaire de fenêtres : cette partie est à concevoir, MenuetOS/KolibriOS servent de modèle.)

---

## C. Rappel MenuetOS et équivalent AArch64

### C.1 Modèle MenuetOS (de mémoire, à vérifier)

- OS x86 (32 bits, puis 64 bits) entièrement en FASM ; KolibriOS en est un fork 32 bits.
- **Appels système via `int 0x40`** : `eax` = numéro de fonction, `ebx, ecx, edx, esi, edi` = paramètres, résultat dans `eax` (parfois `ebx`). Coordonnées souvent empaquetées : `ebx = x<<16 | largeur`, `ecx = y<<16 | hauteur`.
- Fonctions emblématiques : 0 définir/dessiner fenêtre, 1 pixel, 2 lire touche, 3 heure, 4 texte, 5 délai, 8 bouton, 10 attendre événement, 11 tester événement, 12 début/fin de redessin, 13 rectangle plein, 17 lire bouton, 23 attente avec timeout, 37 souris, 38 ligne, 40 masque d'événements, 47 afficher nombre, 52/53 réseau (53 = sockets UDP/TCP : open, close, poll, read, write, status…), −1 terminer.
- **Fenêtres gérées par le noyau** : décor dessiné par le noyau ; l'application dessine directement à l'écran, le noyau clippe via une carte d'appartenance des pixels ; quand une fenêtre est découverte, l'app reçoit REDRAW et redessine tout.
- **Événements** : 1 redraw, 2 touche, 3 bouton, 6 souris (si masque), 7 IPC, 8 réseau… Pas de file de messages riche : un bit par type + buffers de touches/boutons.
- **Application** : binaire plat avec en-tête `'MENUET01'`, version, point d'entrée, fin d'image, mémoire requise, pile. Boucle typique :

```asm
start:  call draw_window
still:  mov  eax, 10        ; attendre événement
        int  0x40
        cmp  eax, 1         ; redraw
        je   red
        cmp  eax, 2         ; touche
        je   key
        cmp  eax, 3         ; bouton
        je   button
        jmp  still
red:    call draw_window
        jmp  still
key:    mov  eax, 2
        int  0x40
        jmp  still
button: mov  eax, 17
        int  0x40
        cmp  ah, 1          ; id 1 = fermer
        jne  still
        or   eax, -1
        int  0x40
draw_window:
        mov  eax, 12
        mov  ebx, 1         ; début redessin
        int  0x40
        xor  eax, eax       ; fonction 0
        mov  ebx, 100*65536+300
        mov  ecx, 100*65536+120
        mov  edx, 0x03FFFFFF
        int  0x40
        mov  eax, 12
        mov  ebx, 2         ; fin redessin
        int  0x40
        ret
```

### C.2 Équivalent AArch64 proposé

- **`SVC #0`**, `x8` = numéro de fonction (convention Linux AArch64, familière aux outils), `x0..x5` = arguments, retour `x0` (et `x1` si 2 valeurs). Registres `x9..x15` éventuellement écrasés, `x19..x28`, `sp`, `x29/x30` préservés.
- Vecteur `EL0 Sync` → `esr_el1.EC == 0x15` (SVC AArch64) → `cmp x8, #NMAX` → `ldr x16, [tbl, x8, lsl #3]` → `blr x16` → `eret`. Toute autre `EC` → faute processus.
- Applications en EL0, noyau en EL1, pages utilisateur `AP=01` ; validation des pointeurs utilisateurs (plage de l'espace du processus) avant tout accès noyau — ou `LDTR/STTR` (accès « non privilégiés » qui fautent proprement).
- En-tête binaire : `'ARMENU01'`, entrée, fin d'image, mémoire, pile — chargé à une adresse virtuelle fixe (ex. 0x0040_0000), comme MenuetOS.

```asm
; FASMARM, application AArch64
start:  bl      draw_window
still:  mov     x8, 10              ; wait_event
        svc     #0
        cmp     x0, 1
        b.eq    red
        cmp     x0, 2
        b.eq    key
        cmp     x0, 3
        b.eq    button
        cmp     x0, 8               ; NET : socket prêt
        b.eq    net
        b       still
red:    bl      draw_window
        b       still
key:    mov     x8, 2
        svc     #0
        b       still
button: mov     x8, 17
        svc     #0
        cmp     x0, 1               ; fermer
        b.ne    still
        mov     x8, -1
        svc     #0
net:    mov     x8, 53              ; sock_read non bloquant
        mov     x0, 3
        ldr     w1, [handle]
        adr     x2, rxbuf
        mov     x3, 4096
        mov     x4, 1
        svc     #0
        b       still

draw_window:
        stp     x29, x30, [sp, -16]!
        mov     x8, 12
        mov     x0, 1
        svc     #0
        mov     x8, 0               ; draw_window
        mov     x0, 100
        mov     x1, 100
        mov     x2, 300
        mov     x3, 120
        ldr     w4, =0x03FFFFFF
        adr     x5, title
        svc     #0
        mov     x8, 4               ; draw_text
        mov     x0, 10
        mov     x1, 10
        mov     x2, 0
        adr     x3, msg
        mov     x4, msg_len
        svc     #0
        mov     x8, 12
        mov     x0, 2
        svc     #0
        ldp     x29, x30, [sp], 16
        ret
```

---

## Synthèse

| Bloc | Réf. Circle (l. C++) | Asm estimé | Faisabilité |
|---|---|---|---|
| Pile TCP/IP minimale | ≈ 8 700 (+ GENET 1 933) | ≈ 5 500 l. / 22 Ko (+ GENET ~1 800 l.) | **Oui** ; TCP = principal risque |
| GUI noyau (FB, primitives, WM, compositeur, événements) | ≈ 2 500 (pas de WM dans Circle) | ≈ 3 900 l. / 16 Ko | **Oui** |
| Entrée USB (PCIe + xHCI + HID) | ≈ 5 400 | ≈ 4 000–5 000 l. | Difficile, prérequis GUI |
| Accélération V3D / VCHIQ | ≈ 44 000 (32 bits seulement) | – | **Hors de portée** |
