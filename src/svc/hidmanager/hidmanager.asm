; hidmanager - point d'arrivee de toutes les entrees (docs/FEUILLE-DE-ROUTE.md,
; section 2), lance par init2 avant Elegant.
;  * il lit l'anneau partage de chaque pilote (gt911, usbkeyboard,
;    usbmouse...) : 10 ms au plus d'attente ;
;  * il applique la disposition du clavier : integree (FR), « keyb= » de
;    SD0:/etc/options.txt, ou SD0:/res/keyb/<NOM>.txt (op HID_OP_KEYMAP) ;
;  * il repete la touche tenue ;
;  * il depose les evenements dans un anneau partage par abonne (Elegant) :
;    l'abonne les lit sans appel systeme.
; Service « hid » :
;  HID_OP_ABONNER (1) -> numero d'un tampon partage (mappe chez l'appelant par
;    buf_map) : +0 tete (ecrite par hidmanager), +8 queue (ecrite par
;    l'abonne), +ANNEAU_EV : ANNEAU_N evenements de 32 octets (format
;    d'input.asm, le caractere de la disposition en +4 pour une touche)
;  HID_OP_KEYMAP (2), donnees = nom (« FR », « US »...) -> 0 ou -2
;  HID_OP_PILOTE (3) -> numero d'un tampon partage, meme anneau, mais c'est
;    le pilote qui ecrit (la tete) et hidmanager qui lit (la queue)
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/pad.inc'

HID_OP_ABONNER	= 1
HID_OP_KEYMAP	= 2
HID_OP_PILOTE	= 3
ABONNES_MAX	= 4
PILOTES_MAX	= 16			; pilotes en services (tactile, claviers, souris...)
AB_PID		= 0
AB_BUF		= 8
AB_ADR		= 16
AB_TAILLE	= 24
ANNEAU_EV	= 64
ANNEAU_N	= 64			; puissance de 2
EV_KEY		= 1
REP_DELAI	= 400			; touche maintenue : premiere repetition (ms)
REP_PAS		= 35			; puis toutes les REP_PAS ms
E_NOENT		= -2
E_INVAL		= -22
E_NOMEM		= -12

KEY_MAP_N	= 0x65			; codes HID 0x00..0x64
KB_COLS		= 3
KB_TABLE	= KEY_MAP_N * 2		; octets d'une colonne

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	bl	kits_open
	mov	x0,1				; disposition : FR integre, ou keyb=
	bl	kb_use_builtin
	adr	x0,p_options
	adr	x1,option_ligne
	bl	ini_read
	adr	x0,s_nom
	mov	x1,SVF_FINS			; (abonnes qui s'arretent)
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	adr	x1,canal
	str	x0,[x1]
	bl	pad_load			; le mapping (pad.inc)
	mov	x0,16384			; le lecteur des entrees : un thread
	bl	malloc
	cbz	x0,quit
	add	x1,x0,16384
	adr	x0,lecteur
	mov	x2,0
	sys	SYS_THREAD_CREATE
	adr	x0,m_pret
	bl	puts
.loop:	adr	x0,canal
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
	cmp	x0,HID_OP_ABONNER
	b.eq	.abonner
	cmp	x0,HID_OP_KEYMAP
	b.eq	.keymap
	cmp	x0,HID_OP_PILOTE
	b.eq	.pilote
	cmp	x0,HID_OP_PADMAP
	b.eq	.pad
	mov	x0,E_INVAL
	b	.rep
.abonner:
	ldr	w0,[x20,44]			; pid de l'appelant
	adr	x1,abonnes
	bl	abonner
	b	.rep
.pad:	bl	pad_load
	mov	x0,0
	b	.rep
.pilote:
	ldr	w0,[x20,44]
	adr	x1,pilotes
	bl	abonner
	b	.rep
.keymap:
	strb	wzr,[x20,48+15]			; nom : 15 caracteres au plus
	add	x0,x20,48
	bl	kb_load
	cmp	x0,0
	mov	x0,E_NOENT
	csel	x0,xzr,x0,ne
	b	.rep
.fin:	ldr	x0,[x20,8]			; (avis) : un abonne de moins
	bl	desabonner
	b	.loop
.rep:	str	x0,[x20]
	mov	x0,x19
	mov	x1,x20
	sys	SYS_SVC_REPLY
	b	.loop
quit:	mov	x0,1				; (sur une erreur : init2 relance)
	sys	SYS_EXIT

; (ini_read de options.txt) x0 = cle, x1 = valeur : « keyb = NOM »
option_ligne:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	adr	x1,k_keyb
	bl	str_eq
	cbz	x0,.out
	mov	x0,x19
	bl	kb_load
.out:	ldr	x19,[sp],16
	epilogue

; x0 = pid, x1 = table (abonnes ou pilotes) -> x0 = numero du tampon
; partage (anneau), ou erreur
abonner:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x0
	mov	x19,x1
	str	x1,[sp,-16]!
	bl	desabonner			; (un seul anneau par process)
	ldr	x19,[sp],16			; une place libre
	mov	x20,0
	adr	x0,pilotes			; (taille de la table)
	cmp	x19,x0
	mov	x22,ABONNES_MAX
	mov	x0,PILOTES_MAX
	csel	x22,x0,x22,eq
.s:	cmp	x20,x22
	b.hs	.plein
	ldr	x0,[x19,AB_PID]
	cbz	x0,.place
	add	x19,x19,AB_TAILLE
	add	x20,x20,1
	b	.s
.place:	mov	x0,4096
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.out
	mov	x22,x0
	mov	x1,x21
	sys	SYS_BUF_GRANT
	tbnz	x0,63,.out
	mov	x0,x22
	sys	SYS_BUF_MAP
	tbnz	x0,63,.out
	stp	xzr,xzr,[x0]			; tete, queue
	str	x0,[x19,AB_ADR]
	str	x22,[x19,AB_BUF]
	dmb	ish
	str	x21,[x19,AB_PID]		; (le lecteur le voit apres l'adresse)
	mov	x0,x22
	b	.out
.plein:	mov	x0,E_NOMEM
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid : ses anneaux sont rendus (abonne et pilote)
desabonner:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x2,x0
	adr	x19,abonnes
	mov	x20,0
	mov	x3,ABONNES_MAX+PILOTES_MAX		; (pilotes : juste apres)
.s:	cmp	x20,x3
	b.hs	.out
	ldr	x0,[x19,AB_PID]
	cmp	x0,x2
	b.ne	.n
	str	xzr,[x19,AB_PID]
	dmb	ish
	ldr	x0,[x19,AB_BUF]
	stp	x2,x3,[sp,-16]!
	sys	SYS_BUF_FREE
	ldp	x2,x3,[sp],16
.n:	add	x19,x19,AB_TAILLE
	add	x20,x20,1
	b	.s
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Le lecteur des entrees (thread)
; ---------------------------------------------------------------------------

lecteur:
	bl	lire_pilotes
	adr	x0,rep_on			; attente : courte si une touche
	ldr	x2,[x0]				; est tenue, ou s'il y a des pilotes
	adr	x0,pilotes_n			; en services (leurs anneaux)
	ldr	x0,[x0]
	orr	x2,x2,x0
	cmp	x2,0
	mov	x2,50
	mov	x3,10
	csel	x2,x3,x2,ne
	mov	x0,x2				; (les pilotes ecrivent dans leurs anneaux)
	sys	SYS_SLEEP
.rep:	bl	repeter
	b	lecteur

; les anneaux des pilotes en services : chaque evenement comme une entree
; brute du noyau -> pilotes_n = nombre de pilotes
lire_pilotes:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,pilotes
	mov	x20,0
	mov	x22,0				; pilotes vus
.s:	cmp	x20,PILOTES_MAX
	b.hs	.out
	ldr	x0,[x19,AB_PID]
	cbz	x0,.n
	add	x22,x22,1
	dmb	ish
	ldr	x21,[x19,AB_ADR]
.e:	ldp	x0,x1,[x21]			; tete (pilote), queue (ici)
	cmp	x0,x1
	b.eq	.n
	dmb	ish				; l'evenement apres la tete
	and	x1,x1,ANNEAU_N-1
	add	x1,x21,x1,lsl 5
	add	x1,x1,ANNEAU_EV
	adr	x0,ev_pilote			; copie (le pilote peut reecrire)
	ldp	x2,x3,[x1]
	stp	x2,x3,[x0]
	ldp	x2,x3,[x1,16]
	stp	x2,x3,[x0,16]
	dmb	ish
	ldr	x1,[x21,8]
	add	x1,x1,1
	str	x1,[x21,8]
	adr	x0,ev_pilote
	ldr	w1,[x0]
	cmp	w1,EV_PAD_RAW
	b.ne	.touche
	mov	x1,x20				; (la manette : le rang du pilote)
	bl	pad_raw
	b	.e
.touche:
	bl	traduire
	adr	x0,ev_pilote
	bl	pad_touche
	adr	x0,ev_pilote
	bl	tenir
	adr	x0,ev_pilote
	bl	diffuser
	b	.e
.n:	add	x19,x19,AB_TAILLE
	add	x20,x20,1
	b	.s
.out:	adr	x0,pilotes_n
	str	x22,[x0]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = EV_PAD_RAW, x1 = rang du pilote : le mapping du modele, puis EV_PAD
pad_raw:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	and	x1,x1,1
	add	x20,x1,1			; manette 1 ou 2
	ldr	w0,[x19,24]			; vid, pid -> jeu de boutons
	adr	x1,pad_sets
	mov	x2,PADSET_MAX
	mov	x3,x1				; (le premier : [default])
.f:	ldr	w4,[x1]
	cbz	w4,.n
	cmp	w4,w0
	csel	x3,x1,x3,eq
.n:	add	x1,x1,PADSET_SIZE
	subs	x2,x2,1
	b.ne	.f
	ldr	w4,[x19,8]			; bruts
	mov	w5,0				; PAD_*
	mov	x6,0
.b:	add	x7,x3,x6
	ldrb	w7,[x7,4]
	cbz	w7,.bn
	sub	w7,w7,1
	lsr	w8,w4,w7
	tbz	w8,0,.bn
	mov	w8,1
	lsl	w8,w8,w6
	orr	w5,w5,w8
.bn:	add	x6,x6,1
	cmp	x6,PAD_NBUTTONS
	b.lo	.b
	lsr	w4,w4,28			; chapeau
	orr	w5,w5,w4
	ldrsh	w6,[x19,16]			; le stick gauche fait aussi la croix
	ldrsh	w7,[x19,18]
	cmn	w6,500
	b.ge	.g1
	orr	w5,w5,PAD_LEFT
.g1:	cmp	w6,500
	b.le	.g2
	orr	w5,w5,PAD_RIGHT
.g2:	cmn	w7,500
	b.ge	.g3
	orr	w5,w5,PAD_UP
.g3:	cmp	w7,500
	b.le	.g4
	orr	w5,w5,PAD_DOWN
.g4:	ldr	x6,[x19,16]			; axes
	adr	x1,pad_btn
	ldr	w2,[x1,x20,lsl 2]
	adr	x3,pad_ax
	ldr	x4,[x3,x20,lsl 3]
	ldr	w7,[x19,8]			; bruts, modele
	ldr	w8,[x19,24]
	adr	x9,pad_id
	str	w8,[x9,x20,lsl 2]
	adr	x9,pad_rawb
	ldr	w8,[x9,x20,lsl 2]
	str	w7,[x9,x20,lsl 2]
	cmp	w8,w7
	b.ne	.ch
	cmp	w2,w5
	b.ne	.ch
	cmp	x4,x6
	b.eq	.out
.ch:	str	w5,[x1,x20,lsl 2]
	str	x6,[x3,x20,lsl 3]
	mov	x0,x20
	bl	pad_emit
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = evenement : une touche passe par le mapping ; si elle donne un bouton de
; manette, EV_PAD (manette 0). Elle reste une touche (feuille de route).
pad_touche:
	ldr	w1,[x0]
	cmp	w1,EV_KEY
	b.ne	.r
	prologue
	stp	x19,x20,[sp,-16]!
	ldr	x19,[x0,8]			; code HID
	ldr	x20,[x0,16]			; 1 appui, 0 relache
	mov	w3,0				; caractere, sans modificateur
	cmp	x19,0xE0
	b.hs	.m
	mov	x0,x19
	mov	x1,0
	bl	key_to_char
	orr	w3,w0,0x20
.m:	adr	x1,pad_keys
	adr	x2,pad_keys_n
	ldr	x2,[x2]
	mov	w0,0				; boutons
.l:	cbz	x2,.ap
	ldrb	w4,[x1]				; code HID (0 : par le caractere)
	ldrb	w5,[x1,1]
	ldr	w6,[x1,4]
	add	x1,x1,8
	sub	x2,x2,1
	cbz	w4,.c
	cmp	w4,w19
	b.ne	.l
	orr	w0,w0,w6
	b	.l
.c:	cmp	w5,w3
	b.ne	.l
	orr	w0,w0,w6
	b	.l
.ap:	cbz	w0,.out
	adr	x1,pad_btn
	ldr	w2,[x1]
	bic	w3,w2,w0
	orr	w4,w2,w0
	cmp	x20,0
	csel	w3,w4,w3,ne
	cmp	w3,w2
	b.eq	.out
	str	w3,[x1]
	mov	x0,0
	bl	pad_emit
.out:	ldp	x19,x20,[sp],16
	epilogue
.r:	ret

; x0 = manette : EV_PAD dans l'anneau des abonnes
pad_emit:
	stp	x29,x30,[sp,-16]!
	adr	x1,pad_btn
	ldr	w2,[x1,x0,lsl 2]
	adr	x1,pad_ax
	ldr	x3,[x1,x0,lsl 3]
	adr	x1,pad_rawb
	ldr	w5,[x1,x0,lsl 2]
	adr	x1,pad_id
	ldr	w6,[x1,x0,lsl 2]
	adr	x1,pad_ev
	mov	w4,EV_PAD
	stp	w4,w0,[x1]
	stp	w2,w5,[x1,8]
	stp	x3,x6,[x1,16]
	mov	x0,x1
	bl	diffuser
	ldp	x29,x30,[sp],16
	ret

; ---------------------------------------------------------------------------
;  Le mapping : SD0:/etc/gamepad.ini (cles d'Onyx, inc/pad.inc)
;	[keyboard]	a = z		bouton = touche : une lettre, un chiffre,
;					up down left right enter backspace space
;					tab esc prtsc pause, none
;	[default], [vvvv:pppp]	a = 3	bouton = numero du bouton brut (1 : le
;					premier), un modele par section
; ---------------------------------------------------------------------------

; x0 = texte, x1 = fin, x2 = liste de mots (0 final), mots de x3 octets ->
; x0 = rang, -1 : aucun
pad_mot:
	sub	x4,x1,x0
	mov	x5,0
.w:	ldrb	w6,[x2]
	cbz	w6,.non
	mov	x7,0
.c:	cmp	x7,x4
	b.hs	.fin
	ldrb	w8,[x0,x7]
	ldrb	w9,[x2,x7]
	cmp	w8,w9
	b.ne	.n
	add	x7,x7,1
	b	.c
.fin:	ldrb	w9,[x2,x7]
	cbnz	w9,.n
	mov	x0,x5
	ret
.n:	add	x2,x2,x3
	add	x5,x5,1
	b	.w
.non:	mov	x0,-1
	ret

; relit le mapping : les valeurs par defaut, puis le fichier
pad_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,pad_keys			; les touches par defaut
	adr	x1,pad_def_keys
	mov	x2,0
.d:	ldr	x3,[x1,x2,lsl 3]
	cbz	x3,.d1
	str	x3,[x0,x2,lsl 3]
	add	x2,x2,1
	b	.d
.d1:	adr	x0,pad_keys_n
	str	x2,[x0]
	adr	x0,pad_sets			; les boutons : [default]
	mov	x1,0
.z:	strb	wzr,[x0,x1]
	add	x1,x1,1
	cmp	x1,PADSET_MAX*PADSET_SIZE
	b.lo	.z
	adr	x1,pad_def_btn
	mov	x2,0
.db:	ldrb	w3,[x1,x2]
	add	x4,x0,x2
	strb	w3,[x4,4]
	add	x2,x2,1
	cmp	x2,PAD_NBUTTONS
	b.lo	.db
	adr	x0,p_padini
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,kb_fichier
	mov	x2,KB_FICHIER_MAX-1
	bl	f_lire
	mov	x20,x0
	mov	x0,x19
	bl	f_fermer
	cmp	x20,0
	b.le	.out
	adr	x19,kb_fichier
	add	x20,x19,x20			; fin
	strb	wzr,[x20]
	mov	x21,0				; 1 : [keyboard]
	mov	x22,0				; un jeu de boutons
	adr	x23,pad_sets			; le dernier pris
.line:	cmp	x19,x20
	b.hs	.out
	mov	x24,x19				; fin de ligne
.e:	cmp	x24,x20
	b.hs	.e1
	ldrb	w0,[x24]
	cmp	w0,10
	b.eq	.e1
	add	x24,x24,1
	b	.e
.e1:	mov	x0,x19
	mov	x1,x24
	add	x19,x24,1
	bl	pad_trim
	cmp	x0,x1
	b.hs	.line
	ldrb	w2,[x0]
	cmp	w2,'#'
	b.eq	.line
	cmp	w2,';'
	b.eq	.line
	cmp	w2,'['
	b.ne	.kv
	add	x0,x0,1
	sub	x1,x1,1				; (sans le ])
	mov	x21,0
	mov	x22,0
	adr	x2,pad_w_sect
	mov	x3,12
	stp	x0,x1,[sp,-16]!
	bl	pad_mot
	mov	x2,x0
	ldp	x0,x1,[sp],16
	cbz	x2,.kbd
	cmp	x2,1
	b.eq	.def
	bl	pad_hex2			; vvvv:pppp -> w0
	cbz	w0,.line
	add	x23,x23,PADSET_SIZE
	adr	x1,pad_sets
	add	x1,x1,PADSET_MAX*PADSET_SIZE
	cmp	x23,x1
	b.hs	.out
	str	w0,[x23]
	mov	x22,x23
	b	.line
.kbd:	mov	x21,1
	b	.line
.def:	adr	x22,pad_sets
	b	.line
.kv:	mov	x2,x0				; cle = valeur
.q:	cmp	x2,x1
	b.hs	.line
	ldrb	w3,[x2]
	cmp	w3,'='
	b.eq	.q1
	add	x2,x2,1
	b	.q
.q1:	stp	x1,x2,[sp,-16]!
	mov	x1,x2
	bl	pad_trim
	adr	x2,pad_w_btn
	mov	x3,8
	bl	pad_mot
	ldp	x1,x2,[sp],16
	tbnz	x0,63,.line
	mov	x3,x0				; numero du bouton
	add	x0,x2,1
	bl	pad_trim
	cbz	x21,.num
	mov	x2,x3
	bl	pad_set_key
	b	.line
.num:	cbz	x22,.line
	strb	wzr,[x1]
	stp	x3,xzr,[sp,-16]!
	bl	parse_dec
	ldp	x3,x4,[sp],16
	add	x1,x22,x3
	strb	w0,[x1,4]
	b	.line
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = texte, sans les blancs des bords
pad_trim:
.a:	cmp	x0,x1
	b.hs	.b
	ldrb	w2,[x0]
	cmp	w2,' '
	b.eq	.a1
	cmp	w2,9
	b.ne	.b
.a1:	add	x0,x0,1
	b	.a
.b:	cmp	x0,x1
	b.hs	.f
	sub	x9,x1,1				; (x3 : le bouton de l'appelant)
	ldrb	w2,[x9]
	cmp	w2,' '
	b.eq	.b1
	cmp	w2,9
	b.eq	.b1
	cmp	w2,13
	b.ne	.f
.b1:	sub	x1,x1,1
	b	.b
.f:	ret

; x0, x1 = « vvvv:pppp » -> w0 = vid << 16 | pid, 0 : autre chose
pad_hex2:
	sub	x2,x1,x0
	cmp	x2,9
	b.ne	.non
	mov	x2,0
	mov	w4,0
.l:	cmp	x2,9
	b.hs	.f
	ldrb	w3,[x0,x2]
	add	x2,x2,1
	cmp	x2,5
	b.eq	.l
	sub	w5,w3,'0'
	cmp	w5,9
	b.ls	.d
	orr	w5,w3,0x20
	sub	w5,w5,'a'
	cmp	w5,5
	b.hi	.non
	add	w5,w5,10
.d:	orr	w4,w5,w4,lsl 4
	b	.l
.f:	mov	w0,w4
	ret
.non:	mov	w0,0
	ret

; x0, x1 = touche, x2 = numero du bouton : remplace sa touche
pad_set_key:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	w19,1
	lsl	w19,w19,w2			; le bit
	adr	x3,pad_keys			; ses entrees sortent de la table
	adr	x4,pad_keys_n
	ldr	x4,[x4]
	mov	x5,0
	mov	x6,0
.s:	cmp	x5,x4
	b.hs	.s1
	ldr	x7,[x3,x5,lsl 3]
	add	x5,x5,1
	lsr	x8,x7,32
	cmp	w8,w19
	b.eq	.s
	str	x7,[x3,x6,lsl 3]
	add	x6,x6,1
	b	.s
.s1:	adr	x4,pad_keys_n
	str	x6,[x4]
	sub	x2,x1,x0
	cmp	x2,1
	b.ne	.w
	ldrb	w20,[x0]			; une lettre, un chiffre
	orr	w20,w20,0x20
	lsl	w20,w20,8
	b	.add
.w:	adr	x2,pad_w_key
	mov	x3,12
	bl	pad_mot
	tbnz	x0,63,.out
	adr	x1,pad_c_key
	ldrb	w20,[x1,x0]
	cbz	w20,.out			; none
.add:	adr	x3,pad_keys
	adr	x4,pad_keys_n
	ldr	x5,[x4]
	cmp	x5,PAD_KEYS_MAX
	b.hs	.out
	mov	x0,x19
	lsl	x0,x0,32
	orr	x0,x0,x20
	str	x0,[x3,x5,lsl 3]
	add	x5,x5,1
	str	x5,[x4]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = evenement brut : une touche recoit son caractere en +4
traduire:
	ldr	w1,[x0]
	cmp	w1,EV_KEY
	b.ne	.out
	ldr	x1,[x0,24]			; bit 31 des modificateurs : caractere
	tbz	x1,31,.calc			; deja traduit (pilotes virtuels : vncd,
	and	x1,x1,0x7FFFFFFF		; rdpd : la disposition du PC est deja
	str	x1,[x0,24]			; appliquee), garde tel quel
	ret
.calc:	str	x30,[sp,-16]!
	str	x0,[sp,-16]!
	ldr	x1,[x0,24]
	ldr	x0,[x0,8]
	bl	key_to_char
	ldr	x1,[sp],16
	str	w0,[x1,4]
	ldr	x30,[sp],16
.out:	ret

; x0 = evenement : appui d'une touche -> repetee tant qu'elle est tenue ;
; relache -> plus de repetition (codes HID 0xE0.. : modificateurs, jamais)
tenir:
	ldr	w1,[x0]
	cmp	w1,EV_KEY
	b.ne	.out
	ldr	x1,[x0,8]
	cmp	x1,0xE0
	b.hs	.out
	ldr	x2,[x0,16]
	cbz	x2,.relache
	adr	x2,rep_ev			; copie de l'appui
	ldp	x3,x4,[x0]
	stp	x3,x4,[x2]
	ldp	x3,x4,[x0,16]
	stp	x3,x4,[x2,16]
	str	x30,[sp,-16]!
	sys	SYS_TIME_MS
	ldr	x30,[sp],16
	add	x0,x0,REP_DELAI
	adr	x1,rep_time
	mov	x2,1
	stp	x0,x2,[x1]			; rep_time, rep_on
	ret
.relache:
	adr	x2,rep_ev
	ldr	x3,[x2,8]
	cmp	x3,x1
	b.ne	.out
	adr	x1,rep_on
	str	xzr,[x1]
.out:	ret

; touche tenue : renvoie l'appui
repeter:
	prologue
	adr	x0,rep_on
	ldr	x0,[x0]
	cbz	x0,.out
	sys	SYS_TIME_MS
	adr	x1,rep_time
	ldr	x2,[x1]
	cmp	x0,x2
	b.lo	.out
	add	x0,x0,REP_PAS
	str	x0,[x1]
	adr	x0,rep_ev
	bl	diffuser
.out:	epilogue

; x0 = evenement (32 o) : dans l'anneau de chaque abonne (perdu s'il est plein)
diffuser:
	adr	x1,abonnes
	mov	x2,0
.s:	cmp	x2,ABONNES_MAX
	b.hs	.out
	ldr	x3,[x1,AB_PID]
	cbz	x3,.n
	dmb	ish
	ldr	x3,[x1,AB_ADR]
	ldp	x4,x5,[x3]			; tete, queue
	sub	x6,x4,x5
	cmp	x6,ANNEAU_N
	b.hs	.n
	and	x6,x4,ANNEAU_N-1
	add	x6,x3,x6,lsl 5
	ldp	x7,x8,[x0]
	stp	x7,x8,[x6,ANNEAU_EV]
	ldp	x7,x8,[x0,16]
	stp	x7,x8,[x6,ANNEAU_EV+16]
	dmb	ish				; l'evenement avant la tete
	add	x4,x4,1
	str	x4,[x3]
.n:	add	x1,x1,AB_TAILLE
	add	x2,x2,1
	b	.s
.out:	ret

; ---------------------------------------------------------------------------
;  Dispositions du clavier (avant : src/kernel/keymap.asm)
;
;  Table active kb_live : 3 colonnes (normal, Maj, AltGr) de KEY_MAP_N
;  caracteres Unicode (16 bits), indexees par le code HID (page 7).
;  Fichier SD0:/res/keyb/<NOM>.txt (une touche par ligne, # : commentaire) :
;	<code HID en hexa>  <normal>  [<Maj>  [<AltGr>]]
;  chaque caractere est ecrit tel quel (UTF-8), ou « U+20AC », ou « - » pour
;  aucun. Exemple :  14  a  A  -     (touche HID 0x14 : 'a', 'A', rien)
; ---------------------------------------------------------------------------

; x0 = 0 US / 1 FR : disposition integree dans kb_live
kb_use_builtin:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,kb_live
	adr	x1,map_us_n
	adr	x2,map_us_s
	mov	x3,0
	cbz	x19,.copy
	adr	x1,map_fr_n
	adr	x2,map_fr_s
	adr	x3,map_fr_a
.copy:	mov	x20,x3
	mov	x19,x2
	mov	x2,KB_TABLE
	bl	mem_move
	adr	x0,kb_live + KB_TABLE
	mov	x1,x19
	mov	x2,KB_TABLE
	bl	mem_move
	adr	x0,kb_live + 2*KB_TABLE
	mov	x1,0
.z:	strh	wzr,[x0,x1]			; AltGr : vide par defaut
	add	x1,x1,2
	cmp	x1,KB_TABLE
	b.lo	.z
	cbz	x20,.out
	mov	x1,x20
	mov	x2,KB_TABLE
	bl	mem_move
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom de la disposition (« US », « FR »...) -> x0 = 1 si chargee
; depuis SD0:/res/keyb/<nom>.txt
kb_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,64
	mov	x19,x0
	mov	x0,sp				; chemin
	strb	wzr,[sp]
	adr	x1,kb_dir
	bl	str_cat
	mov	x1,x19
	mov	x2,15
.n:	ldrb	w3,[x1],1			; nom : 15 caracteres au plus
	cbz	w3,.ext
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.n
.ext:	strb	wzr,[x0]
	adr	x1,kb_ext
	bl	str_cat
	mov	x0,sp
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.nofile
	mov	x20,x0
	adr	x1,kb_fichier
	mov	x2,KB_FICHIER_MAX
	sys	SYS_READ
	mov	x21,x0
	mov	x0,x20
	sys	SYS_CLOSE
	cmp	x21,0
	b.le	.empty
	adr	x2,kb_tmp			; table de travail : vide
	mov	x3,0
.zt:	str	xzr,[x2,x3]
	add	x3,x3,8
	cmp	x3,KB_COLS*KB_TABLE
	b.lo	.zt
	adr	x0,kb_fichier
	mov	x1,x21
	bl	kb_parse
	mov	x22,x0				; touches lues
	cbz	x22,.empty
	adr	x0,kb_live			; remplacement de la table active
	adr	x1,kb_tmp
	mov	x2,KB_COLS*KB_TABLE
	bl	mem_move
	adr	x0,kb_msg
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,kb_msg2
	bl	puts
	mov	x0,x22
	bl	print_dec
	adr	x0,kb_msg3
	bl	puts
	mov	x0,1
	b	.out
.empty:	adr	x0,kb_msg_empty
	b	.err
.nofile:
	adr	x0,kb_msg_none
.err:	bl	puts
	mov	x0,sp
	bl	puts
	adr	x0,m_nl
	bl	puts
	mov	x0,0
.out:	add	sp,sp,64
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = taille -> kb_tmp rempli, x0 = nombre de touches lues
kb_parse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0				; position
	add	x20,x0,x1			; fin
	mov	x24,0				; touches
.line:	cmp	x19,x20
	b.hs	.end
	bl	.blank
	ldrb	w0,[x19]
	cmp	w0,'#'
	b.eq	.skip
	mov	x21,0				; code HID (hexa)
	mov	x22,0				; chiffres
.hex:	cmp	x19,x20
	b.hs	.code
	ldrb	w0,[x19]
	bl	.hexval
	tbnz	x0,63,.code
	add	x21,x0,x21,lsl 4
	add	x19,x19,1
	add	x22,x22,1
	b	.hex
.code:	cbz	x22,.skip
	cmp	x21,KEY_MAP_N
	b.hs	.skip
	mov	x23,0				; colonne
.col:	bl	.blank
	cmp	x19,x20
	b.hs	.done_key
	ldrb	w0,[x19]
	cmp	w0,10
	b.eq	.done_key
	cmp	w0,13
	b.eq	.done_key
	bl	.token				; -> w0 = caractere
	adr	x1,kb_tmp
	mov	x2,KB_TABLE
	madd	x1,x23,x2,x1
	strh	w0,[x1,x21,lsl 1]
	add	x23,x23,1
	cmp	x23,KB_COLS
	b.lo	.col
.done_key:
	cbz	x23,.skip
	add	x24,x24,1
.skip:	cmp	x19,x20				; fin de ligne
	b.hs	.end
	ldrb	w0,[x19],1
	cmp	w0,10
	b.ne	.skip
	b	.line
.end:	mov	x0,x24
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (interne) saute espaces et tabulations
.blank:	cmp	x19,x20
	b.hs	.b_out
	ldrb	w0,[x19]
	cmp	w0,' '
	b.eq	.b_next
	cmp	w0,9
	b.ne	.b_out
.b_next:
	add	x19,x19,1
	b	.blank
.b_out:	ret

; (interne) w0 = caractere -> x0 = valeur hexa, -1 sinon
.hexval:
	sub	w1,w0,'0'
	cmp	w1,9
	b.ls	.hv
	orr	w0,w0,0x20			; minuscule
	sub	w1,w0,'a'
	cmp	w1,5
	b.hi	.hx
	add	w1,w1,10
.hv:	mov	x0,x1
	ret
.hx:	mov	x0,-1
	ret

; (interne) lit un caractere en x19 -> w0 = point de code (0 : Â« - Â»)
.token:	stp	x29,x30,[sp,-16]!
	ldrb	w0,[x19]
	cmp	w0,'-'				; Â« - Â» seul : aucun caractere
	b.ne	.t_u
	add	x1,x19,1
	cmp	x1,x20
	b.hs	.t_none
	ldrb	w1,[x1]
	cmp	w1,' '
	b.eq	.t_none
	cmp	w1,9
	b.eq	.t_none
	cmp	w1,10
	b.eq	.t_none
	cmp	w1,13
	b.eq	.t_none
	b	.t_utf8
.t_none:
	add	x19,x19,1
	mov	w0,0
	b	.t_out
.t_u:	cmp	w0,'U'				; Â« U+XXXX Â»
	b.ne	.t_utf8
	add	x1,x19,1
	cmp	x1,x20
	b.hs	.t_utf8
	ldrb	w1,[x1]
	cmp	w1,'+'
	b.ne	.t_utf8
	add	x19,x19,2
	mov	x2,0
.t_hex:	cmp	x19,x20
	b.hs	.t_hdone
	ldrb	w0,[x19]
	str	x2,[sp,-16]!
	bl	.hexval
	ldr	x2,[sp],16
	tbnz	x0,63,.t_hdone
	add	x2,x0,x2,lsl 4
	add	x19,x19,1
	b	.t_hex
.t_hdone:
	and	w0,w2,0xFFFF
	b	.t_out
.t_utf8:					; un caractere UTF-8 (1 a 3 octets)
	ldrb	w0,[x19],1
	tbz	w0,7,.t_out
	and	w1,w0,0xE0
	cmp	w1,0xC0
	b.ne	.t_three
	and	w0,w0,0x1F
	b	.t_cont1
.t_three:
	and	w0,w0,0x0F
	cmp	x19,x20
	b.hs	.t_out
	ldrb	w1,[x19],1
	and	w1,w1,0x3F
	orr	w0,w1,w0,lsl 6
.t_cont1:
	cmp	x19,x20
	b.hs	.t_out
	ldrb	w1,[x19],1
	and	w1,w1,0x3F
	orr	w0,w1,w0,lsl 6
.t_out:	ldp	x29,x30,[sp],16
	ret

; x0 = code HID, x1 = modificateurs -> x0 = caractere Unicode (0 si aucun)
; AltGr (Alt droite) : 3e colonne, a defaut la colonne normale / Maj.
key_to_char:
	cmp	x0,KEY_MAP_N
	b.hs	.none
	mov	x5,0x11				; Ctrl : pas de caractere
	tst	x1,x5
	b.ne	.none
	adr	x4,kb_live
	tbz	x1,6,.plain
	add	x2,x4,2*KB_TABLE		; AltGr
	ldrh	w2,[x2,x0,lsl 1]
	cbz	w2,.plain
	mov	x0,x2
	ret
.plain:	mov	x5,0x22				; Maj gauche / droite
	tst	x1,x5
	b.eq	.look
	add	x4,x4,KB_TABLE
.look:	ldrh	w0,[x4,x0,lsl 1]
	ret
.none:	mov	x0,0
	ret
; ---------------------------------------------------------------------------
; dispositions integrees (kb_use_builtin) : tables HID -> Unicode (0x00..0x64)
macro keymap name*, letters*, digits*, p2d*, p2e*, p2f*, p30*, p31*, p32*, p33*, p34*, p35*, p36*, p37*, p38*, p64* {
	align 2
name:
	dh 0,0,0,0
	irps c, letters \{ dh c \}
	irps c, digits \{ dh c \}
	dh 10, 27, 8, 9, ' '
	dh p2d, p2e, p2f, p30, p31, p32, p33, p34, p35, p36, p37, p38
	rept 0x64-0x39 \{ dh 0 \}
	dh p64
}

keymap map_us_n, 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 'u' 'v' 'w' 'x' 'y' 'z', \
	'1' '2' '3' '4' '5' '6' '7' '8' '9' '0', '-', '=', '[', ']', '\', '#', ';', "'", '`', ',', '.', '/', '\'
keymap map_us_s, 'A' 'B' 'C' 'D' 'E' 'F' 'G' 'H' 'I' 'J' 'K' 'L' 'M' 'N' 'O' 'P' 'Q' 'R' 'S' 'T' 'U' 'V' 'W' 'X' 'Y' 'Z', \
	'!' '@' '#' '$' '%' '^' '&' '*' '(' ')', '_', '+', '{', '}', '|', '~', ':', '"', '~', '<', '>', '?', '|'
; AZERTY : positions physiques US -> caracteres francais
keymap map_fr_n, 'q' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' ',' 'n' 'o' 'p' 'a' 'r' 's' 't' 'u' 'v' 'z' 'x' 'y' 'w', \
	'&' 0xE9 '"' "'" '(' '-' 0xE8 '_' 0xE7 0xE0, ')', '=', '^', '$', '*', '*', 'm', 0xF9, 0xB2, ';', ':', '!', '<'
keymap map_fr_s, 'Q' 'B' 'C' 'D' 'E' 'F' 'G' 'H' 'I' 'J' 'K' 'L' '?' 'N' 'O' 'P' 'A' 'R' 'S' 'T' 'U' 'V' 'Z' 'X' 'Y' 'W', \
	'1' '2' '3' '4' '5' '6' '7' '8' '9' '0', 0xB0, '+', 0xA8, 0xA3, 0xB5, 0xB5, 'M', '%', 0xB2, '.', '/', 0xA7, '>'
keymap map_fr_a, 0 0 0 0 0x20AC 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0, \
	0 '~' '#' '{' '[' '|' '`' '\' '^' '@', ']', '}', 0, 0xA4, 0, 0, 0, 0, 0, 0, 0, 0, '|'
		align 4

s_nom		db 'hid',0
k_keyb		db 'keyb',0
p_options	db 'SD0:/etc/options.txt',0
kb_dir		db 'SD0:/res/keyb/',0
kb_ext		db '.txt',0
m_pret		db 'hidmanager : en service (disposition, repetition, anneaux)',10,0
kb_msg		db 'hidmanager : disposition ',0
kb_msg2		db ' (',0
kb_msg3		db ' touches)',10,0
kb_msg_none	db 'hidmanager : fichier absent ',0
kb_msg_empty	db 'hidmanager : aucune touche dans ',0
m_nl		db 10,0
		align 8

macro pad_k hid*, car*, bouton* {		; une entree de pad_keys : 8 octets
	db	hid, car, 0, 0
	dw	bouton
}
pad_def_keys:				; feuille de route : fleches, A B X Y L R telles
	pad_k 0x52,0,PAD_UP		; quelles, Impr. ecran et Retour Select, Pause et
	pad_k 0x51,0,PAD_DOWN		; Entree Start
	pad_k 0x50,0,PAD_LEFT
	pad_k 0x4F,0,PAD_RIGHT
	pad_k 0x46,0,PAD_SELECT
	pad_k 0x2A,0,PAD_SELECT
	pad_k 0x48,0,PAD_START
	pad_k 0x28,0,PAD_START
	pad_k 0,'a',PAD_A
	pad_k 0,'b',PAD_B
	pad_k 0,'x',PAD_X
	pad_k 0,'y',PAD_Y
	pad_k 0,'l',PAD_L
	pad_k 0,'r',PAD_R
	dd	0
pad_def_btn:				; manette HID ordinaire (l'ordre generique d'Onyx)
	db	0,0,0,0,3,2,4,1,5,6,7,8,9,10,11,12,13
macro pad_w8 m* {
	local a
a:	db	m
	rb	8-($-a)
}
macro pad_w12 m* {
	local a
a:	db	m
	rb	12-($-a)
}
pad_w_btn:				; dans l'ordre des bits PAD_*
	pad_w8 'up'
	pad_w8 'down'
	pad_w8 'left'
	pad_w8 'right'
	pad_w8 'a'
	pad_w8 'b'
	pad_w8 'x'
	pad_w8 'y'
	pad_w8 'l'
	pad_w8 'r'
	pad_w8 'l2'
	pad_w8 'r2'
	pad_w8 'select'
	pad_w8 'start'
	pad_w8 'l3'
	pad_w8 'r3'
	pad_w8 'home'
	db	0
pad_w_sect:
	pad_w12 'keyboard'
	pad_w12 'default'
	db	0
pad_w_key:
	pad_w12 'up'
	pad_w12 'down'
	pad_w12 'left'
	pad_w12 'right'
	pad_w12 'enter'
	pad_w12 'backspace'
	pad_w12 'space'
	pad_w12 'tab'
	pad_w12 'esc'
	pad_w12 'prtsc'
	pad_w12 'pause'
	pad_w12 'none'
	db	0
pad_c_key:
	db	0x52,0x51,0x50,0x4F,0x28,0x2A,0x2C,0x2B,0x29,0x46,0x48,0
p_padini db	'SD0:/etc/gamepad.ini',0
		align 8

include '../../inc/applist.inc'		; ini_read

segment readable writeable
canal		dd -1
kit_data
applis_data
		align 8
KB_FICHIER_MAX	= 8192
rep_time	dd 0				; touche tenue : prochaine repetition
rep_on		dd 0
rep_ev		rb 32
abonnes		rb ABONNES_MAX * AB_TAILLE
pilotes		rb PILOTES_MAX * AB_TAILLE	; (juste apres abonnes : desabonner)
pilotes_n	dd 0
ev_pilote	rb 32
PADSET_SIZE	= 24			; vid << 16 | pid (0 : libre), 17 numeros de bouton
PADSET_MAX	= 8
PAD_KEYS_MAX	= 40
pad_btn		rb PAD_MAX*4
		align 8
pad_ax		rb PAD_MAX*8
pad_ev		rb 32
pad_rawb	rb PAD_MAX*4			; boutons bruts
pad_id		rb PAD_MAX*4			; modele : vid << 16 | pid
pad_keys_n	dd 0
pad_keys	rb PAD_KEYS_MAX*8
pad_sets	rb PADSET_MAX*PADSET_SIZE
msg		rb 512
kb_live		rh KB_COLS * KEY_MAP_N
		align 8
kb_tmp		rh KB_COLS * KEY_MAP_N
		align 8
kb_fichier	rb KB_FICHIER_MAX
