; vncd - l'ecran a distance (docs/RESEAU.md 5.1 et 5.4, etape 11), port 5900,
; lance par init2 (SD0:/etc/services.txt). RFB 3.3 / 3.7 / 3.8 (RFC 6143),
; securite "None" (pas de mot de passe : decision de l'utilisateur), un
; client a la fois ; tout client VNC, ou vncdotool (Python) pour les scripts.
;  * image : l'ecran compose d'Elegant (op WS_OP_SNAPSHOT : seules les zones
;    changees depuis la derniere mise a jour), codage Raw, dans le format de
;    pixels du client (8, 16 ou 32 bits, n'importe quels decalages) ;
;  * entrees : pilote virtuel de HIDManager (op HID_OP_PILOTE, comme le
;    GT911) : pointeur absolu, boutons, molette ; touches : keysyms X11 ->
;    caractere deja traduit (la disposition du PC est appliquee) + code HID
;    pour les lettres, chiffres et touches speciales.
; Pas de CopyRect pour l'instant (Raw seulement).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/netkit/netkit.inc'

PORT		= 5900
HID_OP_PILOTE	= 3
ANNEAU_EV	= 64
ANNEAU_N	= 64
EV_KEY		= 1
EV_MOUSE_ABS	= 3
EV_WHEEL	= 4
IN_MAX		= 4096
OUT_MAX		= 49152			; envoi par morceaux

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/netkit/netkit.abi'			; SD0:/lib/netkit.so

start:
	bl	kits_open
	movl	x0,OUT_MAX + 4096
	bl	malloc
	cbz	x0,quit
	adr	x1,outbuf
	str	x0,[x1]
	adr	x0,s_hid			; pilote virtuel de HIDManager
	movl	x1,10000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.sock
	adr	x1,msg
	mov	x2,HID_OP_PILOTE
	str	x2,[x1]
	str	wzr,[x1,WM_LEN]
	sys	SYS_SVC_CALL
	tbnz	x0,63,.sock
	sys	SYS_BUF_MAP
	tbnz	x0,63,.sock
	adr	x1,anneau
	str	x0,[x1]
.sock:	mov	x0,SOCK_TCP
	bl	net_socket
	tbnz	x0,63,.attente
	mov	x19,x0
	mov	x1,PORT
	bl	net_bind
	tbnz	x0,63,.ferme
	mov	x0,x19
	mov	x1,1
	bl	net_listen
	tbnz	x0,63,.ferme
	adr	x0,m_pret
	bl	puts
.loop:	mov	x0,x19
	movl	x1,3600000
	bl	net_accept
	cmn	x0,11
	b.eq	.loop
	tbnz	x0,63,.erreur
	mov	x20,x0
	adr	x0,m_client
	bl	puts
	mov	x0,x20
	bl	client
	mov	x0,x20
	bl	net_close
	adr	x0,m_fin
	bl	puts
	b	.loop
.erreur:
	cmn	x0,32				; netd relance : tout reprendre
	b.ne	.loop
.ferme:	mov	x0,x19
	bl	net_close
.attente:
	mov	x0,2000
	sys	SYS_SLEEP
	b	.sock
quit:	mov	x0,1
	sys	SYS_EXIT

; x0 = socket, x1 = donnees, x2 = octets -> x0 < 0 si echec
envoi:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
.e:	cbz	x21,.ok
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	net_send
	cmp	x0,0
	b.le	.ko
	add	x20,x20,x0
	sub	x21,x21,x0
	b	.e
.ok:	mov	x0,0
	b	.out
.ko:	mov	x0,-1
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = socket, x1 = tampon, x2 = octets : tous (15 s au plus) -> x0 < 0
recu_tout:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
.r:	cbz	x21,.ok
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	movl	x3,15000
	bl	net_recv
	cmp	x0,0
	b.le	.ko
	add	x20,x20,x0
	sub	x21,x21,x0
	b	.r
.ok:	mov	x0,0
	b	.out
.ko:	mov	x0,-1
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Un client : x0 = socket (x19 dans tout ce qui suit)
; ---------------------------------------------------------------------------
client:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	adr	x0,ecran			; taille de l'ecran
	sys	SYS_SCREEN_INFO
	mov	x0,x19				; version
	adr	x1,s_version
	mov	x2,12
	bl	envoi
	tbnz	x0,63,.out
	mov	x0,x19
	adr	x1,tmp
	mov	x2,12
	bl	recu_tout
	tbnz	x0,63,.out
	adr	x0,tmp				; "RFB 003.00x" : x = 3, 7 ou 8
	ldrb	w20,[x0,10]
	sub	w20,w20,'0'
	cmp	w20,7
	b.lo	.v33
	adr	x1,tmp				; 3.7, 3.8 : la liste {None}
	mov	w2,1
	strb	w2,[x1]
	strb	w2,[x1,1]
	mov	x0,x19
	mov	x2,2
	bl	envoi
	tbnz	x0,63,.out
	mov	x0,x19
	adr	x1,tmp
	mov	x2,1
	bl	recu_tout
	tbnz	x0,63,.out
	cmp	w20,8				; 3.8 : resultat 0
	b.lo	.init
	adr	x1,tmp
	str	wzr,[x1]
	mov	x0,x19
	mov	x2,4
	bl	envoi
	tbnz	x0,63,.out
	b	.init
.v33:	adr	x1,tmp				; 3.3 : le type (1 : None), gros-boutiste
	mov	w2,0x01000000
	str	w2,[x1]
	mov	x0,x19
	mov	x2,4
	bl	envoi
	tbnz	x0,63,.out
.init:	mov	x0,x19				; ClientInit (partage) : ignore
	adr	x1,tmp
	mov	x2,1
	bl	recu_tout
	tbnz	x0,63,.out
	adr	x21,tmp				; ServerInit
	adr	x0,ecran
	ldp	x1,x2,[x0]
	rev16	w1,w1
	rev16	w2,w2
	strh	w1,[x21]
	strh	w2,[x21,2]
	add	x0,x21,4			; format de pixels du serveur
	adr	x1,pf_serveur
	ldp	x2,x3,[x1]
	stp	x2,x3,[x0]
	mov	w0,0x05000000			; nom : "Korin" (5, gros-boutiste)
	str	w0,[x21,20]
	adr	x1,s_nom
	ldr	w0,[x1]
	str	w0,[x21,24]
	ldrb	w0,[x1,4]
	strb	w0,[x21,28]
	mov	x0,x19
	mov	x1,x21
	mov	x2,29
	bl	envoi
	tbnz	x0,63,.out
	adr	x0,pf_serveur			; le client garde ce format au depart
	bl	pf_regler
	adr	x0,gen				; tout l'ecran a la premiere demande
	str	xzr,[x0]
	adr	x0,demande
	str	xzr,[x0]
	adr	x0,in_n
	str	xzr,[x0]
	adr	x0,boutons
	str	xzr,[x0]
	adr	x0,mods
	str	xzr,[x0]
.loop:	adr	x0,demande			; une demande en attente : 40 ms
	ldr	x0,[x0]
	movl	x3,1000
	cmp	x0,0
	mov	x1,40
	csel	x3,x1,x3,ne
	adr	x1,inbuf
	adr	x2,in_n
	ldr	x2,[x2]
	add	x1,x1,x2
	mov	x0,IN_MAX
	sub	x2,x0,x2
	mov	x0,x19
	bl	net_recv
	cmn	x0,11
	b.eq	.maj
	cmp	x0,0
	b.le	.out
	adr	x1,in_n
	ldr	x2,[x1]
	add	x2,x2,x0
	str	x2,[x1]
	bl	messages
	tbnz	x0,63,.out
.maj:	adr	x0,demande
	ldr	x0,[x0]
	cbz	x0,.loop
	bl	mise_a_jour
	tbnz	x0,63,.out
	b	.loop
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; inbuf : messages complets du client -> x0 < 0 si le client est fautif
messages:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
.m:	adr	x20,inbuf
	adr	x0,in_n
	ldr	x21,[x0]
	cbz	x21,.ok
	ldrb	w0,[x20]
	cmp	w0,0
	b.eq	.pf
	cmp	w0,2
	b.eq	.enc
	cmp	w0,3
	b.eq	.req
	cmp	w0,4
	b.eq	.key
	cmp	w0,5
	b.eq	.ptr
	cmp	w0,6
	b.eq	.cut
	b	.ko
.pf:	mov	x22,20				; SetPixelFormat
	cmp	x21,x22
	b.lo	.ok
	add	x0,x20,4
	bl	pf_regler
	b	.conso
.enc:	cmp	x21,4				; SetEncodings : Raw toujours
	b.lo	.ok
	ldrh	w0,[x20,2]
	rev16	w0,w0
	lsl	x22,x0,2
	add	x22,x22,4
	cmp	x21,x22
	b.lo	.ok
	b	.conso
.req:	mov	x22,10				; FramebufferUpdateRequest
	cmp	x21,x22
	b.lo	.ok
	ldrb	w0,[x20,1]			; non incremental : tout l'ecran
	cbnz	w0,.inc
	adr	x0,gen
	str	xzr,[x0]
.inc:	adr	x0,demande
	mov	x1,1
	str	x1,[x0]
	b	.conso
.key:	mov	x22,8				; KeyEvent
	cmp	x21,x22
	b.lo	.ok
	ldrb	w0,[x20,1]
	ldr	w1,[x20,4]
	rev	w1,w1
	bl	touche
	b	.conso
.ptr:	mov	x22,6				; PointerEvent
	cmp	x21,x22
	b.lo	.ok
	ldrb	w0,[x20,1]
	ldrh	w1,[x20,2]
	rev16	w1,w1
	ldrh	w2,[x20,4]
	rev16	w2,w2
	bl	pointeur
	b	.conso
.cut:	cmp	x21,8				; ClientCutText : ignore
	b.lo	.ok
	ldr	w0,[x20,4]
	rev	w0,w0
	add	x22,x0,8
	cmp	x22,IN_MAX
	b.hi	.ko
	cmp	x21,x22
	b.lo	.ok
.conso:	sub	x2,x21,x22			; message consomme
	adr	x0,in_n
	str	x2,[x0]
	mov	x0,x20
	add	x1,x20,x22
	bl	mem_move
	b	.m
.ok:	mov	x0,0
	b	.out
.ko:	mov	x0,-1
.out:	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; x0 = format de pixels RFB (16 o) : tables de conversion du client
pf_regler:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x1,pf_client
	ldp	x2,x3,[x19]
	stp	x2,x3,[x1]
	ldrb	w0,[x19]			; octets par pixel
	lsr	w0,w0,3
	cmp	w0,1
	b.eq	.b
	cmp	w0,2
	b.eq	.b
	mov	w0,4
.b:	adr	x1,pf_octets
	str	x0,[x1]
	ldrb	w0,[x19,2]
	adr	x1,pf_gros
	str	x0,[x1]
	; direct : 32 bits, petit-boutiste, 255/255/255, decalages 16/8/0
	mov	x20,0
	ldrb	w0,[x19]
	cmp	w0,32
	b.ne	.tab
	ldrb	w0,[x19,2]
	cbnz	w0,.tab
	ldrh	w0,[x19,4]
	ldrh	w1,[x19,6]
	ldrh	w2,[x19,8]
	movl	w3,0xFF00
	cmp	w0,w3
	b.ne	.tab
	cmp	w1,w3
	b.ne	.tab
	cmp	w2,w3
	b.ne	.tab
	ldrb	w0,[x19,10]
	cmp	w0,16
	b.ne	.tab
	ldrb	w0,[x19,11]
	cmp	w0,8
	b.ne	.tab
	ldrb	w0,[x19,12]
	cbnz	w0,.tab
	mov	x20,1
.tab:	adr	x0,pf_direct
	str	x20,[x0]
	adr	x20,tab_r			; une table par composante
	mov	x21,0
.t:	cmp	x21,3
	b.hs	.out
	lsl	x0,x21,1			; max (gros-boutiste), decalage
	add	x0,x0,4
	ldrh	w1,[x19,x0]
	rev16	w1,w1
	add	x0,x21,10
	ldrb	w2,[x19,x0]
	mov	x3,0
.v:	mul	x4,x3,x1			; (v * max + 127) / 255 << decalage
	add	x4,x4,127
	mov	x5,255
	udiv	x4,x4,x5
	lsl	x4,x4,x2
	str	w4,[x20,x3,lsl 2]
	add	x3,x3,1
	cmp	x3,256
	b.lo	.v
	add	x20,x20,1024
	add	x21,x21,1
	b	.t
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = appui (1) / relache (0), w1 = keysym X11 : une touche a HIDManager
touche:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	w20,w1
	adr	x2,keysyms			; touches speciales (keysym, code HID,
.s:	ldr	w3,[x2]				; bit de modificateur)
	cbz	w3,.texte
	cmp	w3,w20
	b.eq	.special
	add	x2,x2,12
	b	.s
.special:
	ldr	w21,[x2,4]			; code HID
	ldr	w3,[x2,8]			; modificateur
	cbz	w3,.sans_car
	adr	x4,mods
	ldr	x5,[x4]
	bic	x6,x5,x3
	orr	x7,x5,x3
	cmp	x19,0
	csel	x5,x6,x7,eq
	str	x5,[x4]
.sans_car:
	mov	x22,0				; caractere : calcule par HIDManager
	b	.pousse
.texte:	mov	w22,w20				; un caractere (Latin-1, ou Unicode
	movl	w3,0x01000000			; 0x01xxxxxx)
	cmp	w20,w3
	b.lo	.l1
	and	w22,w20,0xFFFFFF
	b	.code
.l1:	cmp	w20,0xFF
	b.hi	.out				; (autre keysym : ignore)
.code:	mov	w21,0				; code HID : lettres, chiffres, espace
	orr	w3,w22,0x20
	sub	w4,w3,'a'
	cmp	w4,25
	b.hi	.chiffre
	cmp	w22,'z'
	b.hi	.chiffre
	add	w21,w4,0x04
	b	.marque
.chiffre:
	sub	w4,w22,'1'
	cmp	w4,8
	b.hi	.zero
	add	w21,w4,0x1E
	b	.marque
.zero:	cmp	w22,'0'
	b.ne	.espace
	mov	w21,0x27
	b	.marque
.espace:
	cmp	w22,' '
	b.ne	.marque
	mov	w21,0x2C
.marque:
	mov	x3,1				; caractere deja traduit (bit 31)
	lsl	x3,x3,31
	b	.p2
.pousse:
	mov	x3,0
.p2:	adr	x4,mods
	ldr	x4,[x4]
	orr	x3,x3,x4
	mov	x0,EV_KEY
	orr	x0,x0,x22,lsl 32		; caractere en +4
	mov	x1,x21
	mov	x2,x19
	bl	anneau_pousser
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = boutons RFB, w1 = x, w2 = y : pointeur absolu, boutons, molette
pointeur:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	w19,w0
	adr	x3,boutons			; molette : bits 3 (haut) et 4 (bas),
	ldr	x20,[x3]			; a l'appui
	str	x19,[x3]
	bic	x21,x19,x20			; nouveaux appuis
	stp	x1,x2,[sp,-16]!
	tbz	x21,3,.bas
	mov	x0,EV_WHEEL
	mov	x1,1
	mov	x2,0
	mov	x3,0
	bl	anneau_pousser
.bas:	tbz	x21,4,.pos
	mov	x0,EV_WHEEL
	mov	x1,-1
	mov	x2,0
	mov	x3,0
	bl	anneau_pousser
.pos:	ldp	x1,x2,[sp],16
	adr	x3,ecran			; 0..32767
	ldp	x4,x5,[x3]
	sub	x4,x4,1
	sub	x5,x5,1
	movl	x6,32767
	mul	x1,x1,x6
	udiv	x1,x1,x4
	cmp	x1,x6
	csel	x1,x6,x1,hi
	mul	x2,x2,x6
	udiv	x2,x2,x5
	cmp	x2,x6
	csel	x2,x6,x2,hi
	and	x3,x19,1			; boutons : gauche, droit (RFB 4),
	tbz	x19,2,.m			; milieu (RFB 2)
	orr	x3,x3,2
.m:	tbz	x19,1,.go
	orr	x3,x3,4
.go:	mov	x0,EV_MOUSE_ABS
	bl	anneau_pousser
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = type, x1 = a, x2 = b, x3 = c : un evenement dans l'anneau de
; HIDManager (perdu s'il est plein)
anneau_pousser:
	adr	x9,anneau
	ldr	x9,[x9]
	cbz	x9,.plein
	ldp	x10,x11,[x9]			; tete (ici), queue (HIDManager)
	sub	x12,x10,x11
	cmp	x12,ANNEAU_N
	b.hs	.plein
	and	x12,x10,ANNEAU_N-1
	add	x12,x9,x12,lsl 5
	add	x12,x12,ANNEAU_EV
	stp	x0,x1,[x12]
	stp	x2,x3,[x12,16]
	dmb	ish				; l'evenement avant la tete
	add	x10,x10,1
	str	x10,[x9]
.plein:	ret

; Une demande en attente : l'ecran change depuis "gen" (Elegant), envoye
; en Raw dans le format du client -> x0 < 0 si le client est parti
mise_a_jour:
	prologue
	stp	x20,x21,[sp,-16]!
	stp	x22,x23,[sp,-16]!
	stp	x24,x25,[sp,-16]!
	adr	x0,msg
	mov	x1,WS_OP_SNAPSHOT
	str	x1,[x0,WM_OP]
	adr	x1,gen
	ldr	x1,[x1]
	str	x1,[x0,WM_B]
	str	wzr,[x0,WM_LEN]
	sys	SYS_WS_CALL
	tbnz	x0,63,.rien			; (Elegant absent : plus tard)
	cbz	x0,.rien			; rien de neuf
	adr	x9,msg
	ldr	x0,[x9,WM_C]			; generation vue
	adr	x1,gen
	str	x0,[x1]
	adr	x1,ecran_num			; le tampon d'Elegant (refait si l'ecran
	ldr	x2,[x1]				; change de taille)
	ldr	x0,[x9,WM_D]
	str	x0,[x1]
	adr	x1,ecran_buf
	ldr	x3,[x1]
	cbz	x3,.map
	cmp	x0,x2
	b.eq	.zone
	str	xzr,[x1]
	mov	x0,x2
	sys	SYS_BUF_FREE
	adr	x9,msg
	ldr	x0,[x9,WM_D]
.map:	sys	SYS_BUF_MAP
	tbnz	x0,63,.rien
	adr	x1,ecran_buf
	str	x0,[x1]
.zone:	adr	x9,msg
	ldp	x0,x1,[x9,WM_A]			; autre taille : RFB ne la dit pas a un
	adr	x2,ecran			; client ordinaire, il se reconnecte
	ldp	x2,x3,[x2]
	cmp	x0,x2
	ccmp	x1,x3,0,eq
	b.ne	.ko
	ldp	x20,x21,[x9,WM_DATA]		; x0, y0
	ldp	x22,x23,[x9,WM_DATA+16]		; x1, y1
	sub	x22,x22,x20			; largeur, hauteur
	sub	x23,x23,x21
	cmp	x22,0
	b.le	.rien
	cmp	x23,0
	b.le	.rien
	adr	x0,demande
	str	xzr,[x0]
	adr	x24,outbuf			; en-tete : FramebufferUpdate, 1 rectangle
	ldr	x24,[x24]
	mov	w0,0
	strb	w0,[x24]
	strb	w0,[x24,1]
	mov	w0,0x0100			; 1 (gros-boutiste)
	strh	w0,[x24,2]
	rev16	w0,w20
	strh	w0,[x24,4]
	rev16	w0,w21
	strh	w0,[x24,6]
	rev16	w0,w22
	strh	w0,[x24,8]
	rev16	w0,w23
	strh	w0,[x24,10]
	str	wzr,[x24,12]			; codage 0 : Raw
	mov	x25,16				; octets dans outbuf
.ligne:	cbz	x23,.envoi
	adr	x0,ecran_buf			; source : ligne y, a partir de x
	ldr	x0,[x0]
	adr	x1,ecran
	ldr	x1,[x1]
	madd	x0,x21,x1,x20
	adr	x1,ecran_buf
	ldr	x1,[x1]
	add	x0,x1,x0,lsl 2
	add	x2,x24,x25
	mov	x1,x22
	bl	convertir			; -> x0 = fin
	sub	x25,x0,x24
	add	x21,x21,1
	sub	x23,x23,1
	adr	x0,pf_octets			; place pour une ligne de plus ?
	ldr	x0,[x0]
	mul	x0,x0,x22
	add	x0,x0,x25
	movl	x1,OUT_MAX
	cmp	x0,x1
	b.ls	.ligne
	mov	x0,x19
	mov	x1,x24
	mov	x2,x25
	bl	envoi
	tbnz	x0,63,.ko
	mov	x25,0
	b	.ligne
.envoi:	cbz	x25,.ok
	mov	x0,x19
	mov	x1,x24
	mov	x2,x25
	bl	envoi
	tbnz	x0,63,.ko
.ok:	mov	x0,0
	b	.out
.rien:	mov	x0,0
	b	.out
.ko:	mov	x0,-1
.out:	ldp	x24,x25,[sp],16
	ldp	x22,x23,[sp],16
	ldp	x20,x21,[sp],16
	epilogue

; x0 = pixels 0x00RRGGBB, x1 = nombre, x2 = destination -> x0 = fin
convertir:
	adr	x3,pf_direct
	ldr	x3,[x3]
	cbz	x3,.tab
	lsl	x4,x1,2				; direct : copie
.d:	cbz	x4,.dfin
	ldr	w5,[x0],4
	str	w5,[x2],4
	sub	x4,x4,4
	b	.d
.dfin:	mov	x0,x2
	ret
.tab:	adr	x3,tab_r
	adr	x9,pf_octets
	ldr	x9,[x9]
	adr	x10,pf_gros
	ldr	x10,[x10]
.p:	cbz	x1,.fin
	ldr	w5,[x0],4
	ubfx	w6,w5,16,8
	ldr	w6,[x3,x6,lsl 2]
	ubfx	w7,w5,8,8
	add	x8,x3,1024
	ldr	w7,[x8,x7,lsl 2]
	orr	w6,w6,w7
	and	w7,w5,0xFF
	add	x8,x3,2048
	ldr	w7,[x8,x7,lsl 2]
	orr	w6,w6,w7
	cmp	x9,1
	b.eq	.o1
	cmp	x9,2
	b.eq	.o2
	cbz	x10,.o4
	rev	w6,w6
.o4:	str	w6,[x2],4
	b	.n
.o2:	cbz	x10,.o2l
	rev16	w6,w6
.o2l:	strh	w6,[x2],2
	b	.n
.o1:	strb	w6,[x2],1
.n:	sub	x1,x1,1
	b	.p
.fin:	mov	x0,x2
	ret

s_hid		db 'hid',0
s_version	db 'RFB 003.008',10
s_nom		db 'Korin',0,0,0
m_pret		db 'vncd : en ',0xC3,0xA9,'coute sur le port 5900 (sans mot de passe)',10,0
m_client	db 'vncd : client',10,0
m_fin		db 'vncd : client parti',10,0
		align 8
; format du serveur : 32 bits, profondeur 24, petit-boutiste, vraies
; couleurs, maxima 255 (gros-boutistes), decalages 16, 8, 0
pf_serveur	db 32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 16, 8, 0, 0, 0, 0
		align 4
; touches speciales : keysym, code HID, bit de modificateur (octet HID)
keysyms:
	dw	0xFF08, 0x2A, 0			; BackSpace
	dw	0xFF09, 0x2B, 0			; Tab
	dw	0xFF0D, 0x28, 0			; Return
	dw	0xFF8D, 0x58, 0			; KP_Enter
	dw	0xFF1B, 0x29, 0			; Escape
	dw	0xFF50, 0x4A, 0			; Home
	dw	0xFF51, 0x50, 0			; Left
	dw	0xFF52, 0x52, 0			; Up
	dw	0xFF53, 0x4F, 0			; Right
	dw	0xFF54, 0x51, 0			; Down
	dw	0xFF55, 0x4B, 0			; Page_Up
	dw	0xFF56, 0x4E, 0			; Page_Down
	dw	0xFF57, 0x4D, 0			; End
	dw	0xFF63, 0x49, 0			; Insert
	dw	0xFFFF, 0x4C, 0			; Delete
	dw	0xFFBE, 0x3A, 0			; F1..F12
	dw	0xFFBF, 0x3B, 0
	dw	0xFFC0, 0x3C, 0
	dw	0xFFC1, 0x3D, 0
	dw	0xFFC2, 0x3E, 0
	dw	0xFFC3, 0x3F, 0
	dw	0xFFC4, 0x40, 0
	dw	0xFFC5, 0x41, 0
	dw	0xFFC6, 0x42, 0
	dw	0xFFC7, 0x43, 0
	dw	0xFFC8, 0x44, 0
	dw	0xFFC9, 0x45, 0
	dw	0xFFE3, 0xE0, 0x01		; Control_L
	dw	0xFFE1, 0xE1, 0x02		; Shift_L
	dw	0xFFE9, 0xE2, 0x04		; Alt_L
	dw	0xFFE7, 0xE3, 0x08		; Meta_L
	dw	0xFFEB, 0xE3, 0x08		; Super_L
	dw	0xFFE4, 0xE4, 0x10		; Control_R
	dw	0xFFE2, 0xE5, 0x20		; Shift_R
	dw	0xFFEA, 0xE6, 0x40		; Alt_R
	dw	0xFE03, 0xE6, 0x40		; ISO_Level3_Shift (AltGr)
	dw	0
		align 8

segment readable writeable
kit_data
		align 8
anneau		dd 0				; anneau de HIDManager
outbuf		dd 0
ecran		dd 0, 0, 0			; largeur, hauteur, barre (SCREEN_INFO)
ecran_buf	dd 0				; tampon de l'ecran (Elegant), son numero
ecran_num	dd 0
gen		dd 0				; generation deja envoyee
demande		dd 0				; une demande de mise a jour attend
in_n		dd 0
boutons		dd 0
mods		dd 0
pf_octets	dd 4
pf_gros		dd 0
pf_direct	dd 1
pf_client	rb 16
tmp		rb 64
msg		rb WS_MSG
inbuf		rb IN_MAX
		align 8
tab_r		rb 3 * 1024			; tables : rouge, vert, bleu
