; busd - le bus d'evenements du systeme (docs/BUS.md), lance par init2 avant
; hwmanager. Service "bus" (inc/bus.inc), client : BusKit.
;  * des sujets nommes ("korin.theme", "korin.usb.device"...) : un programme
;    publie une valeur (BUS_VALUE_MAX octets au plus) sous un nom ;
;  * des abonnements : un nom exact, ou un prefixe suivi de "*"
;    ("korin.usb.*", "*" : tout). A chaque publication, chaque abonnement qui
;    correspond recoit WEV_BUS dans la file d'evenements de son programme
;    (celle d'ui_event : SVF_POST permet a busd le WS_POST du serveur
;    graphique), avec les BUS_INLINE premiers octets de la valeur ;
;  * la derniere valeur d'un sujet reste lisible (BUS_OP_GET) ; une valeur
;    BUS_RETAIN est en plus rendue a un abonne BUS_CURRENT des son arrivee
;    (l'etat courant : theme, reseau, peripheriques...) ;
;  * fin d'un programme (SVF_FINS) : ses abonnements sont retires.
; File d'evenements pleine (64) : l'evenement est perdu pour ce programme ;
; la valeur, elle, reste lisible.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/bus.inc'
include '../../kits/buskit/buskit.inc'

TOPIC_MAX	= 128
TP_USED		= 0			; 1 : place prise
TP_FLAGS	= 8			; BUS_RETAIN
TP_LEN		= 16			; octets de la valeur (-1 : en train d'etre retire)
TP_PID		= 24			; dernier editeur
TP_NAME		= 32			; BUS_NAME_MAX
TP_VALUE	= 96			; BUS_VALUE_MAX
TP_SIZE		= 352

SUB_MAX		= 256
SB_PID		= 0			; abonne (0 : place libre)
SB_ID		= 8			; numero rendu au programme
SB_PLEN		= 16			; prefixe : octets avant "*" (-1 : nom exact)
SB_PAT		= 24			; BUS_NAME_MAX
SB_SIZE		= 88

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	bl	kits_open
	adr	x0,s_name
	mov	x1,SVF_FINS or SVF_POST
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	adr	x1,chan
	str	x0,[x1]
.loop:	adr	x0,chan
	ldr	x0,[x0]
	adr	x1,msg
	mov	x2,60000
	sys	SYS_SVC_NEXT
	tbnz	x0,63,quit
	cbz	x0,.loop
	mov	x19,x0
	adr	x20,msg
	ldr	x0,[x20]
	movl	x1,SVC_OP_FIN
	cmp	x0,x1
	b.eq	.fin
	movl	x1,SVC_OP_SIGNAL
	cmp	x0,x1
	b.eq	.loop				; (sans reponse)
	cmp	x0,BUS_OP_PUBLISH
	b.eq	.pub
	cmp	x0,BUS_OP_SUBSCRIBE
	b.eq	.sub
	cmp	x0,BUS_OP_UNSUBSCRIBE
	b.eq	.unsub
	cmp	x0,BUS_OP_GET
	b.eq	.get
	cmp	x0,BUS_OP_LIST
	b.eq	.list
	mov	x0,BUS_EINVAL
	b	.rep
.pub:	bl	op_publish
	b	.rep
.sub:	bl	op_subscribe
	b	.rep
.unsub:	ldr	w0,[x20,44]
	ldr	x1,[x20,8]
	mov	x21,x1
	bl	unsubscribe
	cbz	x21,.rep			; (tous : combien, meme 0)
	cbnz	x0,.rep0
	mov	x0,BUS_ENOENT
	b	.rep
.rep0:	mov	x0,0
	b	.rep
.get:	bl	op_get
	b	.rep
.list:	bl	op_list
.rep:	str	x0,[x20]
	mov	x0,x19
	mov	x1,x20
	sys	SYS_SVC_REPLY
	b	.loop
.fin:	ldr	x0,[x20,8]			; (avis, sans reponse) a = pid
	cbz	x0,.loop
	mov	x1,0
	bl	unsubscribe
	b	.loop
quit:	mov	x0,1
	sys	SYS_EXIT

; --- les tables ----------------------------------------------------------------

; x0 = place -> x0 = sujet
topic_entry:
	adr	x1,topic_table
	mov	x2,TP_SIZE
	madd	x0,x0,x2,x1
	ret

; x0 = place -> x0 = abonnement
sub_entry:
	adr	x1,sub_table
	mov	x2,SB_SIZE
	madd	x0,x0,x2,x1
	ret

; x0 = nom (0 final), x1 = 1 si un "*" final est permis (motif) -> x0 =
; longueur (1..BUS_NAME_MAX-1), -1 si faux (vide, trop long, espace ou
; caractere de controle, "*" ailleurs qu'a la fin d'un motif)
name_check:
	mov	x2,0
.c:	ldrb	w3,[x0,x2]
	cbz	w3,.end
	cmp	w3,' '
	b.ls	.bad
	cmp	w3,'*'
	b.ne	.ok
	cbz	x1,.bad
	add	x4,x2,1
	ldrb	w4,[x0,x4]
	cbnz	w4,.bad
.ok:	add	x2,x2,1
	cmp	x2,BUS_NAME_MAX
	b.hs	.bad
	b	.c
.end:	cbz	x2,.bad
	mov	x0,x2
	ret
.bad:	mov	x0,-1
	ret

; x0 = nom -> x0 = place du sujet, -1 : aucun
topic_find:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x0
	mov	x19,0
.t:	mov	x0,x19
	bl	topic_entry
	ldr	x1,[x0,TP_USED]
	cbz	x1,.n
	add	x0,x0,TP_NAME
	mov	x1,x20
	bl	str_eq
	cbnz	x0,.found
.n:	add	x19,x19,1
	cmp	x19,TOPIC_MAX
	b.lo	.t
	mov	x0,-1
	b	.out
.found:	mov	x0,x19
.out:	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = une place libre de sujet, -1 : aucune
topic_alloc:
	prologue
	str	x19,[sp,-16]!
	mov	x19,0
.t:	mov	x0,x19
	bl	topic_entry
	ldr	x1,[x0,TP_USED]
	cbz	x1,.found
	add	x19,x19,1
	cmp	x19,TOPIC_MAX
	b.lo	.t
	mov	x0,-1
	b	.out
.found:	mov	x0,x19
.out:	ldr	x19,[sp],16
	epilogue

; x0 = abonnement, x1 = nom de sujet -> x0 = 1 si le motif correspond
sub_match:
	ldr	x2,[x0,SB_PLEN]
	add	x0,x0,SB_PAT
	tbnz	x2,63,.exact
.p:	cbz	x2,.yes				; prefixe
	ldrb	w3,[x0],1
	ldrb	w4,[x1],1
	cmp	w3,w4
	b.ne	.no
	sub	x2,x2,1
	b	.p
.exact:	ldrb	w3,[x0],1
	ldrb	w4,[x1],1
	cmp	w3,w4
	b.ne	.no
	cbnz	w3,.exact
.yes:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = place du sujet, x1 = place de l'abonnement + 1 (0 : tous ceux qui
; correspondent) : WEV_BUS a leurs programmes -> x0 = evenements deposes
notify:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x23,x1
	bl	topic_entry
	mov	x20,x0
	adr	x21,ev				; l'evenement, le meme pour tous
	mov	w0,WEV_BUS
	str	w0,[x21]
	add	w0,w19,1
	str	w0,[x21,BEV_TOPIC]
	ldr	x0,[x20,TP_LEN]
	str	w0,[x21,BEV_LEN]		; (-1 : BUS_DELETED)
	stp	xzr,xzr,[x21,16]
	tbnz	x0,63,.built
	mov	x2,BUS_INLINE
	cmp	x0,x2
	csel	x2,x0,x2,lo
	add	x0,x21,BEV_DATA
	add	x1,x20,TP_VALUE
	bl	mem_move
.built:	mov	x22,0
	mov	x24,0
	cbz	x23,.s
	sub	x24,x23,1			; un seul abonnement
.s:	mov	x0,x24
	bl	sub_entry
	ldr	x1,[x0,SB_PID]
	cbz	x1,.n
	add	x1,x20,TP_NAME
	bl	sub_match
	cbz	x0,.n
	mov	x0,x24
	bl	sub_entry
	ldr	x1,[x0,SB_ID]
	str	w1,[x21,BEV_SUB]
	ldr	x0,[x0,SB_PID]
	mov	x1,x21
	sys	SYS_WS_POST
	tbnz	x0,63,.n			; file pleine, programme parti
	add	x22,x22,1
.n:	cbnz	x23,.done
	add	x24,x24,1
	cmp	x24,SUB_MAX
	b.lo	.s
.done:	mov	x0,x22
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid, x1 = numero (0 : tous ceux du pid) : abonnements retires -> x0 =
; combien
unsubscribe:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,0
	mov	x22,0
.s:	mov	x0,x21
	bl	sub_entry
	ldr	x1,[x0,SB_PID]
	cmp	x1,x19
	b.ne	.n
	cbz	x20,.rm
	ldr	x1,[x0,SB_ID]
	cmp	x1,x20
	b.ne	.n
.rm:	str	xzr,[x0,SB_PID]
	add	x22,x22,1
.n:	add	x21,x21,1
	cmp	x21,SUB_MAX
	b.lo	.s
	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; --- les ops (msg) -------------------------------------------------------------

; BUS_OP_PUBLISH -> x0 = evenements deposes / erreur
op_publish:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,msg
	add	x0,x19,BUS_DATA
	mov	x1,0
	bl	name_check
	tbnz	x0,63,.inval
	mov	x21,x0				; longueur du nom
	ldr	x22,[x19,16]			; octets de la valeur
	cmp	x22,BUS_VALUE_MAX
	b.hi	.inval
	add	x0,x21,1
	add	x0,x0,x22
	cmp	x0,BUS_DATA_MAX
	b.hi	.inval
	add	x0,x19,BUS_DATA
	bl	topic_find
	mov	x20,x0				; place (-1 : nouveau sujet)
	ldr	x1,[x19,8]
	tbnz	x1,1,.del			; BUS_DELETE
	tbz	x20,63,.have
	bl	topic_alloc
	tbnz	x0,63,.nomem
	mov	x20,x0
	bl	topic_entry
	mov	x1,1
	str	x1,[x0,TP_USED]
	add	x0,x0,TP_NAME
	add	x1,x19,BUS_DATA
	add	x2,x21,1
	bl	mem_move
.have:	mov	x0,x20
	bl	topic_entry
	ldr	x1,[x19,8]
	and	x1,x1,BUS_RETAIN
	str	x1,[x0,TP_FLAGS]
	str	x22,[x0,TP_LEN]
	ldr	w1,[x19,44]
	str	x1,[x0,TP_PID]
	add	x0,x0,TP_VALUE
	add	x1,x19,BUS_DATA			; la valeur suit le 0 du nom
	add	x1,x1,x21
	add	x1,x1,1
	mov	x2,x22
	bl	mem_move
	mov	x0,x20
	mov	x1,0
	bl	notify
	b	.out
.del:	tbnz	x20,63,.noent
	mov	x0,x20
	bl	topic_entry
	mov	x1,-1
	str	x1,[x0,TP_LEN]
	mov	x0,x20
	mov	x1,0
	bl	notify
	mov	x21,x0
	mov	x0,x20
	bl	topic_entry
	str	xzr,[x0,TP_USED]
	mov	x0,x21
	b	.out
.noent:	mov	x0,BUS_ENOENT
	b	.out
.nomem:	mov	x0,BUS_ENOMEM
	b	.out
.inval:	mov	x0,BUS_EINVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; BUS_OP_SUBSCRIBE -> x0 = numero d'abonnement / erreur
op_subscribe:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,msg
	add	x0,x19,BUS_DATA
	mov	x1,1
	bl	name_check
	tbnz	x0,63,.inval
	mov	x21,x0				; longueur du motif
	mov	x20,0
.f:	mov	x0,x20
	bl	sub_entry
	ldr	x1,[x0,SB_PID]
	cbz	x1,.take
	add	x20,x20,1
	cmp	x20,SUB_MAX
	b.lo	.f
	mov	x0,BUS_ENOMEM
	b	.out
.take:	mov	x22,x0
	ldr	w1,[x19,44]
	str	x1,[x22,SB_PID]
	adr	x2,next_id			; numeros : 1, 2... (31 bits)
	ldr	x1,[x2]
	str	x1,[x22,SB_ID]
	add	x1,x1,1
	and	x1,x1,0x7FFFFFFF
	cbnz	x1,.id
	mov	x1,1
.id:	str	x1,[x2]
	add	x1,x19,BUS_DATA			; "*" final : un prefixe
	add	x1,x1,x21
	sub	x1,x1,1
	ldrb	w1,[x1]
	mov	x2,-1
	cmp	w1,'*'
	b.ne	.pl
	sub	x2,x21,1
.pl:	str	x2,[x22,SB_PLEN]
	add	x0,x22,SB_PAT
	add	x1,x19,BUS_DATA
	add	x2,x21,1
	bl	mem_move
	ldr	x0,[x19,8]
	tbz	x0,0,.ret			; BUS_CURRENT : les valeurs gardees
	mov	x21,0
.t:	mov	x0,x21
	bl	topic_entry
	ldr	x1,[x0,TP_USED]
	cbz	x1,.tn
	ldr	x1,[x0,TP_FLAGS]
	tbz	x1,0,.tn			; BUS_RETAIN
	mov	x0,x21
	add	x1,x20,1
	bl	notify
.tn:	add	x21,x21,1
	cmp	x21,TOPIC_MAX
	b.lo	.t
.ret:	ldr	x0,[x22,SB_ID]
	b	.out
.inval:	mov	x0,BUS_EINVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; BUS_OP_GET -> x0 = octets de la valeur / BUS_ENOENT
op_get:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,msg
	ldr	x0,[x19,8]
	cbz	x0,.byname
	sub	x0,x0,1
	cmp	x0,TOPIC_MAX
	b.hs	.noent
	b	.have
.byname:
	strb	wzr,[x19,BUS_DATA + BUS_NAME_MAX - 1]
	add	x0,x19,BUS_DATA
	bl	topic_find
	tbnz	x0,63,.noent
.have:	mov	x20,x0
	bl	topic_entry
	ldr	x1,[x0,TP_USED]
	cbz	x1,.noent
	mov	x3,x0
	add	x1,x20,1
	str	x1,[x19,8]
	ldr	x1,[x3,TP_FLAGS]
	str	x1,[x19,16]
	ldr	x20,[x3,TP_LEN]
	add	x0,x19,BUS_DATA
	add	x1,x3,TP_VALUE
	mov	x2,x20
	bl	mem_move
	mov	x0,x20
	b	.out
.noent:	mov	x0,BUS_ENOENT
.out:	ldp	x19,x20,[sp],16
	epilogue

; BUS_OP_LIST -> x0 = sujet suivant (0 : fin)
op_list:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x19,msg
	ldr	x20,[x19,8]			; (sujet a = place a - 1 : on part de a)
.t:	cmp	x20,TOPIC_MAX
	b.hs	.end
	mov	x0,x20
	bl	topic_entry
	ldr	x1,[x0,TP_USED]
	cbnz	x1,.found
	add	x20,x20,1
	b	.t
.found:	mov	x21,x0
	ldr	x1,[x21,TP_LEN]
	str	x1,[x19,16]
	ldr	x1,[x21,TP_FLAGS]
	str	x1,[x19,24]
	ldr	x1,[x21,TP_PID]
	str	x1,[x19,32]
	add	x0,x19,BUS_DATA
	add	x1,x21,TP_NAME
	mov	x2,BUS_NAME_MAX
	bl	mem_move
	add	x0,x20,1
	b	.out
.end:	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

s_name		db BUS_SVC_NAME,0
		align 8

segment readable writeable
kit_data
		align 8
chan		dd -1
next_id		dd 1
ev		rb 32
msg		rb 512
topic_table		rb TOPIC_MAX * TP_SIZE
sub_table		rb SUB_MAX * SB_SIZE
