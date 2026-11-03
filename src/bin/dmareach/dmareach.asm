; ============================================================================
;  dmareach - jusqu'ou le DMA de la carte atteint-il la memoire ? Lit 1 Mo de
;  blk.sd0 vers un tampon sous 1 Go (la reference), puis vers des tampons de
;  plus en plus hauts (16 Mo, pris et gardes), et compare. Lecture seule.
;    dmareach : a 1040 Mo : identique		(adresse physique du tampon)
;  A lancer avec un sd dont SD_DMA_LIMIT couvre ces adresses ; sinon sd y passe
;  en PIO et le resultat ne dit rien.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/blk.inc'

SECTEUR		= 200000		; premier secteur lu
NOMBRE		= 2048			; 1 Mo
OCTETS		= NOMBRE * 512
TAMPON		= 0x1000000		; 16 Mo
HAUT_MAX	= 0xC0000000		; on s'arrete a 3 Go
TOURS		= 110

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	bl	kits_open
	adr	x0,s_blk
	mov	x1,2000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.err
	mov	x19,x0
	adr	x1,msg
	mov	x0,BLK_INFO
	str	x0,[x1]
	mov	x0,x19
	sys	SYS_SVC_CALL
	tbnz	x0,63,.err
	adr	x1,msg
	ldr	x22,[x1,32]			; pid du service
	movl	x0,OCTETS or BUF_LOW		; la reference
	bl	tampon
	tbnz	x0,63,.err
	mov	x23,x0
	mov	x0,x23
	bl	lire
	tbnz	x0,63,.err
	mov	x24,TOURS
.tour:	movl	x0,TAMPON
	bl	tampon
	tbnz	x0,63,.fin
	mov	x25,x0
	adr	x1,info
	mov	x0,x20				; (son numero)
	sys	SYS_BUF_INFO
	adr	x1,info
	ldr	x26,[x1,BI_PHYS]
	mov	x0,x25
	bl	lire
	tbnz	x0,63,.err
	mov	x0,x25				; compare a la reference
	mov	x1,x23
	movl	x2,OCTETS
.cmp:	ldr	x3,[x0],8
	ldr	x4,[x1],8
	cmp	x3,x4
	b.ne	.diff
	subs	x2,x2,8
	b.ne	.cmp
	adr	x21,m_same
	b	.dit
.diff:	adr	x21,m_diff
.dit:	adr	x0,m_a
	bl	lang_puts
	lsr	x0,x26,20
	bl	print_dec
	mov	x0,x21
	bl	lang_puts
	movl	x0,HAUT_MAX
	cmp	x26,x0
	b.hs	.fin
	subs	x24,x24,1
	b.ne	.tour
.fin:	adr	x0,m_fin
	bl	lang_puts
	mov	x0,0
	sys	SYS_EXIT
.err:	mov	x19,x0
	adr	x0,m_err
	bl	lang_puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = taille (BUF_LOW compris) : un tampon, autorise pour le service et
; attache (x19 canal, x22 son pid) -> x0 = adresse, x20 = numero
tampon:
	prologue
	str	x21,[sp,-16]!
	mov	x21,x0
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.out
	mov	x20,x0
	mov	x1,x22
	sys	SYS_BUF_GRANT
	adr	x1,msg
	mov	x0,BLK_ATTACHER
	stp	x0,x20,[x1]
	and	x0,x21,not BUF_LOW
	str	x0,[x1,16]
	mov	x0,x19
	sys	SYS_SVC_CALL
	tbnz	x0,63,.out
	movl	x1,BUF_SLOT
	movl	x2,BUF_VA_BASE
	madd	x0,x20,x1,x2
.out:	ldr	x21,[sp],16
	epilogue

; x0 = adresse dans le tampon attache -> resultat du service
lire:
	prologue
	adr	x1,msg
	mov	x2,BLK_LIRE
	str	x2,[x1]
	movl	x2,SECTEUR
	str	x2,[x1,8]
	mov	x2,NOMBRE
	str	x2,[x1,16]
	str	x0,[x1,24]
	mov	x0,x19
	sys	SYS_SVC_CALL
	epilogue

s_blk	db 'blk.sd0',0
m_a	db 'dmareach : a ',0
m_same	db ' Mo : identique',10,0
m_diff	db ' Mo : DIFFERENT',10,0
m_fin	db 'dmareach : fin',10,0
m_err	db 'dmareach : erreur ',0
m_nl	db 10,0
	align 8

segment readable writeable
kit_data
	align 8
msg	rb 512
info	rb BI_SIZE
