; ============================================================================
;  elegant.asm - Elegant, le serveur graphique d'ARMPI (SD0:/sys/elegant)
;  (chantier 4, docs/CHANTIER4-GUI.md ; modele d'Onyx : user/Servers/elegant)
;
;  Lance par le noyau au demarrage. S'enregistre comme serveur (ws.asm),
;  prend l'ecran et les entrees, puis sert les programmes : fenetres,
;  surfaces partagees, evenements. Le mode (shell= de SD0:/etc/system.ini,
;  ou l'argument) choisit la table de reglages : « desktop » aujourd'hui ;
;  « console » rend la main (la console texte du noyau garde l'ecran).
;
;	elegant.asm		boucle, choix du mode, polices
;	elegant/mode.inc	interface d'un mode (table POL_*)
;	elegant/core.inc	fenetres, recomposition, entrees, requetes
;	elegant/menu.inc	menus des applications (barre de menus globale)
;	elegant/desktop.inc	mode « desktop » (fond, session : voronoy,
;				menubar, dock)
;	elegant/present.inc	presentation a l'ecran (DMA 2D ou processeur)
;	elegant/snapshot.inc	l'ecran compose (vncd, screenshot)
;	elegant/remote.inc	les fenetres une a une (rdpd)
;	elegant/surface.inc	surface attachee a une fenetre (tampon du programme)
;	elegant/screen.inc	resolution de l'ecran, a chaud
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include 'mode.inc'
include '../../inc/ws_ext.inc'

E_BADF		= -9
E_INVAL		= -22
E_NOMEM		= -12

segment readable executable
kit_use '../../kits/appkit/appkit.abi'

start:
	mov	x19,x0				; argument : mode
	bl	kits_open
	mov	x0,x19
	bl	mode_choose
	cbz	x0,.console
	sys	SYS_WS_REGISTER
	tbnz	x0,63,.busy
	adr	x0,info
	sys	SYS_WS_DISPLAY
	tbnz	x0,63,.busy
	adr	x0,info				; ecran : adresse, largeur, hauteur, pas
	ldp	x1,x2,[x0]
	ldp	x3,x4,[x0,16]
	adr	x5,fb_va
	stp	x1,x4,[x5]			; fb_va, fb_pitch
	adr	x5,scr_w
	stp	x2,x3,[x5]			; scr_w, scr_h
	movl	x0,SCR_BB_BYTES			; tampon arriere (pour la plus grande
	bl	present_init			; resolution), presentation : present.inc
	cbz	x0,.nomem
	adr	x0,scr_w			; curseur au centre
	ldp	x0,x1,[x0]
	lsr	x0,x0,1
	lsr	x1,x1,1
	adr	x2,cur_x
	stp	x0,x1,[x2]
	bl	dirty_all
	pol_call POL_INIT			; (session du bureau)
	bl	hid_abonner			; entrees : HIDManager
	adr	x0,m_ready
	bl	puts

.loop:	bl	do_input			; entrees
	pol_call POL_TICK
	bl	screen_tick			; resolution non confirmee
	bl	notify_listeners		; liste des fenetres changee
	bl	damage_collect			; present asynchrone des programmes
	bl	compose
	bl	frames_post			; -> attente : 8 ms au plus pour la
	mov	x20,x0				; premiere requete (ou un signal), puis
.req:	adr	x0,msg				; toutes celles en file
	mov	x1,x20
	sys	SYS_WS_NEXT
	cbz	x0,.loop
	tbnz	x0,63,.idle
	adr	x1,msg
	ldr	x1,[x1,WM_OP]
	movl	x2,SVC_OP_SIGNAL
	cmp	x1,x2
	b.eq	.sig				; (damage_collect, au tour suivant)
	bl	req_handle
.sig:	mov	x20,0
	b	.req
.idle:	mov	x0,8
	sys	SYS_SLEEP
	b	.loop

.console:
	mov	x0,0
	sys	SYS_EXIT
.busy:	adr	x0,m_busy
	bl	puts
	mov	x0,1
	sys	SYS_EXIT
.nomem:	adr	x0,m_nomem
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = argument (0 : aucun) -> policy = table du mode ; x0 = 0 si « console »
mode_choose:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cbz	x19,.ini
	ldrb	w0,[x19]
	cbnz	w0,.name
.ini:	bl	read_shell			; shell= de SD0:/etc/system.ini
	adr	x19,mode_name
.name:	mov	x0,x19
	adr	x1,str_console
	bl	str_eq
	cbnz	x0,.console
	adr	x20,modes			; modes connus ; inconnu : le premier
.m:	ldr	x0,[x20],8
	cbz	x0,.first
	ldr	x1,[x0,POL_NAME]
	str	x0,[sp,-16]!
	mov	x0,x19
	bl	str_eq
	ldr	x1,[sp],16
	cbz	x0,.m
	mov	x0,x1
	b	.set
.first:	adr	x0,modes
	ldr	x0,[x0]
.set:	adr	x1,policy
	str	x0,[x1]
	mov	x0,1
	b	.out
.console:
	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; lit SD0:/etc/system.ini et recopie la valeur de « shell= » dans mode_name
; (vide si absent)
read_shell:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,mode_name
	strb	wzr,[x0]
	adr	x0,path_ini
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,ini_buf
	mov	x2,INI_MAX-1
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	b.le	.out
	adr	x0,ini_buf
	strb	wzr,[x0,x20]
	mov	x1,x0				; x1 = debut de ligne
.line:	ldrb	w2,[x1]
	cbz	w2,.out
	adr	x3,str_shell			; la ligne commence par « shell= » ?
	mov	x4,0
.cmp:	ldrb	w5,[x3,x4]
	cbz	w5,.found
	ldrb	w6,[x1,x4]
	cmp	w5,w6
	b.ne	.skip
	add	x4,x4,1
	b	.cmp
.skip:	ldrb	w2,[x1],1			; ligne suivante
	cbz	w2,.out
	cmp	w2,10
	b.ne	.skip
	b	.line
.found:	add	x1,x1,x4
	adr	x3,mode_name
	mov	x4,0
.v:	ldrb	w5,[x1,x4]
	cmp	w5,' '
	b.ls	.vend				; fin, espace, tabulation, CR, LF
	cmp	w5,'#'
	b.eq	.vend
	cmp	x4,30
	b.hs	.vend
	strb	w5,[x3,x4]
	add	x4,x4,1
	b	.v
.vend:	strb	wzr,[x3,x4]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = taille -> x0 = bloc (tas du programme, aligne 16), 0 si echec
tas_alloc:
	add	x0,x0,15
	and	x0,x0,not 15
	sys	SYS_SBRK
	tbnz	x0,63,.fail
	ret
.fail:	mov	x0,0
	ret

include 'core.inc'
include 'present.inc'
include 'menu.inc'
include 'uikit_ops.inc'
include 'desktop.inc'
include 'snapshot.inc'
include 'remote.inc'
include 'surface.inc'
include 'screen.inc'

		align 8
modes		dd mode_bureau, 0		; modes connus (le premier par defaut)
path_ini	db 'SD0:/etc/system.ini',0
str_shell	db 'shell=',0
str_console	db 'console',0
m_ready		db 'elegant : ecran pris, en service',10,0
m_sans_hid	db 'elegant : pas de HIDManager (service hid), aucune entree',10,0
s_hid		db 'hid',0
m_busy		db 'elegant : un serveur graphique tourne deja',10,0
m_nomem		db 'elegant : memoire insuffisante',10,0
		align 4

segment readable writeable
kit_data
INI_MAX		= 1024
		align 8
policy		dd 0				; table du mode actif
scr_w		dd 0				; scr_w, scr_h contigus
scr_h		dd 0
fb_va		dd 0				; fb_va, fb_pitch contigus
fb_pitch	dd 0
bb		dd 0				; bb, bb_pitch contigus
bb_pitch	dd 0
dirty_n		dd 0				; zones sales (core.inc : dirty)
dirty_overflow	dd 0				; (diagnostic) listes pleines, repli sur l'union
cmp_n		dd 0				; zones de la recomposition en cours
pres_mode	dd 0				; presentation (present.inc) : 1 DMA 2D, 0 processeur
pres_busy	dd 0				; 1 : un transfert lance, pas encore attendu
pres_t0		dd 0				; (ms) son lancement
pres_phys	dd 0				; (dma_alloc) adresse physique rendue
bb_bus		dd 0				; adresses de bus : tampon arriere,
fb_bus		dd 0				; framebuffer
dma_base	dd 0				; registres du DMA (dev_map), du canal
dma_regs	dd 0
dma_chan	dd 0
dma_cb		dd 0				; blocs de controle (page non cachee) : adresse,
dma_cb_bus	dd 0				; adresse de bus
dma_ybias	dd 0				; 1 : YLENGTH = lignes - 1 (BCM2711), 0 : QEMU
dma_ti		dd 0				; TI des blocs (largeur 128 ou 32 bits)
dma_n		dd 0				; (diagnostic) transferts lances
dma_irq		dd 0				; ligne du canal (0 : pas d'interruption)
dma_irqs	dd 0				; (diagnostic) fins vues par interruption
pres_line	rb 192				; ligne du journal (present_init)
pres_fw		rb 32				; message au firmware
clip_r		rd 4				; x0, y0, x1, y1
cover_only	dd 0				; (compose_clip : la surface et les boutons)
cur_mods	dd 0				; modificateurs de la derniere touche
cur_x		dd 0				; cur_x, cur_y contigus
cur_y		dd 0
cur_buttons	dd 0
drag_win	dd 0
drag_dx		rd 2				; dx, dy
capture_win	dd 0
lock_win	dd 0				; fenetre qui tient le pointeur (WS_OP_POINTER_LOCK)
cur_req_win	dd 0				; forme demandee (WS_OP_CURSOR) : fenetre,
cur_req		dd 0				; forme
cur_shape	dd 0				; forme dessinee (WS_OP_CURSOR_GET)
drag_on		dd 0				; glisser-deposer : en cours, genre,
drag_type	dd 0
drag_len	dd 0				; octets (drag_data)
resize_win	dd 0				; redimensionnement : fenetre (uikit_ops.inc),
resize_st	rd 5				; x, y de depart, largeur, hauteur, bords
win_count	dd 0
win_focus	dd 0
desk_cur	dd 0				; espaces de travail (uikit_ops.inc) : le
desk_count	dd 1				; courant, combien, compteur de changements
desk_gen	dd 0
curtain_on	dd 0				; une fenetre WF_CURTAIN est ouverte
win_z		rd MAX_WINDOWS
next_win_id	dd 0
win_cascade	dd 0
area_top	dd 0				; bords reserves (barre de menus, dock),
area_bot	dd 0				; contigus
wins_dirty	dd 0				; liste des fenetres changee
need_all	dd 0				; fenetres deplacees : tout recomposer
hid_anneau	dd 0				; entrees : anneau de HIDManager
hist_n		dd 0				; (diagnostic) historique
diag_bad	dd 0				; (diagnostic) mises a jour refusees
mesure_opaque	dd 0				; 1 : composer sans transparence (mesure)
menu_gen	dd 0				; menus : derniere generation donnee
menu_last_gen	dd 0				; generation vue par MENU_GET
menu_serial	dd 0				; numero de serie rendu par MENU_GET
frame_next	dd 0				; (frames_post) premier WEV_FRAME permis, ms
snapshot_data
screen_data
info		rb 40
evbuf		rb 32
msg		rb WS_MSG
mode_name	rb 32
dirent		rb DIRENT_BYTES
ini_buf		rb INI_MAX
		align 16
windows		rb MAX_WINDOWS * WIN_SIZE
hist_buf	rb 64 * 40
dirty_list	rb DIRTY_MAX * 32		; x0, y0, x1, y1
cmp_list	rb DIRTY_MAX * 32
		align 16
clip_len	dd 0				; presse-papiers (uikit_ops.inc) : longueur,
clip_gen	dd 0				; generation (contigus)
clip_buf	dd 0				; (pris par sbrk au premier envoi)
drag_data	rb WS_DRAG_MAX			; donnees glissees
		align 8
win_restore	rb MAX_WINDOWS * 40		; agrandie : x, y, l, h d'avant, 1
		align 16
menu_specs	rb MAX_WINDOWS * WS_MENU_MAX	; texte des menus, par place de fenetre
		align 16
surfaces	rb MAX_WINDOWS * SF_SIZE		; surfaces attachees (surface.inc), par place
