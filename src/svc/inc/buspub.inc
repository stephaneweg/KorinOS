; buspub.inc - publier sur le bus (docs/BUS.md) sans BusKit : pour les
; services lances avant que SD0:/lib soit lisible (vfs) ou qui ne chargent
; que AppKit (usbd). Protocole de inc/bus.inc ; le programme declare dans ses
; donnees :
;	bp_canal	dd 0		; canal de busd + 1 (0 : pas ouvert)
; Un service qui publie depuis deux threads le peut : le message est sur la
; pile de l'appelant.

; x0 = attente en ms -> x0 = 1 si le canal de busd est ouvert
bp_open:
	adr	x1,bp_canal
	ldr	x2,[x1]
	cbnz	x2,.ok
	mov	x1,x0
	adr	x0,bp_name
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.no
	add	x0,x0,1
	adr	x1,bp_canal
	str	x0,[x1]
.ok:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = sujet, x1 = valeur (chaine, 255 octets au plus ; 0 : vide),
; x2 = BUS_RETAIN, BUS_DELETE -> x0 = reponse de busd, < 0 : erreur ou pas
; de canal (busd relance : le canal est oublie, a rouvrir)
bp_publish:
	prologue
	sub	sp,sp,512
	mov	x3,sp
	mov	x4,BUS_OP_PUBLISH
	stp	x4,x2,[x3]			; op, a = drapeaux
	add	x5,x3,BUS_DATA
	mov	x6,BUS_NAME_MAX
.n:	ldrb	w4,[x0],1			; le nom
	strb	w4,[x5],1
	cbz	w4,.v0
	sub	x6,x6,1
	cbnz	x6,.n
	mov	x0,BUS_EINVAL
	b	.out
.v0:	mov	x7,x5				; la valeur
	cbz	x1,.vf
	mov	x6,BUS_VALUE_MAX - 1
.v:	ldrb	w4,[x1],1
	cbz	w4,.vf
	strb	w4,[x5],1
	sub	x6,x6,1
	cbnz	x6,.v
.vf:	strb	wzr,[x5]
	sub	x4,x5,x7
	str	x4,[x3,16]			; b = octets
	adr	x0,bp_canal
	ldr	x0,[x0]
	cbz	x0,.nodev
	sub	x0,x0,1
	mov	x1,x3
	sys	SYS_SVC_CALL
	tbz	x0,63,.out
	ldr	x1,[sp]				; reponse de busd ?
	cmp	x0,x1
	b.eq	.out
	adr	x1,bp_canal			; non : le canal est mort
	str	xzr,[x1]
.nodev:	mov	x0,BUS_ENODEV
.out:	add	sp,sp,512
	epilogue

; x0 = tampon, x1 = valeur, x2 = chiffres -> x0 = fin (chiffres hexadecimaux
; minuscules, sans 0 final)
bp_hex:
	lsl	x3,x2,2
.c:	sub	x3,x3,4
	lsr	x4,x1,x3
	and	x4,x4,15
	cmp	x4,10
	add	x5,x4,'0'
	add	x4,x4,'a' - 10
	csel	x4,x5,x4,lo
	strb	w4,[x0],1
	cbnz	x3,.c
	ret

; x0 = tampon, x1 = valeur -> x0 = fin (decimal, sans 0 final)
bp_dec:
	sub	sp,sp,32
	mov	x2,sp
	mov	x6,sp
	mov	x3,10
.d:	udiv	x4,x1,x3
	msub	x5,x4,x3,x1
	add	x5,x5,'0'
	strb	w5,[x2],1
	mov	x1,x4
	cbnz	x1,.d
.r:	ldrb	w5,[x2,-1]!
	strb	w5,[x0],1
	cmp	x2,x6
	b.ne	.r
	add	sp,sp,32
	ret

; x0 = tampon, x1 = chaine -> x0 = fin (copiee sans son 0 final)
bp_cat:
.c:	ldrb	w2,[x1],1
	cbz	w2,.f
	strb	w2,[x0],1
	b	.c
.f:	ret

bp_name		db 'bus',0
		align 8
