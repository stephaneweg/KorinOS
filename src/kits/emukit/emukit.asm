; ============================================================================
;  kits/emukit/emukit.asm - EmuKit : le socle des emulateurs (docs/EMULATEURS.md)
;  -> SD0:/lib/emukit.so
;
;  Tout ce qui n'est pas la console. L'application donne un descripteur (EMD_*,
;  emukit.inc) ; emu_run ouvre la fenetre, charge la ROM, et tourne jusqu'a la
;  fermeture.
;
;    thread de la fenetre : evenements, menus, image a l'echelle dans une surface
;      UIKit, ecriture de <rom>.sav, vitesse dans le titre ;
;    thread de la machine (priorite haute) : une image, le son vers AudioKit, la
;      cadence ; les boutons viennent de WEV_PAD (Elegant : la fenetre qui a le clavier) ;
;      remise a zero et commandes de l'application, entre deux images.
;
;  Image : trois tampons (la machine dessine dans l'un, le dernier fini attend
;  dans le deuxieme, la fenetre lit le troisieme) ; un echange atomique.
;  Cadence : l'horloge du processeur, corrigee par ce qui attend dans le flux
;  du son (une horloge qui ne depend pas de sound : pas de son, meme cadence).
; ============================================================================
include '../../inc/armpi.inc'
include '../../inc/kit.inc'
kit_header
include '../../kits/uikit/uikit.inc'
include '../../inc/pad.inc'
include 'emukit.inc'

EM_NBUF		= 3
EM_STACK	= 0x800000		; pile de la machine (n64emu y compile ses shaders par Mesa)
EM_PATH		= 512
EM_AUDIO	= 4096			; trames par lecture du son
EM_SAVEBUF	= 0x400000		; (a la demande)
EM_CHUNK	= 0x100000		; lecture de la ROM
EM_SAVE_EVERY	= 300			; images entre deux coups d'oeil a la sauvegarde
EM_XMAP		= 4096
EM_TW		= 160			; (--thumb) comme TW, TH de gamelib
EM_TH		= 120
EM_TH_FRAMES	= 900

ID_OPEN		= 1
ID_PAUSE	= 2
ID_RESET	= 3
ID_QUIT		= 4
ID_ZOOM1	= 5
ID_ZOOM2	= 6
ID_ZOOM3	= 7
ID_MAX		= 8
ID_SPEED	= 9
ID_SOUND	= 10
ID_FULL		= 11
KEY_F9		= 0x42
KEY_ESC		= 0x29
KEY_F12		= 0x45
ID_USER		= 100

macro ldv reg, var {
	adr	x16,var
	ldr	reg,[x16]
}
macro stv reg, var {
	adr	x16,var
	str	reg,[x16]
}
; un champ du descripteur
macro ldd reg, field {
	adr	x16,em_desc
	ldr	x16,[x16]
	ldr	reg,[x16,field]
}

segment readable executable
include 'emukit.abi'
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'
kit_use '../../kits/audiokit/audiokit.abi'

; ---------------------------------------------------------------------------
;  emu_run
; ---------------------------------------------------------------------------

emu_run:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	stv	x19,em_desc
	bl	kits_open
	mov	x0,x20
	bl	em_options
	mov	x20,x0
	ldv	x0,em_thumb
	cbz	x0,.ui
	mov	x1,x20				; (--thumb : sans fenetre)
	adr	x0,em_path
	bl	em_strcpy
	mrs	x0,cntfrq_el0
	stv	x0,em_hz
	bl	em_thumbnail
	b	.out
.ui:	adr	x0,ui_win
	bl	ui_init
	adr	x0,em_scr
	bl	ui_screen
	mrs	x0,cntfrq_el0
	stv	x0,em_hz
	ldr	x21,[x19,EMD_W]
	ldr	x22,[x19,EMD_H]
	stv	x21,em_iw
	stv	x22,em_ih
	ldr	x0,[x19,EMD_MAXW]
	cmp	x0,0
	csel	x0,x21,x0,eq
	stv	x0,em_maxw
	ldr	x0,[x19,EMD_MAXH]
	cmp	x0,0
	csel	x0,x22,x0,eq
	stv	x0,em_maxh
	; la fenetre : le plus grand zoom entier qui tient, 3 au plus
	adr	x0,em_scr
	ldp	x1,x2,[x0]			; largeur, hauteur
	ldp	x3,x4,[x0,16]			; reserves en bas, en haut
	sub	x1,x1,8
	sub	x2,x2,x3
	sub	x2,x2,x4
	sub	x2,x2,128			; cadre, dock
	udiv	x1,x1,x21
	udiv	x2,x2,x22
	cmp	x1,x2
	csel	x1,x2,x1,hi
	cmp	x1,3
	mov	x2,3
	csel	x1,x2,x1,hi
	cmp	x1,1
	mov	x2,1
	csel	x1,x2,x1,lo
	mul	x2,x21,x1
	mul	x3,x22,x1
	mov	x0,0
	mov	x1,x4
	ldr	x4,[x19,EMD_NAME]
	mov	x5,WF_RESIZABLE or WF_GLYPH_LIGHT or WF_BODY_DARK
	bl	ui_window_ex
	tbnz	x0,63,.fail
	mov	x0,x21
	mov	x1,x22
	bl	ui_min_size
	adr	x0,em_on_resize
	mov	x1,0
	bl	ui_on_resize
	bl	ui_jetons
	stv	x0,em_tok
	bl	em_menus
	bl	em_layout
	; la ROM
	cbz	x20,.pick
	mov	x1,x20
.sk:	ldrb	w0,[x1]				; (un argument entre guillemets)
	cmp	w0,' '
	b.eq	.sk1
	cmp	w0,'"'
	b.ne	.cp
.sk1:	add	x1,x1,1
	b	.sk
.cp:	adr	x0,em_path
	bl	em_strcpy
	sub	x0,x0,1
	adr	x1,em_path
.tr:	cmp	x0,x1
	b.ls	.load
	ldrb	w2,[x0,-1]
	cmp	w2,' '
	b.eq	.tr1
	cmp	w2,'"'
	b.ne	.load
.tr1:	sub	x0,x0,1
	strb	wzr,[x0]
	b	.tr
.pick:	ldv	x0,em_gflags
	tbnz	x0,0,.load
	bl	em_pick
	cbz	x0,.cancel
.load:	bl	em_load
	cbz	x0,.bad
	bl	em_alloc
	cbz	x0,.fail
	bl	em_open_sound
	bl	em_start
	ldv	x0,em_wantfull
	cbz	x0,.lp
	bl	em_fullscreen
.lp:	bl	em_loop
	bl	em_stop
	ldv	x0,em_stream
	tbnz	x0,63,.cl
	bl	ak_close
.cl:	bl	ui_close
	mov	x0,0
	b	.out
.bad:	ldr	x1,[x19,EMD_ERROR]
	adr	x0,em_badrom
	cmp	x1,0
	csel	x1,x0,x1,eq
	ldr	x0,[x19,EMD_NAME]
	mov	x2,UM_OK
	bl	ui_message
.fail:	mov	x0,1
	b	.out
.cancel:
	mov	x0,EME_CANCEL
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = arguments -> x0 = le chemin ; « --full chemin », « --thumb sortie chemin »
em_options:
	cbz	x0,.out
.sk:	ldrb	w1,[x0]
	cmp	w1,' '
	b.ne	.a
	add	x0,x0,1
	b	.sk
.a:	cmp	w1,'-'
	b.ne	.out
	ldrb	w1,[x0,2]
	cmp	w1,'f'
	b.eq	.full
	cmp	w1,'t'
	b.ne	.out
	add	x0,x0,8				; « --thumb »
.b:	ldrb	w1,[x0]
	cmp	w1,' '
	b.ne	.c
	add	x0,x0,1
	b	.b
.c:	adr	x2,em_thumb_out
.d:	ldrb	w1,[x0]
	cbz	w1,.e
	add	x0,x0,1
	cmp	w1,' '
	b.eq	.e
	strb	w1,[x2],1
	b	.d
.e:	strb	wzr,[x2]
	mov	x1,1
	adr	x2,em_thumb
	str	x1,[x2]
	ret
.full:	add	x0,x0,7				; « --full »
	mov	x1,1
	adr	x2,em_wantfull
	str	x1,[x2]
	b	.sk
.out:	ret

; --thumb : EM_TH_FRAMES images sans les montrer, la derniere reduite a
; EM_TW x EM_TH (pixels bruts), pour la bibliotheque de jeux
em_thumbnail:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x19,em_desc
	ldr	x21,[x19,EMD_W]
	ldr	x22,[x19,EMD_H]
	stv	x21,em_iw
	stv	x22,em_ih
	ldr	x0,[x19,EMD_MAXW]
	cmp	x0,0
	csel	x0,x21,x0,eq
	stv	x0,em_maxw
	ldr	x0,[x19,EMD_MAXH]
	cmp	x0,0
	csel	x0,x22,x0,eq
	stv	x0,em_maxh
	bl	em_load
	cbz	x0,.bad
	bl	em_alloc
	cbz	x0,.bad
	stv	xzr,em_back
	movl	x20,EM_TH_FRAMES
.f:	ldr	x0,[x19,EMD_CTX]
	adr	x1,em_buf
	ldr	x1,[x1]
	ldv	x2,em_pitch
	mov	x3,0				; pas de boutons, sauf Start de temps en temps
	tst	x20,0x100
	b.eq	.fr
	tst	x20,0x10
	b.ne	.fr
	mov	x3,PAD_START
.fr:	mov	x4,0
	ldr	x5,[x19,EMD_FRAME]
	blr	x5
.au:	ldr	x3,[x19,EMD_AUDIO]
	cbz	x3,.n
	ldr	x0,[x19,EMD_CTX]
	adr	x1,em_pcm
	mov	x2,EM_AUDIO
	blr	x3
	cmp	x0,EM_AUDIO
	b.eq	.au
.n:	subs	x20,x20,1
	b.ne	.f
	movl	x0,EM_TW*EM_TH*4
	bl	em_map
	cbz	x0,.bad
	mov	x20,x0
	mov	x3,0				; y
	adr	x1,em_buf
	ldr	x4,[x1]
	ldv	x5,em_pitch
	adr	x1,em_fw
	ldr	x6,[x1]
	adr	x1,em_fh
	ldr	x7,[x1]
	mov	x8,x20
.y:	mul	x9,x3,x7
	mov	x10,EM_TH
	udiv	x9,x9,x10
	mul	x9,x9,x5
	add	x9,x4,x9
	mov	x10,0
.x:	mul	x11,x10,x6
	mov	x12,EM_TW
	udiv	x11,x11,x12
	ldr	w11,[x9,x11,lsl 2]
	str	w11,[x8],4
	add	x10,x10,1
	cmp	x10,EM_TW
	b.lo	.x
	add	x3,x3,1
	cmp	x3,EM_TH
	b.lo	.y
	adr	x0,em_thumb_out
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	bl	f_ouvrir
	tbnz	x0,63,.bad
	mov	x19,x0
	mov	x1,x20
	movl	x2,EM_TW*EM_TH*4
	bl	f_ecrire
	mov	x0,x19
	bl	f_fermer
	mov	x0,0
	b	.out
.bad:	mov	x0,1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = source
em_strcpy:
	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,em_strcpy
	ret

; les menus, avec ceux de l'application
em_menus:
	prologue
	adr	x0,em_menu_buf
	adr	x1,em_menu_game
	ldv	x2,em_gflags
	tbz	x2,0,.c
	adr	x1,em_menu_norom
.c:	bl	str_cat
	ldd	x1,EMD_MENU
	cbz	x1,.m
	bl	str_cat
.m:	adr	x0,em_menu_buf
	bl	ui_menu
	mov	x0,ID_SOUND
	mov	x1,UMS_CHECKED
	bl	ui_menu_state
	epilogue

; -> x0 = 1 : em_path rempli
em_pick:
	prologue
	adr	x0,em_dir0
	bl	f_mkdir
	adr	x0,em_dir1
	bl	f_mkdir
	ldd	x0,EMD_EXT
	bl	ui_file_filter
	adr	x0,em_dir1
	adr	x1,em_path
	mov	x2,EM_PATH
	bl	ui_file_open
	stp	x0,xzr,[sp,-16]!
	mov	x0,0
	bl	ui_file_filter
	ldp	x0,xzr,[sp],16
	epilogue

; x0 = octets -> x0 = memoire a zero (0 : echec)
em_map:
	mov	x1,x0
	mov	x0,0
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	mov	x4,x30
	sys	SYS_VM_MAP
	mov	x30,x4
	cmp	x0,0
	csel	x0,xzr,x0,lt
	ret

; Lit em_path, la passe au coeur, puis <rom>.sav -> x0 = 1
em_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x0,em_gflags
	tbz	x0,0,.rom
	ldd	x0,EMD_CTX			; (EMG_NOROM)
	mov	x1,0
	mov	x2,0
	ldd	x3,EMD_LOAD
	blr	x3
	cbz	x0,.bad
	adr	x0,em_title
	ldd	x1,EMD_NAME
	bl	str_cat
	adr	x0,em_title
	bl	ui_title
	mov	x0,1
	b	.out
.rom:	adr	x0,em_path
	adr	x1,em_stat
	bl	f_stat
	tbnz	x0,63,.bad
	ldv	x20,em_stat			; taille
	cbz	x20,.bad
	ldv	x0,em_bypath
	cbz	x0,.mem
	adr	x19,em_path			; (trop grosse pour la memoire : l'app lit ce qu'il lui faut)
	b	.give
.mem:	add	x0,x20,4096
	bl	em_map
	cbz	x0,.bad
	mov	x19,x0				; la ROM reste en memoire
	stv	x19,em_rom
	adr	x0,em_path
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.bad
	mov	x21,x0
	mov	x22,0
.rd:	cmp	x22,x20
	b.hs	.done
	sub	x2,x20,x22
	movl	x0,EM_CHUNK
	cmp	x2,x0
	csel	x2,x0,x2,hi
	mov	x0,x21
	add	x1,x19,x22
	bl	f_lire
	cmp	x0,0
	b.le	.done
	add	x22,x22,x0
	mov	x0,x22
	mov	x1,x20
	bl	em_progress
	b	.rd
.done:	mov	x0,x21
	bl	f_fermer
	cmp	x22,x20
	b.ne	.bad
.give:	ldd	x0,EMD_CTX
	mov	x1,x19
	mov	x2,x20
	ldd	x3,EMD_LOAD
	blr	x3
	cbz	x0,.bad
	ldv	x0,em_thumb
	cbnz	x0,.nt
	bl	em_titre
.nt:	bl	em_sav_name
	bl	em_restore
	mov	x0,1
	b	.out
.bad:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = lus, x1 = en tout : le pourcentage dans le titre, pendant le chargement
; (la surface couvre la fenetre : rien a y dessiner tant qu'il n'y a pas d'image)
em_progress:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x2,em_thumb
	cbnz	x2,.out
	mov	x2,100
	mul	x0,x0,x2
	udiv	x19,x0,x1			; %
	adr	x1,em_lastpc
	ldr	x2,[x1]
	cmp	x2,x19
	b.eq	.out
	str	x19,[x1]
	adr	x0,em_line
	ldd	x1,EMD_NAME
	bl	em_strcpy2
	adr	x1,em_sep
	bl	em_strcpy2
	mov	x1,x0
	mov	x0,x19
	bl	em_dec
	adr	x1,em_s5
	bl	em_strcpy2
	adr	x0,em_line
	bl	ui_title
.out:	ldp	x19,x20,[sp],16
	epilogue

em_titre:
	prologue
	adr	x0,em_title
	ldd	x1,EMD_NAME
	bl	str_cat
	adr	x1,em_sep
	bl	str_cat
	adr	x1,em_path
	mov	x2,x1
.s:	ldrb	w3,[x2],1
	cbz	w3,.f
	cmp	w3,'/'
	b.eq	.m
	cmp	w3,':'
	b.ne	.s
.m:	mov	x1,x2
	b	.s
.f:	bl	str_cat
	mov	x2,x0
.e:	sub	x2,x2,1				; sans l'extension
	ldrb	w3,[x2]
	cmp	w3,'.'
	b.eq	.c
	cmp	w3,'/'
	b.eq	.t
	cmp	w3,' '
	b.ne	.e
	b	.t
.c:	strb	wzr,[x2]
.t:	adr	x0,em_title
	bl	ui_title
	epilogue

; em_path, son extension remplacee par .sav -> em_sav
em_sav_name:
	adr	x0,em_sav
	adr	x1,em_path
	mov	x2,0				; le dernier point
	mov	x3,x0
.s:	ldrb	w4,[x1],1
	strb	w4,[x3],1
	cbz	w4,.f
	cmp	w4,'.'
	b.ne	.a
	sub	x2,x3,1
.a:	cmp	w4,'/'
	b.ne	.s
	mov	x2,0
	b	.s
.f:	cbnz	x2,.c
	sub	x2,x3,1
.c:	movl	w0,0x7661732E			; Â« .sav Â»
	str	w0,[x2]
	strb	wzr,[x2,4]
	ret

; <rom>.sav -> EMD_RESTORE
em_restore:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	ldd	x0,EMD_RESTORE
	cbz	x0,.out
	adr	x0,em_sav
	adr	x1,em_stat
	bl	f_stat
	tbnz	x0,63,.out
	ldv	x20,em_stat
	cbz	x20,.out
	mov	x0,x20
	bl	em_map
	cbz	x0,.out
	mov	x19,x0
	adr	x0,em_sav
	mov	x1,O_READ
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x21,x0
	mov	x1,x19
	mov	x2,x20
	bl	f_lire
	mov	x20,x0
	mov	x0,x21
	bl	f_fermer
	cmp	x20,0
	b.le	.free
	ldd	x0,EMD_CTX
	mov	x1,x19
	mov	x2,x20
	ldd	x3,EMD_RESTORE
	blr	x3
.free:
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = donnees, x1 = octets : <rom>.sav
em_save_write:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,em_sav
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	bl	f_ouvrir
	tbnz	x0,63,.out
	mov	x1,x19
	mov	x19,x0
	mov	x2,x20
	bl	f_ecrire
	mov	x0,x19
	bl	f_fermer
.out:	ldp	x19,x20,[sp],16
	epilogue

; les images ; le tampon de sauvegarde -> x0 = 1
em_alloc:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	ldv	x0,em_maxw
	lsl	x1,x0,2
	stv	x1,em_pitch
	ldv	x1,em_maxh
	mul	x0,x0,x1
	lsl	x19,x0,2
	ldd	x20,EMD_BUFS
	mov	x21,0
.l:	cbz	x20,.m
	ldr	x0,[x20,x21,lsl 3]
	cbnz	x0,.s
.m:	mov	x0,x19
	bl	em_map
	cbz	x0,.out
.s:	adr	x1,em_buf
	str	x0,[x1,x21,lsl 3]
	adr	x1,em_fw
	ldv	x2,em_iw
	str	x2,[x1,x21,lsl 3]
	adr	x1,em_fh
	ldv	x2,em_ih
	str	x2,[x1,x21,lsl 3]
	add	x21,x21,1
	cmp	x21,EM_NBUF
	b.lo	.l
	movl	x0,EM_SAVEBUF
	bl	em_map
	stv	x0,em_savebuf
	mov	x0,1
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

em_open_sound:
	prologue
	mov	x0,-1
	stv	x0,em_stream
	ldd	x0,EMD_AUDIO
	cbz	x0,.out
	ldd	x0,EMD_RATE
	cbz	x0,.out
	mov	x1,2
	mov	x2,16
	mov	x3,0
	bl	ak_open
	stv	x0,em_stream
	tbnz	x0,63,.out
	mov	x1,16384			; ~90 ms d'avance au plus
	bl	ak_latency
.out:	epilogue

; ---------------------------------------------------------------------------
;  Les threads
; ---------------------------------------------------------------------------

; x0 = fonction, x1 = argument, x2 = octets de pile, x3 = nom -> tid
emu_thread:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x0,x2
	bl	em_map
	cbz	x0,.nope
	add	x1,x0,x21
	sub	x1,x1,16
	stp	x19,x20,[x1]			; fonction, argument
	adr	x0,em_tramp
	mov	x2,x1
	mrs	x3,tpidr_el0
	mov	x4,THREAD_PRIO_HIGH
	mov	x5,x22
	sys	SYS_THREAD_CREATE_EX
	b	.out
.nope:	mov	x0,-12
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

em_tramp:				; x0 = sommet : fonction, argument
	ldp	x1,x0,[x0]
	blr	x1
	mov	x0,0
	sys	SYS_THREAD_EXIT

em_start:
	prologue
	stv	xzr,em_quit
	stv	xzr,em_back
	mov	x0,1
	stv	x0,em_front
	mov	x0,2
	adr	x1,em_mid
	str	w0,[x1]
	adr	x0,em_machine
	mov	x1,0
	movl	x2,EM_STACK
	adr	x3,em_tname
	bl	emu_thread
	stv	x0,em_tid
	epilogue

; arrete la machine ; la derniere sauvegarde
em_stop:
	prologue
	mov	x0,1
	stv	x0,em_quit
	ldv	x0,em_tid
	tbnz	x0,63,.s
	sys	SYS_THREAD_JOIN
.s:	bl	em_save_flush
	ldd	x3,EMD_SAVE
	cbz	x3,.out
	ldd	x0,EMD_CTX
	sub	sp,sp,16
	mov	x1,sp
	blr	x3
	ldr	x1,[sp]
	add	sp,sp,16
	cmp	x0,0
	b.le	.out
	mov	x2,x0
	mov	x0,x1
	mov	x1,x2
	bl	em_save_write
.out:	epilogue

em_save_flush:
	prologue
	ldv	x1,em_savelen
	cbz	x1,.out
	dmb	ish
	ldv	x0,em_savebuf
	bl	em_save_write
	stv	xzr,em_savelen
.out:	epilogue

macro now reg {
	isb
	mrs	reg,cntpct_el0
}

; x0 = manette, x1 = 4 x 16 bits signes (lx, ly, rx, ry) : thread de la machine
emu_pad_axes:
	and	x0,x0,PAD_MAX-1
	adr	x2,em_axes
	ldr	x2,[x2,x0,lsl 3]
	str	x2,[x1]
	ret

; x0 = id : numero d'une commande (thread de la fenetre)
emu_command:
	stv	x0,em_cmd
	ret

emu_rom_path:
	adr	x0,em_path
	ret

; x0 = largeur, x1 = hauteur : depuis EMD_FRAME
emu_frame_size:
	ldv	x2,em_back
	adr	x3,em_fw
	str	x0,[x3,x2,lsl 3]
	adr	x3,em_fh
	str	x1,[x3,x2,lsl 3]
	ret

; avant emu_run : EMD_LOAD recevra le chemin, pas le contenu
emu_rom_by_path:
	mov	x0,1
	stv	x0,em_bypath
	ret

; depuis EMD_FRAME : rien de neuf, la derniere image publiee reste
emu_frame_same:
	mov	x0,1
	stv	x0,em_same
	ret

; x0 = touches, x1 = EMG_* : avant emu_run
emu_game:
	stv	x0,em_keyfn
	stv	x1,em_gflags
	ret

; x0 = 1 : souris relative (le thread de la fenetre l'applique)
emu_pointer:
	stv	x0,em_ptr_want
	ret

; x0 = fonction, x1 = contexte : a chaque tour de la fenetre, sur son thread, f (ctx, fenetre, x, y, l, h) -> images
; montrees. Le rectangle est celui de l'image ; pas de surface du kit : l'application y rend (armgl_window).
emu_gl_hook:
	stv	x0,em_glfn
	stv	x1,em_glctx
	ret

; le thread de la machine
em_machine:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x19,em_desc
	ldv	x0,em_hz
	movl	x1,1000000
	mul	x0,x0,x1
	ldr	x1,[x19,EMD_FPS]
	udiv	x23,x0,x1			; duree d'une image (ticks) x 1000
	mov	x1,1000
	udiv	x23,x23,x1
	mov	x20,0				; EMF_*
	mov	x21,0				; images
	now	x22				; prochaine echeance
.loop:	ldv	x0,em_quit
	cbnz	x0,.end
	; les commandes
	adr	x1,em_cmd
	ldr	x24,[x1]
	cbz	x24,.nocmd
	str	xzr,[x1]
	cmp	x24,ID_RESET
	b.ne	.cu
	ldr	x1,[x19,EMD_RESET]
	cbz	x1,.nocmd
	ldr	x0,[x19,EMD_CTX]
	blr	x1
	b	.nocmd
.cu:	ldr	x2,[x19,EMD_CMD]
	cbz	x2,.nocmd
	ldr	x0,[x19,EMD_CTX]
	mov	x1,x24
	blr	x2
.nocmd:	ldv	x0,em_pause
	cbz	x0,.run
	mov	x0,20
	sys	SYS_SLEEP
	now	x22
	b	.loop
.run:	adr	x0,em_pad
	ldp	w1,w2,[x0]
	ldp	w3,w4,[x0,8]
	ldv	x0,em_gflags
	cmp	x0,0
	csel	w1,wzr,w1,ne			; (EMG_NOROM : la manette 0 est le clavier, deja lu)
	orr	w1,w1,w2
	orr	w1,w1,w3
	orr	w24,w1,w4
	now	x25
	ldv	x1,em_back
	adr	x0,em_buf
	ldr	x1,[x0,x1,lsl 3]
	ldv	x2,em_pitch
	mov	x3,x24
	mov	x4,x20
	ldr	x0,[x19,EMD_CTX]
	ldr	x5,[x19,EMD_FRAME]
	blr	x5
	now	x0
	sub	x0,x0,x25
	adr	x1,em_spent
	ldr	x2,[x1]
	add	x2,x2,x0
	str	x2,[x1]
	adr	x1,em_frames
	ldr	x2,[x1]
	add	x2,x2,1
	str	x2,[x1]
	add	x21,x21,1
	tbnz	x20,0,.nopub			; (image sautee : la precedente reste)
	adr	x1,em_same
	ldr	x0,[x1]
	cbz	x0,.pub
	str	xzr,[x1]
	b	.nopub
.pub:	; publier : back <-> mid
	ldv	x0,em_back
	orr	w0,w0,4
	dmb	ish
	adr	x1,em_mid
.xc:	ldaxr	w2,[x1]
	stlxr	w3,w0,[x1]
	cbnz	w3,.xc
	and	x2,x2,3
	stv	x2,em_back
.nopub:	; le son
	ldr	x3,[x19,EMD_AUDIO]
	cbz	x3,.pace
.a:	ldr	x0,[x19,EMD_CTX]
	adr	x1,em_pcm
	mov	x2,EM_AUDIO
	ldr	x3,[x19,EMD_AUDIO]
	blr	x3
	cmp	x0,0
	b.le	.pace
	mov	x25,x0
	ldv	x0,em_stream
	tbnz	x0,63,.a1
	ldv	x1,em_sound
	cbnz	x1,.a1				; (em_sound : 1 = muet)
	adr	x1,em_pcm
	lsl	x2,x25,2
	bl	ak_write
.a1:	cmp	x25,EM_AUDIO
	b.eq	.a
.pace:	; la sauvegarde
	mov	x0,EM_SAVE_EVERY
	udiv	x1,x21,x0
	msub	x1,x1,x0,x21
	cbnz	x1,.tick
	ldr	x3,[x19,EMD_SAVE]
	cbz	x3,.tick
	ldv	x0,em_savelen
	cbnz	x0,.tick
	ldr	x0,[x19,EMD_CTX]
	sub	sp,sp,16
	mov	x1,sp
	blr	x3
	ldr	x1,[sp]
	add	sp,sp,16
	cmp	x0,0
	b.le	.tick
	movl	x2,EM_SAVEBUF
	cmp	x0,x2
	b.hi	.tick
	mov	x25,x0
	ldv	x0,em_savebuf
	cbz	x0,.tick
	mov	x2,x25
	bl	mem_move
	dmb	ish
	stv	x25,em_savelen
.tick:	; l'echeance suivante
	add	x22,x22,x23
	ldv	x0,em_gflags			; (un jeu vit au temps reel : le son le suit)
	cbnz	x0,.wait
	ldv	x0,em_stream
	tbnz	x0,63,.wait
	bl	ak_pending
	cmp	x0,0				; 0 juste apres l'ecriture : pas de son, l'horloge seule
	b.le	.wait
	lsr	x1,x23,2
	cmp	x0,EM_Q_HIGH
	b.ls	.w1
	add	x22,x22,x1			; trop d'avance : on ralentit
	b	.wait
.w1:	cmp	x0,EM_Q_LOW
	b.hs	.wait
	sub	x22,x22,x1			; le flux se vide : on accelere
.wait:	now	x0
	mov	x20,0
	subs	x1,x22,x0
	b.hi	.sl
	; en retard
	neg	x1,x1
	cmp	x1,x23
	b.ls	.loop
	ldr	x2,[x19,EMD_SKIP]
	cmp	x2,0
	cset	x20,ne				; la prochaine sans dessin
	lsl	x2,x23,2
	cmp	x1,x2
	b.ls	.loop
	mov	x22,x0				; trop loin : on oublie ce retard
	mov	x20,0
	b	.loop
.sl:	ldv	x2,em_hz
	mov	x3,1000
	udiv	x2,x2,x3
	udiv	x0,x1,x2			; ms
	cmp	x0,2
	b.lo	.y
	sub	x0,x0,1
	sys	SYS_SLEEP
	b	.wait
.y:	sys	SYS_YIELD
	b	.wait
.end:	ldp	x19,x20,[sp],16
	epilogue

EM_Q_LOW	= 0x2000			; octets en attente dans le flux (s16 stereo 44,1 kHz) : 46 ms
EM_Q_HIGH	= 0x3000

; ---------------------------------------------------------------------------
;  La fenetre
; ---------------------------------------------------------------------------

em_loop:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	now	x21
	mov	x22,0				; images a l'ecran
.l:	mov	x0,4
	bl	ui_event
	cmp	x0,WEV_CLOSE
	b.eq	.end
	cmp	x0,WEV_MENU
	b.eq	.menu
	cmp	x0,WEV_KEY
	b.eq	.key
	cmp	x0,WEV_FOCUS
	b.eq	.focus
	cmp	x0,WEV_MOUSE
	b.eq	.mouse
	cmp	x0,WEV_PAD
	b.eq	.pad
	cmp	x0,WEV_WHEEL
	b.eq	.wheel
	cmp	x0,WEV_RESIZE
	b.ne	.img
	mov	x0,0				; (ui_on_resize, em_layout)
	bl	ui_w_event
	b	.img
.pad:	adr	x0,ui_ev			; l'etat de cette manette
	ldr	w1,[x0,4]
	cmp	w1,PAD_MAX
	b.hs	.img
	ldr	w2,[x0,WEV_PAD_BUTTONS]
	adr	x3,em_pad
	str	w2,[x3,x1,lsl 2]
	ldr	x2,[x0,WEV_PAD_AXES]
	adr	x3,em_axes
	str	x2,[x3,x1,lsl 3]
	b	.img
.menu:	adr	x0,ui_ev
	ldr	w0,[x0,EV_CODE]
	bl	em_do
	cbnz	x0,.end
	b	.img
.wheel:	ldv	x5,em_keyfn
	cbz	x5,.img
	adr	x0,ui_ev
	ldrsw	x2,[x0,EV_Y]
	mov	x1,EMG_KEY_WHEEL
	mov	x3,0
	mov	x4,0
	ldd	x0,EMD_CTX
	blr	x5
	b	.img
.key:	ldv	x5,em_keyfn
	cbz	x5,.ckey
	adr	x0,ui_ev			; un jeu : tout lui va, sauf la vue
	ldr	w2,[x0,EV_KIND]
	ldr	w1,[x0,EV_CODE]
	ldr	w3,[x0,EV_CHAR]
	ldr	w4,[x0,EV_MODS]
	cbz	w2,.ksend
	cmp	w1,KEY_F9
	b.eq	.fs
	cmp	w1,KEY_F12
	b.ne	.ksend
	mov	x0,ID_SPEED
	bl	em_do
	b	.img
.ksend:	ldd	x0,EMD_CTX
	blr	x5
	b	.img
.ckey:	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.img
	ldr	w0,[x0,EV_CODE]
	cmp	w0,KEY_F9
	b.eq	.fs
	cmp	w0,KEY_ESC
	b.ne	.mk
	ldv	x0,em_full
	cbz	x0,.mk
.fs:	bl	em_fullscreen
	b	.img
.mk:	mov	x0,0
	bl	ui_menu_key
	tbnz	x0,63,.img
	bl	em_do
	cbnz	x0,.end
	b	.img
.focus:	adr	x0,ui_ev
	ldr	w1,[x0,EV_WIN]			; (la fenetre en cours seulement)
	ldv	x2,ui_win
	cmp	x1,x2
	b.ne	.img
	ldr	w0,[x0,EV_KIND]
	adr	x1,em_focus
	str	x0,[x1]
	stv	xzr,em_ptr_on			; (Elegant l'a repris)
	cbnz	w0,.img
	adr	x1,em_pad			; plus le clavier : tout est relache
	stp	xzr,xzr,[x1]
	ldv	x5,em_keyfn
	cbz	x5,.img
	ldd	x0,EMD_CTX
	mov	x1,EMG_KEY_NONE
	mov	x2,0
	mov	x3,0
	mov	x4,0
	blr	x5
	b	.img
.mouse:	bl	em_mouse
.img:	bl	em_pointer_apply
	bl	em_take
	add	x22,x22,x0
	bl	em_save_flush
	now	x0
	sub	x1,x0,x21
	ldv	x2,em_hz
	cmp	x1,x2
	b.lo	.l
	mov	x21,x0
	mov	x0,x22
	mov	x22,0
	bl	em_speed
	b	.l
.end:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; la souris relative voulue par le jeu (emu_pointer), si la fenetre a le clavier
em_pointer_apply:
	ldv	x0,em_ptr_want
	ldv	x1,em_focus
	cmp	x1,0
	csel	x0,xzr,x0,eq
	ldv	x1,em_ptr_on
	cmp	x0,x1
	b.eq	.r
	stv	x0,em_ptr_on
	b	ui_pointer_lock
.r:	ret

; x0 = id -> x0 = 1 : quitter
em_do:
	prologue
	cmp	x0,ID_QUIT
	b.eq	.q
	cmp	x0,ID_USER
	b.hs	.u
	cmp	x0,ID_RESET
	b.eq	.u
	cmp	x0,ID_PAUSE
	b.eq	.p
	cmp	x0,ID_OPEN
	b.eq	.o
	cmp	x0,ID_MAX
	b.eq	.mx
	cmp	x0,ID_FULL
	b.eq	.fl
	cmp	x0,ID_SPEED
	b.eq	.sp
	cmp	x0,ID_SOUND
	b.eq	.so
	sub	x1,x0,ID_ZOOM1
	cmp	x1,2
	b.hi	.r
	add	x1,x1,1
	ldv	x0,em_iw
	mul	x0,x0,x1
	ldv	x2,em_ih
	mul	x1,x2,x1
	bl	ui_resize
	b	.r
.u:	stv	x0,em_cmd
	b	.r
.p:	ldv	x0,em_pause
	eor	x0,x0,1
	stv	x0,em_pause
	mov	x1,x0
	cmp	x1,0
	mov	x2,UMS_CHECKED
	csel	x1,x2,xzr,ne
	mov	x0,ID_PAUSE
	bl	ui_menu_state
	bl	em_retitle
	b	.r
.o:	bl	em_reopen
	b	.r
.mx:	bl	ui_maximize
	b	.r
.fl:	bl	em_fullscreen
	b	.r
.sp:	ldv	x0,em_show
	eor	x0,x0,1
	stv	x0,em_show
	mov	x1,x0
	cmp	x1,0
	mov	x2,UMS_CHECKED
	csel	x1,x2,xzr,ne
	mov	x0,ID_SPEED
	bl	ui_menu_state
	bl	em_retitle
	b	.r
.so:	ldv	x0,em_sound
	eor	x0,x0,1
	stv	x0,em_sound
	cmp	x0,0
	mov	x2,UMS_CHECKED
	csel	x1,xzr,x2,ne
	mov	x0,ID_SOUND
	bl	ui_menu_state
.r:	mov	x0,0
	epilogue
.q:	mov	x0,1
	epilogue

;  Plein ecran : une fenetre sans cadre, au-dessus de tout, de la taille de
;  l'ecran ; la surface y passe, l'image s'etire en gardant ses proportions.
;  (F10 et F11 sont a Elegant : son diagnostic.) F9 ou Echap : retour a la fenetre.
em_fullscreen:
	prologue
	ldv	x0,em_full
	cbnz	x0,.back
	ldv	x0,ui_win
	stv	x0,em_win0
	ldv	x0,em_surf
	stv	x0,em_surf0
	stv	xzr,em_surf
	adr	x0,em_scr
	bl	ui_screen
	adr	x4,em_scr
	ldp	x2,x3,[x4]
	mov	x0,0
	mov	x1,0
	ldd	x4,EMD_NAME
	mov	x5,WF_FRAMELESS or WF_TOP
	bl	ui_window_ex
	tbnz	x0,63,.fail
	mov	x0,1
	stv	x0,em_full
	bl	em_layout
	b	.out
.fail:	ldv	x0,em_surf0
	stv	x0,em_surf
	b	.out
.back:	ldv	x0,em_surf
	bl	ui_surface_free
	mov	x0,0
	bl	ui_window_close
	ldv	x0,em_win0
	bl	ui_window_use
	ldv	x0,em_surf0
	stv	x0,em_surf
	stv	xzr,em_full
	bl	em_layout
.out:	stv	xzr,em_ptr_on			; (une autre fenetre : a reprendre)
	epilogue

; Game > Open : la machine s'arrete, une autre ROM, elle repart
em_reopen:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	em_pick
	cbz	x0,.out
	bl	em_stop
	adr	x0,em_title
	strb	wzr,[x0]
	bl	em_load
	cbz	x0,.bad
	bl	em_start
	b	.out
.bad:	ldd	x0,EMD_NAME
	adr	x1,em_badrom
	mov	x2,UM_OK
	bl	ui_message
	bl	em_start
.out:	ldp	x19,x20,[sp],16
	epilogue

em_retitle:
	prologue
	ldv	x0,em_pause
	cbnz	x0,.pz
	ldv	x0,em_show
	cbnz	x0,.out
	adr	x0,em_title
	bl	ui_title
	b	.out
.pz:	adr	x0,em_line
	adr	x1,em_title
	bl	em_strcpy
	adr	x0,em_line
	adr	x1,em_paused
	bl	em_cat0
	adr	x0,em_line
	bl	ui_title
.out:	epilogue

em_cat0:				; x0 = chaine, x1 = a ajouter
	mov	x2,x0
.s:	ldrb	w3,[x2],1
	cbnz	w3,.s
	sub	x0,x2,1
	b	em_strcpy2

; x0 = images montrees cette seconde : la vitesse dans le titre (F12)
em_speed:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x1,em_frames
	ldr	x20,[x1]			; images faites
	str	xzr,[x1]
	adr	x1,em_spent
	ldr	x2,[x1]
	str	xzr,[x1]
	ldv	x0,em_show
	cbz	x0,.out
	ldv	x0,em_pause
	cbnz	x0,.out
	; ms par image, en dixiemes
	ldv	x3,em_hz
	mov	x1,10000
	mul	x2,x2,x1
	udiv	x2,x2,x3
	cmp	x20,1
	csinc	x1,x20,xzr,hs
	udiv	x2,x2,x1
	stp	x2,xzr,[sp,-16]!
	adr	x0,em_line
	adr	x1,em_title
	bl	em_strcpy
	adr	x0,em_line
	adr	x1,em_s1
	bl	em_cat0
	mov	x1,x0
	mov	x0,x20
	bl	em_dec
	adr	x1,em_s2
	bl	em_strcpy2
	mov	x1,x0
	mov	x0,x19
	bl	em_dec
	adr	x1,em_s3
	bl	em_strcpy2
	mov	x4,x0
	ldp	x2,xzr,[sp],16
	mov	x1,10
	udiv	x0,x2,x1
	msub	x19,x0,x1,x2
	mov	x1,x4
	bl	em_dec
	mov	w1,'.'
	strb	w1,[x0],1
	add	w1,w19,'0'
	strb	w1,[x0],1
	adr	x1,em_s4
	bl	em_strcpy2
	adr	x0,em_line
	bl	ui_title
.out:	ldp	x19,x20,[sp],16
	epilogue

em_strcpy2:				; x0 = destination, x1 = texte -> x0 = fin
	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,em_strcpy2
	sub	x0,x0,1
	ret

em_dec:					; x0 = valeur, x1 = destination -> x0 = fin
	stp	x29,x30,[sp,-16]!
	mov	x2,x0
	mov	x0,x2
	bl	fmt_dec
	ldp	x29,x30,[sp],16
	ret

; WEV_MOUSE : dans l'image -> EMD_MOUSE
em_mouse:
	prologue
	ldd	x5,EMD_MOUSE
	cbz	x5,.out
	adr	x6,ui_ev
	ldr	w2,[x6,EV_X]
	ldr	w3,[x6,EV_Y]
	ldr	w1,[x6,EV_KIND]
	cmp	w1,WEV_M_REL
	b.eq	.rel				; (dx, dy)
	ldv	x0,em_vx
	ldv	x1,em_vy
	sub	x2,x2,x0
	sub	x3,x3,x1
	ldv	x0,em_iw
	ldv	x1,em_vw
	mul	x2,x2,x0
	sdiv	x2,x2,x1
	ldv	x0,em_ih
	ldv	x1,em_vh
	mul	x3,x3,x0
	sdiv	x3,x3,x1
.rel:	sxtw	x2,w2
	sxtw	x3,w3
	ldr	w1,[x6,EV_KIND]
	ldr	w4,[x6,EV_CODE]
	ldd	x0,EMD_CTX
	blr	x5
.out:	epilogue

; ---------------------------------------------------------------------------
;  L'image dans la fenetre
; ---------------------------------------------------------------------------

; fonction de ui_on_resize : x0 = largeur, x1 = hauteur
em_on_resize:
	b	em_layout

; la zone de l'image : la plus grande qui garde les proportions, au centre
em_layout:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x19,ui_w
	ldv	x20,ui_h
	ldv	x0,em_iw
	ldv	x1,em_ih
	mov	x21,x19				; dw
	mul	x22,x19,x1
	udiv	x22,x22,x0			; dh
	cmp	x22,x20
	b.ls	.ok
	mov	x22,x20
	mul	x21,x20,x0
	udiv	x21,x21,x1
.ok:	stv	x21,em_vw
	stv	x22,em_vh
	mov	x0,0
	mov	x1,0
	mov	x2,x19
	mov	x3,x20
	ldv	x4,em_tok
	ldr	w4,[x4,TJ_SOMBRE]
	bl	ui_fill
	bl	ui_update_all
	sub	x0,x19,x21
	lsr	x0,x0,1
	stv	x0,em_vx
	sub	x1,x20,x22
	lsr	x1,x1,1
	stv	x1,em_vy
	ldv	x4,em_glfn
	cbnz	x4,.out				; (l'application rend elle-meme)
	ldv	x4,em_surf
	cbz	x4,.new
	mov	x2,x1
	mov	x1,x0
	mov	x0,x4
	mov	x3,x21
	mov	x4,x22
	bl	ui_surface_move
	b	.out
.new:	mov	x2,x21
	mov	x3,x22
	mov	x4,WS_SF_XRGB8888
	bl	ui_surface
	stv	x0,em_surf
.out:	mov	x0,0
	stv	x0,em_xsw			; (table a refaire)
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Une image neuve ? On la prend et on la montre -> x0 = 1
em_take:
	prologue
	ldv	x6,em_glfn
	cbz	x6,.mine
	ldv	x0,em_glctx
	ldv	x1,ui_win
	ldv	x2,em_vx
	ldv	x3,em_vy
	ldv	x4,em_vw
	ldv	x5,em_vh
	blr	x6
	epilogue
.mine:	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	adr	x1,em_mid
	ldar	w0,[x1]
	tbz	w0,2,.none
	ldv	x0,em_front
.xc:	ldaxr	w2,[x1]
	stlxr	w3,w0,[x1]
	cbnz	w3,.xc
	and	x2,x2,3
	stv	x2,em_front
	adr	x0,em_fw
	ldr	x20,[x0,x2,lsl 3]		; largeur source
	adr	x0,em_fh
	ldr	x21,[x0,x2,lsl 3]
	adr	x0,em_buf
	ldr	x19,[x0,x2,lsl 3]		; source
	ldv	x0,em_iw
	ldv	x1,em_ih
	cmp	x0,x20
	b.ne	.size
	cmp	x1,x21
	b.eq	.go
.size:	stv	x20,em_iw
	stv	x21,em_ih
	bl	em_layout
.go:	ldv	x0,em_surf
	cbz	x0,.none
	bl	ui_surface_pixels
	cbz	x0,.none
	mov	x22,x0				; destination
	mov	x23,x1				; pas
	mov	x24,x2				; l
	mov	x25,x3				; h
	; table des colonnes
	ldv	x0,em_xsw
	cmp	x0,x20
	b.ne	.tab
	ldv	x0,em_xdw
	cmp	x0,x24
	b.eq	.blit
.tab:	stv	x20,em_xsw
	stv	x24,em_xdw
	adr	x0,em_xmap
	mov	x1,0
	mov	x3,0
	cmp	x24,EM_XMAP
	b.hi	.none
.tx:	udiv	x2,x3,x24
	str	w2,[x0,x1,lsl 2]
	add	x3,x3,x20
	add	x1,x1,1
	cmp	x1,x24
	b.lo	.tx
.blit:	adr	x9,em_xmap
	ldv	x10,em_pitch
	mov	x11,0				; y
	mov	x12,0				; y * sh
	mov	x13,-1				; ligne d'avant
.y:	udiv	x14,x12,x25
	cmp	x14,x13
	b.ne	.row
	sub	x0,x22,x23			; la meme : recopiee
	mov	x1,x22
	mov	x2,x24
.c8:	cmp	x2,8
	b.lo	.c1
	ldp	q0,q1,[x0],32			; 8 points
	stp	q0,q1,[x1],32
	sub	x2,x2,8
	b	.c8
.c1:	cbz	x2,.nexty
	ldr	w3,[x0],4
	str	w3,[x1],4
	sub	x2,x2,1
	b	.c1
.row:	mov	x13,x14
	mul	x0,x14,x10
	add	x0,x19,x0
	mov	x1,x22
	mov	x2,0
.x:	ldr	w3,[x9,x2,lsl 2]
	ldr	w3,[x0,x3,lsl 2]
	str	w3,[x1,x2,lsl 2]
	add	x2,x2,1
	cmp	x2,x24
	b.lo	.x
.nexty:	add	x22,x22,x23
	add	x12,x12,x21
	add	x11,x11,1
	cmp	x11,x25
	b.lo	.y
	ldv	x0,em_surf
	mov	x1,0
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	ui_surface_damage
	mov	x0,1
	b	.out
.none:	mov	x0,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

em_tname	db 'emu-machine',0
em_dir0		db 'SD0:/home',0
em_dir1		db 'SD0:/home/roms',0
em_sep		db ' - ',0
em_paused	db ' (paused)',0
em_s1		db ' - ',0
em_s2		db ' fps, shown ',0
em_s3		db ', ',0
em_s4		db ' ms',0
em_s5		db ' %',0
em_badrom	db 'This file cannot be played here.',0
em_menu_norom	db 'MGame',10
		db 'I2',9,'Pause',10
		db '-',10
		db 'I4',9,'Quit',10
		db 'MView',10
		db 'I5',9,'Zoom 1x',10
		db 'I6',9,'Zoom 2x',10
		db 'I7',9,'Zoom 3x',10
		db 'I8',9,'Maximize',10
		db 'I11',9,'Full Screen',9,'F9',10
		db '-',10
		db 'I9',9,'Show Speed',9,'F12',10
		db 'MSound',10
		db 'I10',9,'Sound',10
		db 0
em_menu_game	db 'MGame',10
		db 'I1',9,'Open...',9,'Ctrl+O',10
		db '-',10
		db 'I2',9,'Pause',9,'P',10
		db 'I3',9,'Reset',10
		db '-',10
		db 'I4',9,'Quit',9,'Ctrl+Q',10
		db 'MView',10
		db 'I5',9,'Zoom 1x',10
		db 'I6',9,'Zoom 2x',10
		db 'I7',9,'Zoom 3x',10
		db 'I8',9,'Maximize',10
		db 'I11',9,'Full Screen',9,'F9',10
		db '-',10
		db 'I9',9,'Show Speed',9,'F12',10
		db 'MSound',10
		db 'I10',9,'Sound',10
		db 0
		align 4

segment readable writeable
kit_data
		align 8
ui_data
		align 8
em_desc		dd 0
em_scr		rb 32
em_stat		rb 32
em_hz		dd 0
em_tok		dd 0
em_iw		dd 0
em_ih		dd 0
em_maxw		dd 0
em_maxh		dd 0
em_pitch	dd 0
em_rom		dd 0
em_lastpc	dd -1
em_buf		rb EM_NBUF*8
em_fw		rb EM_NBUF*8
em_fh		rb EM_NBUF*8
em_back		dd 0
em_front	dd 0
em_mid		dd 0
em_quit		dd 0
em_pause	dd 0
em_show		dd 0
em_thumb	dd 0
em_wantfull	dd 0
em_thumb_out	rb 320
		align 8
em_full		dd 0
em_win0		dd 0
em_surf0	dd 0
em_sound	dd 0			; 1 : muet
em_pad		rb PAD_MAX*4
		align 8
em_axes		rb PAD_MAX*8
em_cmd		dd 0
em_same		dd 0
em_bypath	dd 0
em_tid		dd -1
em_keyfn	dd 0			; emu_game
em_gflags	dd 0
em_focus	dd 1
em_ptr_want	dd 0
em_ptr_on	dd 0
em_stream	dd -1
em_surf		dd 0
em_glfn		dd 0
em_glctx	dd 0
em_vx		dd 0
em_vy		dd 0
em_vw		dd 1
em_vh		dd 1
em_xsw		dd 0
em_xdw		dd 0
em_frames	dd 0
em_spent	dd 0
em_savebuf	dd 0
em_savelen	dd 0
em_path		rb EM_PATH
em_sav		rb EM_PATH+8
em_title	rb 256
em_line		rb 320
em_menu_buf	rb 2048
em_pcm		rb EM_AUDIO*4
em_xmap		rb EM_XMAP*4
