; ============================================================================
;  inc/remotepw.inc - le mot de passe de l'acces a distance (telnetd, ftpd,
;  vncd, rdpd ; docs/RESEAU.md). Un seul pour les quatre, choisi au premier
;  demarrage (SD0:/sys/setup) ou dans Reglages (Acces a distance), dans
;  SD0:/etc/remote.txt : « password = ... » (UTF-8, 63 octets au plus). Relu a
;  chaque connexion : un changement vaut tout de suite. Sans mot de passe (pas
;  de fichier, ou vide) : toute connexion est refusee. Le fichier n'entre dans
;  aucun paquet (tools/pkg/mkrepo.py), comme etc/wifi.txt.
;
;	bl	rp_lire			; -> x0 = longueur (0 : aucun, refuser)
;	adr	x0,saisie
;	mov	x1,longueur
;	bl	rp_egal			; -> x0 = 1 si c'est le bon
;
;  Le mot de passe est en clair dans rp_pw (VNC en a besoin pour son defi DES).
;  A inclure dans le code ; rp_data dans « segment readable writeable ».
; ============================================================================
RP_MAX		= 63
	align 4

; -> x0 = longueur du mot de passe (0 : aucun), lui dans rp_pw (termine par 0)
rp_lire:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,rp_pw
	strb	wzr,[x19]
	adr	x0,rp_s_fichier
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.vide
	mov	x20,x0
	adr	x1,rp_tampon
	mov	x2,RP_TAMPON-1
	sys	SYS_READ
	mov	x21,x0
	mov	x0,x20
	sys	SYS_CLOSE
	cmp	x21,0
	b.le	.vide
	adr	x20,rp_tampon
	strb	wzr,[x20,x21]
.ligne:	ldrb	w0,[x20]			; une ligne « password = ... »
	cbz	w0,.vide
	adr	x1,rp_s_cle
	mov	x2,x20
.k:	ldrb	w3,[x1],1
	cbz	w3,.cle
	ldrb	w4,[x2],1
	cmp	w3,w4
	b.eq	.k
.suiv:	ldrb	w0,[x20],1			; ligne suivante
	cbz	w0,.vide
	cmp	w0,10
	b.ne	.suiv
	b	.ligne
.cle:	ldrb	w0,[x2]				; espaces, « = », espaces
	cmp	w0,' '
	b.eq	.e1
	cmp	w0,9
	b.ne	.eg
.e1:	add	x2,x2,1
	b	.cle
.eg:	cmp	w0,'='
	b.ne	.suiv
	add	x2,x2,1
.e2:	ldrb	w0,[x2]
	cmp	w0,' '
	b.ne	.v
	add	x2,x2,1
	b	.e2
.v:	mov	x22,0				; la valeur, jusqu'a la fin de ligne
.vl:	ldrb	w0,[x2,x22]
	cbz	w0,.fin
	cmp	w0,10
	b.eq	.fin
	cmp	w0,13
	b.eq	.fin
	cmp	x22,RP_MAX
	b.hs	.fin
	strb	w0,[x19,x22]
	add	x22,x22,1
	b	.vl
.fin:	strb	wzr,[x19,x22]
.tr:	cbz	x22,.out			; (espaces de fin retirees)
	sub	x1,x22,1
	ldrb	w0,[x19,x1]
	cmp	w0,' '
	b.ne	.out
	strb	wzr,[x19,x1]
	mov	x22,x1
	b	.tr
.vide:	mov	x22,0
.out:	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = saisie, x1 = sa longueur -> x0 = 1 si c'est le mot de passe de rp_pw
; (rp_lire avant) ; meme duree quel que soit le premier octet faux
rp_egal:
	adr	x2,rp_pw
	mov	x3,0				; longueur de rp_pw
.l:	ldrb	w4,[x2,x3]
	cbz	w4,.n
	add	x3,x3,1
	b	.l
.n:	cbz	x3,.non				; aucun mot de passe : refuse
	eor	x5,x3,x1			; longueurs differentes -> non nul
	mov	x6,0
.c:	cmp	x6,x3
	b.hs	.f
	ldrb	w4,[x2,x6]
	cmp	x6,x1
	mov	w7,0
	b.hs	.z
	ldrb	w7,[x0,x6]
.z:	eor	w4,w4,w7
	orr	x5,x5,x4
	add	x6,x6,1
	b	.c
.f:	cmp	x5,0
	cset	x0,eq
	ret
.non:	mov	x0,0
	ret

rp_s_fichier	db 'SD0:/etc/remote.txt',0
rp_s_cle	db 'password',0
	align 4

RP_TAMPON	= 1024
macro rp_data {
align 8
rp_pw		rb RP_MAX+1
rp_tampon	rb RP_TAMPON
}
