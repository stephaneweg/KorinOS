; ============================================================================
;  elegant/core.inc - coeur du serveur graphique, commun a tous les modes :
;  fenetres, recomposition, routage des entrees, requetes des programmes.
;  Ce qui change d'un mode a l'autre (fond, session, reglages) passe par la
;  table du mode actif (POL_*, comme struct ws_policy d'Onyx).
;
;  Recomposition : les zones sales (une liste de rectangles) sont redessinees
;  dans un tampon arriere (memoire du serveur, cacheable), puis presentees au
;  framebuffer (present.inc : DMA 2D asynchrone, ou copie par le processeur).
;  Chaque fenetre a ses images dans un tampon partage (buf_create) que le
;  programme mappe chez lui (buf_map) : le contenu, puis deux copies du cadre
;  (active, inactive), dessinees par UIKit comme dans Onyx. Le serveur ne
;  dessine aucun texte : il compose, et route les entrees.
;
;  Roles (WF_* de armpi.inc) : trois couches (fond, normale, dessus), sans
;  cadre, sans focus, hors liste, a l'ecoute de la liste, capture (menu
;  ouvert), reserve d'un bord (barre de menus, dock), transparence.
; ============================================================================

; --- entrees (anneau de HIDManager, format commun des pilotes) --------------
IN_KEY		= 1			; +4 caractere, +8 code HID, +16 appui, +24 mods
IN_MOUSE_REL	= 2			; +8 dx, +16 dy, +24 boutons
IN_MOUSE_ABS	= 3			; +8 x, +16 y (0..32767), +24 boutons
IN_WHEEL	= 4			; +8 crans
HID_OP_ABONNER	= 1			; service � hid � (src/svc/hidmanager/hidmanager.asm)
HID_ANNEAU_EV	= 64			; anneau : +0 tete, +8 queue, evenements
HID_ANNEAU_N	= 64

; --- fenetres ----------------------------------------------------------------
MAX_WINDOWS	= 32
WEV_SIZE	= 32
TRANSPARENT	= 0xFF000000		; pixel invisible (octet haut : transparence)

W_USED		= 0
W_ID		= 8
W_PID		= 16
W_X		= 24			; contenu, coordonnees ecran
W_Y		= 32
W_W		= 40
W_H		= 48
W_SURF		= 56			; contenu (tampon partage, chez le serveur)
W_BUF		= 64			; numero du tampon partage
W_FLAGS		= 72			; role (WF_*)
W_MINI		= 80			; non nul : cachee ; MINI_REDUITE, MINI_HORS_BUREAU
W_CHR_A		= 88			; cadre actif (largeur + 2 bords, hauteur + titre + bord)
W_CHR_I		= 96			; cadre inactif
W_NUPD		= 104			; (diagnostic) mises a jour recues
W_MLEN		= 112			; texte des menus : longueur (0 : aucun ; menu.inc)
W_MGEN		= 120			; generation du menu (unique par envoi)
W_TITLE		= 128			; UTF-8, 128 octets
W_GEN		= 248			; generation du contenu (32 bits ; WS_OP_UPDATE)
W_CGEN		= 252			; generation du cadre (32 bits ; WS_OP_FRAME)
W_DMG		= 256			; en-tete WD_* en fin de tampon (ws_ext.inc)
W_FRAMEP	= 264			; 1 : WEV_FRAME a envoyer
W_DESK		= 272			; son espace de travail + 1 (0 : tous)
WIN_SHIFT	= 9
WIN_SIZE	= 1 shl WIN_SHIFT
MINI_REDUITE	= 1			; (W_MINI) le programme ou le cadre l'a reduite
MINI_HORS_BUREAU = 2			; (W_MINI) sur un autre espace de travail
MINI_CURTAIN	= 4			; (W_MINI) cachee par une fenetre WF_CURTAIN

; bits de WF_*
WFB_FRAMELESS	= 0
WFB_BACK	= 1
WFB_TOP		= 2
WFB_NOFOCUS	= 3
WFB_NOLIST	= 4
WFB_RESIZABLE	= 9			; (ws_ext.inc : WF_RESIZABLE)
WFB_LISTEN	= 5
WFB_GRAB	= 6
WFB_RESERVE	= 7
WFB_ALPHA	= 8
WFB_MODAL	= 10			; (ws_ext.inc : WF_MODAL)
WFB_CURTAIN	= 11			; (ws_ext.inc : WF_CURTAIN)

; zones (win_hit)
Z_TITLE		= 1
Z_CLOSE		= 2
Z_CLIENT	= 3
Z_BORDER	= 4
Z_MINI		= 5
Z_MAXI		= 6			; agrandir (fenetre WF_RESIZABLE)

; ---------------------------------------------------------------------------
;  Zones sales
; ---------------------------------------------------------------------------

; Une petite liste de rectangles (dirty_list : x0, y0, x1, y1 ; dirty_n),
; pas une seule union : une horloge en haut a droite et le curseur en bas a
; gauche ne font plus recomposer tout l'ecran. Une zone nouvelle est fusionnee
; avec une zone de la liste quand leur union coute peu (aire de l'union <=
; somme des aires + DIRTY_SLACK : zones qui se chevauchent, se touchent, ou
; proches) ; l'union fusionnee est reessayee contre les autres. Liste pleine :
; repli sur l'union de tout (une seule zone).
DIRTY_MAX	= 16
DIRTY_SLACK	= 4096			; pixels redessines en trop acceptes

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur (ecran) : ajoute une zone sale
dirty:
	cmp	x2,0
	b.le	.ret
	cmp	x3,0
	b.le	.ret
	add	x2,x0,x2			; -> x0,y0,x1,y1 bornes a l'ecran
	add	x3,x1,x3
	cmp	x0,0
	csel	x0,xzr,x0,lt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	adr	x4,scr_w
	ldp	x4,x5,[x4]			; scr_w, scr_h
	cmp	x2,x4
	csel	x2,x4,x2,gt
	cmp	x3,x5
	csel	x3,x5,x3,gt
	cmp	x0,x2
	b.ge	.ret
	cmp	x1,x3
	b.ge	.ret
	adr	x6,dirty_list
.again:	adr	x4,dirty_n
	ldr	x5,[x4]
	mov	x7,0
.scan:	cmp	x7,x5
	b.hs	.add
	add	x8,x6,x7,lsl 5			; zone i
	ldp	x9,x10,[x8]
	ldp	x11,x12,[x8,16]
	cmp	x0,x9				; union : x13, x14, x15, x16
	csel	x13,x0,x9,lt
	cmp	x1,x10
	csel	x14,x1,x10,lt
	cmp	x2,x11
	csel	x15,x2,x11,gt
	cmp	x3,x12
	csel	x16,x3,x12,gt
	sub	x11,x11,x9			; aire de la zone i
	sub	x12,x12,x10
	mul	x11,x11,x12
	sub	x9,x2,x0			; + aire de la nouvelle + marge
	sub	x10,x3,x1
	madd	x11,x9,x10,x11
	mov	x9,DIRTY_SLACK
	add	x11,x11,x9
	sub	x9,x15,x13			; aire de l'union
	sub	x10,x16,x14
	mul	x9,x9,x10
	cmp	x9,x11
	b.hi	.next
	mov	x0,x13				; fusion : la nouvelle devient l'union,
	mov	x1,x14				; la zone i sort (la derniere a sa place)
	mov	x2,x15
	mov	x3,x16
	sub	x5,x5,1
	str	x5,[x4]
	add	x9,x6,x5,lsl 5
	ldp	x10,x11,[x9]
	ldp	x12,x13,[x9,16]
	stp	x10,x11,[x8]
	stp	x12,x13,[x8,16]
	b	.again				; l'union, contre les autres
.next:	add	x7,x7,1
	b	.scan
.add:	cmp	x5,DIRTY_MAX
	b.hs	.full
	add	x8,x6,x5,lsl 5
	stp	x0,x1,[x8]
	stp	x2,x3,[x8,16]
	add	x5,x5,1
	str	x5,[x4]
.ret:	ret
.full:	mov	x7,0				; liste pleine : l'union de tout
.u:	cmp	x7,x5
	b.hs	.one
	add	x8,x6,x7,lsl 5
	ldp	x9,x10,[x8]
	ldp	x11,x12,[x8,16]
	cmp	x9,x0
	csel	x0,x9,x0,lt
	cmp	x10,x1
	csel	x1,x10,x1,lt
	cmp	x11,x2
	csel	x2,x11,x2,gt
	cmp	x12,x3
	csel	x3,x12,x3,gt
	add	x7,x7,1
	b	.u
.one:	stp	x0,x1,[x6]
	stp	x2,x3,[x6,16]
	mov	x5,1
	str	x5,[x4]
	adr	x4,dirty_overflow		; (diagnostic F11)
	ldr	x5,[x4]
	add	x5,x5,1
	str	x5,[x4]
	ret

dirty_all:
	mov	x0,0
	mov	x1,0
	adr	x2,scr_w
	ldp	x2,x3,[x2]
	b	dirty

; x0 = fenetre -> x0, x1, x2, x3 = rectangle exterieur (cadre compris)
win_outer:
	ldr	x4,[x0,W_FLAGS]
	ldr	x1,[x0,W_Y]
	ldr	x2,[x0,W_W]
	ldr	x3,[x0,W_H]
	ldr	x0,[x0,W_X]
	tbnz	x4,WFB_FRAMELESS,.ret
	sub	x0,x0,FRAME_BORDER
	sub	x1,x1,FRAME_TITLE_H
	add	x2,x2,2*FRAME_BORDER
	add	x3,x3,FRAME_TITLE_H+FRAME_BORDER
.ret:	ret

; x0 = fenetre : marque son rectangle exterieur
dirty_window:
	prologue
	bl	win_outer
	bl	dirty
	epilogue

; x0 = fenetre -> x0 = couche : 0 fond, 1 normale, 2 dessus
win_layer:
	ldr	x1,[x0,W_FLAGS]
	mov	x0,1
	tbz	x1,WFB_BACK,.n
	mov	x0,0
	ret
.n:	tbz	x1,WFB_TOP,.r
	mov	x0,2
.r:	ret

; la liste des fenetres a change : les fenetres a l'ecoute seront prevenues
wins_changed:
	adr	x0,wins_dirty
	mov	x1,1
	str	x1,[x0]
	ret

; (diagnostic) x0 = genre, x1..x4 : une entree dans l'historique (F11)
hist:
	adr	x5,hist_n
	ldr	x6,[x5]
	add	x7,x6,1
	str	x7,[x5]
	and	x6,x6,63
	mov	x7,40
	adr	x5,hist_buf
	madd	x5,x6,x7,x5
	stp	x0,x1,[x5]
	stp	x2,x3,[x5,16]
	str	x4,[x5,32]
	ret

; ---------------------------------------------------------------------------
;  Dessin dans le tampon arriere, decoupe par le clip courant
;  (clip_r : x0, y0, x1, y1)
; ---------------------------------------------------------------------------

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur -> intersection avec le clip :
; x0,x1 = coin, x2,x3 = taille (<= 0 si vide)
clip_rect:
	adr	x4,clip_r
	ldp	x5,x6,[x4]
	ldp	x7,x8,[x4,16]
	add	x2,x0,x2
	add	x3,x1,x3
	cmp	x0,x5
	csel	x0,x5,x0,lt
	cmp	x1,x6
	csel	x1,x6,x1,lt
	cmp	x2,x7
	csel	x2,x7,x2,gt
	cmp	x3,x8
	csel	x3,x8,x3,gt
	sub	x2,x2,x0
	sub	x3,x3,x1
	ret

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur, w4 = couleur
bb_fill:
	prologue
	str	x4,[sp,-16]!
	bl	clip_rect
	ldr	x4,[sp],16
	cmp	x2,0
	b.le	.out
	cmp	x3,0
	b.le	.out
	adr	x5,bb
	ldp	x5,x6,[x5]			; bb, bb_pitch
	madd	x5,x1,x6,x5
	add	x5,x5,x0,lsl 2
	orr	x4,x4,x4,lsl 32			; deux pixels par str
.row:	mov	x7,x5
	mov	x8,x2
.px2:	cmp	x8,2
	b.lo	.px1
	str	x4,[x7],8
	sub	x8,x8,2
	b	.px2
.px1:	cbz	x8,.next
	str	w4,[x7]
.next:	add	x5,x5,x6
	subs	x3,x3,1
	b.ne	.row
.out:	epilogue

; x0 = source, x1 = pas source (octets), x2 = x, x3 = y (ecran), x4 = largeur,
; x5 = hauteur, x6 = 1 : transparence (octet haut : 0 opaque, 255 invisible)
; Copie decoupee par le clip.
bb_blit:
	mov	x6,0
bb_blit_any:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x6
	mov	x0,x2
	mov	x1,x3
	mov	x2,x4
	mov	x3,x5
	bl	clip_rect
	cmp	x2,0
	b.le	.out
	cmp	x3,0
	b.le	.out
	sub	x4,x0,x21			; decalage dans la source
	sub	x5,x1,x22
	madd	x19,x5,x20,x19
	add	x19,x19,x4,lsl 2
	adr	x5,bb
	ldp	x5,x6,[x5]
	madd	x5,x1,x6,x5
	add	x5,x5,x0,lsl 2
	cbnz	x23,.aset
.row:	mov	x7,x5				; --- opaque
	mov	x8,x19
	mov	x9,x2
.p4:	cmp	x9,4
	b.lo	.p1
	ldp	x10,x11,[x8],16
	stp	x10,x11,[x7],16
	sub	x9,x9,4
	b	.p4
.p1:	cbz	x9,.next
	ldr	w10,[x8],4
	str	w10,[x7],4
	sub	x9,x9,1
	b	.p1
.next:	add	x5,x5,x6
	add	x19,x19,x20
	subs	x3,x3,1
	b.ne	.row
	b	.out
.aset:	movl	w10,0x01010101			; constantes du melange
	dup	v28.4s,w10
	movi	v29.8h,1
	movl	w10,0x00FFFFFF
	dup	v30.4s,w10
.arow:	mov	x7,x5				; --- avec transparence
	mov	x8,x19
	mov	x9,x2
.a4:	cmp	x9,4				; quatre pixels, NEON : t = transparence
	b.lo	.a1
	ld1	{v16.16b},[x8],16
	ushr	v17.4s,v16.4s,24
	umaxv	s18,v17.4s
	fmov	w10,s18
	cbz	w10,.a4put			; tous opaques
	uminv	s18,v17.4s
	fmov	w10,s18
	cmp	w10,255
	b.eq	.a4next				; tous invisibles
	ld1	{v19.16b},[x7]
	mul	v17.4s,v17.4s,v28.4s		; t sur les quatre octets
	mvn	v18.16b,v17.16b			; 255 - t
	umull	v20.8h,v16.8b,v18.8b		; (src * (255 - t) + dst * t) / 255
	umull2	v21.8h,v16.16b,v18.16b
	umlal	v20.8h,v19.8b,v17.8b
	umlal2	v21.8h,v19.16b,v17.16b
	ushr	v22.8h,v20.8h,8
	ushr	v23.8h,v21.8h,8
	add	v20.8h,v20.8h,v22.8h
	add	v21.8h,v21.8h,v23.8h
	add	v20.8h,v20.8h,v29.8h
	add	v21.8h,v21.8h,v29.8h
	shrn	v16.8b,v20.8h,8
	shrn2	v16.16b,v21.8h,8
	and	v16.16b,v16.16b,v30.16b		; (opaque)
.a4put:	st1	{v16.16b},[x7]
.a4next:
	add	x7,x7,16
	sub	x9,x9,4
	b	.a4
.a1:	cbz	x9,.anl
.apx:	ldr	w10,[x8],4
	lsr	w11,w10,24			; transparence
	cbz	w11,.put
	cmp	w11,255
	b.eq	.anext
	ldr	w12,[x7]			; melange, canal par canal :
	mov	w13,255				; (src * (255 - t) + dst * t) / 255
	sub	w13,w13,w11
	mov	w17,0
	mov	w14,0
.ch:	lsrv	w15,w10,w14
	and	w15,w15,0xFF
	lsrv	w16,w12,w14
	and	w16,w16,0xFF
	mul	w15,w15,w13
	madd	w15,w16,w11,w15
	add	w16,w15,w15,lsr 8		; / 255
	add	w16,w16,1
	lsr	w16,w16,8
	lslv	w16,w16,w14
	orr	w17,w17,w16
	add	w14,w14,8
	cmp	w14,24
	b.lo	.ch
	str	w17,[x7]
	b	.anext
.put:	str	w10,[x7]
.anext:	add	x7,x7,4
	subs	x9,x9,1
	b.ne	.apx
.anl:
	add	x5,x5,x6
	add	x19,x19,x20
	subs	x3,x3,1
	b.ne	.arow
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Recomposition
; ---------------------------------------------------------------------------

; Les zones sales sont prises (cmp_list ; d'autres peuvent se marquer pendant
; le dessin : elles iront a la recomposition suivante), redessinees une a une
; dans le tampon arriere (clip = la zone), puis presentees a l'ecran ensemble
; (present.inc : DMA 2D asynchrone, ou copie par le processeur). Avant de
; dessiner, le transfert precedent doit etre fini (le DMA lit le tampon arriere).
compose:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,dirty_n
	ldr	x0,[x0]
	cbz	x0,.out
	bl	present_wait			; (peut tout salir : repli processeur)
	adr	x0,dirty_n
	ldr	x19,[x0]
	cbz	x19,.out
	str	xzr,[x0]
	adr	x0,cmp_n
	str	x19,[x0]
	adr	x0,dirty_list
	adr	x1,cmp_list
	mov	x2,x19
.copy:	ldp	x3,x4,[x0],16
	stp	x3,x4,[x1],16
	ldp	x3,x4,[x0],16
	stp	x3,x4,[x1],16
	subs	x2,x2,1
	b.ne	.copy
	mov	x20,0
.rect:	cmp	x20,x19
	b.hs	.show
	adr	x0,cmp_list
	add	x0,x0,x20,lsl 5
	ldp	x1,x2,[x0]			; clip = la zone
	ldp	x3,x4,[x0,16]
	adr	x0,clip_r
	stp	x1,x2,[x0]
	stp	x3,x4,[x0,16]
	mov	x0,100				; (historique : recomposition, zone)
	bl	hist
	bl	compose_clip
	add	x20,x20,1
	b	.rect
.show:	bl	present				; cmp_list -> ecran
	adr	x0,cmp_list			; union des zones (vncd, screenshot)
	ldp	x0,x1,[x0]
	adr	x5,cmp_list
	ldp	x2,x3,[x5,16]
	mov	x6,1
.un:	cmp	x6,x19
	b.hs	.rec
	add	x7,x5,x6,lsl 5
	ldp	x8,x9,[x7]
	ldp	x10,x11,[x7,16]
	cmp	x8,x0
	csel	x0,x8,x0,lt
	cmp	x9,x1
	csel	x1,x9,x1,lt
	cmp	x10,x2
	csel	x2,x10,x2,gt
	cmp	x11,x3
	csel	x3,x11,x3,gt
	add	x6,x6,1
	b	.un
.rec:	bl	snap_record			; (snapshot.inc)
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; redessine la zone clip_r dans le tampon arriere : fond, fenetres couche par
; couche (du fond vers l'avant), barres et menus, curseur
compose_clip:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	bl	surf_cover			; une surface couvre la zone : rien dessous
	mov	x24,x0
	cbnz	x24,.noback
	pol_call POL_BACK
.noback:
	adr	x19,win_z			; fenetres, couche par couche, du fond
	adr	x0,win_count			; vers l'avant
	ldr	x20,[x0]
	mov	x22,0
.layer:	mov	x21,0
.w:	cmp	x21,x20
	b.hs	.lnext
	ldr	x23,[x19,x21,lsl 3]
	add	x21,x21,1
	ldr	x0,[x23,W_MINI]
	cbnz	x0,.w
	mov	x0,x23
	bl	win_layer
	cmp	x0,x22
	b.ne	.w
	cbz	x24,.draw
	cmp	x23,x24				; celles d'avant sont dessous
	b.ne	.w
	mov	x24,0
	ldr	x0,[x23,W_FLAGS]
	tbnz	x0,WFB_FRAMELESS,.bare
	adr	x0,cover_only
	mov	x1,1
	str	x1,[x0]
	mov	x0,x23
	bl	draw_framed
	adr	x0,cover_only
	str	xzr,[x0]
	b	.w
.bare:	mov	x0,x23
	ldr	x1,[x23,W_X]
	ldr	x2,[x23,W_Y]
	bl	draw_surface
	b	.w
.draw:	mov	x0,x23
	bl	draw_window
	b	.w
.lnext:	add	x22,x22,1
	cmp	x22,3
	b.lo	.layer
	pol_call POL_FRONT
	adr	x0,cur_x			; le curseur, s'il touche la zone
	ldp	x0,x1,[x0]
	sub	x0,x0,8
	sub	x1,x1,10
	adr	x4,clip_r
	ldp	x5,x6,[x4]
	ldp	x7,x8,[x4,16]
	cmp	x0,x7
	b.ge	.out
	cmp	x1,x8
	b.ge	.out
	add	x0,x0,20
	cmp	x0,x5
	b.le	.out
	add	x1,x1,29
	cmp	x1,x6
	b.le	.out
	bl	draw_cursor
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1, x2, x3 = zone (x0, y0, x1, y1) : copie du tampon arriere vers le
; framebuffer, par le processeur
flush_rect:
	sub	x3,x3,x1			; hauteur
	sub	x4,x2,x0			; largeur
	mov	x2,x1
	mov	x1,x0
	mov	x0,x3
	mov	x3,x4
	mov	x4,x0
	cmp	x3,0
	b.le	.ret
	cmp	x4,0
	b.le	.ret
	adr	x5,bb
	ldp	x5,x6,[x5]
	madd	x5,x2,x6,x5
	add	x5,x5,x1,lsl 2
	adr	x7,fb_va
	ldp	x7,x8,[x7]			; fb_va, fb_pitch
	madd	x7,x2,x8,x7
	add	x7,x7,x1,lsl 2
	adr	x14,fb_alpha			; (Pi 5 : pixels opaques)
	ldr	x14,[x14]
.row:	mov	x9,x5
	mov	x10,x7
	mov	x11,x3
.p4:	cmp	x11,4
	b.lo	.p1
	ldp	x12,x13,[x9],16
	orr	x12,x12,x14
	orr	x13,x13,x14
	stp	x12,x13,[x10],16
	sub	x11,x11,4
	b	.p4
.p1:	cbz	x11,.next
	ldr	w12,[x9],4
	orr	w12,w12,w14
	str	w12,[x10],4
	sub	x11,x11,1
	b	.p1
.next:	add	x5,x5,x6
	add	x7,x7,x8
	subs	x4,x4,1
	b.ne	.row
.ret:	ret

; x0 = fenetre : cadre (copie active ou inactive), puis contenu
draw_window:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,W_FLAGS]
	tbnz	x20,WFB_FRAMELESS,.content
	ldr	x9,[x19,W_CHR_I]
	adr	x1,win_focus
	ldr	x1,[x1]
	cmp	x1,x19
	b.ne	.chrome
	ldr	x9,[x19,W_CHR_A]
.chrome:					; agrandie : le contenu seul
	mov	x0,x19
	bl	win_is_max
	cbz	x0,.shadow
	mov	x0,x19
	bl	draw_framed
	ldp	x19,x20,[sp],16
	epilogue
.shadow:					; le cadre, sans ce que le contenu couvre :
	stp	x21,x22,[sp,-16]!		; barre de titre, bords gauche, droit, bas
	stp	x23,x24,[sp,-16]!		; (coins arrondis, ombre : transparence)
	stp	x25,x26,[sp,-16]!
	mov	x25,x9				; image du cadre
	mov	x0,x19
	bl	win_outer
	mov	x21,x0				; ox, oy, ow, oh
	mov	x22,x1
	mov	x23,x2
	mov	x24,x3
	lsl	x26,x23,2			; pas de l'image
	adr	x0,mesure_opaque
	ldr	x6,[x0]
	eor	x6,x6,1
	str	x6,[sp,-16]!
	mov	x0,x25				; barre de titre
	mov	x1,x26
	mov	x2,x21
	mov	x3,x22
	mov	x4,x23
	mov	x5,FRAME_TITLE_H
	bl	bb_blit_any
	mov	x0,FRAME_TITLE_H		; bord gauche
	madd	x0,x0,x26,x25
	mov	x1,x26
	mov	x2,x21
	add	x3,x22,FRAME_TITLE_H
	mov	x4,FRAME_BORDER
	sub	x5,x24,FRAME_TITLE_H
	ldr	x6,[sp]
	bl	bb_blit_any
	mov	x0,FRAME_TITLE_H		; bord droit
	madd	x0,x0,x26,x25
	sub	x1,x23,FRAME_BORDER
	add	x0,x0,x1,lsl 2
	mov	x1,x26
	add	x2,x21,x23
	sub	x2,x2,FRAME_BORDER
	add	x3,x22,FRAME_TITLE_H
	mov	x4,FRAME_BORDER
	sub	x5,x24,FRAME_TITLE_H
	ldr	x6,[sp]
	bl	bb_blit_any
	sub	x0,x24,FRAME_BORDER		; bord bas
	madd	x0,x0,x26,x25
	add	x0,x0,FRAME_BORDER*4
	mov	x1,x26
	add	x2,x21,FRAME_BORDER
	add	x3,x22,x24
	sub	x3,x3,FRAME_BORDER
	sub	x4,x23,2*FRAME_BORDER
	mov	x5,FRAME_BORDER
	ldr	x6,[sp],16
	bl	bb_blit_any
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	mov	x0,x19
	bl	draw_framed
	ldp	x19,x20,[sp],16
	epilogue
.content:
	ldr	x0,[x19,W_SURF]
	ldr	x1,[x19,W_W]
	lsl	x1,x1,2
	ldr	x2,[x19,W_X]
	ldr	x3,[x19,W_Y]
	ldr	x4,[x19,W_W]
	ldr	x5,[x19,W_H]
	ubfx	x6,x20,WFB_ALPHA,1
	adr	x0,mesure_opaque		; (mesure : sans transparence)
	ldr	x0,[x0]
	bic	x6,x6,x0
	ldr	x0,[x19,W_SURF]
	bl	bb_blit_any
	mov	x0,x19				; la surface attachee par-dessus
	ldr	x1,[x19,W_X]			; (elegant/surface.inc)
	ldr	x2,[x19,W_Y]
	bl	draw_surface
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fenetre (son cadre dessine) : les coins du cadre (rayon FRAME_RADIUS),
; le contenu sans ses coins, sa surface, les boutons par-dessus
draw_framed:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	ldr	x25,[x19,W_CHR_I]
	adr	x1,win_focus
	ldr	x1,[x1]
	cmp	x1,x19
	b.ne	.geo
	ldr	x25,[x19,W_CHR_A]
.geo:	ldr	x21,[x19,W_W]
	ldr	x22,[x19,W_H]
	ldr	x23,[x19,W_X]
	ldr	x24,[x19,W_Y]
	add	x26,x21,2*FRAME_BORDER
	lsl	x26,x26,2			; pas de l'image
	adr	x0,cover_only			; (surf_cover : rien sous la surface)
	ldr	x0,[x0]
	cbnz	x0,.surf
	mov	x0,x19				; agrandie : carree
	bl	win_is_max
	cbz	x0,.round
	mov	x0,0
	mov	x1,0
	mov	x2,x22
	bl	nt_rows
	b	.surf
.round:	mov	x0,0				; les coins
	mov	x1,0
	mov	x2,FRAME_RADIUS
	mov	x3,FRAME_RADIUS
	bl	nt_part
	sub	x0,x21,FRAME_RADIUS
	mov	x1,0
	mov	x2,FRAME_RADIUS
	mov	x3,FRAME_RADIUS
	bl	nt_part
	mov	x0,0
	sub	x1,x22,FRAME_RADIUS
	mov	x2,FRAME_RADIUS
	mov	x3,FRAME_RADIUS
	bl	nt_part
	sub	x0,x21,FRAME_RADIUS
	sub	x1,x22,FRAME_RADIUS
	mov	x2,FRAME_RADIUS
	mov	x3,FRAME_RADIUS
	bl	nt_part
	mov	x20,0				; contenu : lignes coupees, en haut et en bas
.c:	adr	x0,nt_inset
	ldrb	w1,[x0,x20]
	mov	x0,x20
	mov	x2,1
	bl	nt_rows
	adr	x0,nt_inset
	ldrb	w1,[x0,x20]
	sub	x0,x22,1
	sub	x0,x0,x20
	mov	x2,1
	bl	nt_rows
	add	x20,x20,1
	cmp	x20,FRAME_CORNER
	b.lo	.c
	mov	x0,FRAME_CORNER			; le reste
	mov	x1,0
	sub	x2,x22,2*FRAME_CORNER
	bl	nt_rows
.surf:	mov	x0,x19
	mov	x1,x23
	mov	x2,x24
	bl	draw_surface
	sub	x0,x21,FRAME_CAP_MARGIN+FRAME_CAP_W	; les boutons, par-dessus
	mov	x1,FRAME_CAP_MARGIN
	mov	x2,FRAME_CAP_W
	mov	x3,FRAME_CAP_H
	bl	nt_part
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (draw_framed) x0, x1 = coin dans le contenu, x2, x3 = taille : ce morceau de
; l'image du cadre, avec sa transparence
nt_part:
	prologue
	add	x9,x1,FRAME_BORDER
	madd	x9,x9,x26,x25
	add	x10,x0,FRAME_BORDER
	add	x9,x9,x10,lsl 2
	mov	x4,x2
	mov	x5,x3
	add	x2,x23,x0
	add	x3,x24,x1
	mov	x0,x9
	mov	x1,x26
	mov	x6,1
	bl	bb_blit_any
	epilogue

; (draw_framed) x0 = premiere ligne, x1 = retrait a chaque bout, x2 = lignes :
; ces lignes du contenu
nt_rows:
	prologue
	lsl	x4,x21,2			; pas du contenu
	ldr	x9,[x19,W_SURF]
	madd	x9,x0,x4,x9
	add	x9,x9,x1,lsl 2
	add	x3,x24,x0
	mov	x5,x2
	add	x2,x23,x1
	sub	x6,x21,x1,lsl 1
	mov	x1,x4
	mov	x4,x6
	mov	x0,x9
	ldr	x6,[x19,W_FLAGS]
	ubfx	x6,x6,WFB_ALPHA,1
	bl	bb_blit_any
	epilogue

nt_inset	db 7,5,4,3,2,1,1,0		; retrait des FRAME_CORNER lignes du bord
		align 4

; curseur fleche (12 x 19) dessine par-dessus tout
draw_cursor:
	adr	x9,lock_win			; (souris relative : pas de curseur)
	ldr	x9,[x9]
	cbz	x9,.go
	ret
.go:	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	cursor_pick			; forme (uikit_ops.inc) -> x0 = dessin,
	mov	x21,x0				; x1, x2 = point chaud
	adr	x0,cur_x
	ldp	x19,x20,[x0]
	sub	x19,x19,x1
	sub	x20,x20,x2
	mov	x22,0				; ligne
.row:	cmp	x22,19
	b.hs	.out
	mov	x9,0
.col:	cmp	x9,12
	b.hs	.nrow
	ldrb	w10,[x21],1
	cmp	w10,' '
	b.eq	.ncol
	movl	w4,0x00FFFFFF
	cmp	w10,'X'
	b.ne	.draw
	mov	w4,0
.draw:	stp	x9,x21,[sp,-16]!
	add	x0,x19,x9
	add	x1,x20,x22
	mov	x2,1
	mov	x3,1
	bl	bb_fill
	ldp	x9,x21,[sp],16
.ncol:	add	x9,x9,1
	b	.col
.nrow:	add	x22,x22,1
	b	.row
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

cursor_shape:
	db 'X           '
	db 'XX          '
	db 'X.X         '
	db 'X..X        '
	db 'X...X       '
	db 'X....X      '
	db 'X.....X     '
	db 'X......X    '
	db 'X.......X   '
	db 'X........X  '
	db 'X.........X '
	db 'X..........X'
	db 'X......XXXXX'
	db 'X...X..X    '
	db 'X..XX..X    '
	db 'X.X  X..X   '
	db 'XX   X..X   '
	db 'X     X..X  '
	db '      XXX   '
		align 4

; ---------------------------------------------------------------------------
;  Entrees
; ---------------------------------------------------------------------------

; abonnement aux entrees : HIDManager (service � hid �) rend un tampon
; partage, l'anneau des evenements, lu ici sans appel systeme
hid_abonner:
	prologue
	adr	x0,s_hid
	mov	x1,10000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.ko
	adr	x1,msg
	mov	x2,HID_OP_ABONNER
	str	x2,[x1]
	sys	SYS_SVC_CALL
	tbnz	x0,63,.ko
	sys	SYS_BUF_MAP
	tbnz	x0,63,.ko
	adr	x1,hid_anneau
	str	x0,[x1]
	b	.out
.ko:	adr	x0,m_sans_hid
	bl	puts
.out:	epilogue

; vide l'anneau des entrees (HIDManager)
do_input:
	prologue
	str	x19,[sp,-16]!
	adr	x0,hid_anneau
	ldr	x19,[x0]
	cbz	x19,.out
.e:	ldp	x0,x1,[x19]			; tete, queue
	cmp	x0,x1
	b.eq	.out
	dmb	ish				; (l'evenement apres la tete)
	and	x1,x1,HID_ANNEAU_N-1
	add	x0,x19,x1,lsl 5
	add	x0,x0,HID_ANNEAU_EV
	bl	in_event
	dmb	ish				; lu avant de rendre la place
	ldr	x1,[x19,8]
	add	x1,x1,1
	str	x1,[x19,8]
	b	.e
.out:	ldr	x19,[sp],16
	epilogue

; -> x0 = fenetre qui recoit le clavier : celle qui capture, sinon l'active
key_target:
	prologue
	bl	find_grab
	cbnz	x0,.out
	adr	x0,win_focus
	ldr	x0,[x0]
.out:	epilogue

; -> x0 = fenetre visible qui capture (WF_GRAB, la plus en avant), 0 sinon
find_grab:
	adr	x1,win_count
	ldr	x2,[x1]
	adr	x1,win_z
.w:	cbz	x2,.none
	sub	x2,x2,1
	ldr	x0,[x1,x2,lsl 3]
	ldr	x3,[x0,W_MINI]
	cbnz	x3,.w
	ldr	x3,[x0,W_FLAGS]
	tbz	x3,WFB_GRAB,.w
	ret
.none:	mov	x0,0
	ret

; x0 = entree brute
in_event:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	w0,[x19]
	cmp	w0,IN_KEY
	b.eq	.key
	cmp	w0,IN_WHEEL
	b.eq	.wheel
	cmp	w0,17				; EV_PAD (inc/pad.inc)
	b.eq	.pad
	adr	x2,cur_x
	ldp	x3,x4,[x2]
	ldr	x5,[x19,24]			; boutons
	adr	x6,scr_w
	ldp	x6,x7,[x6]			; scr_w, scr_h
	adr	x8,lock_win
	ldr	x8,[x8]
	cbnz	x8,.locked
	cmp	w0,IN_MOUSE_ABS
	b.eq	.abs
	cmp	w0,IN_MOUSE_REL
	b.ne	.out
	ldr	x0,[x19,8]			; relatif
	ldr	x1,[x19,16]
	add	x0,x3,x0
	add	x1,x4,x1
	b	.move
.abs:	ldr	x0,[x19,8]
	mul	x0,x0,x6
	lsr	x0,x0,15
	ldr	x1,[x19,16]
	mul	x1,x1,x7
	lsr	x1,x1,15
.move:	cmp	x0,0				; borner a l'ecran
	csel	x0,xzr,x0,lt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	sub	x6,x6,1
	cmp	x0,x6
	csel	x0,x6,x0,gt
	sub	x7,x7,1
	cmp	x1,x7
	csel	x1,x7,x1,gt
	mov	x2,x5
	bl	mouse
	b	.out
.locked:					; souris relative : la fenetre, si elle a le clavier
	bl	key_target
	adr	x8,lock_win
	ldr	x8,[x8]
	cmp	x0,x8
	b.eq	.lk1
	bl	pointer_unlock
	b	.out
.lk1:	ldr	w0,[x19]
	adr	x2,cur_x
	ldp	x3,x4,[x2]
	cmp	w0,IN_MOUSE_ABS
	b.eq	.lka
	cmp	w0,IN_MOUSE_REL
	b.ne	.out
	ldr	x3,[x19,8]
	ldr	x4,[x19,16]
	b	.lks
.lka:	adr	x6,scr_w
	ldp	x6,x7,[x6]
	ldr	x0,[x19,8]
	mul	x0,x0,x6
	lsr	x0,x0,15
	ldr	x1,[x19,16]
	mul	x1,x1,x7
	lsr	x1,x1,15
	sub	x3,x0,x3
	sub	x4,x1,x4
	stp	x0,x1,[x2]
.lks:	adr	x9,evbuf
	stp	xzr,xzr,[x9]
	stp	xzr,xzr,[x9,16]
	mov	w2,WEV_MOUSE
	str	w2,[x9]
	mov	w2,WEV_M_REL
	str	w2,[x9,4]
	str	w3,[x9,EV_X]
	str	w4,[x9,EV_Y]
	ldr	x2,[x19,24]
	str	w2,[x9,16]
	adr	x3,cur_buttons
	str	x2,[x3]
	mov	x0,x8
	mov	x1,x9
	bl	win_push
	b	.out
.pad:	bl	key_target			; manette : comme le clavier
	cbz	x0,.out
	adr	x1,evbuf
	stp	xzr,xzr,[x1]
	stp	xzr,xzr,[x1,16]
	mov	w2,WEV_PAD
	str	w2,[x1]
	ldr	w2,[x19,4]
	str	w2,[x1,4]			; manette
	ldr	x2,[x19,8]
	str	x2,[x1,8]			; boutons, boutons bruts
	ldp	x2,x3,[x19,16]
	stp	x2,x3,[x1,16]			; axes, modele
	bl	win_push
	b	.out
.wheel:	bl	key_target			; molette : comme le clavier
	cbz	x0,.out
	adr	x1,evbuf
	stp	xzr,xzr,[x1]
	stp	xzr,xzr,[x1,16]
	mov	w2,WEV_WHEEL
	str	w2,[x1]
	ldr	x2,[x19,8]
	str	w2,[x1,12]
	bl	win_push
	b	.out
.key:	ldr	x0,[x19,24]			; (modificateurs : aussi pour la souris)
	adr	x1,cur_mods
	str	x0,[x1]
	ldr	x0,[x19,8]			; F10 : tout recomposer ; F11 : etat
	cmp	x0,0x43				; d'Elegant (diagnostic)
	b.ne	.f11
	ldr	x0,[x19,16]
	cbz	x0,.out
	bl	dirty_all
	b	.out
.f11:	cmp	x0,0x44
	b.ne	.k
	ldr	x0,[x19,16]
	cbz	x0,.out
	bl	diag_windows
	b	.out
.k:	mov	x0,x19				; Ctrl+Alt+Gauche, Droite : espace de
	bl	desk_key			; travail voisin
	cbnz	x0,.out
	bl	key_target
	cbz	x0,.out
	adr	x1,evbuf
	stp	xzr,xzr,[x1]
	stp	xzr,xzr,[x1,16]
	mov	w2,WEV_KEY
	str	w2,[x1]
	ldr	x2,[x19,16]
	str	w2,[x1,4]			; appui / relache
	ldr	x2,[x19,8]
	str	w2,[x1,EV_CODE]
	ldr	w2,[x19,4]
	str	w2,[x1,EV_CHAR]
	ldr	x2,[x19,24]
	str	w2,[x1,EV_MODS]
	adr	x1,evbuf			; (HIDManager repete la touche tenue)
	bl	win_push
.out:	ldp	x19,x20,[sp],16
	epilogue

; zones salies signalees par les programmes (en-tetes WD_*, ws_ext.inc) : au
; compositeur ; WEV_FRAME voulu -> W_FRAMEP
damage_collect:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x19,windows
	mov	x20,MAX_WINDOWS
.w:	ldr	x0,[x19,W_USED]
	cbz	x0,.n
	ldr	x9,[x19,W_DMG]
	ldr	x0,[x9,WD_DAMAGE]
	cbz	x0,.fr
.take:	ldaxr	x0,[x9]
	stlxr	w1,xzr,[x9]
	cbnz	w1,.take
	ubfx	x10,x0,0,16
	ubfx	x11,x0,16,16
	ubfx	x12,x0,32,16
	ubfx	x13,x0,48,16
	sub	x12,x12,x10
	sub	x13,x13,x11
	mov	x0,x19
	bl	win_damage
	ldr	x9,[x19,W_DMG]
.fr:	ldr	x0,[x9,WD_FRAME]
	cbz	x0,.n
	str	xzr,[x9,WD_FRAME]
	mov	x0,1
	str	x0,[x19,W_FRAMEP]
.n:	add	x19,x19,WIN_SIZE
	subs	x20,x20,1
	b.ne	.w
	ldp	x19,x20,[sp],16
	epilogue

; apres compose : WEV_FRAME aux fenetres qui l'attendent, au plus un tour tous
; les WS_FRAME_MS -> x0 = attente suivante (ms)
frames_post:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x21,8
	adr	x19,windows
	mov	x20,MAX_WINDOWS
.any:	ldr	x0,[x19,W_FRAMEP]
	cbz	x0,.an
	ldr	x0,[x19,W_USED]
	cbnz	x0,.due
	str	xzr,[x19,W_FRAMEP]
.an:	add	x19,x19,WIN_SIZE
	subs	x20,x20,1
	b.ne	.any
	b	.out
.due:	sys	SYS_TIME_MS
	adr	x1,frame_next
	ldr	x2,[x1]
	cmp	x0,x2
	b.hs	.send
	sub	x2,x2,x0			; trop tot : on revient a l'heure
	cmp	x2,x21
	csel	x21,x2,x21,lo
	b	.out
.send:	add	x0,x0,WS_FRAME_MS
	str	x0,[x1]
	adr	x19,windows
	mov	x20,MAX_WINDOWS
.w:	ldr	x0,[x19,W_FRAMEP]
	cbz	x0,.n
	ldr	x0,[x19,W_USED]
	cbz	x0,.clr
	adr	x1,evbuf
	stp	xzr,xzr,[x1]
	stp	xzr,xzr,[x1,16]
	mov	w0,WEV_FRAME
	str	w0,[x1]
	mov	x0,x19
	bl	win_push
	tbnz	x0,63,.n			; file pleine : au prochain tour
.clr:	str	xzr,[x19,W_FRAMEP]
.n:	add	x19,x19,WIN_SIZE
	subs	x20,x20,1
	b.ne	.w
.out:	mov	x0,x21
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fenetre, x1 = evenement (32 o) : depose dans la file du programme
win_push:
	ldr	x2,[x0,W_ID]
	str	w2,[x1,EV_WIN]
	ldr	x0,[x0,W_PID]
	sys	SYS_WS_POST
	ret

; x0 = x, x1 = y, x2 = boutons
mouse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	adr	x0,cur_x			; ancien curseur sale (formes : le point
	ldp	x0,x1,[x0]			; chaud jusqu'a 8, 10 dans la forme)
	sub	x0,x0,8
	sub	x1,x1,10
	mov	x2,20
	mov	x3,29
	bl	dirty
	adr	x0,cur_x
	stp	x19,x20,[x0]
	sub	x0,x19,8
	sub	x1,x20,10
	mov	x2,20
	mov	x3,29
	bl	dirty
	adr	x0,cur_buttons
	ldr	x22,[x0]			; anciens boutons
	str	x21,[x0]
	bic	x23,x21,x22			; appuyes
	bic	x24,x22,x21			; relaches

	; --- un menu ouvert capture toute la souris ---------------------------
	bl	find_grab
	cbz	x0,.no_grab
	mov	x1,1
	cbnz	x23,.gsend
	mov	x1,2
	cbnz	x24,.gsend
	mov	x1,0
.gsend:	bl	send_mouse
	b	.out
.no_grab:
	; --- redimensionnement en cours (bord droit / bas) : au relache -------
	adr	x0,resize_win
	ldr	x0,[x0]
	cbz	x0,.no_resize
	tbnz	x21,0,.out
	mov	x1,x19
	mov	x2,x20
	bl	resize_end
	b	.out
.no_resize:
	; --- deplacement d'une fenetre en cours ------------------------------
	adr	x0,drag_win
	ldr	x0,[x0]
	cbz	x0,.no_drag
	tbz	x21,0,.drag_end
	str	x0,[sp,-16]!
	bl	dirty_window
	ldr	x0,[sp]
	adr	x1,drag_dx
	ldp	x2,x3,[x1]
	sub	x2,x19,x2
	sub	x3,x20,x3
	adr	x4,area_top			; barre de titre sous la barre de menus
	ldr	x4,[x4]
	add	x4,x4,FRAME_TITLE_H
	cmp	x3,x4
	csel	x3,x4,x3,lt
	str	x2,[x0,W_X]
	str	x3,[x0,W_Y]
	bl	dirty_window
	ldr	x0,[sp],16
	b	.out
.drag_end:
	adr	x0,drag_win
	str	xzr,[x0]
	b	.out
.no_drag:
	; --- capture : boutons tenus apres un clic dans un contenu -----------
	adr	x0,capture_win
	ldr	x0,[x0]
	cbz	x0,.no_capture
	mov	x1,0				; deplacement
	cbz	x24,.send
	mov	x1,2				; relache
.send:	bl	send_mouse
	cbnz	x21,.out
	adr	x0,capture_win
	str	xzr,[x0]
	mov	x0,x19				; glisser-deposer : la fenetre dessous
	mov	x1,x20
	bl	drag_drop
	b	.out
.no_capture:
	cbz	x23,.hover
	tbz	x23,0,.right			; bouton gauche : le mode, puis les fenetres
	mov	x0,x19
	mov	x1,x20
	pol_call POL_CLICK
	cbnz	x0,.out
.right:	mov	x0,x19
	mov	x1,x20
	bl	win_hit				; x0 = fenetre, x1 = zone
	cbz	x0,.desktop
	mov	x22,x0
	ldr	x2,[x22,W_FLAGS]
	tbz	x2,WFB_NOFOCUS,.focusable
	cmp	x1,Z_CLIENT			; sans focus (barre de menus, dock) : le
	b.ne	.out				; clic va au contenu, sans changer d'ordre
	b	.client
.focusable:
	mov	x0,x22				; bloquee par un dialogue de son programme :
	str	x1,[sp,-16]!			; elle passe devant, le dialogue par-dessus,
	bl	win_blocker			; et le clic s'arrete la
	cbz	x0,.free
	add	sp,sp,16
	mov	x19,x0
	mov	x0,x22
	bl	win_stack_top
	mov	x0,x22
	bl	dirty_window
	mov	x0,x19
	bl	win_raise
	b	.out
.free:	mov	x0,x22
	bl	win_raise
	ldr	x1,[sp],16
	cmp	x1,Z_CLIENT
	b.eq	.client
	tbz	x23,0,.out			; cadre : bouton gauche seulement
	cmp	x1,Z_TITLE
	b.eq	.title
	cmp	x1,Z_MAXI
	b.ne	.nmax
	mov	x0,x22
	bl	win_maximize
	b	.out
.nmax:	cmp	x1,Z_BORDER			; bord d'une fenetre redimensionnable
	b.ne	.nbord
	ldr	x2,[x22,W_FLAGS]
	tbz	x2,WFB_RESIZABLE,.out
	mov	x0,x22
	mov	x1,x19
	mov	x2,x20
	bl	resize_begin
	b	.out
.nbord:
	cmp	x1,Z_CLOSE
	b.eq	.close
	cmp	x1,Z_MINI
	b.ne	.out
	mov	x0,x22
	bl	win_minimise
	b	.out
.client:
	adr	x0,capture_win
	str	x22,[x0]
	mov	x0,x22
	mov	x1,1
	bl	send_mouse
	b	.out
.title:	adr	x0,drag_win
	str	x22,[x0]
	ldr	x0,[x22,W_X]
	ldr	x1,[x22,W_Y]
	sub	x0,x19,x0
	sub	x1,x20,x1
	adr	x2,drag_dx
	stp	x0,x1,[x2]
	b	.out
.close:	adr	x1,evbuf
	stp	xzr,xzr,[x1]
	stp	xzr,xzr,[x1,16]
	mov	w0,WEV_CLOSE
	str	w0,[x1]
	mov	x0,x22
	bl	win_push
	b	.out
.desktop:
	mov	x0,0
	bl	win_set_focus
	b	.out
.hover:	mov	x0,x19				; survol : le mode, puis le contenu sous
	mov	x1,x20				; le pointeur (fenetre active, ou sans
	pol_call POL_HOVER			; focus : dock, barre de menus)
	cbnz	x0,.out
	mov	x0,x19
	mov	x1,x20
	bl	win_hit
	cbz	x0,.out
	cmp	x1,Z_CLIENT
	b.ne	.out
	ldr	x2,[x0,W_FLAGS]
	tbnz	x2,WFB_NOFOCUS,.hsend
	adr	x3,win_focus
	ldr	x3,[x3]
	cmp	x0,x3
	b.ne	.out
.hsend:	mov	x1,0
	bl	send_mouse
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fenetre, x1 = genre (0 deplacement, 1 appui, 2 relache)
; position et boutons : cur_x, cur_y, cur_buttons
send_mouse:
	prologue
	adr	x9,evbuf
	stp	xzr,xzr,[x9]
	stp	xzr,xzr,[x9,16]
	mov	w2,WEV_MOUSE
	str	w2,[x9]
	str	w1,[x9,4]
	adr	x2,cur_x
	ldp	x3,x4,[x2]
	ldr	x5,[x0,W_X]
	sub	x3,x3,x5
	ldr	x5,[x0,W_Y]
	sub	x4,x4,x5
	str	w3,[x9,EV_X]
	str	w4,[x9,EV_Y]
	adr	x2,cur_buttons
	ldr	x2,[x2]
	str	w2,[x9,16]
	adr	x2,cur_mods			; Maj, Ctrl tenus (EV_MODS)
	ldr	x2,[x2]
	str	w2,[x9,EV_MODS]
	mov	x1,x9
	bl	win_push
	epilogue

; x0 = x, x1 = y -> x0 = fenetre la plus en avant sous le point (0 : aucune),
; x1 = zone (Z_*)
win_hit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x22,2				; couches, du dessus vers le fond
.layer:	adr	x0,win_count
	ldr	x21,[x0]
.w:	cbz	x21,.lnext
	sub	x21,x21,1
	adr	x0,win_z
	ldr	x23,[x0,x21,lsl 3]
	ldr	x0,[x23,W_MINI]
	cbnz	x0,.w
	mov	x0,x23
	bl	win_layer
	cmp	x0,x22
	b.ne	.w
	mov	x0,x23
	bl	win_outer
	cmp	x19,x0
	b.lt	.w
	add	x4,x0,x2
	cmp	x19,x4
	b.ge	.w
	cmp	x20,x1
	b.lt	.w
	add	x4,x1,x3
	cmp	x20,x4
	b.ge	.w
	ldr	x4,[x23,W_FLAGS]		; dedans : quelle zone ?
	tbnz	x4,WFB_FRAMELESS,.cl
	ldr	x5,[x23,W_X]
	ldr	x6,[x23,W_Y]
	ldr	x7,[x23,W_W]
	ldr	x8,[x23,W_H]
	sub	x9,x5,FRAME_HIT			; (l'ombre laisse passer)
	cmp	x19,x9
	b.lt	.w
	add	x9,x5,x7
	add	x9,x9,FRAME_HIT
	cmp	x19,x9
	b.ge	.w
	sub	x9,x6,FRAME_HIT
	cmp	x20,x9
	b.lt	.w
	add	x9,x6,x8
	add	x9,x9,FRAME_HIT
	cmp	x20,x9
	b.ge	.w
	cmp	x20,x6
	b.lt	.title
	cmp	x19,x5
	b.lt	.border
	add	x9,x5,x7
	cmp	x19,x9
	b.ge	.border
	add	x9,x6,x8
	cmp	x20,x9
	b.ge	.border
.client:
	sub	x10,x20,x6			; boutons du cadre (depuis la droite)
	sub	x10,x10,FRAME_CAP_MARGIN
	cmp	x10,FRAME_CAP_H
	b.hs	.cl
	sub	x9,x19,x5
	sub	x11,x7,FRAME_CAP_MARGIN
	subs	x9,x11,x9			; 1.. depuis leur bord droit
	b.le	.cl
	cmp	x9,FRAME_CAP_W
	b.hi	.cl
	sub	x9,x9,1
	mov	x10,FRAME_CAP_BTN
	udiv	x9,x9,x10
	cbz	x9,.close
	cmp	x9,2
	b.hi	.title				; (une poignee invisible a leur gauche)
	b.eq	.capmi
	tbnz	x4,WFB_RESIZABLE,.maxi
	b	.title
.capmi:	tbnz	x4,WFB_MODAL,.title
	b	.mini
.cl:	mov	x1,Z_CLIENT
	b	.found
.border:
	mov	x1,Z_BORDER
	b	.found
.title:	mov	x1,Z_TITLE
	b	.found
.close:	mov	x1,Z_CLOSE
	b	.found
.mini:	mov	x1,Z_MINI
	b	.found
.maxi:	mov	x1,Z_MAXI
	b	.found
.lnext:	subs	x22,x22,1
	b.pl	.layer
	mov	x0,0
	mov	x1,0
	b	.out
.found:	mov	x0,x23
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fenetre -> x0 = dialogue modal qui la bloque (0 : aucun) : le plus en
; avant de son programme, sauf si c'est elle (feuille)
win_blocker:
	mov	x4,x0
	ldr	x3,[x0,W_PID]
	adr	x1,win_count
	ldr	x2,[x1]
	adr	x1,win_z
.w:	cbz	x2,.none
	sub	x2,x2,1
	ldr	x0,[x1,x2,lsl 3]
	ldr	x5,[x0,W_PID]
	cmp	x5,x3
	b.ne	.w
	ldr	x5,[x0,W_MINI]
	cbnz	x5,.w
	ldr	x5,[x0,W_FLAGS]
	tbz	x5,WFB_MODAL,.w
	cmp	x0,x4
	b.ne	.ret
.none:	mov	x0,0
.ret:	ret

; x0 = fenetre : au sommet de la pile (de sa couche a l'affichage), focus si
; elle le prend
win_raise:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	win_stack_top
	mov	x0,x19
	bl	win_set_focus
	mov	x0,x19
	bl	dirty_window
	bl	wins_changed
	ldr	x19,[sp],16
	epilogue

; x0 = fenetre : au sommet de la pile, sans toucher au focus (feuille)
win_stack_top:
	mov	x6,x0
	adr	x1,win_z
	adr	x2,win_count
	ldr	x2,[x2]
	mov	x3,0
.find:	cmp	x3,x2
	b.hs	.focus
	ldr	x4,[x1,x3,lsl 3]
	cmp	x4,x6
	b.eq	.shift
	add	x3,x3,1
	b	.find
.shift:	add	x5,x3,1				; decaler les suivantes
	cmp	x5,x2
	b.hs	.top
	ldr	x4,[x1,x5,lsl 3]
	str	x4,[x1,x3,lsl 3]
	mov	x3,x5
	b	.shift
.top:	str	x6,[x1,x3,lsl 3]
.focus:	ret

; x0 = fenetre (0 : aucune) : focus clavier, evenements WEV_FOCUS
; (une fenetre sans focus est ignoree)
win_set_focus:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cbz	x19,.take
	ldr	x0,[x19,W_FLAGS]
	tbnz	x0,WFB_NOFOCUS,.out
.take:	adr	x0,win_focus
	ldr	x20,[x0]
	cmp	x20,x19
	b.eq	.out
	str	x19,[x0]
	cbz	x20,.gain
	adr	x1,evbuf
	stp	xzr,xzr,[x1]
	stp	xzr,xzr,[x1,16]
	mov	w0,WEV_FOCUS
	str	w0,[x1]
	mov	x0,x20
	bl	win_push
	mov	x0,x20
	bl	dirty_window
.gain:	cbz	x19,.done
	adr	x1,evbuf
	stp	xzr,xzr,[x1]
	stp	xzr,xzr,[x1,16]
	mov	w0,WEV_FOCUS
	str	w0,[x1]
	mov	w0,1
	str	w0,[x1,4]
	mov	x0,x19
	bl	win_push
	mov	x0,x19
	bl	dirty_window
.done:	bl	wins_changed
.out:	ldp	x19,x20,[sp],16
	epilogue

; le focus va a la fenetre normale visible la plus en avant (ou a aucune)
focus_top:
	adr	x1,win_count
	ldr	x2,[x1]
	adr	x1,win_z
.w:	cbz	x2,.none
	sub	x2,x2,1
	ldr	x0,[x1,x2,lsl 3]
	ldr	x3,[x0,W_MINI]
	cbnz	x3,.w
	ldr	x3,[x0,W_FLAGS]
	tbnz	x3,WFB_NOFOCUS,.w
	tbnz	x3,WFB_BACK,.w
	tbnz	x3,WFB_TOP,.w
	b	win_set_focus
.none:	mov	x0,0
	b	win_set_focus

; x0 = fenetre : reduite (cachee ; le dock la fait revenir)
win_minimise:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,W_MINI]
	orr	x0,x0,MINI_REDUITE
	str	x0,[x19,W_MINI]
	mov	x0,x19
	bl	dirty_window
	adr	x0,win_focus
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.out
	bl	focus_top
.out:	bl	wins_changed
	ldr	x19,[sp],16
	epilogue

; x0 = fenetre : retire de la pile d'affichage (la place reste a liberer)
win_unlink:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	dirty_window
	adr	x1,win_z
	adr	x2,win_count
	ldr	x3,[x2]
	mov	x4,0
.find:	cmp	x4,x3
	b.hs	.done
	ldr	x5,[x1,x4,lsl 3]
	cmp	x5,x19
	b.eq	.shift
	add	x4,x4,1
	b	.find
.shift:	add	x5,x4,1
	cmp	x5,x3
	b.hs	.last
	ldr	x6,[x1,x5,lsl 3]
	str	x6,[x1,x4,lsl 3]
	mov	x4,x5
	b	.shift
.last:	sub	x3,x3,1
	str	x3,[x2]
.done:	adr	x0,win_focus			; focus a la fenetre du dessus
	ldr	x1,[x0]
	cmp	x1,x19
	b.ne	.cap
	str	xzr,[x0]
	bl	focus_top
.cap:	adr	x0,capture_win
	ldr	x1,[x0]
	cmp	x1,x19
	b.ne	.drag
	str	xzr,[x0]
.drag:	adr	x0,drag_win
	ldr	x1,[x0]
	cmp	x1,x19
	b.ne	.out
	str	xzr,[x0]
.out:	bl	wins_changed
	ldr	x19,[sp],16
	epilogue

; x0 = fenetre : retiree, tampon rendu, place liberee
win_destroy:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,lock_win
	ldr	x0,[x0]
	cmp	x0,x19
	b.ne	.nl
	bl	pointer_unlock
.nl:	mov	x0,x19
	bl	win_unlink
	mov	x0,x19				; (sa surface attachee)
	bl	surf_detach
	ldr	x0,[x19,W_BUF]
	sys	SYS_BUF_FREE
	adr	x1,windows			; (plus agrandie : la fenetre qui reprendra
	sub	x1,x19,x1			;  cette place n'est pas a « rendre »)
	lsr	x1,x1,WIN_SHIFT
	mov	x2,40
	adr	x0,win_restore
	madd	x0,x1,x2,x0
	str	xzr,[x0,32]
	str	xzr,[x19,W_USED]
	mov	x0,x19				; (la place ne sera plus agrandie)
	bl	restore_clear
	bl	recompute_reserve
	ldr	x0,[x19,W_FLAGS]		; (programme mort compris : le rideau tombe)
	tbz	x0,WFB_CURTAIN,.out
	bl	curtain_apply
.out:	ldr	x19,[sp],16
	epilogue

; bords reserves (area_top, area_bot) : fenetres WF_RESERVE collees en haut ou
; en bas de l'ecran
recompute_reserve:
	mov	x5,0				; haut
	mov	x6,0				; bas
	adr	x7,scr_h
	ldr	x7,[x7]
	adr	x2,windows
	mov	x3,MAX_WINDOWS
.w:	ldr	x4,[x2,W_USED]
	cbz	x4,.n
	ldr	x4,[x2,W_FLAGS]
	tbz	x4,WFB_RESERVE,.n
	ldr	x0,[x2,W_Y]
	ldr	x1,[x2,W_H]
	cbnz	x0,.bottom
	cmp	x1,x5
	csel	x5,x1,x5,gt
	b	.n
.bottom:
	add	x0,x0,x1
	cmp	x0,x7
	b.lt	.n
	cmp	x1,x6
	csel	x6,x1,x6,gt
.n:	add	x2,x2,WIN_SIZE
	subs	x3,x3,1
	b.ne	.w
	adr	x0,area_top
	stp	x5,x6,[x0]			; area_top, area_bot
	; les fenetres normales deja placees sous la barre du haut descendent
	; (programmes ouverts avant la barre de menus)
	adr	x2,windows
	mov	x3,MAX_WINDOWS
.f:	ldr	x4,[x2,W_USED]
	cbz	x4,.fn
	ldr	x4,[x2,W_FLAGS]
	mov	x0,WF_BACK or WF_TOP or WF_FRAMELESS
	tst	x4,x0
	b.ne	.fn
	add	x1,x5,FRAME_TITLE_H
	ldr	x0,[x2,W_Y]
	cmp	x0,x1
	b.ge	.fn
	adr	x0,windows			; (une fenetre agrandie garde sa place :
	sub	x0,x2,x0			;  elle commence juste sous la barre)
	lsr	x0,x0,WIN_SHIFT
	mov	x1,40
	adr	x6,win_restore
	madd	x0,x0,x1,x6
	ldr	x0,[x0,32]
	cbnz	x0,.fn
	add	x1,x5,FRAME_TITLE_H
	str	x1,[x2,W_Y]
	mov	x0,1				; tout est a recomposer
	adr	x4,need_all
	str	x0,[x4]
.fn:	add	x2,x2,WIN_SIZE
	subs	x3,x3,1
	b.ne	.f
	ret

; x0 = id, x1 = pid (-1 : quelconque) -> x0 = fenetre (0 : aucune)
win_by_id:
	adr	x2,windows
	mov	x3,MAX_WINDOWS
.w:	ldr	x4,[x2,W_USED]
	cbz	x4,.n
	ldr	x4,[x2,W_ID]
	cmp	x4,x0
	b.ne	.n
	cmn	x1,1
	b.eq	.ok
	ldr	x4,[x2,W_PID]
	cmp	x4,x1
	b.ne	.none
.ok:	mov	x0,x2
	ret
.n:	add	x2,x2,WIN_SIZE
	subs	x3,x3,1
	b.ne	.w
.none:	mov	x0,0
	ret

; x0 = destination, x1 = titre, x2 = taille max (NUL compris) : copie bornee
copy_title:
	sub	x2,x2,1
	mov	x3,0
.c:	cmp	x3,x2
	b.hs	.end
	ldrb	w4,[x1,x3]
	strb	w4,[x0,x3]
	cbz	w4,.ret
	add	x3,x3,1
	b	.c
.end:	strb	wzr,[x0,x3]
.ret:	ret

; previent les fenetres a l'ecoute (WF_LISTEN) si la liste a change
notify_listeners:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,wins_dirty
	ldr	x1,[x0]
	cbz	x1,.out
	str	xzr,[x0]
	pol_call POL_WINS
	adr	x19,windows
	mov	x20,MAX_WINDOWS
.w:	ldr	x0,[x19,W_USED]
	cbz	x0,.n
	ldr	x0,[x19,W_FLAGS]
	tbz	x0,WFB_LISTEN,.n
	adr	x1,evbuf
	stp	xzr,xzr,[x1]
	stp	xzr,xzr,[x1,16]
	mov	w0,WEV_WINLIST
	str	w0,[x1]
	mov	x0,x19
	bl	win_push
.n:	add	x19,x19,WIN_SIZE
	subs	x20,x20,1
	b.ne	.w
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Requetes des programmes (message dans msg)
; ---------------------------------------------------------------------------

; x0 = numero de requete : traite, puis repond (sauf messages du noyau)
req_handle:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x9,msg				; (historique : la requete)
	ldr	x0,[x9,WM_OP]
	add	x0,x0,1000
	ldr	x1,[x9,WM_A]
	ldr	x2,[x9,WM_B]
	ldr	x3,[x9,WM_C]
	ldr	w4,[x9,WM_PID]
	bl	hist
	adr	x1,msg
	ldr	x0,[x1,WM_OP]
	cmp	x0,WS_OP_CREATE
	b.eq	.create
	cmp	x0,WS_OP_UPDATE
	b.eq	.update
	cmp	x0,WS_OP_CLOSE
	b.eq	.close
	cmp	x0,WS_OP_TITLE
	b.eq	.title
	cmp	x0,WS_OP_SCREEN
	b.eq	.screen
	cmp	x0,WS_OP_FRAME
	b.eq	.frame
	cmp	x0,WS_OP_LIST
	b.eq	.list
	cmp	x0,WS_OP_RAISE
	b.eq	.raise
	cmp	x0,WS_OP_MESURE
	b.eq	.mesure
	cmp	x0,WS_OP_MENU_SET
	b.eq	.mset
	cmp	x0,WS_OP_MENU_GET
	b.eq	.mget
	cmp	x0,WS_OP_MENU_COMMAND
	b.eq	.mcmd
	cmp	x0,WS_OP_SNAPSHOT
	b.eq	.snap
	cmp	x0,WS_OP_WIN_LIST
	b.eq	.wlist
	cmp	x0,WS_OP_WIN_READ
	b.eq	.wread
	cmp	x0,WS_OP_WIN_MOVE
	b.eq	.wmove
	cmp	x0,WS_OP_WIN_CLOSE
	b.eq	.wclose
	cmp	x0,WS_OP_CLIP_SET
	b.eq	.cset
	cmp	x0,WS_OP_CLIP_GET
	b.eq	.cget
	cmp	x0,WS_OP_WIN_POS
	b.eq	.wpos
	cmp	x0,WS_OP_RESIZE
	b.eq	.wsize
	cmp	x0,WS_OP_CURSOR
	b.eq	.wcur
	cmp	x0,WS_OP_CURSOR_GET
	b.eq	.wcget
	cmp	x0,WS_OP_POINTER_LOCK
	b.eq	.wlock
	cmp	x0,WS_OP_DRAG
	b.eq	.wdrag
	cmp	x0,WS_OP_DROP_GET
	b.eq	.wdrop
	cmp	x0,WS_OP_SURFACE_ATTACH
	b.eq	.sattach
	cmp	x0,WS_OP_SURFACE_DAMAGE
	b.eq	.sdamage
	cmp	x0,WS_OP_MAXIMIZE
	b.eq	.wmaxi
	cmp	x0,WS_OP_SCREEN_SET
	b.eq	.sset
	cmp	x0,WS_OP_SCREEN_KEEP
	b.eq	.skeep
	cmp	x0,WS_OP_SCREEN_MODES
	b.eq	.smodes
	cmp	x0,WS_OP_WIN_PLACE
	b.eq	.wplace
	cmp	x0,WS_OP_DESK
	b.eq	.desk
	cmp	x0,WS_OP_WIN_DRAG
	b.eq	.wdragw
	movl	x2,WS_OP_EXIT
	cmp	x0,x2
	b.eq	.exit
	mov	x0,E_INVAL
	b	.reply
.create:
	bl	req_create
	b	.reply
.wdragw:
	bl	req_win_drag
	b	.reply
.update:
	bl	req_update
	b	.reply
.close:	bl	req_close
	b	.reply
.title:	bl	req_title
	b	.reply
.frame:	bl	req_frame
	b	.reply
.list:	bl	req_list
	b	.reply
.raise:	bl	req_raise
	b	.reply
.mesure:
	bl	req_mesure
	b	.reply
.mset:	bl	req_menu_set			; menus (elegant/menu.inc)
	b	.reply
.mget:	bl	req_menu_get
	b	.reply
.mcmd:	bl	req_menu_command
	b	.reply
.snap:	bl	req_snapshot
	b	.reply
.wlist:	bl	req_win_list			; rdpd (elegant/remote.inc)
	b	.reply
.wread:	bl	req_win_read
	b	.reply
.wmove:	bl	req_win_move
	b	.reply
.wclose:
	bl	req_win_close
	b	.reply
.wcur:	bl	req_cursor			; (elegant/uikit_ops.inc)
	b	.reply
.wcget:	bl	req_cursor_get
	b	.reply
.wlock:	bl	req_pointer_lock		; (elegant/uikit_ops.inc)
	b	.reply
.wdrag:	bl	req_drag
	b	.reply
.wdrop:	bl	req_drop_get
	b	.reply
.sattach:
	bl	req_surface_attach		; surface attachee (elegant/surface.inc)
	b	.reply
.sdamage:
	bl	req_surface_damage
	b	.reply
.wsize:	bl	req_resize			; (elegant/uikit_ops.inc)
	b	.reply
.wmaxi:	bl	req_maximize			; (elegant/uikit_ops.inc)
	b	.reply
.sset:	bl	req_screen_set			; (elegant/screen.inc)
	b	.reply
.skeep:	bl	req_screen_keep
	b	.reply
.smodes:
	bl	req_screen_modes
	b	.reply
.wplace:
	bl	req_win_place
	b	.reply
.desk:	bl	req_desk			; (elegant/uikit_ops.inc)
	b	.reply
.wpos:	bl	req_win_pos			; (elegant/uikit_ops.inc)
	b	.reply
.cset:	bl	req_clip_set			; presse-papiers (elegant/uikit_ops.inc)
	b	.reply
.cget:	bl	req_clip_get
	b	.reply
.screen:
	adr	x1,msg
	adr	x0,scr_w
	ldp	x2,x3,[x0]
	stp	x2,x3,[x1,WM_A]			; largeur, hauteur
	adr	x0,area_top
	ldp	x2,x3,[x0]
	str	x3,[x1,WM_C]			; reserve en bas
	str	x2,[x1,WM_D]			; en haut
	mov	x0,0
	b	.reply
.exit:	adr	x1,msg				; fin d'un process : ses fenetres
	ldr	x0,[x1,WM_A]
	bl	req_exit
	b	.out				; (le noyau n'attend pas de reponse)
.reply:	adr	x1,msg
	str	x0,[x1,WM_OP]
	mov	x0,x19
	sys	SYS_WS_REPLY
.out:	ldr	x19,[sp],16
	epilogue

; x0 = adresse, x1 = octets (multiple de 8), x2 = motif de 64 bits
fill64:
	cbz	x1,.ret
.l:	str	x2,[x0],8
	subs	x1,x1,8
	b.hi	.l
.ret:	ret

; WS_OP_CREATE : a x, b y (-1 : auto), c largeur, d hauteur ; donnees : role,
; titre (+8) -> x0 = id (< 0 : erreur) ; msg a = tampon, b / c = decalages
; des deux copies du cadre dans le tampon (0 : sans cadre)
req_create:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	adr	x9,msg
	ldr	x19,[x9,WM_A]
	ldr	x20,[x9,WM_B]
	ldr	x21,[x9,WM_C]
	ldr	x22,[x9,WM_D]
	ldr	x27,[x9,WM_DATA]		; role
	cmp	x21,1				; une fenetre plus grande que l'ecran est
	b.lt	.inval				; acceptee (decoupee a l'affichage)
	cmp	x22,1
	b.lt	.inval
	cmp	x21,2048
	b.gt	.inval
	cmp	x22,2048
	b.gt	.inval
	adr	x23,windows			; place libre
	mov	x0,MAX_WINDOWS
.slot:	ldr	x1,[x23,W_USED]
	cbz	x1,.got
	add	x23,x23,WIN_SIZE
	subs	x0,x0,1
	b.ne	.slot
	mov	x0,E_NOMEM
	b	.out
.got:	mul	x24,x21,x22			; contenu
	lsl	x24,x24,2
	mov	x28,0				; un cadre (deux copies)
	tbnz	x27,WFB_FRAMELESS,.size
	add	x0,x21,2*FRAME_BORDER
	add	x1,x22,FRAME_TITLE_H+FRAME_BORDER
	mul	x28,x0,x1
	lsl	x28,x28,2
.size:	add	x0,x24,x28,lsl 1
	add	x0,x0,WD_SIZE+7			; en-tete WD_* en fin de tampon
	movl	x1,BUF_SLOT
	cmp	x0,x1
	b.hi	.inval
	sys	SYS_BUF_CREATE
	tbnz	x0,63,.out
	mov	x25,x0
	movl	x1,BUF_VA_BASE
	mov	x2,BUF_SLOT
	madd	x26,x25,x2,x1
	mov	x0,x26				; contenu blanc (transparent : WF_ALPHA)
	mov	x1,x24
	mov	x2,-1
	tbz	x27,WFB_ALPHA,.fill
	movl	x2,TRANSPARENT
	orr	x2,x2,x2,lsl 32
.fill:	bl	fill64
	add	x0,x26,x24			; cadres invisibles jusqu'au dessin
	lsl	x1,x28,1
	movl	x2,TRANSPARENT
	orr	x2,x2,x2,lsl 32
	bl	fill64
	adr	x9,msg				; le programme pourra mapper le tampon
	ldr	w1,[x9,WM_PID]
	mov	x0,x25
	sys	SYS_BUF_GRANT
	adr	x9,msg
	ldr	w0,[x9,WM_PID]
	str	x0,[x23,W_PID]
	str	x21,[x23,W_W]
	str	x22,[x23,W_H]
	mov	x0,x23				; pas de surface attachee
	bl	surf_clear
	str	x26,[x23,W_SURF]
	str	x25,[x23,W_BUF]
	add	x0,x24,x28,lsl 1
	add	x0,x0,7
	and	x0,x0,not 7
	add	x0,x26,x0
	str	x0,[x23,W_DMG]
	str	xzr,[x23,W_FRAMEP]
	str	x27,[x23,W_FLAGS]
	str	xzr,[x23,W_MINI]
	mov	x0,0				; sur l'espace courant ; fond et dessus :
	mov	x1,WF_BACK or WF_TOP		; sur tous
	tst	x27,x1
	b.ne	.desk
	adr	x0,desk_cur
	ldr	x0,[x0]
	add	x0,x0,1
.desk:	str	x0,[x23,W_DESK]
	str	xzr,[x23,W_MLEN]		; pas de menu
	str	xzr,[x23,W_MGEN]
	str	xzr,[x23,W_GEN]		; generations (contenu, cadre)
	add	x0,x26,x24
	str	x0,[x23,W_CHR_A]
	add	x0,x0,x28
	str	x0,[x23,W_CHR_I]
	add	x0,x23,W_TITLE
	add	x1,x9,WM_DATA+8
	mov	x2,120
	bl	copy_title
	tbz	x27,WFB_FRAMELESS,.place
	cmn	x19,1				; sans cadre : a la place demandee
	csel	x19,xzr,x19,eq
	cmn	x20,1
	csel	x20,xzr,x20,eq
	b	.placed
.place:	cmn	x19,1				; position : demandee, sinon en cascade
	b.ne	.clamp
	adr	x2,win_cascade
	ldr	x3,[x2]
	add	x3,x3,30
	cmp	x3,240
	mov	x4,30
	csel	x3,x4,x3,gt
	str	x3,[x2]
	add	x19,x3,40
	sub	x20,x3,20
	adr	x0,area_top
	ldr	x0,[x0]
	add	x20,x20,x0
	add	x20,x20,FRAME_GAP
.clamp:	adr	x0,scr_w			; dans l'ecran autant que possible
	ldp	x0,x1,[x0]
	sub	x0,x0,FRAME_GAP
	sub	x0,x0,x21
	cmp	x19,x0
	csel	x19,x0,x19,gt
	cmp	x19,FRAME_GAP
	mov	x0,FRAME_GAP
	csel	x19,x0,x19,lt
	adr	x0,area_top			; entre les bords reserves, barre de
	ldp	x2,x3,[x0]			; titre visible
	sub	x1,x1,x3
	sub	x1,x1,FRAME_GAP
	sub	x1,x1,x22
	cmp	x20,x1
	csel	x20,x1,x20,gt
	add	x2,x2,FRAME_GAP
	cmp	x20,x2
	csel	x20,x2,x20,lt
.placed:
	str	x19,[x23,W_X]
	str	x20,[x23,W_Y]
	adr	x0,next_win_id
	ldr	x1,[x0]
	add	x1,x1,1
	str	x1,[x0]
	str	x1,[x23,W_ID]
	mov	x0,1
	str	x0,[x23,W_USED]
	tbz	x27,WFB_RESERVE,.stack
	bl	recompute_reserve
	adr	x0,need_all
	ldr	x1,[x0]
	cbz	x1,.stack
	str	xzr,[x0]
	bl	dirty_all
.stack:	adr	x0,win_count			; au sommet de la pile
	ldr	x1,[x0]
	adr	x2,win_z
	str	x23,[x2,x1,lsl 3]
	add	x1,x1,1
	str	x1,[x0]
	mov	x0,x23
	bl	win_raise
	tbz	x27,WFB_BACK,.zok		; le fond d'ecran (WF_BACK opaque) : sous les
	tbnz	x27,WFB_ALPHA,.zok		; applets, quel que soit l'ordre de demarrage
	adr	x0,win_count
	ldr	x1,[x0]
	adr	x2,win_z
.zs:	subs	x1,x1,1
	b.ls	.zb
	sub	x3,x1,1
	ldr	x3,[x2,x3,lsl 3]
	str	x3,[x2,x1,lsl 3]
	b	.zs
.zb:	str	x23,[x2]
.zok:	tbnz	x27,WFB_CURTAIN,.curt		; rideau ouvert : la nouvelle dessous
	adr	x0,curtain_on
	ldr	x0,[x0]
	cbz	x0,.nocurt
.curt:	bl	curtain_apply
.nocurt:
	adr	x9,msg
	str	x25,[x9,WM_A]
	mov	x0,0
	mov	x1,0
	cbz	x28,.offs
	mov	x0,x24
	add	x1,x24,x28
.offs:	stp	x0,x1,[x9,WM_B]
	ldr	x0,[x23,W_DMG]			; d : l'en-tete WD_*
	sub	x0,x0,x26
	str	x0,[x9,WM_D]
	ldr	x0,[x23,W_ID]
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fenetre de l'appelant designee par msg a (0 : aucune)
req_win:
	adr	x9,msg
	ldr	x0,[x9,WM_A]
	ldr	w1,[x9,WM_PID]
	b	win_by_id

; WS_OP_UPDATE : a id, b x, c y, d largeur, donnees : hauteur
req_update:
	prologue
	bl	req_win
	cbz	x0,.bad
	adr	x9,msg
	ldr	x10,[x9,WM_B]
	ldr	x11,[x9,WM_C]
	ldr	x12,[x9,WM_D]
	ldr	x13,[x9,WM_DATA]
	bl	win_damage
	mov	x0,0
	b	.out
.bad:	adr	x1,diag_bad			; (diagnostic)
	ldr	x2,[x1]
	add	x2,x2,1
	str	x2,[x1]
	mov	x0,E_BADF
.out:	epilogue

; x0 = fenetre, x10..x13 = x, y, largeur, hauteur dans le contenu : zone salie
win_damage:
	prologue
	ldr	x1,[x0,W_NUPD]
	add	x1,x1,1
	str	x1,[x0,W_NUPD]
	ldr	w1,[x0,W_GEN]			; (rdpd : le contenu a change)
	add	w1,w1,1
	str	w1,[x0,W_GEN]
	ldr	x1,[x0,W_W]			; borner au contenu
	ldr	x2,[x0,W_H]
	cmp	x10,0
	csel	x10,xzr,x10,lt
	cmp	x11,0
	csel	x11,xzr,x11,lt
	add	x3,x10,x12
	cmp	x3,x1
	csel	x3,x1,x3,gt
	sub	x12,x3,x10
	add	x3,x11,x13
	cmp	x3,x2
	csel	x3,x2,x3,gt
	sub	x13,x3,x11
	ldr	x1,[x0,W_X]
	ldr	x2,[x0,W_Y]
	add	x0,x1,x10
	add	x1,x2,x11
	mov	x2,x12
	mov	x3,x13
	stp	x0,x1,[sp,-16]!			; (historique : zone salie)
	stp	x2,x3,[sp,-16]!
	mov	x4,x3
	mov	x3,x2
	mov	x2,x1
	mov	x1,x0
	mov	x0,200
	bl	hist
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	bl	dirty
	epilogue

; WS_OP_CLOSE : a id
req_close:
	prologue
	bl	req_win
	cbz	x0,.bad
	bl	win_destroy
	mov	x0,0
	b	.out
.bad:	mov	x0,E_BADF
.out:	epilogue

; WS_OP_TITLE : a id, titre
req_title:
	prologue
	str	x19,[sp,-16]!
	bl	req_win
	cbz	x0,.bad
	mov	x19,x0
	add	x0,x19,W_TITLE
	adr	x1,msg+WM_DATA
	mov	x2,120
	bl	copy_title
	mov	x0,x19				; (le nom de l'appli dans la barre)
	bl	menu_title_changed
	bl	wins_changed
	mov	x0,0
	b	.out
.bad:	mov	x0,E_BADF
.out:	ldr	x19,[sp],16
	epilogue

; WS_OP_FRAME : a id (le programme a redessine son cadre)
req_frame:
	prologue
	bl	req_win
	cbz	x0,.bad
	ldr	w1,[x0,W_CGEN]			; (rdpd : le cadre a change)
	add	w1,w1,1
	str	w1,[x0,W_CGEN]
	bl	dirty_window
	mov	x0,0
	b	.out
.bad:	mov	x0,E_BADF
.out:	epilogue

; WS_OP_RAISE : a id (fenetre de n'importe quel programme : le dock)
req_raise:
	prologue
	adr	x9,msg
	ldr	x0,[x9,WM_A]
	mov	x1,-1
	bl	win_by_id
	cbz	x0,.bad
	ldr	x1,[x0,W_MINI]			; sur un autre espace : on y va
	tbz	x1,1,.vis
	str	x0,[sp,-16]!
	ldr	x0,[x0,W_DESK]
	sub	x0,x0,1
	bl	desk_show
	ldr	x0,[sp],16
.vis:	str	xzr,[x0,W_MINI]
	str	x0,[sp,-16]!			; (bloquee : son dialogue vient devant)
	bl	win_blocker
	ldr	x1,[sp],16
	cbnz	x0,.go
	mov	x0,x1
.go:	bl	win_raise
	mov	x0,0
	b	.out
.bad:	mov	x0,E_BADF
.out:	epilogue

; WS_OP_LIST : a premiere -> x0 = nombre de fenetres listees ; donnees :
; WL_PER_MSG entrees a partir de la premiere (fenetres normales, hors
; WF_NOLIST, dans l'ordre des places)
req_list:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x9,msg
	ldr	x21,[x9,WM_A]			; premiere
	adr	x22,msg+WM_DATA			; entree suivante
	mov	x19,0				; nombre
	adr	x20,windows
.w:	ldr	x0,[x20,W_USED]
	cbz	x0,.n
	ldr	x0,[x20,W_FLAGS]
	mov	x1,WF_NOLIST or WF_BACK or WF_TOP
	tst	x0,x1
	b.ne	.n
	sub	x0,x19,x21			; dans la page demandee ?
	add	x19,x19,1
	cmp	x0,WL_PER_MSG
	b.hs	.n
	ldr	x0,[x20,W_ID]
	str	x0,[x22,WL_ID]
	ldr	x0,[x20,W_PID]
	str	w0,[x22,WL_PID]
	ldr	x0,[x20,W_MINI]
	lsl	w0,w0,1
	adr	x1,win_focus
	ldr	x1,[x1]
	cmp	x1,x20
	cinc	w0,w0,eq
	str	w0,[x22,WL_STATE]
	add	x0,x22,WL_TITLE
	add	x1,x20,W_TITLE
	mov	x2,WL_SIZE-WL_TITLE
	bl	copy_title
	add	x22,x22,WL_SIZE
.n:	add	x20,x20,WIN_SIZE
	adr	x0,windows+MAX_WINDOWS*WIN_SIZE
	cmp	x20,x0
	b.lo	.w
	mov	x0,x19
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; WS_OP_MESURE : a images, b = 1 sans transparence -> x0 = ms pour les
; a recompositions de tout l'ecran (cout des ombres sur le vrai materiel)
req_mesure:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x9,msg
	ldr	x19,[x9,WM_A]
	ldr	x0,[x9,WM_B]
	cmp	x0,0
	cset	x0,ne
	adr	x1,mesure_opaque
	str	x0,[x1]
	sys	SYS_TIME_MS
	mov	x20,x0
	mov	x21,x19
.n:	cbz	x21,.done
	bl	dirty_all
	bl	compose
	sub	x21,x21,1
	b	.n
.done:	sys	SYS_TIME_MS
	sub	x19,x0,x20
	adr	x1,mesure_opaque
	str	xzr,[x1]
	bl	dirty_all
	mov	x0,x19
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = pid termine : ses fenetres disparaissent
req_exit:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	bl	snap_exit			; (son tampon de l'ecran)
	adr	x20,windows
	mov	x21,MAX_WINDOWS
.w:	ldr	x0,[x20,W_USED]
	cbz	x0,.n
	ldr	x0,[x20,W_PID]
	cmp	x0,x19
	b.ne	.n
	mov	x0,x20
	bl	win_destroy
.n:	add	x20,x20,WIN_SIZE
	subs	x21,x21,1
	b.ne	.w
	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (F11) diagnostic : chaque fenetre, la pile, la zone sale
diag_windows:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,dg_t
	bl	puts
	adr	x19,windows
	mov	x20,0
.w:	cmp	x20,MAX_WINDOWS
	b.hs	.z
	ldr	x0,[x19,W_USED]
	cbz	x0,.n
	adr	x0,dg_id
	bl	puts
	ldr	x0,[x19,W_ID]
	bl	print_dec
	adr	x0,dg_pid
	bl	puts
	ldr	x0,[x19,W_PID]
	bl	print_dec
	adr	x0,dg_fl
	bl	puts
	ldr	x0,[x19,W_FLAGS]
	bl	print_dec
	adr	x0,dg_xy
	bl	puts
	ldr	x0,[x19,W_X]
	bl	print_dec
	adr	x0,dg_sp
	bl	puts
	ldr	x0,[x19,W_Y]
	bl	print_dec
	adr	x0,dg_sp
	bl	puts
	ldr	x0,[x19,W_W]
	bl	print_dec
	adr	x0,dg_sp
	bl	puts
	ldr	x0,[x19,W_H]
	bl	print_dec
	adr	x0,dg_mi
	bl	puts
	ldr	x0,[x19,W_MINI]
	bl	print_dec
	adr	x0,dg_up
	bl	puts
	ldr	x0,[x19,W_NUPD]
	bl	print_dec
	adr	x0,dg_px			; un pixel du contenu (milieu de la
	bl	puts				; premiere ligne +10, au milieu)
	ldr	x1,[x19,W_SURF]
	ldr	x2,[x19,W_W]
	lsr	x3,x2,1
	ldr	w0,[x1,x3,lsl 2]
	bl	print_dec
	adr	x0,dg_sp
	bl	puts
	ldr	x1,[x19,W_SURF]
	ldr	x2,[x19,W_W]
	ldr	x3,[x19,W_H]
	lsr	x3,x3,1
	mul	x3,x3,x2
	add	x3,x3,10
	ldr	w0,[x1,x3,lsl 2]
	bl	print_dec
	adr	x0,dg_buf
	bl	puts
	ldr	x0,[x19,W_BUF]
	bl	print_dec
	adr	x0,dg_ti
	bl	puts
	add	x0,x19,W_TITLE
	bl	puts
	adr	x0,dg_nl
	bl	puts
.n:	add	x19,x19,WIN_SIZE
	add	x20,x20,1
	b	.w
.z:	adr	x0,dg_z				; pile, mises a jour refusees
	bl	puts
	adr	x0,win_count
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,dg_bad
	bl	puts
	adr	x0,diag_bad
	ldr	x0,[x0]
	bl	print_dec
	adr	x0,dg_nl
	bl	puts
	bl	present_diag			; presentation, zones sales
	adr	x0,hist_n			; historique : les 64 dernieres entrees
	ldr	x19,[x0]
	subs	x20,x19,64
	csel	x20,xzr,x20,lo
.h:	cmp	x20,x19
	b.hs	.hend
	and	x0,x20,63
	mov	x1,40
	adr	x2,hist_buf
	madd	x9,x0,x1,x2
	str	x9,[sp,-16]!
	mov	x0,x20
	bl	print_dec
	adr	x0,dg_ti
	bl	puts
	mov	x10,0
.hc:	ldr	x9,[sp]
	ldr	x0,[x9,x10,lsl 3]
	str	x10,[sp,8]
	bl	print_dec
	adr	x0,dg_sp
	bl	puts
	ldr	x10,[sp,8]
	add	x10,x10,1
	cmp	x10,5
	b.lo	.hc
	add	sp,sp,16
	adr	x0,dg_nl
	bl	puts
	add	x20,x20,1
	b	.h
.hend:	ldp	x19,x20,[sp],16
	epilogue

dg_t	db 10,'=== Elegant (F11) ===',10,0
dg_id	db 'fenetre ',0
dg_pid	db ' pid ',0
dg_fl	db ' role ',0
dg_xy	db ' x,y,l,h ',0
dg_sp	db ' ',0
dg_mi	db ' reduite ',0
dg_up	db ' maj ',0
dg_ti	db ' : ',0
dg_px	db ' pixels ',0
dg_buf	db ' tampon ',0
dg_z	db 'pile : ',0
dg_bad	db ' ; mises a jour refusees : ',0
dg_nl	db 10,0
	align 4
