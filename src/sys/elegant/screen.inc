; ============================================================================
;  elegant/screen.inc - la resolution de l'ecran, a chaud (WS_OP_SCREEN_*,
;  applet Ecran du panneau de configuration ; comme kapi_screen_set d'Onyx).
;
;  Le noyau change de framebuffer (SYS_WS_MODE, ws.asm), meme adresse chez
;  nous ; le firmware met l'image a l'echelle de la sortie HDMI. Le tampon
;  arriere est deja assez grand (SCR_BB_BYTES) : seuls son pas, l'ecran et les
;  zones sales changent. Ensuite :
;  * les bords reserves (dock, en bas) suivent le bas de l'ecran, la zone libre
;    reste de meme marge ;
;  * une fenetre agrandie remplit la nouvelle zone (WEV_RESIZE), sa place
;    d'avant est ramenee dans l'ecran ; une fenetre WF_RESIZABLE trop grande
;    retrecit (WEV_RESIZE) ; les autres sont replacees, titre visible ;
;  * chaque fenetre recoit WEV_SCREEN : menubar, dock et voronoy se refont ;
;  * vncd et rdpd : tampon de copie refait a leur prochain appel (snap_reset).
;
;  Confirmation : le minuteur est ici, pas dans le panneau : si le panneau
;  meurt, ou si l'ecran n'affiche plus rien, la taille d'avant revient quand
;  meme (screen_tick).
; ============================================================================

E_BUSY		= -16
E_IO		= -5
SCR_MAX_W	= 2560			; (noyau : FB_MAX_W, FB_MAX_H)
SCR_MAX_H	= 1600
SCR_BB_BYTES	= SCR_MAX_W * SCR_MAX_H * 4
EDID_MSG	= 160			; message au firmware : un bloc EDID
EDID_DATA	= 28			; les 128 octets du bloc dans le message

; WS_OP_SCREEN_SET : a largeur, b hauteur, c attente de confirmation (ms, 0 :
; definitif) -> 0, E_BUSY, E_INVAL, E_IO
req_screen_set:
	prologue
	str	x19,[sp,-16]!
	adr	x0,scr_pending
	ldr	x0,[x0]
	cbnz	x0,.busy
	adr	x9,msg
	ldp	x0,x1,[x9,WM_A]
	ldr	x19,[x9,WM_C]
	adr	x2,scr_w
	ldp	x2,x3,[x2]
	cmp	x0,x2
	ccmp	x1,x3,0,eq
	b.eq	.ok				; (rien a faire)
	adr	x4,scr_old
	stp	x2,x3,[x4]
	bl	screen_switch
	tbnz	x0,63,.out
	cbz	x19,.ok
	sys	SYS_TIME_MS
	add	x0,x0,x19
	adr	x1,scr_deadline
	str	x0,[x1]
	adr	x1,scr_pending
	mov	x0,1
	str	x0,[x1]
.ok:	mov	x0,0
	b	.out
.busy:	mov	x0,E_BUSY
.out:	ldr	x19,[sp],16
	epilogue

; WS_OP_SCREEN_KEEP : a 1 garder, 0 revenir -> 0 (E_INVAL : rien n'attend)
req_screen_keep:
	prologue
	adr	x1,scr_pending
	ldr	x0,[x1]
	cbz	x0,.inval
	str	xzr,[x1]
	adr	x9,msg
	ldr	x0,[x9,WM_A]
	cbnz	x0,.ok
	adr	x0,scr_old
	ldp	x0,x1,[x0]
	bl	screen_switch
.ok:	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
.out:	epilogue

; (boucle principale) l'attente de confirmation depassee : la taille d'avant
screen_tick:
	prologue
	adr	x0,scr_pending
	ldr	x0,[x0]
	cbz	x0,.out
	sys	SYS_TIME_MS
	adr	x1,scr_deadline
	ldr	x1,[x1]
	cmp	x0,x1
	b.lo	.out
	adr	x0,scr_pending
	str	xzr,[x0]
	adr	x0,m_scr_back
	bl	puts
	adr	x0,scr_old
	ldp	x0,x1,[x0]
	bl	screen_switch
.out:	epilogue

; x0, x1 = nouvelle taille -> x0 = 0 ; E_IO : refusee par le firmware (la
; taille d'avant redemandee, le framebuffer a pu bouger) ; sinon rien n'a change
screen_switch:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x2,scr_w
	ldp	x19,x20,[x2]			; taille d'avant
	mov	x21,x0
	mov	x22,x1
	bl	present_wait			; le transfert en cours fini
	mov	x0,x21
	mov	x1,x22
	adr	x2,info
	sys	SYS_WS_MODE
	mov	x21,x0
	tbz	x21,63,.adopt
	cmn	x21,-E_IO
	b.ne	.out
.adopt:	adr	x0,info
	ldp	x1,x2,[x0]			; adresse, largeur
	ldp	x3,x4,[x0,16]			; hauteur, pas
	adr	x5,fb_va
	stp	x1,x4,[x5]
	adr	x5,scr_w
	stp	x2,x3,[x5]
	lsl	x2,x2,2
	adr	x5,bb_pitch
	str	x2,[x5]
	adr	x0,pres_mode
	ldr	x0,[x0]
	cbz	x0,.zones
	adr	x0,info
	ldr	x0,[x0,32]
	bl	phys_bus
	adr	x1,fb_bus
	str	x0,[x1]
	cbnz	x0,.zones
	adr	x0,pres_mode			; (framebuffer hors de portee du DMA)
	str	xzr,[x0]
.zones:	adr	x0,dirty_n			; (zones de l'ancienne taille)
	str	xzr,[x0]
	mov	x0,x19
	mov	x1,x20
	bl	screen_windows
	bl	snap_reset
	adr	x0,need_all
	str	xzr,[x0]
	bl	wins_changed
	bl	dirty_all
	adr	x0,m_scr
	bl	puts
	adr	x0,scr_w
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_scr_x
	bl	puts
	adr	x0,scr_h
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,m_nl
	bl	puts
.out:	mov	x0,x21
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0, x1 = coin du contenu d'une fenetre agrandie, x2, x3 = sa taille : la
; zone libre de l'ecran
screen_work:
	adr	x4,area_top
	ldp	x4,x5,[x4]
	adr	x6,scr_w
	ldp	x2,x3,[x6]
	mov	x0,0
	mov	x1,x4
	sub	x3,x3,x4
	sub	x3,x3,x5
	ret

; x0 = fenetre -> x0 = 1 si agrandie : ni arrondi, ni ombre (feuille)
win_is_max:
	adr	x1,windows
	sub	x0,x0,x1
	lsr	x0,x0,WIN_SHIFT
	mov	x2,40
	adr	x1,win_restore
	madd	x0,x0,x2,x1
	ldr	x0,[x0,32]
	ret

; x0, x1 = coin, x2, x3 = taille du contenu -> x0, x1 : dans l'ecran autant
; que possible, entre les bords reserves, barre de titre visible
screen_fit:
	adr	x4,scr_w
	ldp	x4,x5,[x4]
	sub	x4,x4,FRAME_GAP
	sub	x4,x4,x2
	cmp	x0,x4
	csel	x0,x4,x0,gt
	mov	x4,FRAME_GAP
	cmp	x0,x4
	csel	x0,x4,x0,lt
	adr	x6,area_top
	ldp	x6,x7,[x6]
	sub	x5,x5,x7
	sub	x5,x5,FRAME_GAP
	sub	x5,x5,x3
	cmp	x1,x5
	csel	x1,x5,x1,gt
	add	x6,x6,FRAME_GAP
	cmp	x1,x6
	csel	x1,x6,x1,lt
	ret

; x0, x1 = taille d'avant : glisser en cours oublie, fenetres replacees,
; curseur dans l'ecran, WEV_SCREEN a toutes
screen_windows:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x2,drag_win
	str	xzr,[x2]
	adr	x2,resize_win
	str	xzr,[x2]
	adr	x2,scr_h			; ce qui est reserve en bas y reste
	ldr	x3,[x2]
	sub	x3,x3,x1
	adr	x2,windows
	mov	x4,MAX_WINDOWS
.r:	ldr	x0,[x2,W_USED]
	cbz	x0,.rn
	ldr	x0,[x2,W_FLAGS]
	tbz	x0,WFB_RESERVE,.rn
	ldr	x0,[x2,W_Y]
	cbz	x0,.rn
	add	x0,x0,x3
	str	x0,[x2,W_Y]
.rn:	add	x2,x2,WIN_SIZE
	subs	x4,x4,1
	b.ne	.r
	bl	recompute_reserve
	adr	x19,windows
	mov	x20,0
.w:	ldr	x0,[x19,W_USED]
	cbz	x0,.next
	ldr	x21,[x19,W_FLAGS]
	mov	x0,WF_RESERVE or WF_BACK	; (ils se refont sur WEV_SCREEN)
	tst	x21,x0
	b.ne	.next
	tbnz	x21,WFB_FRAMELESS,.bare
	mov	x0,40
	adr	x22,win_restore
	madd	x22,x20,x0,x22
	ldr	x0,[x22,32]
	cbz	x0,.norm
	bl	screen_work			; agrandie
	str	x0,[x19,W_X]
	str	x1,[x19,W_Y]
	mov	x23,x2
	mov	x24,x3
	mov	x1,x2
	mov	x2,x3
	mov	x0,x19
	bl	resize_post
	ldp	x2,x3,[x22,16]			; sa place d'avant, dans l'ecran
	cmp	x2,x23
	csel	x2,x23,x2,gt
	cmp	x3,x24
	csel	x3,x24,x3,gt
	stp	x2,x3,[x22,16]
	ldp	x0,x1,[x22]
	bl	screen_fit
	stp	x0,x1,[x22]
	b	.next
.norm:	bl	screen_work
	ldr	x23,[x19,W_W]
	ldr	x24,[x19,W_H]
	tbz	x21,WFB_RESIZABLE,.keep
	cmp	x23,x2				; redimensionnable : dans la zone
	csel	x4,x2,x23,gt
	cmp	x24,x3
	csel	x5,x3,x24,gt
	cmp	x4,x23
	ccmp	x5,x24,0,eq
	b.eq	.keep
	mov	x23,x4
	mov	x24,x5
	mov	x0,x19
	mov	x1,x4
	mov	x2,x5
	bl	resize_post
.keep:	ldr	x0,[x19,W_X]
	ldr	x1,[x19,W_Y]
	mov	x2,x23
	mov	x3,x24
	bl	screen_fit
	str	x0,[x19,W_X]
	str	x1,[x19,W_Y]
	b	.next
.bare:	adr	x4,scr_w			; sans cadre (bulles, menus ouverts)
	ldp	x4,x5,[x4]
	ldr	x0,[x19,W_X]
	ldr	x1,[x19,W_Y]
	ldr	x2,[x19,W_W]
	ldr	x3,[x19,W_H]
	sub	x4,x4,x2
	sub	x5,x5,x3
	cmp	x0,x4
	csel	x0,x4,x0,gt
	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x1,x5
	csel	x1,x5,x1,gt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	str	x0,[x19,W_X]
	str	x1,[x19,W_Y]
.next:	add	x19,x19,WIN_SIZE
	add	x20,x20,1
	cmp	x20,MAX_WINDOWS
	b.lo	.w
	adr	x0,scr_w			; curseur
	ldp	x0,x1,[x0]
	sub	x0,x0,1
	sub	x1,x1,1
	adr	x2,cur_x
	ldp	x3,x4,[x2]
	cmp	x3,x0
	csel	x3,x0,x3,gt
	cmp	x4,x1
	csel	x4,x1,x4,gt
	stp	x3,x4,[x2]
	adr	x19,windows
	mov	x20,MAX_WINDOWS
.ev:	ldr	x0,[x19,W_USED]
	cbz	x0,.en
	adr	x1,evbuf
	stp	xzr,xzr,[x1]
	stp	xzr,xzr,[x1,16]
	mov	w0,WEV_SCREEN
	str	w0,[x1]
	adr	x2,scr_w
	ldp	x2,x3,[x2]
	str	w2,[x1,EV_X]
	str	w3,[x1,EV_Y]
	mov	x0,x19
	bl	win_push
.en:	add	x19,x19,WIN_SIZE
	subs	x20,x20,1
	b.ne	.ev
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; WS_OP_WIN_PLACE : a id, b x, c y -> 0 : une fenetre du programme a cette
; place, quelle que soit sa couche (le dock se recentre)
req_win_place:
	prologue
	str	x19,[sp,-16]!
	bl	req_win
	cbz	x0,.bad
	mov	x19,x0
	bl	pk_owns				; (mode poche : place tenue par Elegant)
	cbnz	x0,.ok
	mov	x0,x19
	bl	dirty_window
	adr	x9,msg
	ldr	x0,[x9,WM_B]
	ldr	x1,[x9,WM_C]
	str	x0,[x19,W_X]
	str	x1,[x19,W_Y]
	mov	x0,x19
	bl	dirty_window
	ldr	x0,[x19,W_FLAGS]
	tbz	x0,WFB_RESERVE,.ok
	bl	reserve_update
.ok:	mov	x0,0
	b	.out
.bad:	mov	x0,E_BADF
.out:	ldr	x19,[sp],16
	epilogue

; x0 = fenetre liberee : sa place dans win_restore est rendue
restore_clear:
	adr	x1,windows
	sub	x0,x0,x1
	lsr	x0,x0,WIN_SHIFT
	mov	x1,40
	adr	x2,win_restore
	madd	x0,x0,x1,x2
	str	xzr,[x0,32]
	ret

; une fenetre WF_RESERVE a bouge (barre, dock, a une autre echelle) : bords
; reserves recalcules ; s'ils changent, les fenetres agrandies suivent
reserve_update:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,area_top
	ldp	x19,x20,[x0]
	bl	recompute_reserve
	adr	x0,area_top
	ldp	x0,x1,[x0]
	cmp	x0,x19
	ccmp	x1,x20,0,eq
	b.eq	.all
	adr	x19,windows
	mov	x20,0
.w:	ldr	x0,[x19,W_USED]
	cbz	x0,.n
	mov	x0,40
	adr	x1,win_restore
	madd	x1,x20,x0,x1
	ldr	x0,[x1,32]
	cbz	x0,.n
	mov	x0,x19
	bl	dirty_window
	bl	screen_work
	str	x0,[x19,W_X]
	str	x1,[x19,W_Y]
	mov	x1,x2
	mov	x2,x3
	mov	x0,x19
	bl	resize_post
.n:	add	x19,x19,WIN_SIZE
	add	x20,x20,1
	cmp	x20,MAX_WINDOWS
	b.lo	.w
.all:	adr	x0,need_all
	ldr	x1,[x0]
	cbz	x1,.out
	str	xzr,[x0]
	bl	dirty_all
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Les modes selon l'ecran : la table d'Onyx (SystemKit, display.inc), les
;  marques de l'EDID de l'ecran (natif, timings etablis et standards), et ce
;  que l'EDID ou l'ecran actuel ajoutent. Tout mode reste possible : le
;  firmware met a l'echelle ; ceux de l'ecran sont les plus nets.
; ---------------------------------------------------------------------------

; WS_OP_SCREEN_MODES -> x0 = nombre ; a, b natif, c, d actuel ; donnees : WSM_*
req_screen_modes:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x19,msg
	add	x20,x19,WM_DATA
	mov	x21,0
	adr	x22,scr_modes
.c:	ldrh	w0,[x22],2
	cbz	w0,.edid
	ldrh	w1,[x22],2
	mov	x2,0
	bl	mode_mark
	b	.c
.edid:	stp	xzr,xzr,[x19,WM_A]
	bl	edid_read
	cbz	x0,.cur
	adr	x22,scr_fw
	add	x22,x22,EDID_DATA
	ldrb	w0,[x22,54]			; 1er descripteur : le timing prefere
	ldrb	w1,[x22,55]
	orr	w0,w0,w1
	cbz	w0,.est
	ldrb	w0,[x22,56]			; pixels actifs : 8 + 4 bits
	ldrb	w1,[x22,58]
	and	w1,w1,0xF0
	orr	w0,w0,w1,lsl 4
	ldrb	w1,[x22,59]
	ldrb	w2,[x22,61]
	and	w2,w2,0xF0
	orr	w1,w1,w2,lsl 4
	str	x0,[x19,WM_A]
	str	x1,[x19,WM_B]
	mov	x2,WSMF_NATIVE or WSMF_EDID
	bl	mode_mark
.est:	ldrb	w3,[x22,35]			; timings etablis
	tbz	w3,5,.e1
	mov	x0,640
	mov	x1,480
	mov	x2,WSMF_EDID
	bl	mode_mark
	ldrb	w3,[x22,35]
.e1:	tbz	w3,0,.e2
	mov	x0,800
	mov	x1,600
	mov	x2,WSMF_EDID
	bl	mode_mark
.e2:	ldrb	w3,[x22,36]
	tbz	w3,3,.e3
	mov	x0,1024
	mov	x1,768
	mov	x2,WSMF_EDID
	bl	mode_mark
	ldrb	w3,[x22,36]
.e3:	tbz	w3,0,.std
	mov	x0,1280
	mov	x1,1024
	mov	x2,WSMF_EDID
	bl	mode_mark
.std:	add	x22,x22,38			; 8 timings standards : (a + 31) x 8,
	mov	x23,8				; rapport dans b
.s:	ldrb	w0,[x22]
	ldrb	w1,[x22,1]
	add	x22,x22,2
	cmp	w0,1
	b.ls	.sn
	add	w0,w0,31
	lsl	w0,w0,3
	lsr	w1,w1,6
	cmp	w1,1
	b.eq	.r43
	cmp	w1,2
	b.eq	.r54
	cmp	w1,3
	b.eq	.r169
	add	w1,w0,w0,lsl 2			; 16:10
	lsr	w1,w1,3
	b	.sm
.r43:	add	w1,w0,w0,lsl 1
	lsr	w1,w1,2
	b	.sm
.r54:	lsl	w1,w0,2
	mov	w2,5
	udiv	w1,w1,w2
	b	.sm
.r169:	add	w1,w0,w0,lsl 3
	lsr	w1,w1,4
.sm:	mov	x2,WSMF_EDID
	bl	mode_mark
.sn:	subs	x23,x23,1
	b.ne	.s
.cur:	adr	x0,scr_w			; actuel : toujours dans la liste
	ldp	x0,x1,[x0]
	stp	x0,x1,[x19,WM_C]
	mov	x2,0
	bl	mode_mark
	mov	w0,WSM_SIZE
	mul	w0,w0,w21
	str	w0,[x19,WM_LEN]
	mov	x0,x21
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = taille, x2 = drapeaux, x20 = entrees, x21 = leur nombre : marque
; l'entree de cette taille, sinon l'ajoute si elle est possible
mode_mark:
	mov	x3,x20
	mov	x4,x21
.f:	cbz	x4,.add
	ldrh	w5,[x3,WSM_W]
	ldrh	w6,[x3,WSM_H]
	cmp	x5,x0
	ccmp	x6,x1,0,eq
	b.eq	.hit
	add	x3,x3,WSM_SIZE
	sub	x4,x4,1
	b	.f
.hit:	ldr	w5,[x3,WSM_FLAGS]
	orr	w5,w5,w2
	str	w5,[x3,WSM_FLAGS]
	ret
.add:	cmp	x21,WS_SM_N
	b.hs	.ret
	tbnz	x0,0,.ret
	cmp	x0,640
	b.lo	.ret
	cmp	x1,480
	b.lo	.ret
	cmp	x0,SCR_MAX_W
	b.hi	.ret
	cmp	x1,SCR_MAX_H
	b.hi	.ret
	strh	w0,[x3,WSM_W]
	strh	w1,[x3,WSM_H]
	str	w2,[x3,WSM_FLAGS]
	add	x21,x21,1
.ret:	ret

; -> x0 = 1 : le premier bloc EDID de l'ecran dans scr_fw (EDID_DATA), 0 : pas
; de reponse (QEMU, ecran muet)
edid_read:
	prologue
	adr	x0,scr_fw
	mov	x1,0
.z:	str	wzr,[x0,x1]
	add	x1,x1,4
	cmp	x1,EDID_MSG
	b.lo	.z
	mov	w1,EDID_MSG
	str	w1,[x0]
	movl	w1,0x00030020			; GET_EDID_BLOCK, bloc 0
	str	w1,[x0,8]
	mov	w1,136
	str	w1,[x0,12]
	sys	SYS_FW_CALL
	cbnz	x0,.non
	adr	x1,scr_fw
	ldr	w2,[x1,4]
	movl	w3,0x80000000
	cmp	w2,w3
	b.ne	.non
	ldr	w2,[x1,16]			; etiquette traitee, statut 0
	tbz	w2,31,.non
	ldr	w2,[x1,24]
	cbnz	w2,.non
	ldr	w2,[x1,EDID_DATA]		; signature 00 FF..FF 00
	movl	w3,0xFFFFFF00
	cmp	w2,w3
	b.ne	.non
	ldr	w2,[x1,EDID_DATA+4]
	movl	w3,0x00FFFFFF
	cmp	w2,w3
	b.ne	.non
	mov	x0,1
	epilogue
.non:	mov	x0,0
	epilogue

scr_modes	dh 640, 480, 800, 480, 800, 600, 1024, 768, 1280, 720, 1280, 800
		dh 1280, 1024, 1366, 768, 1440, 900, 1600, 900, 1600, 1200, 1680, 1050
		dh 1920, 1080, 1920, 1200, 2560, 1440, 0
m_scr		db 'elegant : ecran ',0
m_scr_x		db 'x',0
m_scr_back	db 'elegant : resolution non confirmee, retour a la precedente',10,0
		align 4

macro screen_data {
		align 8
scr_pending	dd 0				; 1 : confirmation attendue,
scr_deadline	dd 0				; jusqu'a cette heure (ms)
scr_old		rd 2				; taille d'avant (largeur, hauteur)
scr_fw		rb EDID_MSG
}
