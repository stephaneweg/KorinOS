; hwtest - service d'essai de l'acces au materiel (docs/NOYAU-HYBRIDE.md,
; 3.3-2), lance par /bin/svctest : registres du minuteur systeme (dev_map),
; refus d'un mappage de RAM, memoire DMA, appels au firmware (Pi 4).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

ST_BASE	= 0xFE003000			; minuteur systeme du BCM2711 (CLO : +4)

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	bl	kits_open
	movl	x0,ST_BASE			; 1. registres : le compteur avance
	mov	x1,0x20
	sys	SYS_DEV_MAP
	mov	x19,x0
	tbnz	x0,63,.e1
	ldr	w20,[x19,4]
	mov	x0,20
	sys	SYS_SLEEP
	ldr	w21,[x19,4]
	sub	w21,w21,w20
	adr	x0,m_st
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_us
	bl	puts
	b	.t2
.e1:	adr	x0,m_st
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
.t2:	mov	x0,0x100000			; 2. de la RAM : refuse
	mov	x1,0x1000
	sys	SYS_DEV_MAP
	mov	x19,x0
	adr	x0,m_ram
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,10000			; 3. memoire DMA : ecrite, relue
	adr	x1,phys
	sys	SYS_DMA_ALLOC
	mov	x19,x0
	adr	x0,m_dma
	bl	puts
	tbnz	x19,63,.e3
	adr	x0,phys
	ldr	x0,[x0]
	bl	print_dec
	mov	x0,0
	movl	w1,0xA5A55A5A
.w:	str	w1,[x19,x0]
	add	x0,x0,4
	cmp	x0,2048
	b.lo	.w
	mov	x0,0
	mov	x2,0				; erreurs
.r:	ldr	w3,[x19,x0]
	cmp	w3,w1
	cinc	x2,x2,ne
	add	x0,x0,4
	cmp	x0,2048
	b.lo	.r
	mov	x19,x2
	adr	x0,m_err
	bl	puts
	mov	x0,x19
.e3:	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x0,fw				; 4. firmware : revision de la carte
	sys	SYS_FW_CALL
	mov	x19,x0
	adr	x0,m_fw
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_rev
	bl	puts
	adr	x0,fw
	ldr	w0,[x0,20]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,0
	sys	SYS_EXIT

m_st	db 'hwtest : minuteur systeme mappe, 20 ms = ',0
m_us	db ' us',10,0
m_ram	db 'hwtest : mapper de la RAM -> ',0
m_dma	db 'hwtest : memoire DMA a ',0
m_err	db ', erreurs de relecture : ',0
m_fw	db 'hwtest : firmware -> ',0
m_rev	db ', revision de la carte ',0
m_nl	db 10,0
	align 8

segment readable writeable
kit_data
	align 16
fw	dw 28, 0, 0x00010002, 4, 0, 0, 0	; taille, code, tag, place, req, valeur, fin
phys	dd 0
