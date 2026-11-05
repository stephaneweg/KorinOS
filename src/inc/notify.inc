; ============================================================================
;  inc/notify.inc - envoyer une notification a notifyd (service « notify »,
;  op 1 : titre, 0, texte, 0 [, app, 0]). Sans notifyd (avant le bureau, en
;  mode console) : rien. Les autres ops servent aux reglages rapides
;  (SD0:/sys/quick) : l'historique.
;
;	segment executable ... include '../../inc/notify.inc'
;	bl notifier			; x0 = titre, x1 = texte (UTF-8)
;	bl notifier_app			; idem, x2 = app (SD0:/apps/<app>.app)
; ============================================================================

NOTIFY_OP		= 1		; titre, 0, texte, 0 [, app, 0] -> 0
NOTIFY_OP_LIST		= 2		; a = rang (0 : la plus recente) -> nombre ; donnees : entree NH_*
NOTIFY_OP_DELETE	= 3		; a = id -> 0 / -2
NOTIFY_OP_CLEAR		= 4		; -> 0 : tout l'historique efface
NOTIFY_OP_READ		= 5		; -> 0 : toutes lues

; une entree de l'historique (NOTIFY_OP_LIST)
NH_ID		= 0			; (64 bits) numero, jamais reutilise
NH_TIME		= 8			; (64 bits) arrivee, ms depuis le demarrage (SYS_TIME_MS)
NH_UNREAD	= 16			; (64 bits) 1 : non lue
NH_APP		= 24			; nom du paquet (ou du process), 0 final
NH_APP_N	= 32
NH_TITRE	= 56
NH_TITRE_N	= 64
NH_TEXTE	= 120
NH_TEXTE_N	= 192
NH_SIZE		= 312

; x0 = titre, x1 = texte -> x0 = 0, ou < 0 si notifyd est absent
notifier:
	mov	x2,0
; x0 = titre, x1 = texte, x2 = app (0 : le process appelant) -> idem
notifier_app:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,512			; message
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	adr	x0,nt_nom
	mov	x1,0				; (sans attendre)
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	mov	x2,NOTIFY_OP
	str	x2,[sp]
	add	x3,sp,48			; titre, 0, texte, 0 [, app, 0]
	add	x4,sp,48+463
	mov	x1,x19
.t:	ldrb	w2,[x1],1
	strb	w2,[x3],1
	cbz	w2,.tx
	cmp	x3,x4
	b.lo	.t
	strb	wzr,[x3,-1]
.tx:	mov	x1,x20
.x:	cmp	x3,x4
	b.hs	.coupe
	ldrb	w2,[x1],1
	strb	w2,[x3],1
	cbnz	w2,.x
	cbz	x21,.fin
	mov	x1,x21				; l'app
.a:	cmp	x3,x4
	b.hs	.coupe
	ldrb	w2,[x1],1
	strb	w2,[x3],1
	cbnz	w2,.a
.fin:	strb	wzr,[x3]			; (pas d'app : un 0 de plus)
	b	.envoi
.coupe:	strb	wzr,[x4]
.envoi:	mov	x1,sp
	sys	SYS_SVC_CALL
.out:	add	sp,sp,512
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

nt_nom	db 'notify',0
	align 4
