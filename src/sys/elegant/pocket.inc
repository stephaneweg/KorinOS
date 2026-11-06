; ============================================================================
;  elegant/pocket.inc - mode « poche » (shell=poche ; docs/MODE-POCHE.md,
;  docs/DESIGN-ELEGANT.md 18) : une coquille pour le PocketTerm, dans le meme
;  serveur. Une app devant, en plein ecran sous la barre de menus ; l'accueil
;  (fenetre WF_HOME de SD0:/sys/home) derriere toutes les apps ; les autres
;  apps restent derriere, cachees (bit MINI_BEHIND : ni composees, ni
;  cliquables).
;
;  Placement (pk_place, a la creation) :
;   * fenetre a cadre, redimensionnable ou de taille fixe : agrandie a la zone
;     sous la barre (le chemin du bouton agrandir : win_restore, WEV_RESIZE) ;
;     une fenetre qui garde une taille plus petite est centree dans la zone ;
;   * dialogue WF_MODAL : garde sa taille, centre ; un voile TEXTE1 a 30 %
;     (veil_fill) est pose sous lui, sur l'app ou l'accueil ;
;   * l'accueil (WF_HOME) : toute la zone sous la barre, comme une agrandie ;
;   * sans cadre : a sa place (menus, volets, toasts, barre).
;  Le cadre n'a que deux glyphes : ‒ (l'accueil : korin.shell.home) et × ;
;  agrandir disparait ; ni deplacement ni bords (core.inc, POLF_POCHE).
;
;  Touches systeme (pk_key, avant l'application) :
;	Super seul		korin.shell.home ; un second Super revient a l'app
;	Alt+Tab, Super+Tab	korin.shell.switch (« alt » : relacher Alt
;				choisira ; Alt relache : « commit »)
;	Super+N			korin.shell.quick
;	Super+L			korin.shell.sleep
;	F11			barre du haut masquee / remontree (plein ecran)
;
;  Sortie de secours : la session poche attend PK_GRACE ms apres la prise de
;  l'ecran ; Maj appuyee (ou tenue au branchement du clavier) pendant ce temps
;  fait demarrer le bureau a la place. « shell=desktop » dans
;  SD0:/etc/options.txt (ou SD0:/options.txt) l'emporte sur system.ini
;  (elegant.asm : mode_choose).
; ============================================================================

PK_GRACE	= 2000			; ms avant la session (Maj : le bureau)
PK_VEIL_COL	= 0x0014171C		; TEXTE1 de Givre (encre du voile, les deux themes)
PK_VEIL_A	= 77			; 30 % (sur 256)
PK_FACE_LIGHT	= 0x00F5F6F8		; FACE de Givre, d'Onyx Noir : le fond sans accueil
PK_FACE_DARK	= 0x0015171B

HID_TAB		= 0x2B
HID_N		= 0x11
HID_L		= 0x0F
HID_F11		= 0x44
HID_LSHIFT	= 0xE1
HID_RSHIFT	= 0xE5
HID_LALT	= 0xE2
HID_RALT	= 0xE6
HID_LGUI	= 0xE3
HID_RGUI	= 0xE7
MOD_SHIFT	= 0x22
MOD_ALT		= 0x44
MOD_GUI		= 0x88

		align 8
mode_poche:	dd str_mode_poche, 0, pk_init, pk_back, bu_none, bu_no
		dd bu_no, pk_tick, pk_restack, POLF_POCHE, pk_place, pk_key, pk_gone, pk_raise

str_mode_poche	db 'poche',0
path_session_p	db 'SD0:/etc/session.poche',0
path_theme_p	db 'SD0:/etc/theme.txt',0
str_theme_p	db 'theme',0
session_p_def	db 'SD0:/sys/menubar --poche',0	; sans SD0:/etc/session.poche
		db 'SD0:/sys/home',0
		db 'SD0:/sys/quick',0
		db 'SD0:/sys/switcher',0
		db 'SD0:/sys/notifyd',0
		db 0
sh_val_alt	db 'alt',0
sh_val_altp	db 'alt-prev',0
sh_val_commit	db 'commit',0
		align 4

; apres la prise de l'ecran : la session attend PK_GRACE ms (sortie de secours)
pk_init:
	prologue
	sys	SYS_TIME_MS
	add	x0,x0,PK_GRACE
	adr	x1,pk_start
	str	x0,[x1]
	adr	x0,path_theme_p			; le fond : korin.theme n'est publie
	adr	x1,str_theme_p			; qu'au changement de theme, theme.txt
	bl	read_key			; donne celui du demarrage
	adr	x0,ini_val
	ldrb	w0,[x0]				; « Onyx Noir » : sombre
	movl	w2,PK_FACE_LIGHT
	movl	w3,PK_FACE_DARK
	cmp	w0,'O'
	csel	w2,w3,w2,eq
	adr	x1,pk_face
	str	x2,[x1]
	epilogue

; a chaque tour : la session (apres l'attente), le bus
pk_tick:
	prologue
	adr	x1,pk_start
	ldr	x2,[x1]
	cbz	x2,.bus
	sys	SYS_TIME_MS
	adr	x1,pk_start
	ldr	x2,[x1]
	cmp	x0,x2
	b.lo	.bus
	str	xzr,[x1]
	adr	x0,path_session_p
	adr	x1,session_p_def
	bl	session_start
.bus:	adr	x0,pk_bus_ev
	bl	sh_tick
	epilogue

; Maj pendant l'attente : le bureau a la place du mode poche
pk_escape:
	prologue
	adr	x0,pk_start
	str	xzr,[x0]
	adr	x0,mode_bureau
	adr	x1,policy
	str	x0,[x1]
	adr	x0,m_escape
	bl	puts
	adr	x0,bp_canal			; le bus sait deja « poche » : corrige
	ldr	x0,[x0]
	cbz	x0,.ses
	adr	x0,sh_topic_mode
	adr	x1,str_mode_bureau
	mov	x2,BUS_RETAIN
	bl	bp_publish
.ses:	bl	bu_init
	bl	dirty_all
	epilogue

; x0 = evenement du bus (WEV_BUS)
pk_bus_ev:
	prologue
	ldr	w1,[x0,BEV_SUB]
	adr	x2,sh_sub_home
	ldr	x2,[x2]
	cmp	x1,x2
	b.eq	.home
	adr	x2,sh_sub_theme
	ldr	x2,[x2]
	cmp	x1,x2
	b.ne	.out
	ldrb	w1,[x0,BEV_DATA]		; « Onyx Noir, ... » : sombre
	movl	w2,PK_FACE_LIGHT
	movl	w3,PK_FACE_DARK
	cmp	w1,'O'
	csel	w2,w3,w2,eq
	adr	x1,pk_face
	str	x2,[x1]
	bl	dirty_all
	b	.out
.home:	bl	pk_show_home
.out:	epilogue

; fond uni (le theme), dans le clip : sans accueil seulement
pk_back:
	prologue
	adr	x4,clip_r
	ldp	x0,x1,[x4]
	ldp	x2,x3,[x4,16]
	sub	x2,x2,x0
	sub	x3,x3,x1
	adr	x4,pk_face
	ldr	w4,[x4]
	bl	bb_fill
	epilogue

; ---------------------------------------------------------------------------
;  Les commandes
; ---------------------------------------------------------------------------

; l'accueil : korin.shell.home sur le bus. SD0:/sys/home le traite lui-meme
; (WS_OP_RAISE de sa fenetre WF_HOME, puis de l'app d'avant au second appel :
; pk_raise) ; sans programme d'accueil, l'evenement revient ici (pk_bus_ev) ;
; sans busd, tout de suite
pk_home_cmd:
	prologue
	adr	x0,sh_topic_home
	adr	x1,sh_val_1
	bl	sh_publish
	cbnz	x0,.out
	adr	x0,pk_home			; (sans busd : l'accueil devant)
	mov	x1,1
	str	x1,[x0]
	bl	wins_changed
.out:	epilogue

; (sans programme d'accueil seulement) les apps cachees
pk_show_home:
	prologue
	bl	pk_find_home
	cbnz	x0,.out
	adr	x0,pk_home
	mov	x1,1
	str	x1,[x0]
	bl	wins_changed
.out:	epilogue

; Super seul : korin.shell.home (l'accueil fait l'aller et le retour) ; sans
; programme d'accueil : l'accueil vide, ou (deja montre) l'app d'avant
pk_super_key:
	prologue
	bl	pk_find_home
	cbnz	x0,.home
	adr	x0,pk_home
	ldr	x1,[x0]
	cbz	x1,.home
	adr	x1,pk_any			; une app derriere ?
	ldr	x1,[x1]
	cbz	x1,.out
	str	xzr,[x0]
	bl	wins_changed
	b	.out
.home:	bl	pk_home_cmd
.out:	epilogue

; F11 : la barre du haut (fenetres WF_RESERVE en haut) masquee ou remontree ;
; les fenetres agrandies suivent (reserve_update)
pk_bar_toggle:
	prologue
	adr	x0,pk_bar_off
	ldr	x1,[x0]
	eor	x1,x1,1
	str	x1,[x0]
	adr	x2,windows
	mov	x3,MAX_WINDOWS
.w:	ldr	x4,[x2,W_USED]
	cbz	x4,.n
	ldr	x4,[x2,W_FLAGS]
	tbz	x4,WFB_RESERVE,.n
	ldr	x4,[x2,W_Y]
	cbnz	x4,.n
	ldr	x4,[x2,W_MINI]
	bic	x4,x4,MINI_BAR
	orr	x4,x4,x1,lsl 4			; (MINI_BAR)
	str	x4,[x2,W_MINI]
.n:	add	x2,x2,WIN_SIZE
	subs	x3,x3,1
	b.ne	.w
	bl	reserve_update
	bl	dirty_all
	bl	wins_changed
	epilogue

; x0 = entree IN_KEY -> x0 = 1 si prise par la coquille
pk_key:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldr	x19,[x0,8]			; code HID
	ldr	x20,[x0,16]			; 1 appui, 0 relache
	ldr	x21,[x0,24]			; modificateurs
	adr	x0,pk_start			; l'attente : Maj -> le bureau
	ldr	x0,[x0]
	cbz	x0,.on
	mov	x0,MOD_SHIFT
	tst	x21,x0
	b.ne	.esc
	cmp	x19,HID_LSHIFT
	b.eq	.esc
	cmp	x19,HID_RSHIFT
	b.ne	.pass
.esc:	bl	pk_escape
	b	.pass
.on:	cmp	x19,HID_LGUI
	b.eq	.gui
	cmp	x19,HID_RGUI
	b.eq	.gui
	cbz	x20,.rel
	adr	x0,pk_super			; une autre touche : Super n'est plus seul
	str	xzr,[x0]
	cmp	x19,HID_F11
	b.eq	.f11
	mov	x0,MOD_GUI
	tst	x21,x0
	b.ne	.super
	mov	x0,MOD_ALT
	tst	x21,x0
	b.eq	.pass
	cmp	x19,HID_TAB			; Alt+Tab
	b.ne	.pass
	adr	x0,pk_alttab
	mov	x1,1
	str	x1,[x0]
	adr	x1,sh_val_alt
	mov	x2,MOD_SHIFT
	tst	x21,x2
	b.eq	.sw
	adr	x1,sh_val_altp
.sw:	adr	x0,sh_topic_switch
	b	.pub
.super:	adr	x0,sh_topic_switch		; Super+Tab, N, L
	adr	x1,sh_val_1
	cmp	x19,HID_TAB
	b.eq	.pub
	adr	x0,sh_topic_quick
	cmp	x19,HID_N
	b.eq	.pub
	adr	x0,sh_topic_sleep
	cmp	x19,HID_L
	b.ne	.pass
.pub:	bl	sh_publish
	b	.eat
.f11:	bl	pk_bar_toggle
.eat:	adr	x0,pk_eat			; son relache aussi
	str	x19,[x0]
	b	.take
.gui:	cbz	x20,.guir			; Super : avale ; seul -> l'accueil
	adr	x0,pk_super
	mov	x1,1
	str	x1,[x0]
	b	.take
.guir:	adr	x0,pk_super
	ldr	x1,[x0]
	str	xzr,[x0]
	cbz	x1,.take
	bl	pk_super_key
	b	.take
.rel:	adr	x0,pk_eat			; relache d'une touche prise
	ldr	x1,[x0]
	cmp	x1,x19
	b.ne	.ralt
	str	xzr,[x0]
	b	.take
.ralt:	cmp	x19,HID_LALT			; Alt relache apres Alt+Tab : le
	b.eq	.alt				; commutateur passe a l'app choisie
	cmp	x19,HID_RALT
	b.ne	.pass
.alt:	adr	x0,pk_alttab
	ldr	x1,[x0]
	cbz	x1,.pass
	str	xzr,[x0]
	adr	x0,sh_topic_switch
	adr	x1,sh_val_commit
	bl	sh_publish
.pass:	mov	x0,0
	b	.out
.take:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Placement et ordre
; ---------------------------------------------------------------------------

; x0 = fenetre -> x0 = 1 si c'est une app : normale, a cadre (dialogues
; compris), hors accueil (feuille)
pk_is_app:
	ldr	x1,[x0,W_FLAGS]
	tst	x1,WF_FRAMELESS or WF_BACK or WF_TOP	; (garde x2..)
	b.ne	.no
	tbnz	x1,WFB_HOME,.no
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = fenetre -> x0 = 1 si le mode poche tient sa place (une app, ou
; l'accueil) : WS_OP_WIN_PLACE, WIN_MOVE, WIN_DRAG sans effet (feuille)
pk_owns:
	adr	x1,policy
	ldr	x1,[x1]
	ldr	x1,[x1,POL_FLAGS]
	tbz	x1,0,.no
	ldr	x1,[x0,W_FLAGS]
	tbnz	x1,WFB_HOME,.yes
	b	pk_is_app
.yes:	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; -> x0 = la fenetre de l'accueil (0 : aucune) (feuille)
pk_find_home:
	adr	x0,windows
	mov	x1,MAX_WINDOWS
.w:	ldr	x2,[x0,W_USED]
	cbz	x2,.n
	ldr	x2,[x0,W_FLAGS]
	tbnz	x2,WFB_HOME,.ret
.n:	add	x0,x0,WIN_SIZE
	subs	x1,x1,1
	b.ne	.w
	mov	x0,0
.ret:	ret

; x0 = fenetre : agrandie a la zone sous la barre (win_restore, comme le bouton
; agrandir ; reserve_update la suivra) ; WEV_RESIZE si sa taille differe
pk_maximize:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x1,windows
	sub	x1,x19,x1
	lsr	x1,x1,WIN_SHIFT
	mov	x2,40
	adr	x20,win_restore
	madd	x20,x1,x2,x20
	ldr	x0,[x20,32]
	cbnz	x0,.place
	ldr	x0,[x19,W_X]
	ldr	x1,[x19,W_Y]
	stp	x0,x1,[x20]
	ldr	x0,[x19,W_W]
	ldr	x1,[x19,W_H]
	stp	x0,x1,[x20,16]
	mov	x0,1
	str	x0,[x20,32]
.place:	mov	x0,x19
	bl	dirty_window
	bl	screen_work
	str	x0,[x19,W_X]
	str	x1,[x19,W_Y]
	ldr	x4,[x19,W_W]
	ldr	x5,[x19,W_H]
	cmp	x4,x2
	ccmp	x5,x3,0,eq
	b.eq	.out
	mov	x0,x19
	mov	x1,x2
	mov	x2,x3
	bl	resize_post
.out:	mov	x0,x19
	bl	dirty_window
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fenetre : centree dans la zone sous la barre (pas au-dessus d'elle)
; -> x0 = 1 si elle a bouge
pk_center:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	screen_work			; x0, x1 = coin, x2, x3 = taille
	ldr	x4,[x19,W_W]
	ldr	x5,[x19,W_H]
	subs	x2,x2,x4
	csel	x2,xzr,x2,lt
	subs	x3,x3,x5
	csel	x3,xzr,x3,lt
	add	x0,x0,x2,lsr 1
	add	x1,x1,x3,lsr 1
	ldr	x4,[x19,W_X]
	ldr	x5,[x19,W_Y]
	cmp	x0,x4
	ccmp	x1,x5,0,eq
	b.eq	.same
	stp	x0,x1,[sp,-16]!
	mov	x0,x19
	bl	dirty_window
	ldp	x0,x1,[sp],16
	str	x0,[x19,W_X]
	str	x1,[x19,W_Y]
	mov	x0,x19
	bl	dirty_window
	mov	x0,1
	b	.out
.same:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = fenetre creee
pk_place:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,W_FLAGS]
	tbnz	x1,WFB_HOME,.home
	bl	pk_is_app
	cbz	x0,.out
	ldr	x1,[x19,W_FLAGS]
	tbnz	x1,WFB_MODAL,.modal
	adr	x0,pk_home			; une app de plus : devant
	str	xzr,[x0]
.home:	mov	x0,x19
	bl	pk_maximize
	b	.out
.modal:	mov	x0,x19
	bl	pk_center
.out:	bl	wins_changed
	ldr	x19,[sp],16
	epilogue

; x0 = fenetre ramenee devant (WS_OP_RAISE : le commutateur, la bande de
; l'accueil) : une app efface l'accueil ; l'accueil le montre
pk_raise:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	ldr	x1,[x19,W_FLAGS]
	tbnz	x1,WFB_HOME,.home
	bl	pk_is_app
	cbz	x0,.out
	adr	x0,pk_home
	str	xzr,[x0]
	b	.out
.home:	adr	x0,pk_home
	mov	x1,1
	str	x1,[x0]
.out:	bl	wins_changed
	ldr	x19,[sp],16
	epilogue

; x0 = fenetre qui va disparaitre : l'app devant se ferme -> l'accueil (si
; son programme n'a plus d'autre fenetre)
pk_gone:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x1,pk_veil			; (le voile sera refait)
	ldr	x2,[x1]
	cmp	x2,x19
	b.ne	.v
	mov	x2,-1
	str	x2,[x1]
.v:	adr	x1,pk_front
	ldr	x1,[x1]
	cmp	x1,x19
	b.ne	.out
	ldr	x20,[x19,W_PID]			; une autre fenetre de ce programme ?
	adr	x2,windows
	mov	x3,MAX_WINDOWS
.w:	ldr	x4,[x2,W_USED]
	cbz	x4,.n
	cmp	x2,x19
	b.eq	.n
	ldr	x4,[x2,W_PID]
	cmp	x4,x20
	b.ne	.n
	ldr	x4,[x2,W_MINI]
	bic	x4,x4,MINI_BEHIND
	cbnz	x4,.n
	mov	x0,x2
	bl	pk_is_app
	cbnz	x0,.out
.n:	add	x2,x2,WIN_SIZE
	subs	x3,x3,1
	b.ne	.w
	adr	x0,pk_home
	mov	x1,1
	str	x1,[x0]
.out:	bl	wins_changed
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fenetre, x1 = 1 : cachee (MINI_BEHIND), 0 : montree -> x0 = 1 si
; elle a change (feuille)
pk_hide:
	ldr	x2,[x0,W_MINI]
	bic	x3,x2,MINI_BEHIND
	orr	x3,x3,x1,lsl 3
	str	x3,[x0,W_MINI]
	cmp	x2,x3
	cset	x0,ne
	ret

; POL_WINS (liste, ordre, focus changes) : qui est devant, qui est cache ;
; places (une agrandie plus petite que la zone est centree, un dialogue aussi),
; voile, focus
pk_restack:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x26,0				; quelque chose a change (tout recomposer)
	; --- places : agrandies et dialogues centres
	adr	x19,windows
	mov	x20,0
.pl:	ldr	x0,[x19,W_USED]
	cbz	x0,.pln
	mov	x0,x19
	bl	pk_is_app
	cbz	x0,.pln
	ldr	x0,[x19,W_FLAGS]
	tbnz	x0,WFB_MODAL,.plc
	mov	x0,40
	adr	x1,win_restore
	madd	x1,x20,x0,x1
	ldr	x0,[x1,32]
	cbz	x0,.pln				; (pas encore agrandie : pk_place le fera)
.plc:	mov	x0,x19
	bl	pk_center
.pln:	add	x19,x19,WIN_SIZE
	add	x20,x20,1
	cmp	x20,MAX_WINDOWS
	b.lo	.pl
	; --- de l'avant vers l'arriere : la premiere app (et les dialogues
	; au-dessus d'elle) montree, les autres cachees
	mov	x21,0				; app devant
	mov	x22,0				; dialogue le plus bas montre (voile)
	mov	x23,0				; fenetre montree la plus en avant
	mov	x25,0				; une app existe
	adr	x0,win_count
	ldr	x24,[x0]
.z:	cbz	x24,.zdone
	sub	x24,x24,1
	adr	x0,win_z
	ldr	x19,[x0,x24,lsl 3]
	mov	x0,x19
	bl	pk_is_app
	cbz	x0,.z
	ldr	x0,[x19,W_MINI]			; reduite, rideau... : pas candidate
	bic	x0,x0,MINI_BEHIND
	cbnz	x0,.z
	mov	x25,1
	mov	x1,1				; cachee par defaut
	adr	x0,pk_home
	ldr	x0,[x0]
	cbnz	x0,.zset
	cbnz	x21,.zset
	mov	x1,0
	cbnz	x23,.zm
	mov	x23,x19
.zm:	ldr	x0,[x19,W_FLAGS]
	tbz	x0,WFB_MODAL,.zfront
	mov	x22,x19
	b	.zset
.zfront:
	mov	x21,x19
.zset:	mov	x0,x19
	bl	pk_hide
	orr	x26,x26,x0
	b	.z
.zdone:	adr	x0,pk_front
	str	x21,[x0]
	adr	x0,pk_any
	str	x25,[x0]
	; --- l'accueil : cache sous l'app devant, meme plus petite que la zone
	; (Reglages, Terminal : centrees ; autour, le fond uni de pk_back, pas
	; la bande « Ouvertes » ni la recherche, qui prendraient les clics)
	bl	pk_find_home
	mov	x19,x0
	cbz	x19,.back
	cmp	x21,0
	cset	x1,ne
	mov	x0,x19
	bl	pk_hide
	orr	x26,x26,x0
	; --- les fenetres du fond (applets) : cachees s'il y a un accueil
.back:	adr	x20,windows
	mov	x24,MAX_WINDOWS
.b:	ldr	x0,[x20,W_USED]
	cbz	x0,.bn
	ldr	x0,[x20,W_FLAGS]
	tbz	x0,WFB_BACK,.bn
	tbnz	x0,WFB_HOME,.bn
	cmp	x19,0
	cset	x1,ne
	mov	x0,x20
	bl	pk_hide
	orr	x26,x26,x0
.bn:	add	x20,x20,WIN_SIZE
	subs	x24,x24,1
	b.ne	.b
	; --- le voile
	adr	x0,pk_veil
	ldr	x1,[x0]
	cmp	x1,x22
	b.eq	.vok
	str	x22,[x0]
	mov	x26,1
.vok:	cbz	x26,.focus
	adr	x0,capture_win			; (plus de capture sur une cachee)
	bl	desk_forget
	adr	x0,drag_win
	bl	desk_forget
	bl	dirty_all
	; --- le focus : la fenetre montree la plus en avant, sinon l'accueil ;
	; pas pris a une fenetre du dessus (volets, commutateur)
.focus:	mov	x0,x23
	cbnz	x0,.f1
	mov	x0,x19
.f1:	adr	x1,win_focus
	ldr	x1,[x1]
	cmp	x1,x0
	b.eq	.out
	cbz	x1,.fset
	ldr	x2,[x1,W_USED]
	cbz	x2,.fset
	ldr	x2,[x1,W_MINI]
	cbnz	x2,.fset
	ldr	x2,[x1,W_FLAGS]
	tbnz	x2,WFB_TOP,.out
	tbnz	x2,WFB_FRAMELESS,.out		; (menus, popups d'une app)
.fset:	bl	win_set_focus
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Le voile : TEXTE1 a 30 % sur ce qui est deja dans le tampon arriere (zone
;  clip_r), juste avant le dialogue (compose_clip)
; ---------------------------------------------------------------------------
veil_fill:
	prologue
	adr	x9,clip_r
	ldp	x0,x1,[x9]
	ldp	x2,x3,[x9,16]
	sub	x2,x2,x0			; largeur, hauteur
	sub	x3,x3,x1
	cmp	x2,0
	b.le	.out
	cmp	x3,0
	b.le	.out
	adr	x9,bb
	ldp	x4,x5,[x9]			; bb, pas
	madd	x4,x1,x5,x4
	add	x4,x4,x0,lsl 2
	movl	x6,PK_VEIL_COL and 0x00FF00FF	; encre * a (deux canaux a la fois)
	mov	x7,PK_VEIL_A
	mul	x6,x6,x7
	movl	x8,PK_VEIL_COL and 0x0000FF00
	mul	x8,x8,x7
	mov	x7,256-PK_VEIL_A
	movl	x12,0x00FF00FF
	movl	x13,0x0000FF00
.row:	mov	x9,x4
	mov	x10,x2
.px:	ldr	w11,[x9]
	and	x14,x11,x12			; rouge, bleu
	mul	x14,x14,x7
	add	x14,x14,x6
	lsr	x14,x14,8
	and	x14,x14,x12
	and	x15,x11,x13			; vert
	mul	x15,x15,x7
	add	x15,x15,x8
	lsr	x15,x15,8
	and	x15,x15,x13
	orr	x14,x14,x15
	str	w14,[x9],4
	subs	x10,x10,1
	b.ne	.px
	add	x4,x4,x5
	subs	x3,x3,1
	b.ne	.row
.out:	epilogue

m_escape	db 'elegant : Maj au demarrage, le bureau a la place du mode poche',10,0
		align 4

macro pocket_data {
		align 8
pk_start	dd 0				; ms : lancement de la session (0 : fait)
pk_home		dd 0				; 1 : l'accueil devant
pk_front	dd 0				; l'app devant (0 : aucune)
pk_any		dd 0				; 1 : au moins une app ouverte
pk_veil		dd 0				; le dialogue sous lequel est le voile
pk_bar_off	dd 0				; 1 : barre masquee (F11)
pk_super	dd 0				; Super appuye, seul jusqu'ici
pk_alttab	dd 0				; Alt+Tab en cours (Alt tenu)
pk_eat		dd 0				; code HID dont le relache est avale
pk_face		dd PK_FACE_LIGHT		; fond sans accueil
}
