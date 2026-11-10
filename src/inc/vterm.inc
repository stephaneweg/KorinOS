; ============================================================================
;  inc/vterm.inc - protocole du service vterm : pseudo-terminaux (pty)
;  (docs/VTERM.md, docs/TERMINAL-ET-FORK.md 2.1 ; service : src/svc/vterm)
;
;  Message (512 o, comme fs.inc) : +0 op / resultat, +8 a, +16 b, +24 c,
;  +32 d, +40 longueur des donnees, +44 pid de l'appelant, +48 donnees.
;
;  Un pty : un bout maitre (le Terminal, telnetd) et un bout esclave (entree
;  et sorties du programme). Poignee d'un bout : VT_TAG + 1 + 2 x pty + cote
;  (0 maitre, 1 esclave) ; le bit VT_TAG la distingue d'un bout de tube de vfs
;  (1..128) partout ou un descripteur standard passe (spawn, V_STDIO). Un bout
;  appartient a un ou plusieurs pid (le createur, les fils qui l'ont recu en
;  entree / sortie, ceux nes d'un fork) : E_BADF pour les autres.
;
;    VT_OPEN                       -> a maitre, b esclave, c numero du pty
;    VT_READ  (a bout, b octets <= 464, c VT_NOWAIT)
;                                  -> octets (donnees), 0 fin, E_AGAIN, E_INTR
;    VT_WRITE (a bout, donnees)    -> octets acceptes (0 : plein, redemander),
;                                     VT_E_IO (l'autre cote est parti)
;    VT_CLOSE (a bout)             l'appelant quitte le bout
;    VT_TCGETS (a bout)            -> donnees : VTT_* (48 o)
;    VT_TCSETS (a bout, b 0 maintenant / 1 apres la sortie / 2 et vider
;              l'entree ; donnees : VTT_*)
;    VT_GWINSZ (a bout)            -> a lignes, b colonnes, c, d pixels
;    VT_SWINSZ (a bout, b lignes, c colonnes, d pixels x | y << 16)
;                                  SIGWINCH au groupe de premier plan si change
;    VT_GPGRP  (a bout)            -> a groupe de premier plan (0 : aucun)
;    VT_SPGRP  (a bout, b groupe)
;    VT_INFO   (a bout)            -> a 1 maitre / 2 esclave, b numero du pty,
;                                     c octets a lire (FIONREAD), d 1 : fin vue
;    VT_FLUSH  (a bout, b 0 entree / 1 sortie / 2 les deux)
;    VT_SLAVE  (b numero du pty)   -> a bout esclave, l'appelant en devient
;                                     proprietaire (open de /dev/pts/N)
;    VT_SCTTY  (a bout)            TIOCSCTTY : le pty devient le terminal de
;                                     controle de la session de l'appelant (son
;                                     meneur seulement ; E_PERM) ; son groupe passe
;                                     au premier plan
;    VT_NOTTY  (a bout)            TIOCNOTTY : le meneur rend le terminal (SIGHUP,
;                                     SIGCONT au premier plan) ; E_NOTTY si ce n'est
;                                     pas le terminal de sa session
;    VT_GSID   (a bout)            -> a session du terminal (0 : aucune ; tcgetsid)
;  Controle des taches : un process de la session (ou de toute session, si le pty
;  n'en a pas) hors du groupe de premier plan qui lit l'esclave recoit SIGTTIN ;
;  qui ecrit avec TOSTOP, ou fait VT_TCSETS, VT_SPGRP, VT_FLUSH, recoit SIGTTOU ;
;  la requete rend alors E_INTR (le client redemande apres SIGCONT), ou VT_E_IO
;  (SIGTTIN ignore ou masque ; groupe orphelin). SIGTTOU ignore ou masque :
;  l'operation se fait.
;  Avis du noyau : naissance (0xFFFE : le fils devient proprietaire des bouts
;  de vterm que le pere lui donne en entree / sortie), fork (0xFFFC : tous les
;  bouts du pere), fin (0xFFFF : le pid quitte ses bouts).
; ============================================================================

VT_TAG		= 0x40000000		; bit des poignees de vterm
VT_OPEN		= 1
VT_READ		= 2
VT_WRITE	= 3
VT_CLOSE	= 4
VT_TCGETS	= 5
VT_TCSETS	= 6
VT_GWINSZ	= 7
VT_SWINSZ	= 8
VT_GPGRP	= 9
VT_SPGRP	= 10
VT_INFO		= 11
VT_FLUSH	= 12
VT_SLAVE	= 13
VT_SCTTY	= 14
VT_NOTTY	= 15
VT_GSID		= 16
VT_NOWAIT	= 1			; (VT_READ, c) E_AGAIN plutot qu'attendre
VT_E_IO		= -5
VT_E_INTR	= -4
VT_E_NOTTY	= -25
VT_E_PERM	= -1

; donnees de VT_TCGETS / VT_TCSETS : struct termios du noyau Linux, cc a 32
VTT_IFLAG	= 0
VTT_OFLAG	= 4
VTT_CFLAG	= 8
VTT_LFLAG	= 12
VTT_CC		= 16			; 32 octets (VINTR..)
VTT_TAILLE	= 48

; c_cc (valeurs de Linux)
VTC_INTR	= 0
VTC_QUIT	= 1
VTC_ERASE	= 2
VTC_KILL	= 3
VTC_EOF		= 4
VTC_TIME	= 5
VTC_MIN		= 6
VTC_SUSP	= 10
VTC_EOL		= 11
VTC_REPRINT	= 12
VTC_WERASE	= 14
VTC_LNEXT	= 15
; c_iflag
VTI_INLCR	= 0x40
VTI_IGNCR	= 0x80
VTI_ICRNL	= 0x100
VTI_ISTRIP	= 0x20
VTI_IUTF8	= 0x4000
; c_oflag
VTO_OPOST	= 1
VTO_ONLCR	= 4
VTO_OCRNL	= 8
; c_lflag
VTL_ISIG	= 1
VTL_ICANON	= 2
VTL_ECHO	= 8
VTL_ECHOE	= 0x10
VTL_ECHOK	= 0x20
VTL_ECHONL	= 0x40
VTL_NOFLSH	= 0x80
VTL_ECHOCTL	= 0x200
VTL_ECHOKE	= 0x800
VTL_TOSTOP	= 0x100
VTL_IEXTEN	= 0x8000
