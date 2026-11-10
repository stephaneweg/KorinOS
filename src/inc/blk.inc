; ============================================================================
;  inc/blk.inc - protocole des peripheriques blocs (docs/MICRO-NOYAU.md,
;  section 4) : un service « blk.<nom> » par disque et par partition
;  (blk.sd0, blk.sd0p1...), secteurs de 512 octets, numerotes depuis le
;  debut du peripherique (une partition : depuis son debut).
;
;  Les donnees passent par un tampon partage du client (appels 70-73) : le
;  client le cree, l'autorise pour le service (buf_grant, pid rendu par
;  info), puis l'attache ; ensuite lire / ecrire designent une adresse dans
;  ce tampon (la meme dans les deux process).
;    1 info -> a 1 si lecture seule, b secteurs, c 512, d pid du service
;    2 attacher(a numero du tampon, b sa taille) -> 0
;    3 lire(a secteur, b nombre, c adresse dans le tampon) -> 0
;    4 ecrire(a secteur, b nombre, c adresse dans le tampon) -> 0
;    5 dma : le pilote passe en DMA s'il le peut (init2, options lues) -> 0
;    6 lire_d(a secteur, b nombre, c adresse chez l'appelant) -> 0 : sans passer
;      par son tampon (SVC_MAP_CALLER) ; E_AGAIN : pas possible, passer par lui
;    7 stats(a attente active en us, -1 : inchangee) -> sommeils, a commandes,
;      b preparation, c commande, d donnees (us) ; compteurs remis a zero
;    8 uhs : la carte passe en 1,8 V, DDR50 si elle le peut (init2) -> 0
; ============================================================================

BLK_INFO	= 1
BLK_ATTACHER	= 2
BLK_LIRE	= 3
BLK_ECRIRE	= 4
BLK_DMA		= 5
BLK_LIRE_D	= 6
BLK_STATS	= 7
BLK_UHS		= 8
BLK_D_MAX	= 2048			; secteurs (1 Mo)
E_PERM		= -1
E_IO		= -5
