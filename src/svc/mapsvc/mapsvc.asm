; mapsvc - service d'essai de SVC_MAP_CALLER (src/kernel/mapcaller.asm, lance
; par maptest). Une requete : op, a adresse, b taille, c valeur ou duree.
;  1 lire : somme des octets de l'appelant
;  2 ecrire : remplit la zone de la valeur c -> taille
;  3 tenir : mappe, dort c ms, remplit de 0x5A -> 0 (munmap, mort de l'appelant)
;  4 pages : combien de pages (SVC_CALLER_PHYS), la premiere non nulle -> nombre
; Une erreur de map_caller est rendue telle quelle.
include '../../inc/armpi.inc'
app_header

MC_WRITE	= 1

segment readable executable

start:
	adr	x0,s_nom
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	mov	x19,x0				; canal
.loop:	mov	x0,x19
	adr	x1,msg
	mov	x2,60000
	sys	SYS_SVC_NEXT
	tbnz	x0,63,quit
	cbz	x0,.loop
	mov	x20,x0
	adr	x1,msg
	ldp	x21,x22,[x1]			; op, a
	ldp	x23,x24,[x1,16]			; b, c
	mov	x3,0
	cmp	x21,1
	b.eq	.map
	cmp	x21,4
	b.eq	.map
	mov	x3,MC_WRITE
.map:	mov	x0,x20
	mov	x1,x22
	mov	x2,x23
	sys	SYS_SVC_MAP_CALLER
	tbnz	x0,63,.rep
	mov	x25,x0
	cmp	x21,1
	b.eq	.read
	cmp	x21,2
	b.eq	.fill
	cmp	x21,3
	b.eq	.hold
	adr	x1,table			; 4
	mov	x2,300
	sys	SYS_SVC_CALLER_PHYS
	tbnz	x0,63,.rep
	adr	x1,table
	ldr	x1,[x1]
	cbnz	x1,.rep
	mov	x0,-1
	b	.rep
.read:	mov	x0,0
	mov	x1,0
.r:	ldrb	w2,[x25,x1]
	add	x0,x0,x2
	add	x1,x1,1
	cmp	x1,x23
	b.lo	.r
	b	.rep
.hold:	mov	x0,x24
	sys	SYS_SLEEP
	mov	x24,0x5A
.fill:	mov	x1,0
.w:	strb	w24,[x25,x1]
	add	x1,x1,1
	cmp	x1,x23
	b.lo	.w
	mov	x0,x23
.rep:	adr	x1,msg
	str	x0,[x1]
	mov	x0,x20
	sys	SYS_SVC_REPLY
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

s_nom	db 'mapsvc',0
	align 8

segment readable writeable
msg	rb 512
table	rb 300 * 8
