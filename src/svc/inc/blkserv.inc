; ============================================================================
;  svc/inc/blkserv.inc - serveur commun des peripheriques blocs (blk.inc) :
;  un pilote (sdblk, imgblk...) fournit deux fonctions, lire et ecrire des
;  secteurs du disque entier ; blk_demarrer lit sa table de partitions (MBR)
;  et sert le disque (« blk.<nom> ») et chaque partition (« blk.<nom>p1 »...),
;  une partition n'etant qu'une fenetre sur le disque. Un thread par
;  peripherique ; les appels au pilote sont serialises (blk_verrou).
;
;	segment readable executable	... include '../inc/blkserv.inc'
;	segment readable writeable	... blkserv_data
;  Le pilote : blk_lire_fn / blk_ecrire_fn (variables) : x0 = secteur du
;  disque, x1 = nombre, x2 = adresse, x3 = son adresse physique (0 : inconnue)
;  -> x0 = 0 ou erreur ; blk_dma_fn : passer en DMA (0 : pas de DMA) ;
;  blk_sg_fn : lire x1 secteurs vers x2 (fenetre sur l'appelant), x3 = adresses
;  physiques de ses pages -> 0, ou E_AGAIN (0 : pas de lecture directe) ;
;  blk_uhs_fn : passer en 1,8 V (0 : pas de mode UHS) ;
;  blk_stat_fn : x0 = attente active en us (-1 : inchangee), x1 = message
;  a remplir -> sommeils (0 : pas de compteurs) ;
;  blk_type_ro : un type de partition servi en lecture seule (0 : aucun).
; ============================================================================

LOG_MAX		= 8			; partitions logiques (p5 a p12)
DEV_MAX		= 5 + LOG_MAX		; le disque + 4 primaires + les logiques
DEV_BASE	= 0			; premier secteur (sur le disque)
DEV_TAILLE	= 8			; secteurs
DEV_RO		= 16
DEV_CANAL	= 24
DEV_CLIENTS	= 32			; CL_MAX x (pid, debut, fin, physique)
CL_MAX		= 8
CL_SIZE		= 32
DEV_SIZE	= DEV_CLIENTS + CL_MAX * CL_SIZE
PILE_THREAD	= 16384
BLK_SG_PAGES	= BLK_D_MAX / 8 + 2	; (une page entamee de chaque cote)
BLK_SG_BYTES	= (BLK_SG_PAGES * 8 + 15) and not 15
SVC_E_AGAIN	= -11
MC_WRITE	= 1

; x0 = nom du disque (« sd0 »), x1 = 1 lecture seule, x2 = secteurs du
; disque (0 : inconnu) : lit la table de partitions, enregistre et sert
; les peripheriques ; ne revient pas (le thread principal sert le disque)
blk_demarrer:
	stp	x0,x1,[sp,-16]!
	str	x2,[sp,-16]!
	mov	x0,0				; table de partitions
	mov	x1,1
	adr	x2,secteur0
	mov	x4,0
	bl	blk_appel_lire
	ldr	x2,[sp],16
	ldp	x0,x1,[sp],16
	mov	x19,x0				; nom
	mov	x20,x1				; lecture seule
	adr	x21,devs			; le disque entier
	str	xzr,[x21,DEV_BASE]
	cmp	x2,0
	mvn	x3,xzr
	csel	x2,x3,x2,eq
	str	x2,[x21,DEV_TAILLE]
	str	x20,[x21,DEV_RO]
	mov	x0,x21
	mov	x1,x19
	mov	x2,0
	bl	blk_inscrire
	adr	x22,secteur0			; MBR valide ? (0x55AA, et pas un
	ldrh	w0,[x22,510]			; secteur de demarrage FAT d'emblee)
	movl	w1,0xAA55
	cmp	w0,w1
	b.ne	.servir
	ldrb	w0,[x22,82]			; « FAT32 » a l'offset 82 : pas de table
	cmp	w0,'F'
	b.eq	.servir
	mov	x25,0				; la partition etendue (0 : aucune)
	mov	x23,0
.p:	cmp	x23,4				; les 4 primaires : p1 a p4
	b.hs	.ext
	add	x24,x22,446
	add	x24,x24,x23,lsl 4
	ldrb	w2,[x24,4]			; type
	cbz	w2,.pn
	bl	blk_entree
	cmp	w2,0x05				; etendue (CHS ou LBA) : ses
	b.eq	.etd				; logiques plus bas
	cmp	w2,0x0F
	b.eq	.etd
	add	x3,x23,1
	bl	blk_partition
	b	.pn
.etd:	mov	x25,x0
.pn:	add	x23,x23,1
	b	.p
.ext:	cbz	x25,.servir			; les logiques : p5, p6... une table
	mov	x26,x25				; (EBR) avant chacune ; la suivante
	mov	x27,5				; relative au debut de l'etendue
.l:	cmp	x27,5+LOG_MAX
	b.hs	.servir
	mov	x0,x26
	mov	x1,1
	mov	x2,x22
	mov	x4,0
	bl	blk_appel_lire
	cbnz	x0,.servir
	ldrh	w0,[x22,510]
	movl	w1,0xAA55
	cmp	w0,w1
	b.ne	.servir
	add	x24,x22,446
	ldrb	w2,[x24,4]
	cbz	w2,.servir
	bl	blk_entree
	add	x0,x0,x26			; (relatif a sa table)
	mov	x3,x27
	bl	blk_partition
	add	x24,x22,462
	ldrb	w2,[x24,4]
	cbz	w2,.servir
	bl	blk_entree
	add	x26,x0,x25
	cmp	x26,x25				; (une table qui boucle : fin)
	b.ls	.servir
	add	x27,x27,1
	b	.l
.servir:
	adr	x0,devs
	b	blk_thread

; x24 = entree de table -> x0 = premier secteur, x1 = nombre (non alignes) ;
; w2 garde
blk_entree:
	ldrb	w0,[x24,8]
	ldrb	w3,[x24,9]
	orr	w0,w0,w3,lsl 8
	ldrb	w3,[x24,10]
	orr	w0,w0,w3,lsl 16
	ldrb	w3,[x24,11]
	orr	w0,w0,w3,lsl 24
	ldrb	w1,[x24,12]
	ldrb	w3,[x24,13]
	orr	w1,w1,w3,lsl 8
	ldrb	w3,[x24,14]
	orr	w1,w1,w3,lsl 16
	ldrb	w3,[x24,15]
	orr	w1,w1,w3,lsl 24
	ret

; (blk_demarrer : x19 nom, x20 lecture seule, x21 dernier peripherique)
; x0 = premier secteur, x1 = nombre, w2 = type, x3 = numero : un
; peripherique de plus, enregistre, et son thread
blk_partition:
	prologue
	adr	x4,devs+(DEV_MAX-1)*DEV_SIZE
	cmp	x21,x4
	b.hs	.out
	add	x21,x21,DEV_SIZE
	str	x0,[x21,DEV_BASE]
	str	x1,[x21,DEV_TAILLE]
	adr	x1,blk_type_ro			; un type protege (exFAT : la
	ldrb	w1,[x1]				; partition personnelle) : en
	mov	x0,x20				; lecture seule
	cbz	w1,.ro
	cmp	w2,w1
	csinc	x0,x20,xzr,ne
.ro:	str	x0,[x21,DEV_RO]
	mov	x0,x21
	mov	x1,x19
	mov	x2,x3
	bl	blk_inscrire
	tbnz	x0,63,.out
	mov	x0,PILE_THREAD			; son thread
	bl	malloc
	cbz	x0,.out
	add	x1,x0,PILE_THREAD
	adr	x0,blk_thread
	mov	x2,x21
	sys	SYS_THREAD_CREATE
.out:	epilogue

; x0 = peripherique, x1 = nom du disque, x2 = partition (0 : le disque) :
; s'enregistre sous « blk.<nom>[p<n>] » -> x0 = canal ou erreur
blk_inscrire:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	adr	x0,blk_nom
	adr	x2,s_blk
.c1:	ldrb	w3,[x2],1
	cbz	w3,.c2
	strb	w3,[x0],1
	b	.c1
.c2:	ldrb	w3,[x1],1
	cbz	w3,.c3
	strb	w3,[x0],1
	b	.c2
.c3:	cbz	x20,.fin
	mov	w3,'p'
	strb	w3,[x0],1
	cmp	x20,10				; (p10, p11, p12 : logiques)
	b.lo	.u
	mov	w3,'1'
	strb	w3,[x0],1
	sub	x20,x20,10
.u:	add	w3,w20,'0'
	strb	w3,[x0],1
.fin:	strb	wzr,[x0]
	adr	x0,blk_nom
	mov	x1,0
	sys	SYS_SVC_REGISTER
	str	x0,[x19,DEV_CANAL]
	ldp	x19,x20,[sp],16
	epilogue

; (thread) x0 = peripherique : sert ses requetes
blk_thread:
	mov	x19,x0
	sub	sp,sp,MSG_TAILLE
.loop:	ldr	x0,[x19,DEV_CANAL]
	tbnz	x0,63,.mort
	mov	x1,sp
	mov	x2,60000
	sys	SYS_SVC_NEXT
	tbnz	x0,63,.mort
	cbz	x0,.loop
	mov	x20,x0
	ldr	x0,[sp]
	cmp	x0,BLK_INFO
	b.eq	.info
	cmp	x0,BLK_ATTACHER
	b.eq	.att
	cmp	x0,BLK_LIRE
	b.eq	.rw
	cmp	x0,BLK_ECRIRE
	b.eq	.rw
	cmp	x0,BLK_DMA
	b.eq	.dma
	cmp	x0,BLK_LIRE_D
	b.eq	.direct
	cmp	x0,BLK_STATS
	b.eq	.stats
	cmp	x0,BLK_UHS
	b.eq	.uhs
	mov	x0,E_INVAL
	b	.rep
.stats:	adr	x3,blk_stat_fn
	ldr	x3,[x3]
	mov	x0,E_INVAL
	cbz	x3,.rep
	ldr	x0,[sp,MSG_A]
	mov	x1,sp
	blr	x3
	b	.rep
.direct:
	adr	x3,blk_sg_fn
	ldr	x3,[x3]
	mov	x0,SVC_E_AGAIN
	cbz	x3,.rep
	ldp	x7,x8,[sp,MSG_A]		; secteur, nombre
	cbz	x8,.inval
	cmp	x8,BLK_D_MAX
	b.hi	.inval
	add	x10,x7,x8
	ldr	x11,[x19,DEV_TAILLE]
	cmp	x10,x11
	b.hi	.inval
	mov	x0,x20
	ldr	x1,[sp,MSG_C]
	lsl	x2,x8,9
	mov	x3,MC_WRITE
	sys	SYS_SVC_MAP_CALLER
	tbnz	x0,63,.rep
	mov	x21,x0
	sub	sp,sp,BLK_SG_BYTES
	mov	x1,sp
	mov	x2,BLK_SG_PAGES
	sys	SYS_SVC_CALLER_PHYS
	tbnz	x0,63,.dfin
	add	x9,sp,BLK_SG_BYTES
	ldp	x7,x8,[x9,MSG_A]
	ldr	x0,[x19,DEV_BASE]
	add	x0,x0,x7
	mov	x1,x8
	mov	x2,x21
	mov	x4,sp
	adr	x3,blk_sg_fn
	bl	blk_appel
.dfin:	add	sp,sp,BLK_SG_BYTES
	b	.rep
.dma:	adr	x3,blk_dma_fn
	ldr	x0,[x3]
	cbz	x0,.rep
	mov	x4,0
	bl	blk_appel
	b	.rep
.uhs:	adr	x3,blk_uhs_fn
	ldr	x0,[x3]
	cbz	x0,.rep
	mov	x4,0
	bl	blk_appel
	b	.rep
.info:	ldr	x0,[x19,DEV_RO]
	ldr	x1,[x19,DEV_TAILLE]
	stp	x0,x1,[sp,MSG_A]
	sys	SYS_GETPID
	mov	x1,512
	stp	x1,x0,[sp,MSG_C]
	mov	x0,0
	b	.rep
.att:	ldr	x0,[sp,MSG_A]			; son tampon : mappe chez nous
	sys	SYS_BUF_MAP
	tbnz	x0,63,.rep
	mov	x21,x0
	ldr	x0,[sp,MSG_A]			; son adresse physique (pour le DMA)
	sub	sp,sp,BI_SIZE
	mov	x1,sp
	sys	SYS_BUF_INFO
	cmp	x0,0
	ldr	x23,[sp,BI_PHYS]
	csel	x23,x23,xzr,eq
	add	sp,sp,BI_SIZE
	ldr	w22,[sp,MSG_PID]
	stp	x24,x25,[sp,-16]!		; les places des clients termines
	sub	sp,sp,64			; (fatfs demonte, mkfs) sont rendues,
	add	x24,x19,DEV_CLIENTS		; leur tampon demappe
	mov	x25,CL_MAX
.rc:	ldr	x0,[x24]
	cbz	x0,.rcn
	cmp	x0,x22
	b.eq	.rcn
	mov	x1,sp
	sys	SYS_PROC_INFO
	tbz	x0,63,.rcn
	str	xzr,[x24]
	ldr	x0,[x24,8]
	movl	x1,BUF_VA_BASE
	sub	x0,x0,x1
	lsr	x0,x0,26			; (BUF_SLOT = 64 Mo)
	sys	SYS_BUF_FREE
.rcn:	add	x24,x24,CL_SIZE
	subs	x25,x25,1
	b.ne	.rc
	add	sp,sp,64
	ldp	x24,x25,[sp],16
	add	x2,x19,DEV_CLIENTS		; une place (la sienne, ou libre)
	mov	x3,0
	mov	x4,0
.cl:	cmp	x3,CL_MAX
	b.hs	.clp
	ldr	x5,[x2]
	cmp	x5,x22
	b.eq	.clset
	cbnz	x5,.cln
	cbnz	x4,.cln
	mov	x4,x2
.cln:	add	x2,x2,CL_SIZE
	add	x3,x3,1
	b	.cl
.clp:	mov	x0,E_NOMEM
	cbz	x4,.rep
	mov	x2,x4
.clset:	ldr	x1,[sp,MSG_B]			; taille annoncee (une fenetre au plus)
	movl	x3,BUF_SLOT
	cmp	x1,x3
	csel	x1,x3,x1,hi
	add	x1,x21,x1
	stp	x21,x1,[x2,8]
	str	x23,[x2,24]
	str	x22,[x2]
	mov	x0,0
	b	.rep
.rw:	ldr	w22,[sp,MSG_PID]		; le tampon de ce client
	add	x2,x19,DEV_CLIENTS
	mov	x3,0
.cr:	cmp	x3,CL_MAX
	b.hs	.perm
	ldr	x5,[x2]
	cmp	x5,x22
	b.eq	.crf
	add	x2,x2,CL_SIZE
	add	x3,x3,1
	b	.cr
.crf:	ldp	x5,x6,[x2,8]			; debut, fin
	ldr	x12,[x2,24]
	ldp	x7,x8,[sp,MSG_A]		; secteur, nombre
	ldr	x9,[sp,MSG_C]			; adresse
	cmp	x9,x5
	b.lo	.inval
	add	x10,x9,x8,lsl 9
	cmp	x10,x6
	b.hi	.inval
	add	x10,x7,x8			; dans le peripherique ?
	ldr	x11,[x19,DEV_TAILLE]
	cmp	x10,x11
	b.hi	.inval
	mov	x4,0				; physique de cette adresse
	cbz	x12,.phys
	sub	x4,x9,x5
	add	x4,x4,x12
.phys:	ldr	x0,[sp]
	cmp	x0,BLK_ECRIRE
	b.ne	.lire
	ldr	x0,[x19,DEV_RO]
	cbnz	x0,.perm
	ldr	x0,[x19,DEV_BASE]
	add	x0,x0,x7
	mov	x1,x8
	mov	x2,x9
	bl	blk_appel_ecrire
	b	.rep
.lire:	ldr	x0,[x19,DEV_BASE]
	add	x0,x0,x7
	mov	x1,x8
	mov	x2,x9
	bl	blk_appel_lire
	b	.rep
.inval:	mov	x0,E_INVAL
	b	.rep
.perm:	mov	x0,E_PERM
.rep:	str	x0,[sp]
	mov	x0,x20
	mov	x1,sp
	sys	SYS_SVC_REPLY
	b	.loop
.mort:	sys	SYS_THREAD_EXIT

; x0 = secteur, x1 = nombre, x2 = adresse, x4 = physique : appel du pilote,
; sous verrou
blk_appel_lire:
	adr	x3,blk_lire_fn
	b	blk_appel
blk_appel_ecrire:
	adr	x3,blk_ecrire_fn
blk_appel:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x23,x4
	ldr	x22,[x3]
	adr	x0,blk_verrou			; un appel au pilote a la fois
.v:	ldaxr	w1,[x0]
	cbnz	w1,.att
	mov	w2,1
	stxr	w1,w2,[x0]
	cbnz	w1,.v
	b	.pris
.att:	clrex
	sys	SYS_YIELD
	adr	x0,blk_verrou
	b	.v
.pris:	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,x23
	blr	x22
	adr	x1,blk_verrou
	stlr	wzr,[x1]
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

s_blk	db 'blk.',0
	align 8

macro blkserv_data {
		align 8
blk_verrou	dw 0
blk_type_ro	db 0
		align 8
blk_lire_fn	dd 0
blk_ecrire_fn	dd 0
blk_dma_fn	dd 0
blk_sg_fn	dd 0
blk_stat_fn	dd 0
blk_uhs_fn	dd 0
blk_nom		rb 32
devs		rb DEV_MAX * DEV_SIZE
		align 16
secteur0	rb 512
}
