; ============================================================================
;  clipd - le presse-papiers partage (clipd d'Onyx, docs/KITS.md), lance par la
;  session du bureau (SD0:/etc/session). Sans fenetre. Service « clipboard »
;  (inc/clip.inc) : les 10 dernieres copies, la plus recente en tete, chacune
;  avec ses representations (un format, ses octets) dans la memoire de clipd,
;  et le curseur, l'element que Ctrl+V colle. L'historique est perdu a l'arret.
;  Une copie nouvelle passe devant, le curseur y va, notifyd le dit.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/clip.inc'

MC_WRITE	= 1
CLIP_CHUNK	= 0x80000		; par SVC_MAP_CALLER (1 Mo au plus)
PAGE		= 4096
ENOENT_		= -2
ENOMEM_		= -12
EINVAL_		= -22

; une copie
CE_ID		= 0
CE_NR		= 8
CE_HM		= 16			; heure * 256 + minute
CE_BYTES	= 24
CE_REP		= 32
; une representation
CR_FMT		= 0
CR_PTR		= CLIP_FMT
CR_N		= CLIP_FMT + 8
CR_MAP		= CLIP_FMT + 16		; octets de la region (pages entieres)
CR_SIZE		= CLIP_FMT + 24
CE_SIZE		= CE_REP + CLIP_REPS * CR_SIZE

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
include '../../inc/notify.inc'

start:
	bl	kits_open
	adr	x0,s_svc
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit			; (un autre clipd tourne)
	mov	x27,x0
	adr	x20,msg
.loop:	mov	x0,x27
	mov	x1,x20
	mov	x2,60000
	sys	SYS_SVC_NEXT
	tbnz	x0,63,quit
	cbz	x0,.loop
	mov	x19,x0
	ldr	x0,[x20,WM_OP]
	cmp	x0,CLIP_PUT
	b.eq	.put
	cmp	x0,CLIP_GET
	b.eq	.get
	cmp	x0,CLIP_INFO
	b.eq	.info
	cmp	x0,CLIP_STATE
	b.eq	.state
	cmp	x0,CLIP_CURSOR
	b.eq	.cursor
	cmp	x0,CLIP_DELETE
	b.eq	.delete
	cmp	x0,CLIP_CLEAR
	b.eq	.clear
	cmp	x0,CLIP_DROPCUT
	b.eq	.drop
	mov	x0,EINVAL_
	b	.rep
.put:	bl	op_put
	b	.rep
.get:	bl	op_get
	b	.rep
.info:	bl	op_info
	b	.rep
.state:	adr	x1,g_n
	ldr	x1,[x1]
	str	x1,[x20,WM_A]
	adr	x0,g_serial
	ldr	x0,[x0]
	b	.rep
.cursor:
	ldr	x0,[x20,WM_A]
	bl	find_id
	tbnz	x0,63,.zero
	ldr	x0,[x20,WM_A]
	adr	x1,g_cursor
	str	x0,[x1]
	bl	changed
	b	.zero
.delete:
	ldr	x0,[x20,WM_A]
	bl	find_id
	tbnz	x0,63,.zero
	bl	remove_at
	bl	changed
	b	.zero
.clear:	bl	clear_all
	bl	changed
	b	.zero
.drop:	bl	drop_cut
.zero:	mov	x0,0
.rep:	str	x0,[x20,WM_OP]
	mov	x0,x19
	mov	x1,x20
	sys	SYS_SVC_REPLY
	adr	x0,g_tell			; (l'avis, l'appelant reparti)
	ldr	x1,[x0]
	cbz	x1,.loop
	str	xzr,[x0]
	bl	tell
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

changed:
	adr	x0,g_serial
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	ret

; x0 = id -> x0 = rang, -1 s'il n'y est pas
find_id:
	adr	x1,g_n
	ldr	x1,[x1]
	adr	x2,g_e
	mov	x3,0
.l:	cmp	x3,x1
	b.hs	.no
	ldr	x4,[x2,CE_ID]
	cmp	x4,x0
	b.eq	.yes
	add	x2,x2,CE_SIZE
	add	x3,x3,1
	b	.l
.yes:	mov	x0,x3
	ret
.no:	mov	x0,-1
	ret

; x0 = rang -> x0 = copie
entry_at:
	mov	x1,CE_SIZE
	mul	x0,x0,x1
	adr	x1,g_e
	add	x0,x0,x1
	ret

; x0 = copie : ses regions rendues
free_entry:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,CE_NR]
	str	xzr,[x19,CE_NR]
	add	x19,x19,CE_REP
.r:	cbz	x20,.out
	ldr	x0,[x19,CR_PTR]
	ldr	x1,[x19,CR_MAP]
	sys	SYS_VM_UNMAP
	add	x19,x19,CR_SIZE
	sub	x20,x20,1
	b	.r
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = rang : la copie s'en va ; le curseur passe a la suivante, plus ancienne
remove_at:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	bl	entry_at
	mov	x20,x0
	ldr	x0,[x20,CE_ID]
	adr	x1,g_cursor
	ldr	x1,[x1]
	cmp	x0,x1
	cset	x21,eq
	mov	x0,x20
	bl	free_entry
	adr	x1,g_n
	ldr	x2,[x1]
	sub	x2,x2,1
	str	x2,[x1]
	sub	x2,x2,x19			; apres elle
	mov	x1,CE_SIZE
	mul	x2,x2,x1
	mov	x0,x20
	add	x1,x20,x1
	bl	mem_move
	cbz	x21,.out
	adr	x1,g_n
	ldr	x1,[x1]
	mov	x0,0
	cbz	x1,.cur
	sub	x1,x1,1
	cmp	x19,x1
	csel	x0,x19,x1,ls
	bl	entry_at
	ldr	x0,[x0,CE_ID]
.cur:	adr	x1,g_cursor
	str	x0,[x1]
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

clear_all:
	prologue
.c:	adr	x0,g_n
	ldr	x0,[x0]
	cbz	x0,.out
	sub	x0,x0,1
	bl	remove_at
	b	.c
.out:	adr	x0,g_cursor
	str	xzr,[x0]
	epilogue

; -> x0 = octets de tout l'anneau
ring_bytes:
	adr	x1,g_n
	ldr	x1,[x1]
	adr	x2,g_e
	mov	x0,0
.l:	cbz	x1,.out
	ldr	x3,[x2,CE_BYTES]
	add	x0,x0,x3
	add	x2,x2,CE_SIZE
	sub	x1,x1,1
	b	.l
.out:	ret

; x0 = requete, x1 = adresse chez l'appelant, x2 = adresse ici, x3 = octets,
; x4 = MC_WRITE (ecrit chez lui), 0 (lu chez lui) -> x0 = 0, < 0 : erreur
xfer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	str	x25,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
.c:	mov	x0,0
	cbz	x22,.out
	movl	x24,CLIP_CHUNK
	cmp	x22,x24
	csel	x24,x22,x24,lo
	mov	x0,x19
	mov	x1,x20
	mov	x2,x24
	mov	x3,x23
	sys	SYS_SVC_MAP_CALLER
	tbnz	x0,63,.out
	mov	x25,x0
	mov	x0,x21				; lu : ici <- chez lui
	mov	x1,x25
	cbz	x23,.mv
	mov	x0,x25
	mov	x1,x21
.mv:	mov	x2,x24
	bl	mem_move
	mov	x0,x25
	sys	SYS_SVC_UNMAP_CALLER
	add	x20,x20,x24
	add	x21,x21,x24
	sub	x22,x22,x24
	b	.c
.out:	ldr	x25,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; CLIP_PUT (x19 requete, x20 message) -> x0 = id, < 0 : refus
op_put:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	ldr	x21,[x20,WM_A]
	ldr	x22,[x20,WM_B]
	ldr	x23,[x20,WM_C]
	ldr	x24,[x20,WM_D]
	mov	x0,EINVAL_
	cbz	x22,.out
	add	x26,x22,8			; octets, avec l'entete
	cbnz	x24,.big
	mov	x26,x22
.big:	movl	x1,CLIP_MAXITEM
	cmp	x26,x1
	b.hi	.out
	adr	x25,g_fresh			; x25 : la copie
	cbz	x23,.new
	mov	x0,x23				; une representation de plus
	bl	find_id
	tbnz	x0,63,.gone
	bl	entry_at
	mov	x25,x0
	ldr	x1,[x25,CE_NR]
	mov	x0,EINVAL_
	cmp	x1,CLIP_REPS
	b.hs	.out
	b	.map
.gone:	mov	x0,ENOENT_
	b	.out
.new:	mov	x0,x25
	mov	x1,0
	mov	x2,CE_SIZE
.z:	strb	wzr,[x0,x1]
	add	x1,x1,1
	cmp	x1,x2
	b.lo	.z
.map:	add	x1,x26,PAGE-1
	and	x1,x1,not (PAGE-1)
	stp	x1,xzr,[sp,-16]!		; (taille de la region)
	mov	x0,0
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	ldp	x1,xzr,[sp],16
	tbnz	x0,63,.nomem
	mov	x9,x0
	ldr	x2,[x25,CE_NR]			; x10 : la representation
	mov	x3,CR_SIZE
	mul	x2,x2,x3
	add	x10,x25,CE_REP
	add	x10,x10,x2
	str	x9,[x10,CR_PTR]
	str	x26,[x10,CR_N]
	str	x1,[x10,CR_MAP]
	stp	x9,x10,[sp,-16]!
	mov	x2,x9				; le contenu, apres l'entete
	cbz	x24,.xf
	str	x24,[x9]
	add	x2,x9,8
.xf:	mov	x0,x19
	mov	x1,x21
	mov	x3,x22
	mov	x4,0
	bl	xfer
	ldp	x9,x10,[sp],16
	tbnz	x0,63,.fail
	add	x0,x20,WM_DATA			; le format (borne)
	mov	x1,0
.f:	ldrb	w2,[x0,x1]
	strb	w2,[x10,x1]
	cbz	w2,.fd
	add	x1,x1,1
	cmp	x1,CLIP_FMT-1
	b.lo	.f
	strb	wzr,[x10,x1]
.fd:	ldr	x0,[x25,CE_NR]
	add	x0,x0,1
	str	x0,[x25,CE_NR]
	ldr	x0,[x25,CE_BYTES]
	add	x0,x0,x26
	str	x0,[x25,CE_BYTES]
	cbz	x23,.ring
	bl	changed
	mov	x0,x23
	b	.out
.fail:	mov	x26,x0
	ldr	x0,[x10,CR_PTR]
	ldr	x1,[x10,CR_MAP]
	sys	SYS_VM_UNMAP
	mov	x0,x26
	b	.out
.nomem:	mov	x0,ENOMEM_
	b	.out
.ring:	adr	x0,g_next			; la copie passe devant
	ldr	x1,[x0]
	add	x2,x1,1
	str	x2,[x0]
	str	x1,[x25,CE_ID]
	mov	x26,x1
	mov	x0,2				; (secondes locales)
	sys	SYS_CLOCK_GET
	movl	x1,86400
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	mov	x1,60
	udiv	x0,x0,x1			; minutes du jour
	udiv	x2,x0,x1
	msub	x3,x2,x1,x0			; minute
	orr	x2,x3,x2,lsl 8
	str	x2,[x25,CE_HM]
	adr	x0,g_n
	ldr	x0,[x0]
	cmp	x0,CLIP_RING
	b.lo	.room
	sub	x0,x0,1
	bl	remove_at
.room:	adr	x1,g_n
	ldr	x2,[x1]
	mov	x0,CE_SIZE
	mul	x2,x2,x0
	adr	x1,g_e
	add	x0,x1,x0
	bl	mem_move
	adr	x0,g_e
	mov	x1,x25
	mov	x2,CE_SIZE
	bl	mem_move
	adr	x0,g_n
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
.trim:	adr	x0,g_n
	ldr	x0,[x0]
	cmp	x0,1
	b.ls	.done
	bl	ring_bytes
	movl	x1,CLIP_MAXBYTES
	cmp	x0,x1
	b.ls	.done
	adr	x0,g_n
	ldr	x0,[x0]
	sub	x0,x0,1
	bl	remove_at
	b	.trim
.done:	adr	x0,g_cursor
	str	x26,[x0]
	bl	changed
	adr	x0,g_tell
	mov	x1,1
	str	x1,[x0]
	mov	x0,x26
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = rang (x20 message : les formats demandes) -> x0 = representation (0 :
; aucune), x1 = rang du format, x2 = id
try_item:
	prologue
	stp	x19,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	bl	entry_at
	mov	x19,x0
	add	x21,x20,WM_DATA			; format demande
	mov	x22,0				; son rang
.f:	ldrb	w0,[x21]
	cbz	w0,.none
	ldr	x23,[x19,CE_NR]
	add	x24,x19,CE_REP
.r:	cbz	x23,.nf
	mov	x0,x24
	mov	x1,x21
	bl	str_eq
	cbnz	x0,.got
	add	x24,x24,CR_SIZE
	sub	x23,x23,1
	b	.r
.nf:	mov	x0,x21
	bl	str_len
	add	x21,x21,x0
	add	x21,x21,1
	add	x22,x22,1
	b	.f
.got:	mov	x0,x24
	mov	x1,x22
	ldr	x2,[x19,CE_ID]
	b	.out
.none:	mov	x0,0
.out:	ldp	x24,x25,[sp],16
	ldp	x22,x23,[sp],16
	ldp	x19,x21,[sp],16
	epilogue

; CLIP_GET -> x0 = octets ecrits
op_get:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	strb	wzr,[x20,WS_MSG-1]		; (la fin de la liste)
	strb	wzr,[x20,WS_MSG-2]
	adr	x0,g_cursor			; le curseur d'abord
	ldr	x0,[x0]
	bl	find_id
	tbnz	x0,63,.all
	bl	try_item
	cbnz	x0,.got
.all:	mov	x21,0				; puis la plus recente qui convient
.i:	adr	x0,g_n
	ldr	x0,[x0]
	cmp	x21,x0
	b.hs	.none
	mov	x0,x21
	bl	try_item
	cbnz	x0,.got
	add	x21,x21,1
	b	.i
.none:	str	xzr,[x20,WM_A]
	mov	x0,0
	b	.out
.got:	ldr	x21,[x20,WM_A]			; destination
	ldr	x22,[x20,WM_B]			; place
	ldr	x23,[x20,WM_C]			; a sauter
	ldr	x24,[x0,CR_N]
	ldr	x3,[x0,CR_PTR]
	str	x24,[x20,WM_A]
	stp	x1,x2,[x20,WM_B]
	cmp	x23,x24
	csel	x23,x24,x23,hi
	add	x9,x3,x23			; ce qu'il reste a partir de la
	sub	x24,x24,x23
	cmp	x24,x22
	csel	x24,x22,x24,hi
	mov	x0,0
	cbz	x21,.out
	cbz	x24,.out
	mov	x0,x19
	mov	x1,x21
	mov	x2,x9
	mov	x3,x24
	mov	x4,MC_WRITE
	bl	xfer
	tbnz	x0,63,.out
	mov	x0,x24
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = copie -> x0 = representation principale : la premiere qui n'est pas du
; texte, sinon le texte
main_rep:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	ldr	x22,[x21,CE_NR]
	add	x23,x21,CE_REP
.r:	cbz	x22,.first
	mov	x0,x23
	adr	x1,s_text
	bl	str_eq
	cbz	x0,.got
	add	x23,x23,CR_SIZE
	sub	x22,x22,1
	b	.r
.first:	add	x23,x21,CE_REP
.got:	mov	x0,x23
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = copie -> x0 = sa representation texte, sinon la principale
text_rep:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x0
	ldr	x22,[x21,CE_NR]
	add	x23,x21,CE_REP
.r:	cbz	x22,.main
	mov	x0,x23
	adr	x1,s_text
	bl	str_eq
	cbnz	x0,.got
	add	x23,x23,CR_SIZE
	sub	x22,x22,1
	b	.r
.main:	mov	x0,x21
	bl	main_rep
	mov	x23,x0
.got:	mov	x0,x23
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; x0 = format -> x0 = 1 s'il commence par « files »
is_files:
	adr	x1,s_files
.c:	ldrb	w2,[x1],1
	cbz	w2,.yes
	ldrb	w3,[x0],1
	cmp	w2,w3
	b.eq	.c
	mov	x0,0
	ret
.yes:	mov	x0,1
	ret

; x0 = representation, x1 = destination (CI_PREVIEW_MAX + 1) : une ligne de texte
line_text:
	ldr	x2,[x0,CR_PTR]
	ldr	x3,[x0,CR_N]
.lead0:	cbz	x3,.keep			; (les blancs de tete : passes)
	ldrb	w6,[x2]
	cmp	w6,' '
	b.hi	.keep
	add	x2,x2,1
	sub	x3,x3,1
	b	.lead0
.keep:	mov	x4,CI_PREVIEW_MAX
	cmp	x3,x4
	csel	x4,x3,x4,lo			; x4 : octets gardes
	mov	x5,0
.c:	cmp	x5,x4
	b.hs	.end
	ldrb	w6,[x2,x5]
	cmp	w6,' '
	b.hs	.st
	mov	w6,' '
.st:	strb	w6,[x1,x5]
	add	x5,x5,1
	b	.c
.end:	cmp	x3,x4
	b.ls	.nul
.back:	cbz	x5,.nul				; coupe : pas au milieu d'un caractere
	sub	x6,x5,1
	ldrb	w7,[x1,x6]
	and	w8,w7,0xC0
	cmp	w8,0x80
	b.ne	.lead
	mov	x5,x6
	b	.back
.lead:	cmp	w7,0xC0
	b.lo	.nul
	mov	x5,x6
.nul:	strb	wzr,[x1,x5]
	ret

; x0 = representation (chemins separes par \n), x1 = destination : leurs noms
; separes par « , » -> x0 = nombre de chemins
line_files:
	ldr	x2,[x0,CR_PTR]
	ldr	x3,[x0,CR_N]
	mov	x4,0				; i
	mov	x5,0				; k
	mov	x0,0				; chemins
.line:	cmp	x4,x3
	b.hs	.end
	mov	x6,x4				; debut du nom
	mov	x7,x4				; fin de ligne
.e:	cmp	x7,x3
	b.hs	.ed
	ldrb	w8,[x2,x7]
	cmp	w8,10
	b.eq	.ed
	add	x7,x7,1
	cmp	w8,'/'
	b.eq	.b
	cmp	w8,':'
	b.ne	.e
.b:	mov	x6,x7
	b	.e
.ed:	cmp	x7,x4				; (ligne vide)
	b.eq	.nx
	add	x0,x0,1
	cmp	x5,0
	b.eq	.cp
	cmp	x5,CI_PREVIEW_MAX-2
	b.hs	.nx
	mov	w8,','
	strb	w8,[x1,x5]
	add	x5,x5,1
	mov	w8,' '
	strb	w8,[x1,x5]
	add	x5,x5,1
.cp:	cmp	x6,x7
	b.hs	.nx
	cmp	x5,CI_PREVIEW_MAX
	b.hs	.nx
	ldrb	w8,[x2,x6]
	strb	w8,[x1,x5]
	add	x5,x5,1
	add	x6,x6,1
	b	.cp
.nx:	add	x4,x7,1
	b	.line
.end:	strb	wzr,[x1,x5]
	ret

; CLIP_INFO
op_info:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	ldr	x0,[x20,WM_A]
	adr	x1,g_n
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.past
	bl	entry_at
	mov	x21,x0
	ldr	x0,[x21,CE_ID]
	str	x0,[x20,WM_A]
	adr	x1,g_cursor
	ldr	x1,[x1]
	cmp	x0,x1
	cset	x0,eq
	str	x0,[x20,WM_B]
	ldr	x0,[x21,CE_HM]
	str	x0,[x20,WM_C]
	mov	x0,x21
	bl	main_rep
	mov	x22,x0
	ldr	x0,[x22,CR_N]
	str	x0,[x20,WM_D]
	add	x23,x20,WM_DATA
	stp	xzr,xzr,[x23,CI_W]
	strb	wzr,[x23,CI_PREVIEW]
	mov	x0,x23				; le genre
	mov	x1,x22
	mov	x2,CLIP_FMT
	bl	mem_move
	mov	x0,x22
	adr	x1,s_image
	bl	str_eq
	cbz	x0,.txt
	ldr	x0,[x22,CR_N]
	cmp	x0,8
	b.lo	.yes
	ldr	x0,[x22,CR_PTR]
	ldr	x0,[x0]
	str	x0,[x23,CI_W]			; (l, h)
	b	.yes
.txt:	mov	x0,x22
	bl	is_files
	cbz	x0,.line
	mov	x0,x22
	add	x1,x23,CI_PREVIEW
	bl	line_files
	str	w0,[x23,CI_NFILES]
	b	.yes
.line:	mov	x0,x21
	bl	text_rep
	add	x1,x23,CI_PREVIEW
	bl	line_text
.yes:	mov	x0,1
	b	.out
.past:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; CLIP_DROPCUT : l'element du curseur s'en va si c'est une coupe
drop_cut:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,g_cursor
	ldr	x0,[x0]
	bl	find_id
	tbnz	x0,63,.out
	mov	x23,x0
	bl	entry_at
	ldr	x21,[x0,CE_NR]
	add	x22,x0,CE_REP
.r:	cbz	x21,.out
	mov	x0,x22
	adr	x1,s_filescut
	bl	str_eq
	cbnz	x0,.del
	add	x22,x22,CR_SIZE
	sub	x21,x21,1
	b	.r
.del:	mov	x0,x23
	bl	remove_at
	bl	changed
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

; la copie de tete : une notification
tell:
	prologue
	stp	x21,x22,[sp,-16]!
	adr	x0,g_e
	bl	main_rep
	mov	x21,x0
	adr	x22,tellbuf
	mov	x0,x21
	adr	x1,s_image
	bl	str_eq
	cbnz	x0,.img
	mov	x0,x21
	bl	is_files
	cbnz	x0,.fil
	mov	x0,x21
	adr	x1,s_url
	bl	str_eq
	adr	x1,s_tlink
	bl	tr_x1
	cbnz	x0,.go
	adr	x1,s_ttext
	bl	tr_x1
.go:	adr	x0,s_title
	bl	tr_x0
	bl	notifier
	b	.out
.img:	adr	x0,tellbuf			; « Image copiee (l x h) »
	strb	wzr,[x0]
	adr	x1,s_timg
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	ldr	x2,[x21,CR_PTR]
	ldr	w0,[x2]
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x1,s_x
	bl	str_cat
	mov	x1,x0
	ldr	x2,[x21,CR_PTR]
	ldr	w0,[x2,4]
	bl	fmt_dec
	mov	w1,')'
	strb	w1,[x0],1
	strb	wzr,[x0]
	b	.say
.fil:	bl	count_paths			; « 3 elements copies »
	mov	x22,x0
	adr	x1,tellbuf
	bl	fmt_dec
	strb	wzr,[x0]
	stp	x0,xzr,[sp,-16]!
	mov	x0,x21
	adr	x1,s_filescut
	bl	str_eq
	mov	x3,x0
	ldp	x0,xzr,[sp],16
	adr	x1,s_tcopy1
	bl	tr_x1
	cmp	x22,1
	b.eq	.cut1
	adr	x1,s_tcopyn
	bl	tr_x1
	cbz	x3,.cat
	adr	x1,s_tcutn
	bl	tr_x1
	b	.cat
.cut1:	cbz	x3,.cat
	adr	x1,s_tcut1
	bl	tr_x1
.cat:	bl	str_cat
.say:	adr	x0,s_title
	bl	tr_x0
	adr	x1,tellbuf
	bl	notifier
.out:	ldp	x21,x22,[sp],16
	epilogue

; (x21 = representation) -> x0 = nombre de chemins
count_paths:
	ldr	x0,[x21,CR_PTR]
	ldr	x1,[x21,CR_N]
	mov	x2,0
	mov	x3,0				; chemins
	mov	x4,0				; caracteres de la ligne
.c:	cmp	x2,x1
	b.hs	.end
	ldrb	w5,[x0,x2]
	add	x2,x2,1
	cmp	w5,10
	b.eq	.nl
	add	x4,x4,1
	b	.c
.nl:	cbz	x4,.c
	add	x3,x3,1
	mov	x4,0
	b	.c
.end:	cbz	x4,.r
	add	x3,x3,1
.r:	mov	x0,x3
	ret

s_svc		db 'clipboard',0
s_text		db 'text',0
s_image		db 'image',0
s_files		db 'files',0
s_filescut	db 'files-cut',0
s_url		db 'url',0
s_title		db 'Presse-papiers',0
s_ttext		db 'Texte copi',0xC3,0xA9,0
s_tlink		db 'Lien copi',0xC3,0xA9,0
s_timg		db 'Image copi',0xC3,0xA9,'e (',0
s_x		db ' x ',0
s_tcopy1	db ' ',0xC3,0xA9,'l',0xC3,0xA9,'ment copi',0xC3,0xA9,0
s_tcopyn	db ' ',0xC3,0xA9,'l',0xC3,0xA9,'ments copi',0xC3,0xA9,'s',0
s_tcut1		db ' ',0xC3,0xA9,'l',0xC3,0xA9,'ment coup',0xC3,0xA9,0
s_tcutn		db ' ',0xC3,0xA9,'l',0xC3,0xA9,'ments coup',0xC3,0xA9,'s',0
		align 8

include '../../inc/tr.inc'

segment readable writeable
kit_data
		align 8
g_n		dd 0				; copies
g_cursor	dd 0				; id de l'element du curseur (0 : aucun)
g_next		dd 1				; id suivant
g_serial	dd 1				; change a chaque modification de l'anneau
g_tell		dd 0				; une notification a envoyer
tellbuf		rb 96
msg		rb WS_MSG
g_fresh		rb CE_SIZE
g_e		rb (CLIP_RING + 1) * CE_SIZE
