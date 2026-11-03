; ============================================================================
;  kits/audiokit/ak_ogg.inc - Ogg (RFC 3533) : pages, paquets ; Vorbis, Opus
;
;  Le premier flux logique (numero de serie de la premiere page) est lu,
;  les autres sautes. Un paquet peut s'etendre sur plusieurs pages ; la
;  position (granule) d'une page vaut pour le dernier paquet qui s'y termine.
;  Duree : granule de la derniere page (cherchee dans les 64 derniers Ko).
;  Recherche : bissection sur les pages, puis « calibrage » : on decode en
;  jetant le son jusqu'au premier paquet dont la page donne la position.
; ============================================================================

OG_SERIAL	= 0
OG_PAGE		= 8		; corps de la page courante (copie, 65 Ko)
OG_LACE		= 16		; table des segments (copie)
OG_NSEG		= 24
OG_SEGI		= 32		; segment suivant
OG_BODYI	= 40		; octet suivant du corps
OG_GRAN		= 48		; granule de la page courante
OG_LASTSEG	= 56		; dernier segment qui termine un paquet (-1 : aucun)
OG_PKT		= 64		; paquet assemble
OG_PKTCAP	= 72
OG_CODEC	= 80		; AKF_VORBIS / AKF_OPUS
OG_CAL		= 88		; 1 : calibrage apres une recherche
OG_TARGET	= 96
OG_PREROLL	= 104		; trames a decoder avant la cible
OG_GRANOFF	= 112		; a retirer des granules (avance d'Opus)
OG_SKIPPKT	= 120		; 1 : jeter la suite d'un paquet commence avant
OG_DATA0	= 128		; premiere page de son
OG_STATE	= 136		; etat du codec (Vorbis, Opus)
OG_EOS		= 144
OG_SIZE		= 160
OG_PAGEMAX	= 65536 + 512

; x0 = decodeur -> 0 / < 0
ogg_open:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x1,OG_SIZE
	bl	akd_alloc
	cbz	x0,.nomem
	mov	x20,x0
	mov	x0,x19
	movl	x1,OG_PAGEMAX + 256
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,OG_PAGE]
	movl	x1,OG_PAGEMAX
	add	x0,x0,x1
	str	x0,[x20,OG_LACE]
	mov	x0,x19
	mov	x1,65536
	bl	akd_alloc
	cbz	x0,.nomem
	str	x0,[x20,OG_PKT]
	mov	x0,65536
	str	x0,[x20,OG_PKTCAP]
	mov	x0,-1
	str	x0,[x20,OG_SERIAL]		; (-1 : le premier vu)
	; le codec du premier paquet
	mov	x0,x19
	mov	x1,x20
	bl	ogg_packet
	tbnz	x0,63,.bad
	mov	x21,x0				; paquet
	mov	x22,x1				; octets
	cmp	x22,8
	b.lo	.bad
	ldr	x0,[x21]
	movl	x1,0x736962726F7601		; "\x01vorbis" (7 octets)
	and	x2,x0,0xFFFFFFFFFFFFFF
	cmp	x2,x1
	b.eq	.vorbis
	movl	x1,0x646165487375704F		; "OpusHead"
	cmp	x0,x1
	b.eq	.opus
.unsup:	mov	x0,AKE_FORMAT
	b	.out
.vorbis:
	str	x20,[x19,AKD_CODEC]
	mov	x0,AKF_VORBIS
	str	x0,[x20,OG_CODEC]
	mov	x0,x19
	bl	vb_new
	tbnz	x0,63,.out
	mov	x23,0				; en-tetes lus
.vh:	mov	x0,x19
	mov	x1,x21
	mov	x2,x22
	bl	vb_header
	tbnz	x0,63,.out
	add	x23,x23,1
	cmp	x23,3
	b.hs	.hdone
	mov	x0,x19
	mov	x1,x20
	bl	ogg_packet
	tbnz	x0,63,.bad
	mov	x21,x0
	mov	x22,x1
	b	.vh
.opus:	str	x20,[x19,AKD_CODEC]
	mov	x0,AKF_OPUS
	str	x0,[x20,OG_CODEC]
	mov	x0,x19
	mov	x1,x21
	mov	x2,x22
	bl	ops_head			; OpusHead, puis OpusTags
	tbnz	x0,63,.out
	mov	x0,x19
	mov	x1,x20
	bl	ogg_packet
	tbnz	x0,63,.bad
	mov	x2,x1
	mov	x1,x0
	mov	x0,x19
	bl	ops_tags
.hdone:	; debut du son : la page qui suit (les en-tetes finissent leur page)
	ldr	x0,[x19,AKD_OFF]
	str	x0,[x20,OG_DATA0]
	str	x0,[x19,AKD_DATA0]
	; duree : granule de la derniere page
	mov	x0,x19
	bl	ogg_last_granule
	tbnz	x0,63,.nodur
	ldr	x1,[x20,OG_GRANOFF]
	subs	x0,x0,x1
	csel	x0,xzr,x0,lt
	str	x0,[x19,AKD_TOTAL]
	mov	x1,1
	str	x1,[x19,AKD_LIMIT]
	cbz	x0,.nodur
	ldr	x1,[x19,AKD_DATAEND]		; debit moyen
	ldr	x2,[x20,OG_DATA0]
	sub	x1,x1,x2
	lsl	x1,x1,3
	ldr	x2,[x19,AKD_RATE]
	mul	x1,x1,x2
	udiv	x1,x1,x0
	str	x1,[x19,AKD_BITRATE]
.nodur:	ldr	x1,[x20,OG_DATA0]		; retour au debut du son
	mov	x0,x19
	bl	akd_seek
	ldr	x0,[x20,OG_NSEG]		; (page courante finie)
	str	x0,[x20,OG_SEGI]
	adr	x0,ogg_next
	str	x0,[x19,AKD_NEXT]
	adr	x0,ogg_seek
	str	x0,[x19,AKD_SEEKFN]
	mov	x0,0
	b	.out
.nomem:	mov	x0,AKE_NOMEM
	b	.out
.bad:	mov	x0,AKE_CORRUPT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = etat Ogg : lit la page suivante du flux (les autres
; flux sont sautes) -> 0 / < 0 (fin)
ogg_page:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x23,0				; octets sautes
.next:	ldr	x0,[x19,AKD_DATAEND]
	ldr	x1,[x19,AKD_OFF]
	sub	x0,x0,x1
	cmp	x0,27
	b.lt	.end
	mov	x0,x19
	mov	x1,27+255
	bl	akd_need
	cmp	x1,27
	b.lo	.end
	mov	x21,x0
	ldr	w0,[x21]
	movl	w2,0x5367674F			; 'OggS'
	cmp	w0,w2
	b.ne	.resync
	ldrb	w0,[x21,4]			; version 0
	cbnz	w0,.resync
	ldrb	w22,[x21,26]			; segments
	add	x0,x22,27
	cmp	x1,x0
	b.lo	.end
	mov	x0,0				; taille du corps
	mov	x2,0
.sum:	cmp	x2,x22
	b.hs	.sumd
	add	x3,x21,27
	ldrb	w3,[x3,x2]
	add	x0,x0,x3
	add	x2,x2,1
	b	.sum
.sumd:	mov	x24,x0
	ldr	w0,[x21,14]			; numero de serie
	ldr	x1,[x20,OG_SERIAL]
	cmn	x1,1
	b.ne	.ser
	mov	x1,x0
	str	x1,[x20,OG_SERIAL]
.ser:	cmp	x0,x1
	b.ne	.skippg
	; page du flux : table des segments, granule, corps
	ldr	x0,[x20,OG_LACE]
	add	x1,x21,27
	mov	x2,x22
	bl	ak_memmove
	str	x22,[x20,OG_NSEG]
	str	xzr,[x20,OG_SEGI]
	str	xzr,[x20,OG_BODYI]
	ldr	x0,[x21,6]
	str	x0,[x20,OG_GRAN]
	ldrb	w0,[x21,5]			; suite d'un paquet commence avant ?
	and	w0,w0,1
	ldr	x1,[x20,OG_SKIPPKT]
	orr	x0,x0,x1
	str	x0,[x20,OG_SKIPPKT]
	ldrb	w0,[x21,5]
	and	w0,w0,4
	str	x0,[x20,OG_EOS]
	; dernier segment qui termine un paquet
	mov	x0,-1
	mov	x2,0
.ls:	cmp	x2,x22
	b.hs	.lsd
	ldr	x3,[x20,OG_LACE]
	ldrb	w3,[x3,x2]
	cmp	w3,255
	csel	x0,x2,x0,lo
	add	x2,x2,1
	b	.ls
.lsd:	str	x0,[x20,OG_LASTSEG]
	mov	x0,x19
	add	x1,x22,27
	bl	akd_skip
	mov	x0,x19
	mov	x1,x24
	bl	akd_need
	cmp	x1,x24
	b.lo	.end
	mov	x1,x0
	ldr	x0,[x20,OG_PAGE]
	mov	x2,x24
	bl	ak_memmove
	mov	x0,x19
	mov	x1,x24
	bl	akd_skip
	mov	x0,0
	b	.out
.skippg:
	mov	x0,x19
	add	x1,x22,27
	add	x1,x1,x24
	bl	akd_skip
	b	.next
.resync:
	mov	x0,x19
	mov	x1,1
	bl	akd_skip
	add	x23,x23,1
	movl	x0,1048576
	cmp	x23,x0
	b.lo	.next
.end:	mov	x0,-1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = etat Ogg -> x0 = paquet, x1 = octets, x2 = granule (si
; c'est le dernier paquet termine sur sa page, -1 sinon) ; x0 < 0 : fin
ogg_packet:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,0				; octets du paquet
.seg:	ldr	x0,[x20,OG_SEGI]
	ldr	x1,[x20,OG_NSEG]
	cmp	x0,x1
	b.lo	.take
	mov	x0,x19				; page suivante
	mov	x1,x20
	bl	ogg_page
	tbnz	x0,63,.end
	cbz	x21,.seg			; (un paquet commence : rien a jeter)
	str	xzr,[x20,OG_SKIPPKT]
	b	.seg
.take:	ldr	x1,[x20,OG_LACE]
	ldrb	w22,[x1,x0]			; longueur du segment
	add	x0,x0,1
	str	x0,[x20,OG_SEGI]
	ldr	x2,[x20,OG_PKTCAP]		; place
	add	x3,x21,x22
	cmp	x3,x2
	b.ls	.cp
	; agrandir le tampon du paquet (x2)
	lsl	x1,x2,1
	str	x1,[x20,OG_PKTCAP]
	mov	x0,x19
	bl	akd_alloc
	cbz	x0,.end
	mov	x2,x21
	ldr	x1,[x20,OG_PKT]
	str	x0,[x20,OG_PKT]
	bl	ak_memmove
.cp:	ldr	x0,[x20,OG_PKT]
	add	x0,x0,x21
	ldr	x1,[x20,OG_PAGE]
	ldr	x2,[x20,OG_BODYI]
	add	x1,x1,x2
	add	x2,x2,x22
	str	x2,[x20,OG_BODYI]
	mov	x2,x22
	bl	ak_memmove
	add	x21,x21,x22
	cmp	x22,255
	b.eq	.seg				; le paquet continue
	; paquet termine
	ldr	x0,[x20,OG_SKIPPKT]		; suite d'un paquet commence avant :
	cbz	x0,.done			; jeter
	str	xzr,[x20,OG_SKIPPKT]
	mov	x21,0
	b	.seg
.done:	mov	x2,-1
	ldr	x0,[x20,OG_SEGI]
	sub	x0,x0,1
	ldr	x1,[x20,OG_LASTSEG]
	cmp	x0,x1
	b.ne	.ng
	ldr	x2,[x20,OG_GRAN]
.ng:	ldr	x0,[x20,OG_PKT]
	mov	x1,x21
	b	.out
.end:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur -> x0 = granule de la derniere page du flux (< 0 : aucune)
; (la position de lecture est perdue)
ogg_last_granule:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x23,-1				; meilleure
	ldr	x0,[x19,AKD_DATAEND]
	ldr	x1,[x20,OG_DATA0]
	sub	x2,x0,x1
	movl	x3,65536+65536
	cmp	x2,x3
	csel	x2,x2,x3,lo
	sub	x21,x0,x2			; debut de la recherche
	mov	x0,x19
	mov	x1,x21
	bl	akd_seek
	mov	x0,x19
	movl	x1,65536+65536
	bl	akd_need
	mov	x21,x0
	mov	x22,x1
	ldr	x0,[x19,AKD_DATAEND]		; (pas au-dela des donnees)
	ldr	x1,[x19,AKD_OFF]
	sub	x0,x0,x1
	cmp	x22,x0
	csel	x22,x22,x0,lo
	mov	x24,0
.s:	add	x0,x24,27
	cmp	x0,x22
	b.hi	.done
	ldr	w0,[x21,x24]
	movl	w1,0x5367674F
	cmp	w0,w1
	b.ne	.n
	add	x2,x21,x24
	ldr	w0,[x2,14]
	ldr	x1,[x20,OG_SERIAL]
	cmp	w0,w1
	b.ne	.n
	ldr	x0,[x2,6]
	cmn	x0,1
	b.eq	.n
	mov	x23,x0
.n:	add	x24,x24,1
	b	.s
.done:	mov	x0,x23
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur -> trames (0 : fin)
ogg_next:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
.pk:	mov	x0,x19
	mov	x1,x20
	bl	ogg_packet
	tbnz	x0,63,.end
	mov	x23,x2				; granule
	mov	x2,x1
	mov	x1,x0
	mov	x0,x19
	ldr	x3,[x20,OG_CODEC]
	cmp	x3,AKF_OPUS
	b.eq	.opus
	bl	vb_packet
	b	.got
.opus:	bl	ops_packet
.got:	cmp	x0,0
	b.lt	.pk				; paquet invalide : le suivant
	b.eq	.pk				; rien a rendre (premier paquet)
	mov	x24,x0
	ldr	x1,[x20,OG_CAL]
	cbz	x1,.ret
	; calibrage : le son est jete jusqu'a une position connue
	tbz	x23,63,.cal
	b	.pk
.cal:	str	xzr,[x20,OG_CAL]
	ldr	x1,[x20,OG_GRANOFF]
	sub	x23,x23,x1			; fin de ce paquet
	sub	x23,x23,x24			; son debut
	ldr	x1,[x20,OG_TARGET]
	subs	x2,x1,x23
	b.lt	.over
	str	x2,[x19,AKD_SKIP]
	str	x1,[x19,AKD_POS]
	b	.ret
.over:	str	xzr,[x19,AKD_SKIP]
	cmp	x23,0
	csel	x23,xzr,x23,lt
	str	x23,[x19,AKD_POS]
.ret:	mov	x0,x24
	b	.out
.end:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = position de la page trouvee a partir de x1 -> x0 =
; position de la page (< 0 : aucune dans les 64 Ko), x1 = sa granule (-1
; possible)
ogg_find:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x0,x19
	bl	akd_seek
	mov	x0,x19
	mov	x1,65536
	bl	akd_need
	mov	x21,x0
	mov	x22,x1
	mov	x23,0
.s:	add	x0,x23,27
	cmp	x0,x22
	b.hi	.none
	ldr	w0,[x21,x23]
	movl	w1,0x5367674F
	cmp	w0,w1
	b.ne	.n
	add	x2,x21,x23
	ldr	w0,[x2,14]
	ldr	x1,[x20,OG_SERIAL]
	cmp	w0,w1
	b.ne	.n
	ldr	x0,[x19,AKD_OFF]
	add	x0,x0,x23
	ldr	x1,[x2,6]
	b	.out
.n:	add	x23,x23,1
	b	.s
.none:	mov	x0,-1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = decodeur, x1 = trame voulue -> 0 : bissection sur les pages, puis
; calibrage (ogg_next)
ogg_seek:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,AKD_CODEC]
	mov	x21,x1
	str	x21,[x20,OG_TARGET]
	ldr	x0,[x20,OG_GRANOFF]		; granule visee : cible - avance de
	add	x22,x21,x0			; decodage, en granule
	ldr	x0,[x20,OG_PREROLL]
	subs	x22,x22,x0
	csel	x22,xzr,x22,lt
	ldr	x23,[x20,OG_DATA0]		; bas : debut (granule <= visee)
	mov	x24,x23				; meilleure position
	ldr	x25,[x19,AKD_DATAEND]		; haut
	mov	x26,48
.bl:	sub	x0,x25,x23
	cmp	x0,8192
	b.lo	.found
	subs	x26,x26,1
	b.eq	.found
	add	x1,x23,x25
	lsr	x1,x1,1
	mov	x0,x19
	bl	ogg_find
	tbnz	x0,63,.hi
	cmn	x1,1
	b.eq	.mv				; page sans fin de paquet : plus loin
	cmp	x1,x22
	b.hi	.hi
	; cette page finit avant la cible : on peut commencer apres elle
	mov	x23,x0
	mov	x24,x0
	b	.bl
.mv:	add	x23,x23,x25			; (avancer le bas a mi-chemin)
	lsr	x23,x23,1
	b	.bl
.hi:	add	x0,x23,x25
	lsr	x25,x0,1
	b	.bl
.found:	; repartir de la page x24 : ses paquets precedent la cible ; le premier
	; paquet complet amorce le decodeur
	mov	x0,x19
	mov	x1,x24
	bl	akd_seek
	ldr	x0,[x20,OG_NSEG]
	str	x0,[x20,OG_SEGI]
	mov	x0,1
	str	x0,[x20,OG_SKIPPKT]		; (un paquet a cheval est jete)
	str	x0,[x20,OG_CAL]
	str	x21,[x19,AKD_POS]
	mov	x0,x19
	ldr	x1,[x20,OG_CODEC]
	cmp	x1,AKF_OPUS
	b.eq	.op
	bl	vb_reset
	b	.r
.op:	bl	ops_reset
.r:	mov	x0,0
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
