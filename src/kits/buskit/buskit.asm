; ============================================================================
;  kits/buskit/buskit.asm - BusKit : le bus d'evenements du systeme
;  -> SD0:/lib/buskit.so (docs/BUS.md, docs/KITS.md)
;
;  Chaque appel est une requete au service "bus" (busd, inc/bus.inc). Les
;  evenements WEV_BUS arrivent dans la file d'evenements du programme :
;  ui_event les rend avec ceux des fenetres ; sans UIKit, bus_wait.
;  busd relance (il est surveille par init2) : le canal est rouvert une fois,
;  mais les abonnements sont a refaire.
;  Variables par process (segment inscriptible du kit).
; ============================================================================
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
kit_header
include '../../inc/bus.inc'
include 'buskit.inc'

E_PIPE		= -32

segment readable executable
include 'buskit.abi'

; -> x0 = canal du service, -1 : busd absent
chan:
	adr	x1,b_chan
	ldr	x0,[x1]
	cbz	x0,.open
	sub	x0,x0,1				; (garde + 1 : 0 = pas ouvert)
	ret
.open:	prologue
	adr	x0,s_bus
	mov	x1,0
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.no
	adr	x1,b_chan
	add	x2,x0,1
	str	x2,[x1]
	epilogue
.no:	mov	x0,-1
	epilogue

; x0 = op, x1 = a, x2 = b (donnees deja dans b_msg) -> x0 = resultat
; (BUS_ENODEV : busd absent ; un canal mort est rouvert une fois). SVC_CALL
; rend le resultat de busd, qui peut etre negatif : c'est une erreur du canal
; seulement si le message n'a pas recu de reponse (+0 : l'op, toujours > 0)
svc_request:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,0
.try:	adr	x1,b_msg
	stp	x19,x20,[x1]
	str	x21,[x1,16]
	bl	chan
	tbnz	x0,63,.nodev
	adr	x1,b_msg
	sys	SYS_SVC_CALL
	tbz	x0,63,.ok
	adr	x1,b_msg			; reponse de busd ?
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.out
	adr	x1,b_chan
	str	xzr,[x1]
	cbnz	x22,.nodev
	mov	x22,1
	b	.try
.ok:	adr	x0,b_msg
	ldr	x0,[x0]
	b	.out
.nodev:	mov	x0,BUS_ENODEV
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom ou motif, x1 = 1 si un "*" final est permis -> x0 = longueur, copie
; dans les donnees de b_msg (0 compris), -1 si faux (busd verifie le reste)
put_name:
	adr	x2,b_msg + BUS_DATA
	mov	x3,0
.c:	ldrb	w4,[x0,x3]
	strb	w4,[x2,x3]
	cbz	w4,.end
	add	x3,x3,1
	cmp	x3,BUS_NAME_MAX
	b.lo	.c
	mov	x0,-1
	ret
.end:	cbz	x3,.bad
	mov	x0,x3
	ret
.bad:	mov	x0,-1
	ret

; x0 = destination, x1 = source, x2 = octets
copy_bytes:
	cbz	x2,.f
.c:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.c
.f:	ret

; -> x0 = 1 si busd repond
bus_available:
	prologue
	bl	chan
	mvn	x0,x0
	lsr	x0,x0,63			; (-1 : 0, canal : 1)
	epilogue

; x0 = nom, x1 = valeur, x2 = octets (-1 : chaine), x3 = drapeaux -> abonnes
; prevenus / BUS_E*
bus_publish:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x21,x3
	cmn	x20,1
	b.ne	.len
	mov	x20,0				; chaine : sa longueur
.sl:	ldrb	w1,[x19,x20]
	cbz	w1,.len
	add	x20,x20,1
	b	.sl
.len:	cmp	x20,BUS_VALUE_MAX
	b.hi	.inval
	mov	x1,0
	bl	put_name
	tbnz	x0,63,.inval
	adr	x1,b_msg + BUS_DATA + 1		; la valeur apres le 0 du nom
	add	x0,x1,x0
	mov	x1,x19
	mov	x2,x20
	bl	copy_bytes
	mov	x0,BUS_OP_PUBLISH
	mov	x1,x21
	mov	x2,x20
	bl	svc_request
	b	.out
.inval:	mov	x0,BUS_EINVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = motif, x1 = BUS_CURRENT -> abonnement / BUS_E*
bus_subscribe:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	mov	x1,1
	bl	put_name
	tbnz	x0,63,.inval
	mov	x0,BUS_OP_SUBSCRIBE
	mov	x1,x19
	mov	x2,0
	bl	svc_request
	b	.out
.inval:	mov	x0,BUS_EINVAL
.out:	ldr	x19,[sp],16
	epilogue

; x0 = abonnement (0 : tous) -> 0 / BUS_E*
bus_unsubscribe:
	prologue
	mov	x1,x0
	mov	x0,BUS_OP_UNSUBSCRIBE
	mov	x2,0
	bl	svc_request
	tbnz	x0,63,.out
	mov	x0,0
.out:	epilogue

; x0 = nom, x1 = tampon, x2 = taille -> octets / BUS_E*
bus_get:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x1,0
	bl	put_name
	tbnz	x0,63,.inval
	mov	x0,0
	b	get_common
.inval:	mov	x0,BUS_EINVAL
	ldp	x19,x20,[sp],16
	epilogue

; x0 = sujet, x1 = tampon, x2 = taille -> octets / BUS_E*
bus_get_id:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	cbnz	x0,get_common
	mov	x0,BUS_ENOENT			; (0 : le nom, pas ici)
	ldp	x19,x20,[sp],16
	epilogue

; (suite de bus_get et bus_get_id : x0 = sujet ou 0, x19 = tampon,
; x20 = taille ; cadre et x19-x20 deja sauves)
get_common:
	str	x21,[sp,-16]!
	mov	x1,x0
	mov	x0,BUS_OP_GET
	mov	x2,0
	bl	svc_request
	tbnz	x0,63,.out
	mov	x21,x0
	mov	x2,x0				; copie : jusqu'a la taille
	cmp	x2,x20
	csel	x2,x2,x20,lo
	mov	x0,x19
	adr	x1,b_msg + BUS_DATA
	bl	copy_bytes
	mov	x0,x21
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = sujet, x1 = tampon BUS_NAME_MAX -> 0 / BUS_ENOENT
bus_name:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cbz	x19,.noent
	sub	x0,x19,1
	bl	bus_list
	cmp	x0,x19
	b.ne	.noent
	mov	x0,0
	b	.out
.noent:	strb	wzr,[x20]
	mov	x0,BUS_ENOENT
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = sujet de depart (0 : le premier), x1 = tampon BUS_NAME_MAX -> sujet
; suivant (0 : fin, ou busd absent)
bus_list:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x1,x0
	mov	x0,BUS_OP_LIST
	mov	x2,0
	bl	svc_request
	cmp	x0,0
	b.le	.none
	mov	x20,x0
	mov	x0,x19
	adr	x1,b_msg + BUS_DATA
	mov	x2,BUS_NAME_MAX
	bl	copy_bytes
	mov	x0,x20
	b	.out
.none:	strb	wzr,[x19]
	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = evenement 32 o, x1 = attente ms -> type (0 : rien) : la file
; d'evenements du programme (comme ui_event, sans UIKit)
bus_wait:
	prologue
	sys	SYS_EV_WAIT
	epilogue

s_bus		db BUS_SVC_NAME,0
		align 8

segment readable writeable
		align 8
b_chan		dd 0
b_msg		rb 512
