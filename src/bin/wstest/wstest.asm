; wstest - essai des mecanismes du serveur graphique (ws.asm, appels 70-81)
;   wstest serveur : s'enregistre, prend l'ecran (mire), cree un tampon
;                    partage, repond aux requetes, voit la fin du client
;   wstest client  : requete (2 + 3), tampon partage, evenement du serveur
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

MAGIC		= 0x41524D50			; « ARMP »

segment readable executable
kit_use '../../kits/appkit/appkit.abi'

start:
	mov	x19,x0
	bl	kits_open
	cbz	x19,usage
	ldrb	w0,[x19]
	cmp	w0,'s'
	b.eq	serveur
	cmp	w0,'c'
	b.eq	client
usage:	adr	x0,m_usage
	bl	puts
	mov	x0,2
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
serveur:
	sys	SYS_WS_REGISTER
	adr	x1,m_reg
	bl	report
	tbnz	x0,63,fail
	adr	x0,info
	sys	SYS_WS_DISPLAY
	adr	x1,m_disp
	bl	report
	tbnz	x0,63,fail
	adr	x0,m_fb				; adresse, taille de l'ecran
	bl	puts
	adr	x1,info
	ldr	x0,[x1,8]
	bl	print_dec
	adr	x0,m_x
	bl	puts
	adr	x1,info
	ldr	x0,[x1,16]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	adr	x9,info				; mire : rectangle vert 200 x 100 en (20, 20)
	ldr	x10,[x9]
	ldr	x11,[x9,24]			; pas
	mov	x12,20
.row:	madd	x13,x12,x11,x10
	add	x13,x13,20*4
	mov	x14,200
	movl	w15,0x0020C040
.px:	str	w15,[x13],4
	subs	x14,x14,1
	b.ne	.px
	add	x12,x12,1
	cmp	x12,120
	b.lo	.row
	mov	x0,4096				; tampon partage
	sys	SYS_BUF_CREATE
	adr	x1,m_buf
	bl	report
	tbnz	x0,63,fail
	mov	x20,x0				; numero
	movl	x1,BUF_VA_BASE
	mov	x2,BUF_SLOT
	madd	x1,x20,x2,x1
	movl	w2,MAGIC
	str	w2,[x1]
	mov	x21,30				; 30 s au plus
.loop:	adr	x0,msg
	mov	x1,1000
	sys	SYS_WS_NEXT
	cbz	x0,.tick
	tbnz	x0,63,fail
	mov	x22,x0				; numero de requete
	adr	x1,msg
	ldr	x2,[x1]				; op
	cmp	x2,1
	b.eq	.hello
	cmp	x2,2
	b.eq	.event
	movl	x3,WS_OP_EXIT
	cmp	x2,x3
	b.eq	.exit
	b	.loop
.hello:	ldr	w1,[x1,44]			; pid de l'appelant
	mov	x0,x20
	sys	SYS_BUF_GRANT
	adr	x1,msg
	ldr	x2,[x1,8]			; a + b
	ldr	x3,[x1,16]
	add	x2,x2,x3
	str	x2,[x1]
	str	x20,[x1,24]			; c = numero du tampon
	add	x0,x1,48			; donnees
	adr	x1,m_bonjour
	bl	str_cat
	mov	x0,x22
	adr	x1,msg
	sys	SYS_WS_REPLY
	b	.loop
.event:	adr	x1,msg
	ldr	w0,[x1,44]
	adr	x1,ev
	mov	w2,42
	str	w2,[x1]
	sys	SYS_WS_POST
	adr	x1,m_post
	bl	report
	adr	x1,msg
	str	xzr,[x1]
	mov	x0,x22
	sys	SYS_WS_REPLY
	b	.loop
.exit:	adr	x0,m_cexit
	bl	puts
	adr	x1,msg
	ldr	x0,[x1,8]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,4000				; l'ecran reste au serveur 4 s (capture)
	sys	SYS_SLEEP
	b	ok
.tick:	subs	x21,x21,1
	b.ne	.loop
	adr	x0,m_timeout
	bl	puts
	b	fail

; ---------------------------------------------------------------------------
client:
	mov	x0,500
	sys	SYS_SLEEP
	adr	x1,msg				; requete 1 : 2 + 3
	mov	x0,1
	str	x0,[x1]
	mov	x0,2
	str	x0,[x1,8]
	mov	x0,3
	str	x0,[x1,16]
	mov	x0,x1
	sys	SYS_WS_CALL
	adr	x1,m_call
	bl	report
	tbnz	x0,63,fail
	adr	x0,m_data
	bl	puts
	adr	x0,msg+48
	bl	puts
	adr	x0,m_nl
	bl	puts
	adr	x1,msg
	ldr	x19,[x1,24]			; tampon partage
	mov	x0,x19
	sys	SYS_BUF_MAP
	adr	x1,m_map
	bl	report
	tbnz	x0,63,fail
	ldr	w1,[x0]
	movl	w2,MAGIC
	cmp	w1,w2
	adr	x0,m_magic_ok
	b.eq	.m
	adr	x0,m_magic_ko
.m:	bl	puts
	adr	x1,msg				; requete 2 : un evenement
	mov	x0,2
	str	x0,[x1]
	mov	x0,x1
	sys	SYS_WS_CALL
	adr	x0,ev
	mov	x1,2000
	sys	SYS_EV_WAIT
	adr	x1,m_ev
	bl	report
	b	ok

; x0 = resultat, x1 = libelle : « wstest : <libelle> <resultat> » ; x0 garde
report:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,m_pre
	bl	puts
	mov	x0,x20
	bl	puts
	mov	x0,x19
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

ok:	adr	x0,m_ok
	bl	puts
	mov	x0,0
	sys	SYS_EXIT
fail:	adr	x0,m_fail
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

segment readable writeable
kit_data
m_usage		db 'usage : wstest serveur | client',10,0
m_pre		db 'wstest : ',0
m_reg		db 'ws_register -> ',0
m_disp		db 'ws_display -> ',0
m_fb		db 'wstest : ecran ',0
m_x		db ' x ',0
m_buf		db 'buf_create -> ',0
m_call		db 'ws_call(2 + 3) -> ',0
m_data		db 'wstest : donnees de la reponse : ',0
m_map		db 'buf_map -> ',0
m_magic_ok	db 'wstest : tampon partage lu : ARMP correct',10,0
m_magic_ko	db 'wstest : tampon partage : VALEUR FAUSSE',10,0
m_post		db 'ws_post -> ',0
m_ev		db 'ev_wait -> type ',0
m_cexit		db 'wstest : le noyau signale la fin du client, pid ',0
m_timeout	db 'wstest : pas de client',10,0
m_bonjour	db 'bonjour du serveur',0
m_ok		db 'wstest : termine',10,0
m_fail		db 'wstest : ECHEC',10,0
m_nl		db 10,0
		align 8
info		rb 40
ev		rb 32
msg		rb 512
