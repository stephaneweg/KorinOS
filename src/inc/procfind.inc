; procfind.inc - trouver un process par son nom, quel que soit le nombre de
; process (SYS_PROCLIST_FROM, par paquets de PF_N entrees sur la pile).

PF_N		= 16

; x0 = nom, x1 = pid apres lequel chercher (0 : depuis le debut ; pour le
; suivant de meme nom, le pid rendu) -> x0 = pid (0 : aucun)
proc_find:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,PF_N * 64
	mov	x19,x0
	and	x20,x1,0xFFFF			; emplacement de depart
.paquet:
	mov	x0,sp
	mov	x1,PF_N
	mov	x2,x20
	sys	SYS_PROCLIST_FROM
	cmp	x0,0
	b.le	.aucun
	mov	x21,x0
	mov	x22,sp
.e:	ldr	x20,[x22]			; (la suite : apres cet emplacement)
	and	x20,x20,0xFFFF
	mov	x0,x19
	add	x1,x22,24
	mov	x2,32
.c:	ldrb	w3,[x0],1
	ldrb	w4,[x1],1
	cmp	w3,w4
	b.ne	.autre
	cbz	w3,.trouve
	subs	x2,x2,1
	b.ne	.c
.trouve:
	ldr	x0,[x22]
	b	.out
.autre:	add	x22,x22,64
	subs	x21,x21,1
	b.ne	.e
	add	x1,sp,PF_N * 64			; paquet plein : il y a peut-etre une suite
	cmp	x22,x1
	b.eq	.paquet
.aucun:	mov	x0,0
.out:	add	sp,sp,PF_N * 64
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
