; builder - la fenetre de pibuild (docs/GCC.md)
;
; Le dossier des sources (choisi par ui_folder_choose), le nom du programme, son type (devine du dossier :
; .../bin/<nom>, .../sys/, .../svc/ ; sinon app), le bouton Construire :
; SD0:/bin/pibuild "<type> <dossier> <nom>" lance par SYS_SPAWN_PIPE. Sa sortie et celle de ses fils arrivent dans
; un tube, lu sans attendre depuis la boucle : le journal se remplit pendant la construction.
; Argument : un dossier, ou un fichier de ce dossier (l'editeur, Ctrl+B).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

WIN_W		= 680
WIN_H		= 480
M		= 12
LBL_W		= 72
BTN_W		= 116
ROW_H		= 28
FLD_X		= M+LBL_W
FLD_W		= WIN_W-FLD_X-M-BTN_W-8-FRAME_CAP_W	; (a gauche des boutons de la fenetre)
TYP_W		= 76
NAM_W		= FLD_W-TYP_W-8
BTN_X		= WIN_W-M-BTN_W-FRAME_CAP_W
Y1		= M
Y2		= Y1+ROW_H+8
LOG_Y		= Y2+ROW_H+10
LOG_MAX		= 65536				; journal garde (la fin)

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'

start:
	mov	x20,x0
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	mov	x0,WIN_W
	mov	x1,WIN_H
	adr	x2,s_title
	bl	ui_window
	tbnz	x0,63,quit
	bl	build
	cbz	x20,.noarg
	ldrb	w0,[x20]
	cbz	w0,.noarg
	mov	x0,x20
	bl	set_arg
.noarg:	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
.loop:	adr	x1,run_pid			; en cours : lire le tube souvent
	ldr	x1,[x1]
	mov	x0,200
	cbz	x1,.wait
	mov	x0,15
.wait:	bl	ui_event
	mov	x19,x0
	adr	x1,run_pid
	ldr	x1,[x1]
	cbz	x1,.ev
	bl	poll_child
.ev:	cbnz	x19,.e
	bl	ui_w_idle
	b	.loop
.e:	cmp	x19,WEV_CLOSE
	b.eq	quit
	mov	x0,0
	bl	ui_w_event
	cbz	x1,.loop
	cmp	x1,UN_ACTIVATE
	b.ne	.loop
	adr	x1,w_choose
	ldr	x1,[x1]
	cmp	x0,x1
	b.eq	.ch
	adr	x1,w_build
	ldr	x1,[x1]
	cmp	x0,x1
	b.ne	.loop
	bl	do_build
	b	.loop
.ch:	bl	do_choose
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; une etiquette : x0 = y, x1 = texte
put_label:
	prologue
	mov	x4,x1
	mov	x1,x0
	add	x1,x1,3
	mov	x0,M
	mov	x2,LBL_W-6
	mov	x3,22
	mov	x5,0
	bl	ui_label
	epilogue

build:
	prologue
	mov	x0,Y1
	adr	x1,s_lsrc
	bl	put_label
	mov	x0,Y2
	adr	x1,s_lname
	bl	put_label
	mov	x0,FLD_X
	mov	x1,Y1
	mov	x2,FLD_W
	mov	x3,ROW_H
	mov	x4,UTF_READONLY
	bl	ui_textbox
	adr	x1,w_dir
	str	x0,[x1]
	mov	x0,FLD_X
	mov	x1,Y2
	mov	x2,NAM_W
	mov	x3,ROW_H
	mov	x4,0
	bl	ui_textbox
	adr	x1,w_name
	str	x0,[x1]
	mov	x0,FLD_X+NAM_W+8
	mov	x1,Y2
	mov	x2,TYP_W
	mov	x3,ROW_H
	adr	x4,s_types
	bl	ui_dropdown
	adr	x1,w_type
	str	x0,[x1]
	mov	x1,0
	bl	ui_w_set_value
	mov	x0,BTN_X
	mov	x1,Y1
	mov	x2,BTN_W
	mov	x3,ROW_H
	adr	x4,s_choose
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_choose
	str	x0,[x1]
	mov	x0,BTN_X
	mov	x1,Y2
	mov	x2,BTN_W
	mov	x3,ROW_H
	adr	x4,s_build
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,w_build
	str	x0,[x1]
	mov	x0,M
	mov	x1,LOG_Y
	mov	x2,WIN_W-2*M
	mov	x3,WIN_H-LOG_Y-M
	mov	x4,UTF_READONLY or UTF_MONO or UTA_VSCROLL
	bl	ui_textarea
	adr	x1,w_log
	str	x0,[x1]
	epilogue

; le dossier choisi ; le nom propose : son dernier element
do_choose:
	prologue
	adr	x0,s_start
	adr	x1,dir
	mov	x2,240
	bl	ui_folder_choose
	cbz	x0,.out
	bl	dir_show
.out:	epilogue

; x0 = argument : un dossier, ou un fichier (un point dans son dernier element) : son dossier
set_arg:
	prologue
	adr	x1,dir
	mov	x2,240
.c:	ldrb	w3,[x0],1
	strb	w3,[x1],1
	cbz	w3,.z
	subs	x2,x2,1
	b.ne	.c
	strb	wzr,[x1]
.z:	adr	x0,dir				; le dernier / ; un point apres lui ?
	mov	x1,0
	mov	x2,0
.f:	ldrb	w3,[x0]
	cbz	w3,.t
	cmp	w3,'/'
	b.ne	.p
	mov	x1,x0
	mov	x2,0
.p:	cmp	w3,'.'
	b.ne	.n
	mov	x2,1
.n:	add	x0,x0,1
	b	.f
.t:	cbz	x2,.show
	cbz	x1,.show
	strb	wzr,[x1]
.show:	bl	dir_show
	epilogue

; dir montre, sans son / final (le dialogue rend « .../ ») ; le nom : son dernier element
dir_show:
	prologue
	adr	x0,dir
	mov	x1,x0
.e:	ldrb	w2,[x1],1
	cbnz	w2,.e
	sub	x1,x1,2
	cmp	x1,x0
	b.lo	.t
	ldrb	w2,[x1]
	cmp	w2,'/'
	b.ne	.t
	strb	wzr,[x1]
.t:	adr	x0,w_dir
	ldr	x0,[x0]
	adr	x1,dir
	bl	ui_w_set_text
	adr	x0,dir
	mov	x1,x0
.f:	ldrb	w2,[x0],1
	cbz	w2,.fin
	cmp	w2,'/'
	b.eq	.s
	cmp	w2,':'
	b.ne	.f
.s:	mov	x1,x0
	b	.f
.fin:	adr	x0,w_name
	ldr	x0,[x0]
	bl	ui_w_set_text
	adr	x0,dir				; le type : d'apres le dossier
	bl	type_guess
	mov	x1,x0
	adr	x0,w_type
	ldr	x0,[x0]
	bl	ui_w_set_value
.out:	epilogue

; x0 = dossier -> x0 = element de s_types : le dernier /app, /bin, /sys ou /svc de sa fin (feuille)
type_guess:
	mov	x1,0
.l:	ldrb	w2,[x0],1
	cbz	w2,.fin
	cmp	w2,'/'
	b.ne	.l
	ldrb	w2,[x0]
	ldrb	w3,[x0,1]
	ldrb	w4,[x0,2]
	ldrb	w5,[x0,3]
	cmp	w5,'/'
	b.ne	.l
	orr	w2,w2,w3,lsl 8
	orr	w2,w2,w4,lsl 16
	movl	w3,'b'+'i'*256+'n'*65536
	cmp	w2,w3
	mov	x3,1
	csel	x1,x3,x1,eq
	movl	w3,'s'+'y'*256+'s'*65536
	cmp	w2,w3
	mov	x3,2
	csel	x1,x3,x1,eq
	movl	w3,'s'+'v'*256+'c'*65536
	cmp	w2,w3
	mov	x3,3
	csel	x1,x3,x1,eq
	b	.l
.fin:	mov	x0,x1
	ret

; ajoute x1 octets de x0 au journal, et le montre
log_add:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x2,log_len
	ldr	x3,[x2]
	add	x4,x3,x20
	movl	x5,LOG_MAX-1
	cmp	x4,x5
	b.lo	.c
	adr	x0,logbuf			; plein : la seconde moitie reste
	mov	x1,LOG_MAX/2
	add	x1,x0,x1
	mov	x2,LOG_MAX/2
.m:	ldrb	w3,[x1],1
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.m
	adr	x2,log_len
	mov	x3,LOG_MAX/2
.c:	adr	x0,logbuf
	add	x0,x0,x3
	mov	x1,x20
.k:	cbz	x1,.z
	ldrb	w4,[x19],1
	strb	w4,[x0],1
	sub	x1,x1,1
	b	.k
.z:	strb	wzr,[x0]
	add	x3,x3,x20
	str	x3,[x2]
	adr	x0,w_log
	ldr	x0,[x0]
	adr	x1,logbuf
	bl	ui_w_set_text
	adr	x0,w_log
	ldr	x0,[x0]
	adr	x1,log_len
	ldr	x1,[x1]
	bl	ui_textarea_goto
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine -> au journal
log_str:
	prologue
	mov	x1,x0
.l:	ldrb	w2,[x1]
	cbz	w2,.f
	add	x1,x1,1
	b	.l
.f:	sub	x1,x1,x0
	bl	log_add
	epilogue

do_build:
	prologue
	adr	x0,run_pid
	ldr	x0,[x0]
	cbnz	x0,.out				; (une a la fois)
	adr	x0,dir
	ldrb	w0,[x0]
	cbz	w0,.nosrc
	adr	x0,w_name
	ldr	x0,[x0]
	bl	ui_w_get_text
	ldrb	w1,[x0]
	cbz	w1,.nosrc
	mov	x2,x0
	adr	x0,w_type			; ? type dossier nom ?
	ldr	x0,[x0]
	str	x2,[sp,-16]!
	bl	ui_w_get_value
	ldr	x2,[sp],16
	adr	x1,s_typearg
	add	x1,x1,x0,lsl 2
	adr	x0,args
	ldr	w3,[x1]				; (trois lettres et une espace)
	str	w3,[x0],4
	adr	x1,dir
.a:	ldrb	w3,[x1],1
	cbz	w3,.b
	strb	w3,[x0],1
	b	.a
.b:	mov	w3,' '
	strb	w3,[x0],1
	mov	x3,240
.d:	ldrb	w4,[x2],1
	strb	w4,[x0],1
	cbz	w4,.go
	subs	x3,x3,1
	b.ne	.d
	strb	wzr,[x0]
.go:	adr	x0,log_len
	str	xzr,[x0]
	adr	x0,s_run
	bl	log_str
	adr	x0,args
	bl	log_str
	adr	x0,s_nl
	bl	log_str
	adr	x0,p_pibuild
	adr	x1,args
	adr	x2,run_fd
	sys	SYS_SPAWN_PIPE
	tbnz	x0,63,.fail
	adr	x1,run_pid
	str	x0,[x1]
	b	.out
.fail:	adr	x0,s_fail
	bl	ui_tr
	bl	log_str
	b	.out
.nosrc:	adr	x0,s_nosrc
	bl	ui_tr
	bl	log_str
.out:	epilogue

; ce qui est arrive dans le tube, sans attendre ; a la fin : le code de sortie
poll_child:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,16
.rd:	cbz	x19,.out
	sub	x19,x19,1
	adr	x0,run_fd
	ldr	x0,[x0]
	adr	x1,iobuf
	mov	x2,512
	sys	SYS_READ
	cmn	x0,11				; E_AGAIN : rien pour l'instant
	b.eq	.out
	cmp	x0,0
	b.le	.eof
	mov	x1,x0
	adr	x0,iobuf
	bl	log_add
	b	.rd
.eof:	adr	x0,run_fd
	ldr	x0,[x0]
	sys	SYS_CLOSE
	adr	x0,run_pid
	ldr	x0,[x0]
	sys	SYS_WAIT
	mov	x19,x0
	adr	x0,run_pid
	str	xzr,[x0]
	adr	x0,s_ok
	cbz	x19,.fin
	adr	x0,s_bad
.fin:	bl	ui_tr
	bl	log_str
.out:	ldp	x19,x20,[sp],16
	epilogue

segment readable writeable
kit_data
ui_data
		align 8
w_dir		dd 0
w_name		dd 0
w_type		dd 0
w_choose	dd 0
w_build		dd 0
w_log		dd 0
run_pid		dd 0
run_fd		dd 0
log_len		dd 0
s_title		db 'PiBuild',0
s_lsrc		db 'Sources',0
s_lname		db 'Nom',0
s_choose	db 'Choisir...',0
s_types		db 'app|bin|sys|svc|lib',0
s_typearg	db 'app bin sys svc lib '
s_build		db 'Construire',0
s_start		db 'SD0:/src/apps',0
p_pibuild	db 'SD0:/bin/pibuild',0
s_run		db 'pibuild ',0
s_nl		db 10,0
s_fail		db 'Impossible de lancer pibuild.',10,0
s_nosrc		db 'Choisissez un dossier et donnez un nom.',10,0
s_ok		db 10,'Construction reussie.',10,0
s_bad		db 10,'Echec de la construction.',10,0
dir		rb 256
args		rb 512
iobuf		rb 520
logbuf		rb LOG_MAX+8
