; ============================================================================
;  kits/uikit/uikit.asm - UIKit : l'interface des programmes (docs/KITS.md)
;  -> SD0:/lib/uikit.so. Les variables de l'interface (fenetre, surface,
;  evenement, polices) sont dans le programme (ui_data) : ui_init en recoit
;  l'adresse. Routines AAPCS64 : x0-x7 arguments, x19-x28 preserves.
;
;  Plusieurs fenetres par programme : les variables ci-dessus et celles du kit
;  (uk_dmg, uk_focus...) sont celles de la fenetre courante ; ui_window_use en
;  change (uikit_window.inc).
;
;  Transport : les fenetres sont servies par Elegant (SD0:/sys/elegant.elf),
;  le serveur graphique en espace utilisateur. Requetes par ws_call (message
;  de 512 octets, WS_OP_* de armpi.inc), surface dans un tampon partage
;  (buf_map), evenements par ev_wait. Cadres, menus, theme :
;  kits/uikit_frame.inc, kits/uikit_theme.inc. Fichiers (polices, theme) :
;  par AppKit, importe par ce kit (kit_use, kits_open au premier besoin).
; ============================================================================
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
kit_header
include 'uikit.inc'
include '../../inc/ws_ext.inc'
include '../../inc/fs.inc'

segment readable executable
include 'uikit.abi'
kit_use '../../kits/appkit/appkit.abi'		; fichiers (polices, theme) par le
						; client d'AppKit (docs/FICHIERS.md)

include 'ttf.asm'
if TTF_FONT_SIZE <> UI_FONT_SIZE
	display 'UIKit : UI_FONT_SIZE ne correspond plus a TTF_FONT_SIZE'
	err
end if

; x0 = variables du programme (ui_win, ui_data) -> x0 = 1 si les polices sont
; chargees
ui_init:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x1,uk_vars
	str	x0,[x1]
	adr	x0,ui_path_sans
	adr	x1,ui_path_sans2
	mov	x2,UV_FONT
	bl	uk_font_ui
	mov	x19,x0
	adr	x0,ui_path_bold
	adr	x1,ui_path_bold2
	mov	x2,UV_FONT_B
	bl	uk_font_ui
	adr	x0,ui_path_mono
	adr	x1,uk_vars			; variables du programme (ui_init)
	ldr	x1,[x1]
	add	x1,x1,UV_FONT_M
	bl	ui_load_font
	bl	uk_theme_load			; SD0:/etc/theme.txt
	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; AppKit ouvert au premier besoin (son client de fichiers) : kits_open
; remplit la table d'import du kit, dans les variables du process. Ne
; modifie que x16, x17.
uk_kits:
	adr	x16,uk_kits_ok
	ldr	x17,[x16]
	cbnz	x17,.ok
	stp	x29,x30,[sp,-16]!
	stp	x0,x1,[sp,-16]!
	stp	x2,x3,[sp,-16]!
	stp	x4,x5,[sp,-16]!
	stp	x6,x7,[sp,-16]!
	str	x8,[sp,-16]!
	bl	kits_open
	adr	x16,uk_kits_ok
	mov	x17,1
	str	x17,[x16]
	ldr	x8,[sp],16
	ldp	x6,x7,[sp],16
	ldp	x4,x5,[sp],16
	ldp	x2,x3,[sp],16
	ldp	x0,x1,[sp],16
	ldp	x29,x30,[sp],16
.ok:	ret

; x0 = chemin, x1 = struct police -> x0 = 1 si chargee
; x0, x1 = chemin, chemin de secours (DejaVu : carte plus ancienne), x2 = decalage
; de la police dans les variables du programme -> x0 = 1 si chargee. Ses glyphes
; absents (symboles) viennent de DejaVu Sans, chargee au premier besoin.
uk_font_ui:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x3,uk_vars
	ldr	x3,[x3]
	add	x19,x3,x2
	mov	x20,x1
	mov	x1,x19
	bl	ui_load_font
	cbnz	x0,.ok
	mov	x0,x20
	mov	x1,x19
	bl	ui_load_font
	b	.out
.ok:	adr	x1,uk_font_repli
	str	x1,[x19,F_FALLBACK]
.out:	ldp	x19,x20,[sp],16
	epilogue

; (F_FALLBACK) -> x0 = DejaVu Sans, 0 si elle ne se charge pas
uk_font_repli:
	prologue
	adr	x1,uk_repli_etat
	ldr	x0,[x1]
	cbnz	x0,.vu
	mov	x0,-1				; (un seul essai)
	str	x0,[x1]
	adr	x0,ui_path_sans2
	adr	x1,uk_repli
	bl	ui_load_font
	cbz	x0,.non
	adr	x1,uk_repli_etat
	mov	x0,1
	str	x0,[x1]
.vu:	cmp	x0,1
	b.ne	.non
	adr	x0,uk_repli
	b	.out
.non:	mov	x0,0
.out:	epilogue

ui_load_font:
	prologue
	bl	uk_kits
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,16
	mov	x20,x1
	mov	x19,x0
	mov	x1,sp				; taille
	sys	SYS_STAT
	cbnz	x0,.fail
	ldr	x21,[sp]
	mov	x0,x21
	bl	uk_malloc
	cbz	x0,.fail
	mov	x22,x0
	mov	x0,x19
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	mov	x1,x22
	mov	x2,x21
	sys	SYS_READ
	str	x0,[sp]
	mov	x0,x19
	sys	SYS_CLOSE
	ldr	x0,[sp]
	cmp	x0,x21
	b.ne	.fail
	mov	x0,UI_SCRATCH+UI_CACHE
	bl	uk_malloc
	cbz	x0,.fail
	mov	x19,x0
	mov	x0,x20
	mov	x1,x22
	mov	x2,x21
	mov	x3,x19
	mov	x4,UI_SCRATCH
	bl	ttf_open
	cbz	x0,.fail
	mov	x0,x20
	add	x1,x19,UI_SCRATCH
	mov	x2,UI_CACHE
	bl	ttf_set_cache
	mov	x0,1
	b	.out
.fail:	mov	x0,0
.out:	add	sp,sp,16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = operation (arguments deja dans uk_msg) -> x0 = resultat du serveur
uk_call:
	adr	x1,uk_msg
	str	x0,[x1,WM_OP]
	mov	x0,x1
	sys	SYS_WS_CALL
	ret

; x0 = destination, x1 = titre : copie bornee (120 octets)
uk_title_copy:
	mov	x2,0
.c:	cmp	x2,120
	b.hs	.end
	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	cbz	w3,.ret
	add	x2,x2,1
	b	.c
.end:	strb	wzr,[x0,x2]
.ret:	ret

; -> x0 = poignee de la fenetre du programme
uk_win:
	adr	x0,uk_vars
	ldr	x0,[x0]
	ldr	x0,[x0]
	ret

; ferme la fenetre courante (la fin du programme la ferme aussi) ; une autre
; fenetre du programme, s'il en reste une, devient la fenetre courante
ui_close:
	prologue
	bl	uk_win
	adr	x1,uk_msg
	str	x0,[x1,WM_A]
	mov	x0,WS_OP_CLOSE
	bl	uk_call
	adr	x0,uk_buf
	ldr	x0,[x0]
	sys	SYS_BUF_FREE
	bl	uk_wb_drop
	epilogue

; x0 = tampon 32 o <- largeur et hauteur de l'ecran, bords reserves en bas
; (dock) et en haut (barre de menus) -> x0 = 0 (< 0 : pas de serveur)
ui_screen:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x0,WS_OP_SCREEN
	bl	uk_call
	tbnz	x0,63,.out
	adr	x1,uk_msg
	ldp	x2,x3,[x1,WM_A]
	stp	x2,x3,[x19]
	ldp	x2,x3,[x1,WM_C]			; reserves en bas, en haut
	stp	x2,x3,[x19,16]
.out:	ldr	x19,[sp],16
	epilogue

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur, w4 = couleur (decoupe a la
; surface et a la zone de ui_clip). Feuille : ne modifie que x0-x9.
ui_fill:
	adr	x5,uk_vars			; variables du programme (ui_init)
	ldr	x5,[x5]
	add	x5,x5,UV_W
	ldp	x5,x6,[x5]			; bornes : la surface...
	mov	x7,0
	mov	x8,0
	adr	x9,uk_clip_on			; ... et la zone de decoupe
	ldr	x9,[x9]
	cbz	x9,.nc
	adr	x9,uk_clip+16
	ldr	x9,[x9]
	cmp	x9,x5
	csel	x5,x9,x5,lt
	adr	x9,uk_clip+24
	ldr	x9,[x9]
	cmp	x9,x6
	csel	x6,x9,x6,lt
	adr	x9,uk_clip
	ldp	x7,x8,[x9]
.nc:	add	x2,x0,x2			; droite, bas
	add	x3,x1,x3
	cmp	x0,x7
	csel	x0,x7,x0,lt
	cmp	x1,x8
	csel	x1,x8,x1,lt
	cmp	x2,x5
	csel	x2,x5,x2,gt
	cmp	x3,x6
	csel	x3,x6,x3,gt
	subs	x2,x2,x0
	b.le	.ret
	subs	x3,x3,x1
	b.le	.ret
	adr	x7,uk_vars			; variables du programme (ui_init)
	ldr	x7,[x7]
	ldr	x5,[x7,UV_W]
	ldr	x7,[x7,UV_SURF]
	lsl	x5,x5,2				; pas
	madd	x7,x1,x5,x7
	add	x7,x7,x0,lsl 2
	orr	x4,x4,x4,lsl 32
.row:	mov	x8,x7
	mov	x9,x2
.p2:	cmp	x9,2
	b.lo	.p1
	str	x4,[x8],8
	sub	x9,x9,2
	b	.p2
.p1:	cbz	x9,.next
	str	w4,[x8]
.next:	add	x7,x7,x5
	subs	x3,x3,1
	b.ne	.row
.ret:	ret

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur, w4 = couleur : contour 1 px
ui_frame:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x4,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x3,1
	bl	ui_fill				; haut
	mov	x0,x19
	add	x1,x20,x22
	sub	x1,x1,1
	mov	x2,x21
	mov	x3,1
	ldr	x4,[sp]
	bl	ui_fill				; bas
	mov	x0,x19
	mov	x1,x20
	mov	x2,1
	mov	x3,x22
	ldr	x4,[sp]
	bl	ui_fill				; gauche
	add	x0,x19,x21
	sub	x0,x0,1
	mov	x1,x20
	mov	x2,1
	mov	x3,x22
	ldr	x4,[sp]
	bl	ui_fill				; droite
	add	sp,sp,16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, y = x1, w2 = couleur : un pixel (dans la surface et la zone de
; ui_clip)
ui_pixel:
	adr	x3,uk_clip_on
	ldr	x3,[x3]
	cbz	x3,.in
	adr	x3,uk_clip
	ldp	x4,x5,[x3]
	cmp	x0,x4
	b.lt	.ret
	cmp	x1,x5
	b.lt	.ret
	ldp	x4,x5,[x3,16]
	cmp	x0,x4
	b.ge	.ret
	cmp	x1,x5
	b.ge	.ret
.in:	adr	x3,uk_vars			; variables du programme (ui_init)
	ldr	x3,[x3]
	add	x3,x3,UV_W
	ldp	x3,x4,[x3]
	cmp	x0,x3
	b.hs	.ret
	cmp	x1,x4
	b.hs	.ret
	adr	x5,uk_vars			; variables du programme (ui_init)
	ldr	x5,[x5]
	add	x5,x5,UV_SURF
	ldr	x5,[x5]
	lsl	x3,x3,2
	madd	x5,x1,x3,x5
	str	w2,[x5,x0,lsl 2]
.ret:	ret

; x0 = chaine UTF-8, x1 = x, x2 = ligne de base, w3 = couleur, x4 = taille px,
; x5 = police (ui_font...), x6 = largeur max (0 : jusqu'au bord) -> x0 = x final
; Decoupe : la surface, la zone de ui_clip, et [x, x + max[ si max > 0 et x >= 0.
ui_text:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	w22,w3
	mov	x23,x5
	mov	x24,x6
	mov	x0,x23
	mov	x1,x4
	bl	ttf_set_size
	adr	x0,uk_vars			; variables du programme (ui_init)
	ldr	x0,[x0]
	add	x0,x0,UV_W
	ldp	x7,x8,[x0]			; [x25, x7[ x [x26, x8[ : decoupe
	mov	x25,0
	mov	x26,0
	adr	x9,uk_clip_on
	ldr	x9,[x9]
	cbz	x9,.nc
	adr	x9,uk_clip
	ldp	x25,x26,[x9]
	ldp	x10,x11,[x9,16]
	cmp	x10,x7
	csel	x7,x10,x7,lt
	cmp	x11,x8
	csel	x8,x11,x8,lt
.nc:	cbz	x24,.lim
	tbnz	x20,63,.lim
	cmp	x20,x25
	csel	x25,x20,x25,gt
	add	x10,x20,x24
	cmp	x10,x7
	csel	x7,x10,x7,lt
.lim:	mov	x0,x20				; zone vide : rien
	cmp	x7,x25
	b.le	.out
	cmp	x8,x26
	b.le	.out
	adr	x2,uk_vars			; variables du programme (ui_init)
	ldr	x2,[x2]
	ldr	x3,[x2,UV_W]
	ldr	x2,[x2,UV_SURF]
	lsl	x3,x3,2				; pas
	madd	x2,x26,x3,x2			; surface depuis le coin de la zone
	add	x2,x2,x25,lsl 2
	sub	x7,x7,x25
	sub	x8,x8,x26
	orr	x7,x7,x8,lsl 32
	sub	x4,x20,x25
	sub	x5,x21,x26
	mov	x0,x23
	mov	x1,x19
	mov	w6,w22
	bl	ttf_draw_text
	add	x0,x0,x25
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine, x1 = taille px, x2 = police -> x0 = largeur en pixels
ui_text_width:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	mov	x0,x2
	bl	ttf_set_size
	mov	x0,x20
	mov	x1,x19
	bl	ttf_text_width
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur, x4 = libelle, x5 = 1 accentue
; Bouton plat avec libelle centre (DejaVu Sans 14 px)
ui_button:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x23,x4
	mov	x24,x5
	mov	x0,x19				; fond de la fenetre sous les coins
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	adr	x17,uk_jetons
	ldr	w4,[x17,TJ_FACE]
	bl	ui_fill
	adr	x17,uk_jetons
	ldr	w5,[x17,TJ_ACCENT]			; aplat, ou contour et ELEVE
	cbnz	x24,.c
	adr	x17,uk_jetons
	ldr	w5,[x17,TJ_LIGNE_FORTE]
.c:	mov	w6,w5
	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	mov	x3,x22
	mov	x4,8
	bl	ui_rbox
	cbnz	x24,.lbl
	adr	x17,uk_jetons
	ldr	w5,[x17,TJ_ELEVE]
	mov	w6,w5
	add	x0,x19,1
	add	x1,x20,1
	sub	x2,x21,2
	sub	x3,x22,2
	mov	x4,7
	bl	ui_rbox
.lbl:	mov	x0,x23
	mov	x1,UK_PX
	adr	x2,uk_vars			; variables du programme (ui_init)
	ldr	x2,[x2]
	add	x2,x2,UV_FONT
	bl	ui_text_width
	sub	x1,x21,x0			; centrage
	asr	x1,x1,1
	add	x1,x1,x19
	lsr	x2,x22,1
	add	x2,x2,x20
	add	x2,x2,5
	adr	x17,uk_jetons
	ldr	w3,[x17,TJ_TEXTE1]
	cbz	x24,.t
	adr	x17,uk_jetons
	ldr	w3,[x17,TJ_ACCENT_INK]
.t:	mov	x0,x23
	mov	x4,UK_PX
	adr	x5,uk_vars			; variables du programme (ui_init)
	ldr	x5,[x5]
	add	x5,x5,UV_FONT
	mov	x6,0
	bl	ui_text
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur : signale une zone modifiee, sans
; attendre Elegant (present asynchrone : ws_ext.inc)
ui_update:
	adr	x9,uk_dmg			; l'en-tete de la fenetre courante
	ldr	x9,[x9]
	cbz	x9,uk_update_call
	adr	x11,uk_vars
	ldr	x11,[x11]
	ldp	x14,x15,[x11,UV_W]
	add	x12,x0,x2			; x10..x13 = x0, y0, x1, y1 bornes
	add	x13,x1,x3
	cmp	x0,0
	csel	x10,xzr,x0,lt
	cmp	x1,0
	csel	x11,xzr,x1,lt
	cmp	x12,x14
	csel	x12,x14,x12,gt
	cmp	x13,x15
	csel	x13,x15,x13,gt
	cmp	x12,x10
	b.le	.none
	cmp	x13,x11
	b.le	.none
.merge:	ldaxr	x14,[x9]
	cbz	x14,.put
	ubfx	x15,x14,0,16			; union avec la zone en attente
	cmp	x15,x10
	csel	x10,x15,x10,lo
	ubfx	x15,x14,16,16
	cmp	x15,x11
	csel	x11,x15,x11,lo
	ubfx	x15,x14,32,16
	cmp	x15,x12
	csel	x12,x15,x12,hi
	ubfx	x15,x14,48,16
	cmp	x15,x13
	csel	x13,x15,x13,hi
.put:	orr	x15,x10,x11,lsl 16
	orr	x15,x15,x12,lsl 32
	orr	x15,x15,x13,lsl 48
	stlxr	w16,x15,[x9]
	cbnz	w16,.merge
	cbnz	x14,.none			; deja signalee : Elegant viendra
	mov	x0,WS_SIG_DAMAGE
	sys	SYS_WS_SIGNAL
.none:	mov	x0,0
	ret

; -> x0 = 1 : un WEV_FRAME suivra, quand Elegant aura pris le contenu (0 : pas
; d'en-tete, pas d'evenement a attendre)
ui_frame_request:
	adr	x9,uk_dmg
	ldr	x9,[x9]
	mov	x0,0
	cbz	x9,.ret
	mov	x0,1
	str	x0,[x9,WD_FRAME]
	dmb	ish
	mov	x0,WS_SIG_DAMAGE
	sys	SYS_WS_SIGNAL
	mov	x0,1
.ret:	ret

; (ui_update) par requete, comme une fenetre sans en-tete
uk_update_call:
	adr	x9,uk_msg
	stp	x0,x1,[x9,WM_B]			; x, y
	str	x2,[x9,WM_D]			; largeur
	str	x3,[x9,WM_DATA]			; hauteur
	adr	x0,uk_vars			; variables du programme (ui_init)
	ldr	x0,[x0]
	ldr	x0,[x0]
	str	x0,[x9,WM_A]
	mov	x0,WS_OP_UPDATE
	b	uk_call

ui_update_all:
	adr	x2,uk_vars			; variables du programme (ui_init)
	ldr	x2,[x2]
	add	x2,x2,UV_W
	ldp	x2,x3,[x2]
	mov	x0,0
	mov	x1,0
	b	ui_update

; x0 = attente max (ms) -> x0 = type d'evenement (0 : aucun), evenement dans ui_ev
; Fenetre a cadre sans widgets (les autres : ui_w_event) : un appui du bouton
; gauche dans le haut attend. S'il bouge, la fenetre se deplace et le programme
; ne voit rien ; sinon il recoit l'appui et le relachement au relachement.
ui_event:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x20,uk_vars			; variables du programme (ui_init)
	ldr	x20,[x20]
	add	x20,x20,UV_EV
	adr	x3,uk_ev_next			; le relachement d'un clic rendu
	ldr	w0,[x3]
	cbz	w0,.wait
	ldp	x1,x2,[x3]
	stp	x1,x2,[x20]
	ldp	x1,x2,[x3,16]
	stp	x1,x2,[x20,16]
	str	wzr,[x3]
	b	.out
.wait:	mov	x1,x19
	mov	x0,x20
	sys	SYS_EV_WAIT
	cmp	x0,WEV_SCREEN			; autre ecran : l'echelle est a revoir
	b.ne	.in
	adr	x1,uk_ech
	mov	x2,-1
	str	x2,[x1]
.in:	cmp	x0,WEV_MOUSE
	b.ne	.out
	adr	x19,uk_ev_held
	ldr	w1,[x19]
	cbnz	w1,.held
	ldr	w1,[x20,EV_KIND]
	cmp	w1,1
	b.ne	.out
	ldr	w1,[x20,EV_Y]
	cmp	w1,UK_INTENT_H
	b.hs	.out
	ldr	w1,[x20,EV_CODE]		; (bouton droit : l'application)
	tbnz	w1,1,.out
	bl	uk_win
	ldr	w1,[x20,EV_WIN]
	cmp	x0,x1
	b.ne	.keep
	adr	x1,uk_flags
	ldr	x1,[x1]
	tbnz	x1,0,.keep			; WF_FRAMELESS
	adr	x2,uk_wfirst
	ldr	x2,[x2]
.w:	cbz	x2,.hold
	ldr	x3,[x2,UW_WIN]
	cmp	x3,x0
	b.eq	.keep
	ldr	x2,[x2,UW_NEXT]
	b	.w
.keep:	mov	x0,WEV_MOUSE
	b	.out
.hold:	ldp	x1,x2,[x20]
	stp	x1,x2,[x19]
	ldp	x1,x2,[x20,16]
	stp	x1,x2,[x19,16]
	b	.nil
.held:	ldr	w1,[x20,EV_KIND]
	cbnz	w1,.flush
	ldrsw	x1,[x20,EV_X]
	ldrsw	x2,[x19,EV_X]
	subs	x1,x1,x2
	cneg	x1,x1,lt
	ldrsw	x2,[x20,EV_Y]
	ldrsw	x3,[x19,EV_Y]
	subs	x2,x2,x3
	cneg	x2,x2,lt
	cmp	x1,UK_INTENT_PX
	b.hs	.drag
	cmp	x2,UK_INTENT_PX
	b.lo	.nil
.drag:	str	wzr,[x19]
	bl	ui_window_drag
.nil:	mov	x0,0
	b	.out
.flush:	adr	x3,uk_ev_next			; le clic : l'appui, puis cet evenement
	ldp	x1,x2,[x20]
	stp	x1,x2,[x3]
	ldp	x1,x2,[x20,16]
	stp	x1,x2,[x3,16]
	ldp	x1,x2,[x19]
	stp	x1,x2,[x20]
	ldp	x1,x2,[x19,16]
	stp	x1,x2,[x20,16]
	str	wzr,[x19]
	mov	x0,WEV_MOUSE
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = x, x1 = y, x2 = largeur, x3 = hauteur -> x0 = 1 si la souris (ui_ev) est dedans
ui_hit:
	adr	x4,uk_vars			; variables du programme (ui_init)
	ldr	x4,[x4]
	add	x4,x4,UV_EV
	ldrsw	x5,[x4,EV_X]
	ldrsw	x6,[x4,EV_Y]
	cmp	x5,x0
	b.lt	.no
	add	x0,x0,x2
	cmp	x5,x0
	b.ge	.no
	cmp	x6,x1
	b.lt	.no
	add	x1,x1,x3
	cmp	x6,x1
	b.ge	.no
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; ---------------------------------------------------------------------------
;  Menus de l'application, montres par la barre de menus globale (Onyx :
;  uikit/menu.h). Texte : lignes separees par « \n » ; « M<titre> » ouvre un
;  menu, « I<id>\t<libelle>\t<raccourci> » une entree (id decimal >= 0,
;  raccourci facultatif, seulement affiche : le programme traite lui-meme
;  ses touches), « - » un separateur. Un choix arrive en WEV_MENU (EV_CODE =
;  id) ; « Quitter » (menu au nom de l'application) en WEV_CLOSE.
; ---------------------------------------------------------------------------

; x0 = texte des menus (0 ou vide : plus de menu) -> x0 = 0 (< 0 : erreur ;
; E_INVAL au-dela de WS_MENU_MAX octets). Envoye en morceaux de WS_MENU_PIECE.
; (ui_menu, uikit_misc.inc : le texte du programme avec ses marques d'etat)
uk_menu_set:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,0				; longueur
	cbz	x19,.sized
.len:	ldrb	w1,[x19,x20]
	cbz	w1,.sized
	add	x20,x20,1
	b	.len
.sized:	mov	x0,-22				; (E_INVAL)
	cmp	x20,WS_MENU_MAX
	b.hi	.out
	mov	x21,0				; decalage
.piece:	sub	x22,x20,x21
	mov	x0,WS_MENU_PIECE
	cmp	x22,x0
	csel	x22,x0,x22,hi
	bl	uk_win
	adr	x9,uk_msg
	stp	x0,x21,[x9,WM_A]		; id, decalage
	str	x20,[x9,WM_C]			; longueur totale
	str	w22,[x9,WM_LEN]
	add	x0,x9,WM_DATA
	add	x1,x19,x21
	mov	x2,0
.c:	cmp	x2,x22
	b.hs	.send
	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	add	x2,x2,1
	b	.c
.send:	mov	x0,WS_OP_MENU_SET
	bl	uk_call
	tbnz	x0,63,.out
	add	x21,x21,x22
	cmp	x21,x20
	b.lo	.piece
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (barre de menus) x0 = tampon du texte (WS_MENU_MAX + 1 octets), x1 = tampon
; du titre (WS_MENU_TITLE octets), x2 = numero de serie deja connu -> x0 =
; numero de serie du menu de la fenetre active (0 : aucun), x1 = id de cette
; fenetre, x2 = longueur du texte. Les tampons ne sont remplis (texte termine
; par NUL) que si le numero n'est pas celui connu.
ui_menu_get:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
.again:	mov	x22,0				; decalage
	mov	x23,0				; numero lu au premier morceau
.piece:	adr	x9,uk_msg
	str	x22,[x9,WM_A]
	mov	x0,WS_OP_MENU_GET
	bl	uk_call
	cmp	x0,0
	b.le	.none
	cbz	x23,.first
	cmp	x0,x23
	b.ne	.again				; change pendant la lecture : on recommence
	b	.data
.first:	mov	x23,x0
	cmp	x0,x21
	b.eq	.known
	adr	x1,uk_msg+WM_DATA		; titre
	mov	x2,0
.t:	ldrb	w3,[x1,x2]
	strb	w3,[x20,x2]
	cbz	w3,.data
	add	x2,x2,1
	cmp	x2,WS_MENU_TITLE-1
	b.lo	.t
	strb	wzr,[x20,x2]
.data:	adr	x9,uk_msg
	ldr	x24,[x9,WM_C]			; longueur totale
	ldr	x2,[x9,WM_D]			; morceau
	mov	x0,WS_MENU_MAX
	cmp	x24,x0
	csel	x24,x0,x24,hi
	add	x0,x22,x2			; (borne au tampon)
	cmp	x0,x24
	b.ls	.cp
	sub	x2,x24,x22
.cp:	add	x1,x9,WM_DATA+WS_MENU_TITLE
	mov	x3,0
.c:	cmp	x3,x2
	b.hs	.cpd
	ldrb	w4,[x1,x3]
	strb	w4,[x19,x22]
	add	x22,x22,1
	add	x3,x3,1
	b	.c
.cpd:	cbz	x2,.end				; (rien de plus : fin)
	cmp	x22,x24
	b.lo	.piece
.end:	strb	wzr,[x19,x22]
	mov	x2,x22
	b	.id
.known:	adr	x9,uk_msg
	ldr	x2,[x9,WM_C]
.id:	adr	x9,uk_msg
	ldr	x1,[x9,WM_B]
	mov	x0,x23
	b	.out
.none:	mov	x0,0
	mov	x1,0
	mov	x2,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (barre de menus) x0 = numero de l'entree (-1 : Quitter), x1 = numero de
; serie du menu montre (0 : sans controle) -> x0 = 0 (E_AGAIN : le menu a
; change) ; le programme de la fenetre active recoit WEV_MENU
ui_menu_command:
	adr	x9,uk_msg
	stp	x0,x1,[x9,WM_A]
	mov	x0,WS_OP_MENU_COMMAND
	b	uk_call

ui_path_sans	db 'SD0:/fonts/Inter-Regular.ttf',0
ui_path_bold	db 'SD0:/fonts/Inter-SemiBold.ttf',0
ui_path_sans2	db 'SD0:/fonts/DejaVuSans.ttf',0
ui_path_bold2	db 'SD0:/fonts/DejaVuSans-Bold.ttf',0
ui_path_mono	db 'SD0:/fonts/DejaVuSansMono.ttf',0
		align 4

; x0 = taille -> x0 = bloc du tas du programme (sbrk), 0 si echec
uk_malloc:
	add	x0,x0,15
	and	x0,x0,not 15
	sys	SYS_SBRK
	tbnz	x0,63,.fail
	ret
.fail:	mov	x0,0
	ret

include 'uikit_theme.inc'
include 'uikit_frame.inc'
include 'uikit_icons.inc'
include '../../inc/tr.inc'
include 'uikit_widgets.inc'
include 'uikit_surface.inc'			; surface attachee (Elegant)

uk_s_ellipsis	db 0xE2,0x80,0xA6,0		; « … »
uk_s_up		db 0xE2,0x96,0xB2,0		; « ▲ »
uk_s_down	db 0xE2,0x96,0xBC,0		; « ▼ »
uk_empty	db 0
uk_s_cross	db 0xC3,0x97,0			; « × »
uk_s_space	db ' ',0
uk_s_minus	db 0xE2,0x88,0x92,0		; « − »
uk_s_plus	db '+',0
uk_s_tab	db '    ',0
uk_s_check	db 0xE2,0x9C,0x93,0		; « ✓ »
uk_s_chev	db 0xE2,0x96,0xBE,0		; « ▾ »
uk_s_ok		db 'OK',0
uk_s_cancel	db 'Annuler',0
uk_s_yes	db 'Oui',0
uk_s_no		db 'Non',0
uk_s_open	db 'Ouvrir un fichier',0
uk_s_save	db 'Enregistrer',0
uk_s_folder	db 'Choisir un dossier',0
uk_s_sd0	db 'SD0:/',0
uk_s_upar	db 0xE2,0x86,0x91,0		; « ↑ »
uk_s_left	db 0xE2,0x86,0x90,0		; « ← »
uk_s_right	db 0xE2,0x86,0x92,0		; « → »
uk_s_date	db 'Date',0
uk_s_pickdate	db 'Choisir une date',0
uk_s_color	db 'Choisir une couleur',0
uk_s_places	db 'Emplacements',0
uk_s_volumes	db 'Volumes',0
uk_s_homel	db 'Accueil',0
uk_s_home	db 'SD0:/home',0
uk_s_sd0c	db 'SD0:',0
uk_s_placesf	db 'SD0:/etc/places.ini',0
uk_s_vfs	db 'vfs',0
uk_s_allfiles	db 'Tous les fichiers',0
uk_s_items	db 0xC3,0xA9,'l',0xC3,0xA9,'ments',0
uk_s_name	db 'Nom',0
uk_s_size	db 'Taille',0
uk_s_namec	db 'Nom :',0
uk_s_bopen	db 'Ouvrir',0
uk_s_bsave	db 'Enregistrer',0
uk_s_bchoose	db 'Choisir',0
uk_s_ko		db ' Ko',0
uk_s_mo		db ' Mo',0
uk_s_go		db ' Go',0
uk_s_oct	db ' o',0
uk_s_dir	db 'Dossier',0
uk_s_replace	db 'Ce fichier existe d',0xC3,0xA9,'j',0xC3,0xA0,'. Le remplacer ?',0
		align 4

segment readable writeable
kit_data					; import d'AppKit (kits_open)
uk_kits_ok	dd 0				; AppKit ouvert
uk_vars		dd 0				; variables du programme (ui_init)
; variables de la fenetre courante : celles des autres sont dans leur bloc
; (WB_*, uikit_window.inc) ; uk_dmg .. uk_flags contigus
uk_dmg		dd 0				; en-tete WD_* de la fenetre (0 : aucun)
uk_buf		dd 0				; tampon partage de la surface
uk_chr_a	dd 0				; cadres actif, inactif (contigus ; 0 : sans)
uk_chr_i	dd 0
uk_ow		dd 0				; taille exterieure (contigus)
uk_oh		dd 0
uk_flags	dd 0				; role de la fenetre
uk_save		rd 3				; surface de dessin mise de cote
uk_theme	rb TH_SIZE			; theme du bureau
uk_tv		rd UK_TV_N			; cles lues dans theme.txt
uk_jetons	rb TJ_SIZE			; jetons du style plat
uk_ech		dd -1				; echelle (-1 : pas encore calculee)
uk_scr		rb 32
uk_pev		rb 32				; evenement d'un menu
uk_title	rb 128
uk_ic_t		rd 3				; icone : taille, x, y
uk_ic_list	dd 0				; icones : le cache (uikit_icons.inc)
uk_ic_path	rb 64
uk_ic_hdr	rb ICF_HDR
uk_ic_lettre	rb 8
uk_pop_rh	dd 0				; menu : hauteur d'une ligne
uk_pop_fl	dd 0				; menu : drapeaux
uk_pop_nom	rb 32
uk_pop_x	dd 0				; menu : x du texte
uk_pop_rac	dd 0				; menu : raccourci de la ligne (bit 4)
uk_pop_lib	rb 128				; menu : texte de la ligne sans raccourci
uk_pop_clic	rd 2				; menu : clic qui l'a ferme (ecran)
uk_pp		rd 12				; menu garde ouvert (ui_popup_ex, drapeau 2)
uk_msg		rb WS_MSG			; message au serveur (une copie par process)
; --- widgets (uikit_widgets.inc) ---
uk_clip		rd 4				; decoupe : x0, y0, x1, y1 (contigus)
uk_clip_on	dd 0
uk_save_clip	dd 0				; (uk_target)
uk_save_rect	rd 5				; (uk_target : uk_clip, uk_clip_sp)
uk_clip_sp	dd 0				; pile de decoupe des widgets
uk_clip_stack	rd 5*8
uk_free_heads	rd 32				; tas du kit : listes libres par classe
uk_wfirst	dd 0				; widgets, dans l'ordre de creation
uk_wlast	dd 0
uk_focus	dd 0				; focus clavier
uk_capture	dd 0				; widget qui capture la souris
uk_hover	dd 0				; widget survole
uk_click	rd 4				; dernier appui : widget, temps, x, y
uk_held		rd 3				; appui qui attend : widget, x, y
uk_ev_held	rb 32				; ui_event : l'appui qui attend (type 0 : aucun)
uk_ev_next	rb 32				; ui_event : le prochain evenement a rendre
uk_replay	dd 0				; 1 : cet appui est rejoue
uk_undo_busy	dd 0				; (annuler en cours : pas de journal)
uk_winact	dd 1
uk_minw		dd 120				; taille minimale (ui_min_size), contigus
uk_minh		dd 80
uk_resize_fn	dd 0
uk_cur_shape	dd -1				; forme du curseur demandee (ui_cursor)
uk_tip_win	rd 2				; infobulle montree : fenetre, tampon
uk_mx		rd 2				; dernier point de la souris (fenetre)
uk_hover_t	dd 0				; (ms) dernier mouvement
uk_menu_tr_d	rd 2				; dernier texte de menus traduit : source, copie
UK_PX		= 14			; texte des boutons, menus, onglets et arbres
UK_TR_ITEMS	= 2048
uk_tr_tampon	rb UK_TR_ITEMS
uk_menu_src	dd 0				; texte des menus du programme
uk_font_st	rb 16				; ui_font_get : 0 inconnue, 1 chargee, 2 absente
uk_mbtn		dd 0				; boutons du dernier evenement souris
uk_pop_place	rd 3				; uk_dialog_at : x, y, 1
uk_dlg_role	dd 0				; role de plus du prochain dialogue
; --- fenetres du programme (uikit_window.inc) ---
uk_cur		dd 0				; bloc de la fenetre courante (0 : aucune)
uk_wins		dd 0				; blocs des fenetres, la derniere en tete
uk_menu_p	dd 0				; memoire des menus (0 : statique, -1 : a faire)
uk_win_dlg	dd 0				; dialogue de la fenetre courante
uk_mwin		dd 0				; fenetre du dernier evenement souris
uk_mt_ok	dd 0
uk_mt		rd 6				; metriques de l'echelle (MT_*)
uk_fbuf		rb 256				; champ d'un element de liste
uk_fitbuf	rb 416				; texte coupe (ui_text_fit)
UK_PW_MAX	= 3*UK_TEXT_MAX+4
uk_pwbuf	rb UK_PW_MAX			; mot de passe montre (« • »)
UK_PASTE_MAX	= UK_TEXT_MAX+1
uk_pastebuf	rb UK_PASTE_MAX			; Ctrl+V d'un champ
uk_chbuf	rb 8
uk_numbuf	rb 32
uk_linebuf	rb UK_LINE_MAX			; ligne d'un texte multiligne
UK_CLIP_BIG	= WS_CLIP_MAX+1
uk_dirent	rb DIRENT_BYTES			; dialogue de fichiers
uk_fdline	rb UK_PATH_MAX
uk_fdpath	rb UK_PATH_MAX
uk_fdtmp	rb UK_PATH_MAX
uk_fdtmp2	rb UK_PATH_MAX
uk_fdname	rb 256
uk_fd_filter	rb 8				; ui_file_filter
uk_fdstat	rd 2
uk_inf		rb INF_SIZE			; inflate (images PNG)
uk_menu_st	rb UK_MENU_IDS*16		; etats des entrees (id, etat)
uk_menubuf	rb WS_MENU_MAX+8		; texte des menus envoye
		align 8
uk_fonts	rb (UK_NFONTS-3)*UI_FONT_SIZE	; polices lues par ui_font
uk_repli	rb UI_FONT_SIZE			; repli des glyphes absents (uk_font_repli)
uk_repli_etat	rd 1
uk_popbuf	rb UK_POP_MAX			; menu d'une liste deroulante
uk_win0		rb WB_SIZE			; bloc de la premiere fenetre
