; gt911 - ecran tactile Goodix GT911 (Waveshare PocketTerm35), pilote en
; service (docs/FEUILLE-DE-ROUTE.md, section 2), lance par hwmanager
; (SD0:/etc/drivers.txt). Repris de src/kernel/touch.asm, sorti du noyau :
;  * bus I2C 1 en logiciel sur GPIO 2 (SDA) / 3 (SCL) : les registres GPIO
;    mappes (dev_map), le compteur du processeur lu directement (cntpct_el0) ;
;  * les gestes (repris d'Onyx, sys/touch.cpp) :
;      un doigt pose puis leve sur place     -> clic
;      un doigt glisse verticalement         -> molette (la page suit le doigt)
;      un doigt glisse de cote, ou reste pose -> glisser (bouton appuye)
;      deux doigts                           -> molette
;  * les evenements (EV_MOUSE_ABS 0..32767, EV_WHEEL) partent dans l'anneau
;    que HIDManager lui donne (service « hid », op 3) : aucun appel par
;    evenement.
;  GT911 : 0x8140 identifiant (« 911 »), 0x8048/0x804A plage x/y, 0x814E etat
;  (bit 7 : nouveaux points, bits 0-3 : nombre), points de 8 octets a 0x814F
;  (piste, x, y, taille) ; l'etat est remis a 0 apres lecture.
;  Adresse 0x14 ou 0x5D. Sans GT911 (QEMU, autre carte) : s'arrete (code 0).
;  Au repos, attend l'interruption du GPIO 4 (front descendant ; une lecture par
;  seconde quand meme) ; doigt pose, lit toutes les TP_POLL_MS ms : les gestes
;  ont besoin d'un rythme. Sans interruption (no_irq, broche non cablee) : sonde
;  toujours, comme le dit le journal.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../inc/i2c.inc'				; GPIO, I2C 1 en logiciel

EV_MOUSE_ABS	= 3
EV_WHEEL	= 4
HID_OP_PILOTE	= 3
ANNEAU_EV	= 64
ANNEAU_N	= 64

GT_ID		= 0x8140
GT_XMAX		= 0x8048
GT_STATUS	= 0x814E
GT_POINT1	= 0x814F
TP_POLL_MS	= 16
TP_WHEEL_STEP	= 20			; pixels par cran de molette
TP_SLOP		= 10			; deplacement ignore (pixels)
TP_HOLD_MS	= 400			; appui long : glisser
TP_INT_PIN	= 4			; INT du GT911 (PocketTerm35)
TP_INT_IRQ	= 145			; ligne du banc GPIO 0-27
TP_INT_MS	= 1000
TP_INT_MISS	= 3			; touchers sans interruption : retour au sondage
GP_EDS0		= 0x40
GP_FEN0		= 0x58

TP_NONE		= 0
TP_WAIT		= 1
TP_SCROLL	= 2
TP_DRAG		= 3


segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	bl	kits_open
	bl	i2c_ouvrir			; registres GPIO, bus I2C 1
	tbnz	x0,63,.ko
	adr	x0,ecran			; taille de l'ecran (pixels)
	sys	SYS_SCREEN_INFO
	adr	x0,s_hid			; l'anneau vers HIDManager
	mov	x1,10000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.ko
	adr	x1,msg
	mov	x2,HID_OP_PILOTE
	str	x2,[x1]
	sys	SYS_SVC_CALL
	tbnz	x0,63,.ko
	sys	SYS_BUF_MAP
	tbnz	x0,63,.ko
	adr	x1,anneau
	str	x0,[x1]
	b	tp_boucle
.ko:	adr	x0,m_ko
	bl	puts
	mov	x0,1				; (sur une erreur : relance)
	sys	SYS_EXIT

; x0 = type, x1 = a, x2 = b, x3 = c : un evenement dans l'anneau de
; HIDManager (perdu s'il est plein)
anneau_pousser:
	adr	x9,anneau
	ldr	x9,[x9]
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

i2c_code

; x0 = valeur : en hexadecimal sur la console
print_hex:
	prologue
	sub	sp,sp,32
	mov	x1,sp
	add	x1,x1,17
	strb	wzr,[x1]
	mov	x2,x0
.c:	and	x3,x2,15
	cmp	x3,10
	add	x4,x3,'0'
	add	x5,x3,'A'-10
	csel	x3,x4,x5,lo
	sub	x1,x1,1
	strb	w3,[x1]
	lsr	x2,x2,4
	cbnz	x2,.c
	mov	x0,x1
	bl	puts
	add	sp,sp,32
	epilogue

; w0 = registre du GT911, x1 = tampon, x2 = octets -> x0 = 0 si ok
; tp_mode 0 : ecriture du registre puis lecture en start repete ;
; tp_mode 1 : ecriture (stop) puis lecture separee
gt_read:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,16
	mov	x19,x1
	mov	x20,x2
	lsr	w3,w0,8
	strb	w3,[sp]
	strb	w0,[sp,1]
	mov	x1,sp
	mov	x2,2
	adr	x0,tp_mode
	ldr	w0,[x0]
	cbnz	w0,.sep
	mov	x3,x19
	mov	x4,x20
	adr	x0,tp_addr
	ldr	w0,[x0]
	bl	i2c1_xfer
	b	.out
.sep:	mov	x3,0
	mov	x4,0
	adr	x0,tp_addr
	ldr	w0,[x0]
	bl	i2c1_xfer
	cbnz	x0,.out
	adr	x0,tp_addr
	ldr	w0,[x0]
	mov	x1,0
	mov	x2,0
	mov	x3,x19
	mov	x4,x20
	bl	i2c1_xfer
.out:	add	sp,sp,16
	ldp	x19,x20,[sp],16
	epilogue

; w0 = registre, w1 = valeur (un octet) -> x0 = 0 si ok
gt_write:
	prologue
	sub	sp,sp,16
	lsr	w3,w0,8
	strb	w3,[sp]
	strb	w0,[sp,1]
	strb	w1,[sp,2]
	mov	x1,sp
	mov	x2,3
	mov	x3,0
	mov	x4,0
	adr	x0,tp_addr
	ldr	w0,[x0]
	bl	i2c1_xfer
	add	sp,sp,16
	epilogue

; -> x0 = 1 si un GT911 repond (0x5D ou 0x14) ; tp_addr, tp_xmax, tp_ymax
gt_probe:
	prologue
	str	x19,[sp,-16]!
	mov	x19,0
.try:	adr	x0,gt_addrs
	ldrb	w0,[x0,x19]
	cbz	w0,.none
	adr	x1,tp_addr
	str	w0,[x1]
	adr	x1,tp_mode			; deux facons de lire un registre
	str	wzr,[x1]
.mode:	adr	x0,tp_buf
	str	wzr,[x0]
	adr	x0,i2c_last_s			; 0 : pas d'echec
	str	wzr,[x0]
	movl	w0,GT_ID
	adr	x1,tp_buf
	mov	x2,4
	bl	gt_read
	str	x0,[sp,-16]!
	adr	x1,tp_quiet
	ldr	w1,[x1]
	cbnz	w1,.logged
	adr	x0,tp_msg_try			; journal : adresse, mode, resultat
	bl	puts
	adr	x0,tp_addr
	ldr	w0,[x0]
	bl	print_hex
	adr	x0,tp_msg_mode
	bl	puts
	adr	x0,tp_mode
	ldr	w0,[x0]
	bl	print_dec
	adr	x0,tp_msg_s
	bl	puts
	adr	x0,i2c_last_s
	ldr	w0,[x0]
	bl	print_hex
	adr	x0,tp_msg_id
	bl	puts
	adr	x0,tp_buf
	ldr	w0,[x0]
	bl	print_hex
	adr	x0,m_nl
	bl	puts
.logged:
	ldr	x0,[sp],16
	cbz	x0,.answered
	adr	x1,tp_mode
	ldr	w0,[x1]
	cbnz	w0,.next
	mov	w0,1
	str	w0,[x1]
	b	.mode
.answered:
	adr	x1,tp_buf
	ldrb	w2,[x1]
	cmp	w2,'9'
	b.ne	.next
	ldrb	w2,[x1,1]
	cmp	w2,'1'
	b.ne	.next
	ldrb	w2,[x1,2]
	cmp	w2,'1'
	b.ne	.next
	movl	w0,GT_XMAX			; plage des coordonnees
	adr	x1,tp_buf
	mov	x2,4
	bl	gt_read
	cbnz	x0,.found
	adr	x1,tp_buf
	ldrh	w2,[x1]
	ldrh	w3,[x1,2]
	cmp	w2,64				; valeurs plausibles seulement
	b.lo	.found
	cmp	w3,64
	b.lo	.found
	cmp	w2,4096
	b.hi	.found
	cmp	w3,4096
	b.hi	.found
	adr	x1,tp_xmax
	str	w2,[x1]
	adr	x1,tp_ymax
	str	w3,[x1]
.found:	mov	x0,1
	b	.out
.next:	add	x19,x19,1
	b	.try
.none:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Thread "tactile"
; ---------------------------------------------------------------------------

; x0 = coordonnee brute, x1 = plage du GT911, x2 = taille de l'ecran
; -> x0 = pixel (borne)
tp_scale:
	cbz	x1,.zero
	mul	x0,x0,x2
	udiv	x0,x0,x1
	sub	x2,x2,1
	cmp	x0,x2
	csel	x0,x0,x2,ls
	ret
.zero:	mov	x0,0
	ret

; x0 = x, x1 = y (pixels), x2 = boutons : position absolue pour la GUI
tp_pointer:
	prologue
	movl	x3,32767
	adr	x5,scr_w
	ldp	x5,x6,[x5]
	mul	x0,x0,x3
	sub	x4,x5,1
	udiv	x0,x0,x4
	mul	x1,x1,x3
	sub	x4,x6,1
	udiv	x1,x1,x4
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,EV_MOUSE_ABS
	bl	anneau_pousser
	epilogue

; x0 = crans (signe) : molette
tp_wheel:
	mov	x1,x0
	mov	x0,EV_WHEEL
	mov	x2,0
	mov	x3,0
	b	anneau_pousser

; l'interruption de la broche INT, si le noyau la donne
tp_irq_open:
	prologue
	mov	x0,TP_INT_IRQ
	mov	x1,-1
	mov	x2,0
	sys	SYS_IRQ_BIND
	tbnz	x0,63,.poll
	adr	x9,gpio_va
	ldr	x9,[x9]
	ldr	w10,[x9,GP_FEN0]
	orr	w10,w10,1 shl TP_INT_PIN
	str	w10,[x9,GP_FEN0]
	adr	x0,tp_irq
	mov	w1,1
	str	w1,[x0]
	adr	x0,tp_msg_irq
	b	.say
.poll:	adr	x0,tp_msg_poll
.say:	bl	puts
	epilogue

; retour au sondage
tp_irq_close:
	prologue
	adr	x0,tp_irq
	str	wzr,[x0]
	adr	x9,gpio_va
	ldr	x9,[x9]
	ldr	w10,[x9,GP_FEN0]
	bic	w10,w10,1 shl TP_INT_PIN
	str	w10,[x9,GP_FEN0]
	mov	x0,TP_INT_IRQ
	sys	SYS_IRQ_UNBIND
	adr	x0,tp_msg_poll
	bl	puts
	epilogue

; attend le moment de lire la puce (x19 = geste) ; tp_timed : le delai, sans
; interruption
tp_wait:
	prologue
	adr	x0,tp_timed
	str	wzr,[x0]
	adr	x0,tp_irq
	ldr	w0,[x0]
	cbz	w0,.sleep
	cmp	x19,TP_NONE
	b.ne	.sleep
	adr	x9,gpio_va			; (la ligne reste masquee doigt pose)
	ldr	x9,[x9]
	mov	w10,1 shl TP_INT_PIN
	str	w10,[x9,GP_EDS0]
	mov	x0,TP_INT_IRQ
	mov	x1,TP_INT_MS
	sys	SYS_IRQ_WAIT
	cmp	x0,1
	b.eq	.fired
	tbnz	x0,63,.bad
	adr	x0,tp_timed
	mov	w1,1
	str	w1,[x0]
	b	.out
.fired:	adr	x0,tp_miss
	str	wzr,[x0]
	b	.out
.bad:	bl	tp_irq_close
.sleep:	mov	x0,TP_POLL_MS
	sys	SYS_SLEEP
.out:	epilogue

; apres la lecture de l'etat : un toucher trouve sans interruption ?
tp_irq_check:
	adr	x0,tp_timed
	ldr	w0,[x0]
	cbz	w0,.out
	adr	x0,tp_buf
	ldrb	w0,[x0]
	tbz	w0,7,.out
	adr	x0,tp_miss
	ldr	w1,[x0]
	add	w1,w1,1
	str	w1,[x0]
	cmp	w1,TP_INT_MISS
	b.hs	tp_irq_close
.out:	ret

; Etat du geste dans x19-x28 :
;   x19 mode, x20 deux doigts, x21/x22 derniere position, x23 y de reference
;   a deux doigts, x24/x25 point de depart, x26 instant du contact, x27 y de
;   reference de la molette, x28 echecs de lecture consecutifs
tp_boucle:
	mov	x19,30				; le GT911 peut demarrer apres le noyau :
.probe:	bl	gt_probe			; essais toutes les 2 s pendant une minute
	cbnz	x0,.found
	subs	x19,x19,1
	b.eq	.absent
	adr	x0,tp_quiet			; un seul jeu de lignes dans le journal
	mov	w1,1
	str	w1,[x0]
	movl	x0,2000
	sys	SYS_SLEEP
	b	.probe
.found:
	adr	x0,tp_msg
	bl	puts
	adr	x0,tp_addr
	ldr	w0,[x0]
	bl	print_hex
	adr	x0,tp_msg2
	bl	puts
	adr	x0,tp_xmax
	ldr	w0,[x0]
	bl	print_dec
	adr	x0,tp_msg3
	bl	puts
	adr	x0,tp_ymax
	ldr	w0,[x0]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
	bl	tp_irq_open
	mov	x19,TP_NONE
	mov	x20,0
	mov	x28,0
.poll:	bl	tp_wait
	movl	w0,GT_STATUS
	adr	x1,tp_buf
	mov	x2,1
	bl	gt_read
	cbz	x0,.st_ok
	add	x28,x28,1
	cmp	x28,50
	b.ne	.poll
	adr	x0,tp_msg_mute
	bl	puts
	b	.poll
.st_ok:	mov	x28,0
	bl	tp_irq_check
	adr	x0,tp_buf
	ldrb	w0,[x0]
	tbz	w0,7,.poll			; rien de neuf
	and	w0,w0,0xF
	cmp	w0,2				; les deux premiers points suffisent
	mov	w1,2
	csel	w0,w0,w1,ls
	str	w0,[sp,-16]!
	cbz	w0,.ack
	lsl	x2,x0,3
	movl	w0,GT_POINT1
	adr	x1,tp_buf
	bl	gt_read
	cbz	x0,.ack
	mov	w0,-1				; lecture ratee : ignorer cet etat
	str	w0,[sp]
.ack:	movl	w0,GT_STATUS			; le GT911 peut remplir a nouveau
	mov	w1,0
	bl	gt_write
	ldr	w0,[sp],16
	tbnz	w0,31,.poll
	cbnz	w0,.touch

	; --- plus aucun doigt -------------------------------------------------------
	cmp	x19,TP_WAIT			; pose et leve sur place : clic
	b.ne	.up_drag
	mov	x0,x24
	mov	x1,x25
	mov	x2,1
	bl	tp_pointer
	mov	x0,x24
	mov	x1,x25
	mov	x2,0
	bl	tp_pointer
	b	.up_end
.up_drag:
	cmp	x19,TP_DRAG
	b.ne	.up_end
	mov	x0,x21
	mov	x1,x22
	mov	x2,0
	bl	tp_pointer
.up_end:
	mov	x19,TP_NONE
	mov	x20,0
	b	.poll

.touch:	str	w0,[sp,-16]!			; nombre de points
	adr	x9,tp_buf			; premier point -> pixels
	ldrh	w0,[x9,1]
	adr	x1,tp_xmax
	ldr	w1,[x1]
	adr	x2,scr_w
	ldr	x2,[x2]
	bl	tp_scale
	str	x0,[sp,8]
	adr	x9,tp_buf
	ldrh	w0,[x9,3]
	adr	x1,tp_ymax
	ldr	w1,[x1]
	adr	x2,scr_h
	ldr	x2,[x2]
	bl	tp_scale
	mov	x1,x0				; y
	ldr	x0,[sp,8]			; x
	ldr	w2,[sp],16
	cmp	w2,2
	b.lo	.one

	; --- deux doigts : molette, au milieu des deux -----------------------------
	stp	x0,x1,[sp,-16]!
	adr	x9,tp_buf
	ldrh	w0,[x9,9]
	adr	x1,tp_xmax
	ldr	w1,[x1]
	adr	x2,scr_w
	ldr	x2,[x2]
	bl	tp_scale
	mov	x3,x0
	adr	x9,tp_buf
	ldrh	w0,[x9,11]
	adr	x1,tp_ymax
	ldr	w1,[x1]
	adr	x2,scr_h
	ldr	x2,[x2]
	str	x3,[sp,-16]!
	bl	tp_scale
	ldr	x3,[sp],16
	mov	x4,x0
	ldp	x0,x1,[sp],16
	add	x0,x0,x3
	lsr	x0,x0,1				; milieu
	add	x1,x1,x4
	lsr	x1,x1,1
	stp	x0,x1,[sp,-16]!
	cmp	x19,TP_DRAG			; un glisser en cours : le lacher
	b.ne	.two_go
	mov	x0,x21
	mov	x1,x22
	mov	x2,0
	bl	tp_pointer
.two_go:
	mov	x19,TP_SCROLL			; pas de clic au lever
	ldp	x0,x1,[sp]
	cbnz	x20,.two_had
	mov	x20,1
	mov	x23,x1
.two_had:
	sub	x2,x1,x23
	mov	x3,TP_WHEEL_STEP
	sdiv	x2,x2,x3
	cbz	x2,.two_end
	madd	x23,x2,x3,x23
	str	x2,[sp,-16]!
	mov	x2,0
	bl	tp_pointer			; le pointeur sous les doigts, puis la molette
	ldr	x0,[sp],16
	bl	tp_wheel
.two_end:
	ldp	x21,x22,[sp],16
	b	.poll

	; --- un doigt -------------------------------------------------------------------
.one:	cbnz	x20,.poll			; un des deux leve : attendre que tout parte
	stp	x0,x1,[sp,-16]!
	cmp	x19,TP_NONE
	b.ne	.wait
	mov	x2,0				; pose : le pointeur y va, geste inconnu
	bl	tp_pointer
	ldp	x24,x25,[sp]
	mov	x27,x25
	sys	SYS_TIME_MS
	mov	x26,x0
	mov	x19,TP_WAIT
	b	.mode
.wait:	cmp	x19,TP_WAIT
	b.ne	.mode
	ldp	x0,x1,[sp]
	subs	x2,x0,x24			; |dx|, |dy|
	cneg	x2,x2,mi
	subs	x3,x1,x25
	cneg	x3,x3,mi
	cmp	x3,TP_SLOP			; vertical : molette
	b.le	.not_v
	cmp	x3,x2
	b.lt	.not_v
	mov	x19,TP_SCROLL
	mov	x27,x1
	b	.mode
.not_v:	cmp	x2,TP_SLOP			; de cote, ou tenu : glisser
	b.gt	.drag
	sys	SYS_TIME_MS
	sub	x0,x0,x26
	cmp	x0,TP_HOLD_MS
	b.lo	.mode
.drag:	mov	x0,x24				; appui la ou le doigt s'est pose
	mov	x1,x25
	mov	x2,1
	bl	tp_pointer
	mov	x19,TP_DRAG
.mode:	cmp	x19,TP_SCROLL
	b.ne	.m_drag
	ldp	x0,x1,[sp]
	sub	x2,x1,x27
	mov	x3,TP_WHEEL_STEP
	sdiv	x2,x2,x3
	cbz	x2,.one_end
	madd	x27,x2,x3,x27
	str	x2,[sp,-16]!
	mov	x2,0
	bl	tp_pointer
	ldr	x0,[sp],16
	bl	tp_wheel			; la page suit le doigt
	b	.one_end
.m_drag:
	cmp	x19,TP_DRAG
	b.ne	.one_end
	ldp	x0,x1,[sp]
	mov	x2,1
	bl	tp_pointer
.one_end:
	ldp	x21,x22,[sp],16
	b	.poll

.absent:
	adr	x0,tp_msg_none
	bl	puts
	mov	x0,0				; (absent : pas de relance)
	sys	SYS_EXIT

tp_msg		db 'gt911 : ecran tactile a 0x',0
tp_msg2		db ', ',0
tp_msg3		db ' x ',0
tp_msg_none	db 'gt911 : aucun GT911 sur le bus I2C 1',10,0
tp_msg_mute	db 'gt911 : le GT911 ne r',0xC3,0xA9,'pond plus',10,0
tp_msg_irq	db 'gt911 : interruption du GPIO 4',10,0
tp_msg_poll	db 'gt911 : sondage',10,0
gt_addrs	db 0x14, 0x5D, 0			; 0x14 d'abord : l'adresse du PocketTerm35 (overlay Waveshare)
tp_msg_try	db 'gt911 : essai 0x',0
tp_msg_mode	db ' mode ',0
tp_msg_s	db ' echec I2C ',0
tp_msg_id	db ' id ',0

s_hid		db 'hid',0
m_ko		db 'gt911 : GPIO ou HIDManager indisponible',10,0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
i2c_data
tp_irq		dd 0				; 1 : interruption en service
tp_timed	dd 0
tp_miss		dd 0
		align 8
anneau		dd 0
ecran:						; screen_info : largeur, hauteur, (barre)
scr_w		dd 640
scr_h		dd 480
		dd 0
msg		rb 512
		align 4
tp_addr		dw 0
tp_mode		dw 0
tp_quiet	dw 0
		align 8
tp_xmax		dw 640
tp_ymax		dw 480
tp_buf		rb 16
		align 8
