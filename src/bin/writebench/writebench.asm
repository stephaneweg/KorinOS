; writebench - debit d'ecriture d'un fichier (diagnostic de la carte) :
;	writebench [chemin]
; ecrit 8 Mo par requetes de 64 Ko dans un fichier neuf (clusters a allouer), les
; reecrit sur place, reecrit 200 secteurs isoles, le tronque, le refait et
; l'efface ; affiche ms et Ko/s de chacun. Par defaut SD0:/writebench.bin,
; jamais SD1:.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

TOTAL		= 8*1024*1024
CHUNK		= 65536
SECTORS		= 200

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so

start:
	mov	x19,x0
	bl	kits_open
	cbz	x19,.def
	ldrb	w0,[x19]
	cbnz	w0,.go
.def:	adr	x19,p_file
.go:	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	adr	x2,m_new
	bl	pass
	mov	x1,O_WRITE
	adr	x2,m_over
	bl	pass
	mov	x0,x19
	mov	x1,O_WRITE
	sys	SYS_OPEN
	tbnz	x0,63,failed
	mov	x22,x0
	sys	SYS_TIME_MS
	mov	x23,x0
	mov	x24,0
.s:	mov	x0,x22
	lsl	x1,x24,12
	mov	x2,0
	sys	SYS_SEEK
	mov	x0,x22
	adr	x1,buffer
	mov	x2,512
	sys	SYS_WRITE
	cmp	x0,512
	b.ne	failed
	add	x24,x24,1
	cmp	x24,SECTORS
	b.lo	.s
	mov	x0,x22
	sys	SYS_CLOSE
	sys	SYS_TIME_MS
	sub	x23,x0,x23
	adr	x0,m_sect
	bl	puts
	mov	x0,x23
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	mov	x0,SECTORS
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	sys	SYS_TIME_MS
	mov	x23,x0
	mov	x0,x19
	mov	x1,O_WRITE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,failed
	sys	SYS_CLOSE
	adr	x0,m_trunc
	bl	elapsed
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	adr	x2,m_new
	bl	pass
	sys	SYS_TIME_MS
	mov	x23,x0
	mov	x0,x19
	sys	SYS_UNLINK
	cbnz	x0,failed
	adr	x0,m_del
	bl	elapsed
	mov	x0,0
	sys	SYS_EXIT

; x0 = libelle, x23 = depart : duree affichee
elapsed:
	prologue
	bl	puts
	sys	SYS_TIME_MS
	sub	x0,x0,x23
	bl	print_dec
	adr	x0,m_only
	bl	puts
	epilogue

; x1 = drapeaux d'ouverture, x2 = libelle : 8 Mo ecrits, duree affichee
pass:
	prologue
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x21,x2
	mov	x0,x19
	sys	SYS_OPEN
	tbnz	x0,63,failed
	mov	x22,x0
	sys	SYS_TIME_MS
	mov	x23,x0
	mov	x24,0
.w:	mov	x0,x22
	adr	x1,buffer
	movl	x2,CHUNK
	sys	SYS_WRITE
	movl	x1,CHUNK
	cmp	x0,x1
	b.ne	failed
	add	x24,x24,x1
	movl	x1,TOTAL
	cmp	x24,x1
	b.lo	.w
	mov	x0,x22
	sys	SYS_CLOSE
	sys	SYS_TIME_MS
	sub	x23,x0,x23
	mov	x0,x21
	bl	puts
	mov	x0,x23
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	lsr	x0,x24,10
	mov	x1,1000
	mul	x0,x0,x1
	add	x1,x23,1
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_kos
	bl	puts
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	epilogue

failed:	mov	x19,x0
	adr	x0,m_err
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

p_file		db 'SD0:/writebench.bin',0
m_new		db 'fichier neuf : ',0
m_over		db 'sur place : ',0
m_sect		db 'secteurs isoles : ',0
m_trunc		db 'troncature de 8 Mo : ',0
m_del		db 'effacement de 8 Mo : ',0
m_only		db ' ms',10,0
m_ms		db ' ms, ',0
m_kos		db ' Ko/s',10,0
m_nl		db 10,0
m_err		db 'writebench : erreur ',0
		align 8

segment readable writeable
kit_data
		align 16
buffer		rb CHUNK
