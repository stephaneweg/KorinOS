; ============================================================================
;  kits/uikit_theme.inc - UIKit : le theme du bureau (SD0:/etc/theme.txt, les
;  memes cles qu'Onyx), les nuances de couleur, les boites arrondies.
;
;  SD0:/etc/theme.txt (« cle = valeur », # : commentaire) :
;	theme    = Givre | Onyx Noir
;	active   = 0xRRGGBB            (cadre de la fenetre active)
;	inactive = 0xRRGGBB            (cadres des autres)
;	window   = 0xRRGGBB            (fond des fenetres)
;	accent   = 0xRRGGBB ou un nom de uk_accents (focus, selection)
;	menubar  = auto | 0xRRGGBB     (auto : comme window)
;	dock     = 0xRRGGBB
;	outline  = none | dark | black
;  Un theme nomme donne sa palette ; les cles ecrites ensuite la corrigent
;  (comme uk_theme_parse d'Onyx). Sans fichier, ou nom inconnu : Givre. Le
;  dessin passe par les jetons de chaque theme (TJ_*, uk_jetons).
; ============================================================================

UK_AUTO		= -2			; menubar = auto
UK_TV_N		= 13			; valeurs lues (uk_tv)
UK_NONE		= -1			; cle absente

; themes nommes : nom (16 o), cadre, style
		align 8
uk_named:
	db 'givre',0,0,0,0,0,0,0,0,0,0,0
	dd 0x00E9EDF2, UI_STYLE_PLAT
	db 'onyx noir',0,0,0,0,0,0,0
	dd 0x001F2228, UI_STYLE_PLAT
	dd 0
UK_GIVRE	= 0			; index dans uk_named
UK_NOIR		= 1
UK_NAMED_SIZE	= 32

; palette : fenetre, accent, cadres inactifs, dock
uk_palettes:
	dd 0x00F5F6F8, 0x002563D8, 0x00F5F6F8, 0x00F5F6F8

; jetons des themes Givre et Onyx Noir (TJ_*, dans l'ordre ; DESIGN-ELEGANT 3.1)
uk_jet_givre:
	dw 0x00F5F6F8, 0x00FFFFFF, 0x00ECEEF2, 0x00FFFFFF, 0x00FFFFFF, 0x00E2E5EA
	dw 0x00B8BFC9, 0x0014171C, 0x004C5563, 0x005F6877, 0x002563D8, 0x00E4ECFB
	dw 0x001D4FB3, 0x002563D8, 0x00D93A3F, 0x001E8A5A, 0x00C4861C, 0x00F7F8FA
	dw 0x00F5F6F8, 0x00E9EDF2, 0x00F5F6F8, 0xC4000000, 0, 0x00FFFFFF
uk_jet_noir:
	dw 0x0015171B, 0x001B1E23, 0x00101216, 0x00262A31, 0x00262A31, 0x002A2E36
	dw 0x00525966, 0x00ECEEF2, 0x00B0B6C0, 0x008B929D, 0x002F72DC, 0x001C2F4D
	dw 0x002560C0, 0x007DB4F2, 0x00D93A3F, 0x0046A758, 0x00F1B33B, 0x0015171B
	dw 0x001B1E23, 0x001F2228, 0x0015171B, 0x40000000, 1, 0x00262A31

; couleurs d'accent : nom (16 o) dans theme.txt, Givre, Onyx Noir. Elles gardent leur
; contraste dans les deux themes ; les claires prennent une encre sombre (TJ_ACCENT_INK).
UK_ACC_NOM	= 0x40000000		; uk_tv : accent nomme (+ son rang), lu avant le theme
UK_ACC_SIZE	= 24
		align 8
uk_accents:
	db 'bleu',0
	times 15-4 db 0
	dw 0x002563D8, 0x002F72DC
	db 'orange',0
	times 15-6 db 0
	dw 0x00CF3F0A, 0x00D04510
	db 'vert',0
	times 15-4 db 0
	dw 0x005FBF12, 0x00A6E22E
	db 'jaune',0
	times 15-5 db 0
	dw 0x00F2B705, 0x00FFD02E
	db 'violet',0
	times 15-6 db 0
	dw 0x007340E0, 0x007E52E6
	db 'fuchsia',0
	times 15-7 db 0
	dw 0x00C2188F, 0x00D12A9C
	db 'sarcelle',0
	times 15-8 db 0
	dw 0x000B7F7A, 0x000D827C
	db 'graphite',0
	times 15-8 db 0
	dw 0x003E4A5B, 0x00A4AEBD
	db 0
		align 8

uk_path_theme	db 'SD0:/etc/theme.txt',0
uk_k_theme	db 'theme',0
uk_k_active	db 'active',0
uk_k_inactive	db 'inactive',0
uk_k_window	db 'window',0
uk_k_accent	db 'accent',0
uk_k_menubar	db 'menubar',0
uk_k_dock	db 'dock',0
uk_k_outline	db 'outline',0
uk_k_button	db 'button',0
uk_k_field	db 'field',0
uk_k_echelle	db 'echelle',0
uk_v_auto	db 'auto',0
uk_v_none	db 'none',0
uk_v_black	db 'black',0
		align 4

; -> x0 = theme du bureau (TH_*, lecture seule)
ui_theme:
	adr	x0,uk_theme
	ret

; x0 = texte d'un theme.txt (modifie ; 0 : relire SD0:/etc/theme.txt) : le
; theme et les jetons de l'application deviennent ceux-ci (apercus : le panneau
; de configuration le prend, copie ui_theme / ui_jetons, puis revient avec 0)
ui_theme_texte:
	cbz	x0,uk_theme_load
	prologue
	adr	x1,uk_tv			; valeurs lues : aucune
	mov	x2,UK_NONE
	mov	x3,UK_TV_N
.clr:	str	x2,[x1],8
	subs	x3,x3,1
	b.ne	.clr
	bl	uk_theme_parse
	bl	uk_theme_apply
	bl	uk_jetons_calc
	epilogue

; theme par defaut, puis SD0:/etc/theme.txt
uk_theme_load:
	prologue
	bl	uk_kits
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,uk_tv			; valeurs lues : aucune
	mov	x1,UK_NONE
	mov	x2,UK_TV_N
.clr:	str	x1,[x0],8
	subs	x2,x2,1
	b.ne	.clr
	mov	x0,2048
	bl	uk_malloc
	cbz	x0,.apply
	mov	x19,x0
	adr	x0,uk_path_theme
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.apply
	mov	x20,x0
	mov	x1,x19
	mov	x2,2047
	sys	SYS_READ
	mov	x21,x0
	mov	x0,x20
	sys	SYS_CLOSE
	cmp	x21,0
	b.le	.apply
	strb	wzr,[x19,x21]
	mov	x0,x19
	bl	uk_theme_parse
.apply:	bl	uk_theme_apply
	bl	uk_jetons_calc
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; valeurs lues (uk_tv) -> theme (uk_theme)
; uk_tv : +0 theme (index), +8 style, +16 active, +24 inactive, +32 window,
; +40 accent, +48 menubar, +56 dock, +64 outline, +72 theme effectif,
; +80 button, +88 field, +96 echelle
uk_theme_apply:
	adr	x9,uk_tv
	adr	x10,uk_theme
	ldr	x8,[x9]				; theme nomme : son cadre, son style
	tbz	x8,63,.named
	mov	x8,UK_GIVRE			; sans theme : Givre (style plat)
.named:	adr	x3,uk_named
	add	x3,x3,x8,lsl 5
	ldp	x1,x0,[x3,16]
	str	x0,[x10,TH_STYLE]
	str	x1,[x10,TH_ACTIVE]
	adr	x3,uk_palettes			; la palette
	str	x8,[x9,72]			; (theme effectif, pour les jetons)
	ldp	x4,x5,[x3]
	str	x4,[x10,TH_WINDOW]
	str	x5,[x10,TH_ACCENT]
	ldp	x4,x5,[x3,16]
	str	x4,[x10,TH_INACTIVE]
	str	x5,[x10,TH_DOCK]
	mov	x4,1				; contour sombre
	str	x4,[x10,TH_OUTLINE]
	ldr	x2,[x9,40]			; accent nomme : sa valeur dans ce theme
	tbnz	x2,63,.keys
	tbz	x2,30,.keys
	and	x2,x2,0xFF
	mov	x4,UK_ACC_SIZE
	adr	x3,uk_accents+16
	madd	x3,x2,x4,x3
	ldr	w2,[x3,x8,lsl 2]
	str	x2,[x9,40]
.keys:	ldr	x2,[x9,16]			; cles ecrites
	tbnz	x2,63,.i
	str	x2,[x10,TH_ACTIVE]
.i:	ldr	x2,[x9,24]
	tbnz	x2,63,.w
	str	x2,[x10,TH_INACTIVE]
.w:	ldr	x2,[x9,32]
	tbnz	x2,63,.a
	str	x2,[x10,TH_WINDOW]
.a:	ldr	x2,[x9,40]
	tbnz	x2,63,.d
	str	x2,[x10,TH_ACCENT]
.d:	ldr	x2,[x9,56]
	tbnz	x2,63,.o
	str	x2,[x10,TH_DOCK]
.o:	ldr	x2,[x9,64]
	tbnz	x2,63,.m
	str	x2,[x10,TH_OUTLINE]
.m:	ldr	x2,[x9,48]			; barre de menus : auto = fenetre
	ldr	x3,[x10,TH_WINDOW]
	tbz	x2,63,.mset
	mov	x2,x3
.mset:	str	x2,[x10,TH_MENUBAR]
	ret

; theme (uk_theme, uk_tv) -> jetons du style plat (uk_jetons, TJ_*)
uk_jetons_calc:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x19,uk_jetons
	adr	x20,uk_theme
	adr	x21,uk_tv
	ldr	x0,[x21,72]			; Givre, Onyx Noir : leurs jetons
	adr	x1,uk_jet_givre
	cmp	x0,UK_GIVRE
	b.eq	.copy
	adr	x1,uk_jet_noir
.copy:	mov	x2,0				; table du theme nomme
.cp:	ldr	w3,[x1,x2,lsl 2]
	str	w3,[x19,x2,lsl 2]
	add	x2,x2,1
	cmp	x2,TJ_TABLE/4
	b.lo	.cp
.keys:	ldr	x0,[x21,40]			; cles ecrites dans theme.txt
	tbnz	x0,63,.k1
	bl	uk_accent_set
.k1:	ldr	x0,[x21,16]
	tbnz	x0,63,.k2
	str	w0,[x19,TJ_TITRE_ACTIF]
.k2:	ldr	x0,[x21,24]
	tbnz	x0,63,.k3
	str	w0,[x19,TJ_TITRE_INACTIF]
.k3:	ldr	x0,[x21,48]			; barre de menus, dock : leur transparence
	tbnz	x0,63,.k4			; est gardee
	ldr	w1,[x19,TJ_BARRE]
	and	w1,w1,0xFF000000
	orr	w0,w0,w1
	str	w0,[x19,TJ_BARRE]
.k4:	ldr	x0,[x21,56]
	tbnz	x0,63,.k5
	ldr	w1,[x19,TJ_DOCK]
	and	w1,w1,0xFF000000
	orr	w0,w0,w1
	str	w0,[x19,TJ_DOCK]
.k5:	ldr	x0,[x21,80]			; button : face des boutons
	tbnz	x0,63,.k6
	str	w0,[x19,TJ_ELEVE]
	str	w0,[x19,TJ_ELEVE_BAS]
.k6:	ldr	x0,[x21,88]			; field : champs, listes
	tbnz	x0,63,.ink
	str	w0,[x19,TJ_SURFACE]
.ink:	ldr	w0,[x19,TJ_ACCENT]
	bl	ui_ink
	str	w0,[x19,TJ_ACCENT_INK]
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (uk_jetons_calc) w0 = accent, x19 = jetons : lui et ses nuances
uk_accent_set:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x0,[sp,-16]!
	str	w0,[x19,TJ_ACCENT]
	mov	w20,w0
	ldr	w1,[x19,TJ_SOMBRE]
	cbnz	w1,.noir
	movl	w0,0x00FFFFFF			; Givre : clair presque blanc,
	mov	w1,w20
	mov	x2,30
	bl	ui_mix
	str	w0,[x19,TJ_ACCENT_CLAIR]
	mov	w0,w20				; vif : assombri s'il est lumineux
	bl	uk_bright
	str	w20,[x19,TJ_ACCENT_VIF]
	cmp	x0,140
	b.ls	.fonce
	mov	w0,w20
	mov	x1,88
	bl	ui_tone
	str	w0,[x19,TJ_ACCENT_VIF]
	b	.fonce
.noir:	ldr	w0,[x19,TJ_FACE]		; Noir : clair sombre, vif eclairci
	mov	w1,w20
	mov	x2,64
	bl	ui_mix
	str	w0,[x19,TJ_ACCENT_CLAIR]
	mov	w0,w20
	movl	w1,0x00FFFFFF
	mov	x2,100
	bl	ui_mix
	str	w0,[x19,TJ_ACCENT_VIF]
.fonce:	mov	w0,w20
	mov	x1,104
	bl	ui_tone
	str	w0,[x19,TJ_ACCENT_FONCE]
	add	sp,sp,16
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = jetons du theme (TJ_*, 32 bits chacun, lecture seule)
ui_jetons:
	adr	x0,uk_jetons
	ret

; -> x0 = echelle : 0 grande (hauteur >= 720), 1 moyenne (>= 540), 2 petite
; (ou largeur < 720) ; « echelle = » de theme.txt l'impose
ui_echelle:
	prologue
	adr	x1,uk_tv
	ldr	x0,[x1,96]
	tbz	x0,63,.out
	adr	x1,uk_ech
	ldr	x0,[x1]
	tbz	x0,63,.out
	adr	x0,uk_scr
	bl	ui_screen
	mov	x1,x0
	mov	x0,0
	tbnz	x1,63,.out			; pas de serveur : grande, non retenue
	adr	x1,uk_scr
	ldp	x2,x3,[x1]
	mov	x0,2
	cmp	x2,720
	b.lo	.set
	cmp	x3,540
	b.lo	.set
	mov	x0,1
	cmp	x3,720
	b.lo	.set
	mov	x0,0
.set:	adr	x1,uk_ech
	str	x0,[x1]
.out:	epilogue

; x0 = texte du fichier (modifie) : remplit uk_tv
uk_theme_parse:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0				; debut de ligne
.line:	ldrb	w0,[x19]
	cbz	w0,.out
	mov	x20,x19				; fin de ligne (# : commentaire) -> 0
.eol:	ldrb	w0,[x20]
	cbz	w0,.cut
	cmp	w0,10
	b.eq	.cut
	cmp	w0,'#'
	b.ne	.e1
	strb	wzr,[x20]			; la suite de la ligne est ignoree
.e1:	add	x20,x20,1
	b	.eol
.cut:	ldrb	w22,[x20]
	strb	wzr,[x20]
	mov	x0,x19
	bl	uk_theme_line
	cbz	w22,.out
	add	x19,x20,1
	b	.line
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = ligne « cle = valeur » (terminee par 0, modifiee)
uk_theme_line:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	uk_skip_sp
	mov	x19,x0				; cle
.k:	ldrb	w1,[x0]
	cbz	w1,.out				; pas de « = »
	cmp	w1,'='
	b.eq	.keq
	cmp	w1,' '
	b.ls	.kend
	orr	w1,w1,0x20			; minuscules (ASCII)
	strb	w1,[x0],1
	b	.k
.kend:	strb	wzr,[x0],1			; fin de cle, puis chercher « = »
.k2:	ldrb	w1,[x0]
	cbz	w1,.out
	add	x0,x0,1
	cmp	w1,'='
	b.ne	.k2
	b	.val
.keq:	strb	wzr,[x0],1
.val:	bl	uk_skip_sp
	mov	x20,x0				; valeur : jusqu'a la fin de ligne (CR
	mov	x1,x0				; compris), espaces de fin retires
.v:	ldrb	w2,[x1]
	cmp	w2,' '
	b.lo	.vend
	orr	w2,w2,0x20
	cmp	w2,'a'				; minuscules (lettres seulement)
	b.lo	.vnext
	cmp	w2,'z'
	b.hi	.vnext
	strb	w2,[x1]
.vnext:	add	x1,x1,1
	b	.v
.vend:	strb	wzr,[x1]
.trim:	cmp	x1,x20
	b.ls	.kind
	ldurb	w2,[x1,-1]
	cmp	w2,' '
	b.ne	.kind
	sub	x1,x1,1
	strb	wzr,[x1]
	b	.trim
.kind:
	adr	x21,uk_tv
	mov	x0,x19				; quelle cle ?
	adr	x1,uk_k_theme
	bl	uk_str_eq
	cbnz	x0,.theme
	mov	x0,x19
	adr	x1,uk_k_outline
	bl	uk_str_eq
	cbnz	x0,.outline
	mov	x22,16				; couleurs : active.. dans l'ordre de uk_tv
	mov	x0,x19
	adr	x1,uk_k_active
	bl	uk_str_eq
	cbnz	x0,.colour
	mov	x22,24
	mov	x0,x19
	adr	x1,uk_k_inactive
	bl	uk_str_eq
	cbnz	x0,.colour
	mov	x22,32
	mov	x0,x19
	adr	x1,uk_k_window
	bl	uk_str_eq
	cbnz	x0,.colour
	mov	x22,40
	mov	x0,x19
	adr	x1,uk_k_accent
	bl	uk_str_eq
	cbnz	x0,.colour
	mov	x22,48
	mov	x0,x19
	adr	x1,uk_k_menubar
	bl	uk_str_eq
	cbnz	x0,.colour
	mov	x22,56
	mov	x0,x19
	adr	x1,uk_k_dock
	bl	uk_str_eq
	cbnz	x0,.colour
	mov	x22,80
	mov	x0,x19
	adr	x1,uk_k_button
	bl	uk_str_eq
	cbnz	x0,.colour
	mov	x22,88
	mov	x0,x19
	adr	x1,uk_k_field
	bl	uk_str_eq
	cbnz	x0,.colour
	mov	x0,x19
	adr	x1,uk_k_echelle
	bl	uk_str_eq
	cbz	x0,.out
	ldrb	w0,[x20]			; echelle = l | m | p (auto : absente)
	mov	x1,0
	cmp	w0,'l'
	b.eq	.eset
	mov	x1,1
	cmp	w0,'m'
	b.eq	.eset
	mov	x1,2
	cmp	w0,'p'
	b.ne	.out
.eset:	str	x1,[x21,96]
	b	.out
.colour:
	mov	x0,x20
	adr	x1,uk_v_auto
	bl	uk_str_eq
	cbz	x0,.hex
	mov	x0,UK_AUTO
	str	x0,[x21,x22]
	b	.out
.hex:	cmp	x22,40				; accent : un nom ?
	b.ne	.hexv
	adr	x19,uk_accents
	mov	x22,0
.an:	ldrb	w0,[x19]
	cbz	w0,.anon
	mov	x0,x20
	mov	x1,x19
	bl	uk_str_eq
	cbnz	x0,.aset
	add	x19,x19,UK_ACC_SIZE
	add	x22,x22,1
	b	.an
.aset:	mov	x0,UK_ACC_NOM
	orr	x0,x0,x22
	str	x0,[x21,40]
	b	.out
.anon:	mov	x22,40
.hexv:	mov	x0,x20
	bl	uk_parse_hex
	tbnz	x0,63,.out
	str	x0,[x21,x22]
	b	.out
.outline:
	mov	x22,0
	mov	x0,x20
	adr	x1,uk_v_none
	bl	uk_str_eq
	cbnz	x0,.oset
	mov	x22,2
	mov	x0,x20
	adr	x1,uk_v_black
	bl	uk_str_eq
	cbnz	x0,.oset
	mov	x22,1
.oset:	str	x22,[x21,64]
	b	.out
.theme:	adr	x22,uk_named			; theme nomme (les espaces du nom :
	mov	x19,0				; « dark coffee » n'est pas repris)
.t:	ldrb	w0,[x22]
	cbz	w0,.out
	mov	x0,x20
	mov	x1,x22
	bl	uk_str_eq
	cbnz	x0,.tset
	add	x22,x22,UK_NAMED_SIZE
	add	x19,x19,1
	b	.t
.tset:	str	x19,[x21]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine -> x0 apres les espaces et tabulations
uk_skip_sp:
	ldrb	w1,[x0]
	cmp	w1,' '
	b.eq	.n
	cmp	w1,9
	b.ne	.ret
.n:	add	x0,x0,1
	b	uk_skip_sp
.ret:	ret

; x0, x1 = chaines -> x0 = 1 si egales
uk_str_eq:
	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	cmp	w2,w3
	b.ne	.no
	cbnz	w2,uk_str_eq
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = « 0xRRGGBB », « #RRGGBB » ou « RRGGBB » -> x0 = couleur (< 0 : invalide)
uk_parse_hex:
	ldrb	w1,[x0]
	cmp	w1,'#'
	b.ne	.0x
	add	x0,x0,1
	b	.go
.0x:	cmp	w1,'0'
	b.ne	.go
	ldrb	w1,[x0,1]
	cmp	w1,'x'
	b.ne	.go
	add	x0,x0,2
.go:	mov	x2,0				; valeur
	mov	x3,0				; chiffres
.d:	ldrb	w1,[x0],1
	sub	w4,w1,'0'
	cmp	w4,9
	b.ls	.add
	sub	w4,w1,'a'
	cmp	w4,5
	b.hi	.end
	add	w4,w4,10
.add:	lsl	x2,x2,4
	add	x2,x2,x4
	add	x3,x3,1
	b	.d
.end:	cbz	x3,.bad
	and	x0,x2,0xFFFFFF
	ret
.bad:	mov	x0,-1
	ret

; (ui_rbox) x0 = ligne dans le coin, w1 = couleur : pixels partiels des deux coins
; de la ligne x26 -> x0 = retrait de la partie pleine
uk_rb_row:
	prologue
	stp	x27,x28,[sp,-16]!
	str	x1,[sp,-16]!
	adr	x2,uk_rm_off
	ldrh	w2,[x2,x23,lsl 1]
	adr	x3,uk_rm
	add	x2,x3,x2
	madd	x27,x0,x23,x2
	mov	x28,0
.p:	cmp	x28,x23
	b.hs	.done
	ldrb	w14,[x27,x28]
	cmp	w14,255
	b.eq	.done
	cbz	w14,.n
	add	x0,x19,x28
	add	x1,x20,x26
	ldr	w2,[sp]
	mov	x3,x14
	bl	uk_rb_px
	add	x0,x19,x21
	sub	x0,x0,1
	sub	x0,x0,x28
	add	x1,x20,x26
	ldr	w2,[sp]
	mov	x3,x14
	bl	uk_rb_px
.n:	add	x28,x28,1
	b	.p
.done:	mov	x0,x28
	add	sp,sp,16
	ldp	x27,x28,[sp],16
	epilogue

; x0 = x, x1 = y, w2 = couleur, x3 = couverture 1..254 : comme uk_blend_px ; sur un
; pixel transparent (WF_ALPHA), la couleur reste et seule la transparence baisse.
; Feuille, ne touche pas a x14.
uk_rb_px:
	adr	x4,uk_vars
	ldr	x4,[x4]
	ldp	x5,x6,[x4,UV_W]
	cmp	x0,x5
	b.hs	.ret
	cmp	x1,x6
	b.hs	.ret
	adr	x7,uk_clip_on
	ldr	x7,[x7]
	cbz	x7,.in
	adr	x7,uk_clip
	ldp	x8,x9,[x7]
	cmp	x0,x8
	b.lt	.ret
	cmp	x1,x9
	b.lt	.ret
	ldp	x8,x9,[x7,16]
	cmp	x0,x8
	b.ge	.ret
	cmp	x1,x9
	b.ge	.ret
.in:	ldr	x7,[x4,UV_SURF]
	madd	x8,x1,x5,x0
	add	x7,x7,x8,lsl 2
	ldr	w8,[x7]
	mov	w13,255
	sub	w13,w13,w3
	lsr	w9,w8,24
	cbz	w9,.opaque
	mul	w9,w9,w13			; transparence : t * (1 - couverture)
	mov	w10,255
	udiv	w9,w9,w10
	and	w2,w2,0xFFFFFF
	orr	w2,w2,w9,lsl 24
	str	w2,[x7]
	ret
.opaque:
	mov	w9,0
	mov	w10,0
.c:	lsr	w11,w2,w10
	and	w11,w11,0xFF
	lsr	w12,w8,w10
	and	w12,w12,0xFF
	mul	w11,w11,w3
	madd	w11,w12,w13,w11
	mov	w12,255
	udiv	w11,w11,w12
	lsl	w11,w11,w10
	orr	w9,w9,w11
	add	w10,w10,8
	cmp	w10,24
	b.lo	.c
	str	w9,[x7]
.ret:	ret

include 'uikit_rmask.inc'

; ---------------------------------------------------------------------------
;  Couleurs (comme uikit/paint.cpp d'Onyx)
; ---------------------------------------------------------------------------

; w0 = couleur, x1 = niveau 0..255 -> w0 : < 128 plus sombre, > 128 plus clair
ui_tone:
	cmp	x1,0
	csel	x1,xzr,x1,lt
	mov	x2,255
	cmp	x1,x2
	csel	x1,x2,x1,gt
	mov	w5,0				; resultat
	mov	w3,0				; decalage
.ch:	lsrv	w4,w0,w3
	and	w4,w4,0xFF
	cmp	x1,128
	b.lo	.dark
	mov	w6,255				; v + (255 - v) * (n - 128) / 127
	sub	w6,w6,w4
	sub	w7,w1,128
	mul	w6,w6,w7
	mov	w7,127
	udiv	w6,w6,w7
	add	w4,w4,w6
	b	.put
.dark:	mul	w4,w4,w1			; v * n / 128
	lsr	w4,w4,7
.put:	lslv	w4,w4,w3
	orr	w5,w5,w4
	add	w3,w3,8
	cmp	w3,24
	b.lo	.ch
	mov	w0,w5
	ret

; w0 = a, w1 = b, x2 = t (0..256) -> w0 = a * (256 - t) / 256 + b * t / 256
ui_mix:
	cmp	x2,0
	b.le	.a
	cmp	x2,256
	b.ge	.b
	mov	w5,0
	mov	w3,0
	mov	w6,256
	sub	w6,w6,w2
.ch:	lsrv	w4,w0,w3
	and	w4,w4,0xFF
	lsrv	w7,w1,w3
	and	w7,w7,0xFF
	mul	w4,w4,w6
	madd	w4,w7,w2,w4
	lsr	w4,w4,8
	lslv	w4,w4,w3
	orr	w5,w5,w4
	add	w3,w3,8
	cmp	w3,24
	b.lo	.ch
	mov	w0,w5
	ret
.a:	and	w0,w0,0xFFFFFF
	ret
.b:	and	w0,w1,0xFFFFFF
	ret

; w0 = couleur -> x0 = luminosite 0..255
uk_bright:
	ubfx	w1,w0,16,8
	ubfx	w2,w0,8,8
	and	w3,w0,0xFF
	mov	w4,77
	mul	w1,w1,w4
	mov	w4,151
	madd	w1,w2,w4,w1
	mov	w4,28
	madd	w1,w3,w4,w1
	lsr	w0,w1,8
	ret

; w0 = fond -> w0 = encre lisible dessus (presque noir, ou blanc)
ui_ink:
	prologue
	bl	uk_bright
	movl	w1,0x00201C1A
	cmp	w0,140
	mov	w0,0x00FFFFFF
	csel	w0,w1,w0,hi
	epilogue

; x0 = n -> x0 = partie entiere de la racine carree (petits n)
uk_isqrt:
	mov	x1,0
.l:	add	x2,x1,1
	mul	x3,x2,x2
	cmp	x3,x0
	b.hi	.ret
	mov	x1,x2
	b	.l
.ret:	mov	x0,x1
	ret

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur, x4 = rayon, w5 = couleur du
; haut, w6 = couleur du bas : boite arrondie en degrade vertical
; (coins anticrenelees jusqu'au rayon 16, couleur opaque ; sinon un pixel de bord
; par ligne, sans anticrenelage)
ui_rbox:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	w24,w5
	mov	w25,w6
	cmp	w22,0				; (en 32 bits : un widget trop petit donne
	b.le	.out				;  une taille negative en w, sans signe en x)
	cmp	w21,0
	b.le	.out
	lsl	x0,x23,1			; rayon <= moitie des cotes
	cmp	x0,x21
	b.le	.r1
	lsr	x23,x21,1
.r1:	lsl	x0,x23,1
	cmp	x0,x22
	b.le	.r2
	lsr	x23,x22,1
.r2:	mov	x26,0				; ligne
.row:	cmp	x26,x22
	b.hs	.out
	mov	w0,w24				; couleur de la ligne
	cmp	x22,1
	b.le	.c
	mov	w1,w25
	lsl	x2,x26,8
	sub	x3,x22,1
	udiv	x2,x2,x3
	bl	ui_mix
	and	w1,w24,0xFF000000		; (transparence de la couleur du haut)
	orr	w0,w0,w1
.c:	mov	w4,w0
	mov	x5,0				; retrait aux coins
	cbz	x23,.fill
	mov	x6,x26				; ligne dans le coin
	cmp	x26,x23
	b.lo	.corner
	sub	x6,x22,1
	sub	x6,x6,x26
	cmp	x6,x23
	b.hs	.fill
.corner:
	lsr	w0,w4,24
	cbnz	w0,.net
	cmp	x23,16
	b.hi	.net
	str	x4,[sp,-16]!
	mov	x0,x6
	mov	w1,w4
	bl	uk_rb_row
	mov	x5,x0
	ldr	x4,[sp],16
	b	.fill
.net:						; r - racine(4r² - (2r - 2j - 1)²) / 2
	str	x4,[sp,-16]!
	lsl	x0,x23,1
	mul	x1,x0,x0			; 4r²
	sub	x2,x0,x6,lsl 1
	sub	x2,x2,1
	mul	x2,x2,x2
	sub	x0,x1,x2
	bl	uk_isqrt
	add	x0,x0,1				; arrondi
	lsr	x0,x0,1
	sub	x5,x23,x0
	ldr	x4,[sp],16
.fill:	add	x0,x19,x5
	add	x1,x20,x26
	sub	x2,x21,x5,lsl 1
	mov	x3,1
	bl	ui_fill
	add	x26,x26,1
	b	.row
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
