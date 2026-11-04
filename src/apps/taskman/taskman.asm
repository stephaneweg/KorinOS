; ============================================================================
;  taskman - le Gestionnaire de taches : le Task Manager d'Onyx
;  (user/Apps/taskman), en assembleur, a la place de l'ancien Moniteur, dont il
;  garde ce qu'Onyx n'a pas (threads, tas, commutations par seconde, temps de
;  marche). Quatre onglets :
;    Processus   les process en grille : nom, genre, etat, CPU, memoire, threads,
;                tas, pid ; un clic sur un titre trie ; Entree ou un double-clic :
;                sa fenetre devant ; Suppr : Terminer (confirme pour un programme
;                de SD0:/sys) ; R : relire.
;    Memoire     utilisee, libre, applications, systeme ; la derniere minute ; ce
;                qui l'utilise.
;    Processeur  un panneau par coeur : charge, derniere minute, le process qui
;                l'a le plus occupe.
;    Reseau      trames par seconde, totaux, derniere minute, interfaces.
;  Une lecture par seconde. La fenetre se redimensionne, les vues suivent.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/netkit/netkit.inc'
include '../../inc/netd.inc'

WIN_W		= 680
WIN_H		= 520
TOP		= 46				; le haut : les onglets
FOOT		= 46				; le bas : le message, les boutons
BTN_W		= 130
BTN_H		= 30
HIST		= 60				; une minute d'echantillons
MAXP		= 256			; process montres (la liste du noyau n'a pas de plafond)
MAXT		= 512				; threads suivis
MAXIF		= 6
CORES		= 4
KEY_R		= 0x15

; un process : l'entree de SYS_PROCLIST (64 o), puis ce qu'on en deduit
PR_PID		= 0
PR_THREADS	= 8
PR_HEAP		= 16				; Ko
PR_NAME		= 24
PR_STATE	= 56				; 1 : en fin de vie
PR_MEM		= 64				; Ko a lui
PR_CPU		= 72				; ms de CPU sur la derniere periode
PR_KIND		= 80				; 0 application, 1 programme, 2 systeme
PR_WIN		= 88				; sa fenetre (0 : aucune)
PR_RUN		= 96				; 0 actif, 1 en veille, 2 en attente, 3 fin de vie
PR_CORE		= 104				; ms par coeur (32 bits chacun)
PR_SIZE		= 128

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'
kit_use '../../kits/netkit/netkit.abi'

macro ldv reg, var {
	adr	x16,var
	ldr	reg,[x16]
}
macro stv reg, var {
	adr	x16,var
	str	reg,[x16]
}
; x = (x0 ... : une couleur du theme dans un registre 32 bits)
macro tok reg, t {
	adr	x16,g_tok
	ldr	x16,[x16]
	ldr	reg,[x16,t]
}

start:
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	mov	x0,-1
	mov	x1,-1
	mov	x2,WIN_W
	mov	x3,WIN_H
	adr	x4,s_title
	bl	tr_x4
	mov	x5,WF_RESIZABLE
	bl	ui_window_ex
	tbnz	x0,63,quit
	bl	ui_jetons
	stv	x0,g_tok
	mov	x0,480
	mov	x1,400
	bl	ui_min_size
	adr	x0,on_resize
	bl	ui_on_resize
	sys	SYS_GETPID
	stv	x0,g_self
	bl	make_widgets
	bl	sample
	bl	show_rows
	mov	x0,WIN_W
	mov	x1,WIN_H
	bl	on_resize
	bl	show_tab
	sys	SYS_TIME_MS
	add	x0,x0,1000
	stv	x0,g_next
.loop:	sys	SYS_TIME_MS
	ldv	x1,g_next
	subs	x0,x1,x0
	b.le	.tick
	bl	ui_event
	cbz	x0,.idle
	cmp	x0,WEV_CLOSE
	b.eq	quit
	mov	x19,x0
	mov	x0,0
	bl	ui_w_event
	mov	x20,x2				; pris par un widget
	bl	on_widget
	cbnz	x20,.flush
	cmp	x19,WEV_KEY
	b.ne	.flush
	bl	on_key
.flush:	bl	ui_view_flush
	b	.loop
.idle:	bl	ui_w_idle
	b	.loop
.tick:	sys	SYS_TIME_MS
	add	x0,x0,1000
	stv	x0,g_next
	bl	refresh
	b	.loop

quit:	mov	x0,0
	sys	SYS_EXIT

; relit tout et montre l'onglet ouvert
refresh:
	prologue
	bl	sample
	bl	show_rows
	ldv	x0,g_view
	bl	ui_view_dirty
	bl	ui_view_flush
	bl	foot_text
	epilogue

; --- les widgets -------------------------------------------------------------
make_widgets:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,10
	mov	x1,8
	mov	x2,0
	mov	x3,30
	adr	x4,s_tabs
	bl	tr_x4
	bl	ui_segmented
	stv	x0,g_seg
	mov	x0,10
	mov	x1,TOP+4
	mov	x2,WIN_W-20
	mov	x3,WIN_H-TOP-FOOT-4
	mov	x4,UL_HEADER or UL_USERSORT or UL_HSCROLL or UL_STRIPES
	bl	ui_list
	stv	x0,g_list
	mov	x19,x0
	adr	x20,col_table
.col:	ldr	w2,[x20],4
	cbz	w2,.cold
	ldr	w3,[x20],4
	ldrsw	x1,[x20],4
	adr	x4,col_table
	add	x1,x4,x1
	bl	tr_x1
	mov	x0,x19
	bl	ui_list_column
	b	.col
.cold:	mov	x0,x19
	ldv	x1,g_sort
	ldv	x2,g_desc
	bl	ui_list_sort
	mov	x0,10
	mov	x1,TOP+4
	mov	x2,WIN_W-20
	mov	x3,WIN_H-TOP-FOOT-4
	mov	x4,0
	bl	ui_view
	stv	x0,g_view
	adr	x1,paint
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	ui_view_on
	mov	x0,12
	mov	x1,WIN_H-FOOT+12
	mov	x2,WIN_W-24
	mov	x3,22
	mov	x4,0
	mov	x5,ULB_DIM
	bl	ui_label
	stv	x0,g_info
	mov	x0,0
	mov	x1,0
	mov	x2,BTN_W
	mov	x3,BTN_H
	adr	x4,s_raise
	mov	x5,0
	bl	ui_pushbutton
	stv	x0,g_braise
	mov	x0,0
	mov	x1,0
	mov	x2,BTN_W
	mov	x3,BTN_H
	adr	x4,s_kill
	mov	x5,0
	bl	ui_pushbutton
	stv	x0,g_bkill
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = taille : tout a sa place
on_resize:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x0,0
	mov	x1,0
	mov	x2,x19
	mov	x3,x20
	tok	w4,TJ_FACE
	bl	ui_fill
	ldv	x0,g_list
	mov	x1,10
	mov	x2,TOP+4
	sub	x3,x19,20
	sub	x4,x20,TOP+FOOT+4
	bl	ui_w_move
	ldv	x0,g_view
	mov	x1,10
	mov	x2,TOP+4
	sub	x3,x19,20
	sub	x4,x20,TOP+FOOT+4
	bl	ui_w_move
	ldv	x0,g_view
	bl	ui_view_dirty
	ldv	x0,g_info
	mov	x1,12
	sub	x2,x20,FOOT-12
	sub	x3,x19,24
	ldv	x4,g_tab
	cbnz	x4,.fi
	sub	x3,x3,2*BTN_W+12
.fi:	mov	x4,22
	bl	ui_w_move
	ldv	x0,g_braise
	sub	x1,x19,10+2*BTN_W+6
	sub	x2,x20,FOOT-8
	mov	x3,BTN_W
	mov	x4,BTN_H
	bl	ui_w_move
	ldv	x0,g_bkill
	sub	x1,x19,10+BTN_W
	sub	x2,x20,FOOT-8
	mov	x3,BTN_W
	mov	x4,BTN_H
	bl	ui_w_move
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	ldp	x19,x20,[sp],16
	epilogue

; l'onglet g_tab : ce qui se montre
show_tab:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x19,g_tab
	cmp	x19,0
	cset	x20,eq
	ldv	x0,g_list
	mov	x1,x20
	bl	ui_w_show
	ldv	x0,g_braise
	mov	x1,x20
	bl	ui_w_show
	ldv	x0,g_bkill
	mov	x1,x20
	bl	ui_w_show
	ldv	x0,g_view
	eor	x1,x20,1
	bl	ui_w_show
	ldv	x0,g_view
	bl	ui_view_dirty
	adr	x0,ui_w
	ldp	x0,x1,[x0]
	bl	on_resize
	bl	foot_text
	bl	sel_changed
	ldp	x19,x20,[sp],16
	epilogue

; x0 = widget, x1 = code (ui_w_event)
on_widget:
	cbz	x0,.ret
	cbz	x1,.ret
	ldv	x2,g_seg
	cmp	x0,x2
	b.eq	.seg
	ldv	x2,g_braise
	cmp	x0,x2
	b.ne	.n1
	cmp	x1,UN_ACTIVATE
	b.eq	do_raise
	ret
.n1:	ldv	x2,g_bkill
	cmp	x0,x2
	b.ne	.n2
	cmp	x1,UN_ACTIVATE
	b.eq	do_kill
	ret
.n2:	ldv	x2,g_list
	cmp	x0,x2
	b.ne	.ret
	cmp	x1,UN_ACTIVATE
	b.eq	do_raise
	cmp	x1,UN_SORT
	b.eq	.sort
	cmp	x1,UN_CHANGE
	b.eq	sel_changed
.ret:	ret
.seg:	cmp	x1,UN_CHANGE
	b.ne	.ret
	prologue
	bl	ui_w_get_value
	cmp	x0,0
	b.lt	.s1
	stv	x0,g_tab
	bl	show_tab
.s1:	epilogue
.sort:	prologue
	bl	ui_list_sorted
	stv	x0,g_sort
	stv	x1,g_desc
	bl	sort_rows
	bl	show_rows
	epilogue

; un clavier non pris par un widget
on_key:
	adr	x0,ui_ev
	ldr	w1,[x0,EV_KIND]
	cbz	w1,.ret
	ldv	x1,g_tab
	cbnz	x1,.ret
	ldr	w1,[x0,EV_CODE]
	cmp	w1,KEY_DELETE
	b.eq	do_kill
	cmp	w1,KEY_ENTER
	b.eq	do_raise
	cmp	w1,KEY_R
	b.eq	refresh
.ret:	ret

; -> x0 = la fiche du process choisi (0 : aucun)
sel_rec:
	ldv	x1,g_sel
	cbz	x1,.none
	ldv	x2,g_nproc
	adr	x3,g_recs
.l:	cbz	x2,.none
	ldr	x4,[x3,PR_PID]
	cmp	x4,x1
	b.eq	.ok
	add	x3,x3,PR_SIZE
	sub	x2,x2,1
	b	.l
.ok:	mov	x0,x3
	ret
.none:	mov	x0,0
	ret

; la ligne choisie a change : g_sel, les boutons
sel_changed:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x0,g_list
	bl	ui_w_get_value
	mov	x1,0
	cmp	x0,0
	b.lt	.set
	ldv	x2,g_nproc
	cmp	x0,x2
	b.hs	.set
	adr	x2,g_order
	ldrh	w0,[x2,x0,lsl 1]
	adr	x1,g_recs
	mov	x2,PR_SIZE
	madd	x1,x0,x2,x1
	ldr	x1,[x1,PR_PID]
.set:	stv	x1,g_sel
	bl	buttons
	ldp	x19,x20,[sp],16
	epilogue

; les boutons selon le process choisi
buttons:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	sel_rec
	mov	x19,x0
	mov	x20,0				; une fenetre ?
	cbz	x19,.r
	ldr	x0,[x19,PR_WIN]
	cmp	x0,0
	cset	x20,ne
.r:	ldv	x0,g_braise
	mov	x1,x20
	bl	ui_w_enable
	mov	x1,0				; soi-meme : non
	cbz	x19,.k
	ldr	x0,[x19,PR_PID]
	ldv	x2,g_self
	cmp	x0,x2
	cset	x1,ne
.k:	ldv	x0,g_bkill
	bl	ui_w_enable
	ldp	x19,x20,[sp],16
	epilogue

do_raise:
	prologue
	bl	sel_rec
	cbz	x0,.out
	ldr	x0,[x0,PR_WIN]
	cbz	x0,.out
	bl	ui_raise
.out:	epilogue

do_kill:
	prologue
	str	x19,[sp,-16]!
	bl	sel_rec
	cbz	x0,.out
	mov	x19,x0
	ldr	x0,[x19,PR_PID]
	ldv	x1,g_self
	cmp	x0,x1
	b.eq	.out
	ldr	x0,[x19,PR_KIND]
	cmp	x0,2
	b.ne	.go
	adr	x0,s_kill
	adr	x1,s_ksys
	bl	tr_x1
	mov	x2,UM_YESNO
	bl	ui_message
	cmp	x0,1
	b.ne	.out
.go:	ldr	x0,[x19,PR_PID]
	sys	SYS_KILL
	mov	x0,100
	sys	SYS_SLEEP
	bl	refresh
.out:	ldr	x19,[sp],16
	epilogue

; la grille : ses lignes, la selection gardee par pid
show_rows:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x19,g_list
	mov	x0,x19
	ldv	x1,g_nproc
	adr	x2,row_text
	bl	ui_list_virtual
	mov	x20,-1				; la ligne du pid choisi
	ldv	x1,g_sel
	cbz	x1,.set
	ldv	x2,g_nproc
	adr	x3,g_order
	adr	x4,g_recs
	mov	x5,0
.l:	cmp	x5,x2
	b.hs	.gone
	ldrh	w6,[x3,x5,lsl 1]
	mov	x7,PR_SIZE
	madd	x6,x6,x7,x4
	ldr	x6,[x6,PR_PID]
	cmp	x6,x1
	b.eq	.f
	add	x5,x5,1
	b	.l
.f:	mov	x20,x5
	b	.set
.gone:	stv	xzr,g_sel
.set:	mov	x0,x19
	bl	ui_w_get_value
	cmp	x0,x20
	b.eq	.same
	mov	x0,x19
	mov	x1,x20
	bl	ui_w_set_value
.same:	bl	buttons
	mov	x0,x19
	bl	ui_w_draw
	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste, x1 = ligne -> x0 = texte de la ligne (colonnes : tabulations)
row_text:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x2,g_order
	ldrh	w1,[x2,x1,lsl 1]
	adr	x19,g_recs
	mov	x2,PR_SIZE
	madd	x19,x1,x2,x19
	adr	x20,g_row
	mov	x0,x20
	add	x1,x19,PR_NAME
	bl	str_cat
	bl	tab
	ldr	x1,[x19,PR_KIND]
	adr	x2,tb_kind
	ldr	x1,[x2,x1,lsl 3]
	adr	x2,tb_base
	add	x1,x2,x1
	bl	tr_x1
	bl	str_cat
	bl	tab
	ldr	x1,[x19,PR_RUN]
	adr	x2,tb_run
	ldr	x1,[x2,x1,lsl 3]
	adr	x2,tb_base
	add	x1,x2,x1
	bl	tr_x1
	bl	str_cat
	bl	tab
	mov	x1,x0
	ldr	x0,[x19,PR_CPU]
	bl	cpu_text
	bl	tab
	mov	x1,x0
	ldr	x0,[x19,PR_MEM]
	bl	fmt_kb
	bl	tab
	mov	x1,x0
	ldr	x0,[x19,PR_THREADS]
	bl	fmt_dec
	bl	tab
	mov	x1,x0
	ldr	x0,[x19,PR_HEAP]
	bl	fmt_kb
	bl	tab
	mov	x1,x0
	ldr	x0,[x19,PR_PID]
	bl	fmt_dec
	mov	x0,x20
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fin d'un texte : une tabulation (x0 suit)
tab:
	mov	w1,9
	strb	w1,[x0],1
	strb	wzr,[x0]
	ret

; x0 = ms de CPU sur la periode, x1 = tampon -> x0 = fin : « 12,3 % » de la machine
cpu_text:
	mov	x2,x0
	ldv	x3,g_dt
	cbz	x3,.zero
	mov	x4,1000/CORES
	mul	x2,x2,x4
	add	x2,x2,x3,lsr 1
	udiv	x2,x2,x3
	mov	x0,x2
	b	per1000
.zero:	mov	x0,0

; x0 = pour mille, x1 = tampon -> x0 = fin : « 12,3 % »
per1000:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x0,x19
	mov	x2,10
	udiv	x0,x0,x2
	bl	fmt_dec
	mov	x2,10
	udiv	x3,x19,x2
	msub	x3,x3,x2,x19
	mov	w2,','
	strb	w2,[x0],1
	add	w3,w3,'0'
	strb	w3,[x0],1
	mov	w2,' '
	strb	w2,[x0],1
	mov	w2,'%'
	strb	w2,[x0],1
	strb	wzr,[x0]
	ldp	x19,x20,[sp],16
	epilogue

; x0 = Ko, x1 = tampon -> x0 = fin : « 812 Ko », « 34,2 Mo », « 118 Mo », « 2,31 Go »
fmt_kb:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	movl	x2,1024
	cmp	x19,x2
	b.lo	.ko
	movl	x2,102400
	cmp	x19,x2
	b.lo	.mo1
	movl	x2,1048576
	cmp	x19,x2
	b.lo	.mo
	lsr	x0,x19,20			; Go, deux chiffres
	bl	fmt_dec
	mov	x1,x0
	ubfx	x2,x19,0,20
	mov	x3,100
	mul	x2,x2,x3
	lsr	x2,x2,20
	mov	w3,','
	strb	w3,[x1],1
	mov	x3,10
	udiv	x4,x2,x3
	msub	x2,x4,x3,x2
	add	w4,w4,'0'
	add	w2,w2,'0'
	strb	w4,[x1],1
	strb	w2,[x1],1
	mov	x0,x1
	adr	x1,s_go
	bl	tr_x1
	b	.fin
.mo:	add	x0,x19,512
	lsr	x0,x0,10
	bl	fmt_dec
	adr	x1,s_mo
	bl	tr_x1
	b	.fin
.mo1:	lsr	x0,x19,10
	bl	fmt_dec
	mov	x1,x0
	ubfx	x2,x19,0,10
	mov	x3,10
	mul	x2,x2,x3
	lsr	x2,x2,10
	mov	w3,','
	strb	w3,[x1],1
	add	w2,w2,'0'
	strb	w2,[x1],1
	mov	x0,x1
	adr	x1,s_mo
	bl	tr_x1
	b	.fin
.ko:	mov	x0,x19
	bl	fmt_dec
	adr	x1,s_ko
	bl	tr_x1
.fin:	bl	str_cat
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur, x1 = tampon -> x0 = fin : « 12 345 » (groupes de trois)
fmt_group:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	sub	sp,sp,32
	mov	x1,sp
	bl	fmt_dec
	mov	x1,sp
	sub	x2,x0,x1			; chiffres
.c:	ldrb	w3,[x1],1
	strb	w3,[x19],1
	sub	x2,x2,1
	cbz	x2,.e
	mov	x3,3
	udiv	x4,x2,x3
	msub	x4,x4,x3,x2
	cbnz	x4,.c
	mov	w3,' '
	strb	w3,[x19],1
	b	.c
.e:	strb	wzr,[x19]
	mov	x0,x19
	add	sp,sp,32
	ldp	x19,x20,[sp],16
	epilogue

; x0 = octets, x1 = tampon -> x0 = fin
fmt_bytes:
	cmp	x0,1024
	b.lo	.o
	lsr	x0,x0,10
	b	fmt_kb
.o:	prologue
	bl	fmt_dec
	adr	x1,s_octets
	bl	str_cat
	epilogue

; x0 = octets par seconde, x1 = tampon -> x0 = fin : « 850 o/s », « 12,4 Ko/s »
fmt_bps:
	prologue
	bl	fmt_bytes
	adr	x1,s_ps
	bl	str_cat
	epilogue

; --- les mesures -------------------------------------------------------------
; x0 = historique (32 bits), x1 = nombre (adresse), w2 = valeur
push_hist:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x2
	ldr	x3,[x1]
	cmp	x3,HIST
	b.lo	.put
	mov	x3,HIST-1
	stp	x1,x3,[sp,-16]!
	mov	x0,x19
	add	x1,x19,4
	mov	x2,(HIST-1)*4
	bl	mem_move
	ldp	x1,x3,[sp],16
.put:	str	w20,[x19,x3,lsl 2]
	add	x3,x3,1
	str	x3,[x1]
	ldp	x19,x20,[sp],16
	epilogue

; lit l'etat du systeme
sample:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	adr	x0,g_info_sys
	sys	SYS_SYSINFO
	sys	SYS_TIME_MS			; la periode
	ldv	x1,g_last
	stv	x0,g_last
	sub	x1,x0,x1
	movl	x2,1000000
	cmp	x1,x2
	b.lo	.dt
	mov	x1,0
.dt:	stv	x1,g_dt
	adr	x19,g_info_sys
	ldr	x1,[x19,16]			; memoire utilisee, pour mille
	ldr	x2,[x19,8]
	subs	x3,x1,x2
	csel	x3,xzr,x3,lo
	mov	x4,1000
	mul	x3,x3,x4
	cbz	x1,.nom
	udiv	x3,x3,x1
.nom:	adr	x0,h_mem
	adr	x1,n_mem
	mov	x2,x3
	bl	push_hist
	mov	x20,0				; les coeurs
.core:	cmp	x20,CORES
	b.hs	.corend
	add	x0,x19,40
	ldr	x2,[x0,x20,lsl 3]
	cmp	x2,100
	mov	x3,100
	csel	x2,x3,x2,hi
	adr	x0,h_core
	mov	x3,HIST*4
	madd	x0,x20,x3,x0
	adr	x1,n_core
	add	x1,x1,x20,lsl 3
	bl	push_hist
	add	x20,x20,1
	b	.core
.corend:
	ldr	x1,[x19,72]			; commutations par seconde
	ldv	x2,g_lastsw
	stv	x1,g_lastsw
	sub	x1,x1,x2
	cbnz	x2,.sw
	mov	x1,0
.sw:	stv	x1,g_swrate
	mov	x21,0				; les process, par morceaux
	mov	x22,0				; (emplacement de depart)
.raw:	adr	x0,g_raw
	add	x0,x0,x21,lsl 6
	mov	x1,MAXP
	sub	x1,x1,x21
	cbz	x1,.rawd
	str	x1,[sp,-16]!
	mov	x2,x22
	sys	SYS_PROCLIST_FROM
	ldr	x1,[sp],16
	cmp	x0,0
	b.le	.rawd
	add	x21,x21,x0
	adr	x2,g_raw			; la suite : apres le dernier
	sub	x3,x21,1
	lsl	x3,x3,6
	ldr	x22,[x2,x3]
	and	x22,x22,0xFFFF
	cmp	x0,x1
	b.eq	.raw
.rawd:	stv	x21,g_nproc
	adr	x19,g_recs
	adr	x20,g_raw
	mov	x22,x21
.cp:	cbz	x22,.cpd
	mov	x0,x19				; fiche : l'entree du noyau, le reste a zero
	mov	x1,x20
	mov	x2,64
	bl	mem_move
	add	x0,x19,64
	mov	x1,8
.z:	str	xzr,[x0],8
	add	x1,x1,8
	cmp	x1,PR_SIZE-64
	b.ls	.z
	add	x19,x19,PR_SIZE
	add	x20,x20,64
	sub	x22,x22,1
	b	.cp
.cpd:	bl	read_windows
	; chaque process : ses threads, sa memoire, ses droits
	adr	x0,g_tprev			; les temps d'avant, de cote
	adr	x1,g_tcur
	ldv	x2,g_tcount
	lsl	x2,x2,4
	bl	mem_move
	ldv	x0,g_tcount
	stv	x0,g_tprevn
	stv	xzr,g_tcount
	stv	xzr,g_apps
	adr	x19,g_recs
	mov	x20,x21
.pr:	cbz	x20,.prd
	ldr	x0,[x19,PR_PID]
	adr	x1,g_vm
	sys	SYS_VM_STATS
	tbnz	x0,63,.nomem
	adr	x0,g_vm
	ldr	x0,[x0,VS_OWN]
	lsl	x0,x0,2
	str	x0,[x19,PR_MEM]
	ldv	x1,g_apps
	add	x1,x1,x0
	stv	x1,g_apps
.nomem:	ldr	x0,[x19,PR_PID]
	sys	SYS_PROC_RIGHTS
	mov	x1,2
	cmp	x0,1
	b.eq	.knd
	ldr	x1,[x19,PR_WIN]
	cmp	x1,0
	cset	x1,eq
.knd:	str	x1,[x19,PR_KIND]
	mov	x0,x19
	bl	read_threads
	add	x19,x19,PR_SIZE
	sub	x20,x20,1
	b	.pr
.prd:	bl	core_owners
	bl	read_net
	bl	sort_rows
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; PR_WIN de chaque process (la derniere de ses fenetres dans la liste)
read_windows:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,0				; premiere fenetre demandee
.page:	mov	x0,x19
	adr	x1,g_wins
	bl	ui_win_details
	cmp	x0,0
	b.le	.out
	mov	x20,x0
	sub	x21,x0,x19			; dans ce paquet
	cmp	x21,WE_PER_MSG
	mov	x1,WE_PER_MSG
	csel	x21,x1,x21,hi
	adr	x22,g_wins
.w:	cbz	x21,.np
	ldr	w0,[x22,WE_PID]
	ldv	x2,g_nproc
	adr	x3,g_recs
.f:	cbz	x2,.nx
	ldr	x4,[x3,PR_PID]
	cmp	x4,x0
	b.eq	.got
	add	x3,x3,PR_SIZE
	sub	x2,x2,1
	b	.f
.got:	ldr	w0,[x22,WE_ID]
	str	x0,[x3,PR_WIN]
.nx:	add	x22,x22,WE_SIZE
	sub	x21,x21,1
	b	.w
.np:	add	x19,x19,WE_PER_MSG
	cmp	x19,x20
	b.lo	.page
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fiche : ses threads -> PR_CPU, PR_CORE, PR_RUN
read_threads:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,PR_PID]
	adr	x1,g_tbuf
	mov	x2,64
	sys	SYS_THREAD_LIST
	cmp	x0,0
	csel	x20,x0,xzr,gt
	adr	x21,g_tbuf
	mov	x22,3				; le meilleur etat vu
	ldr	x0,[x19,PR_STATE]
	cbnz	x0,.t
	mov	x22,2
.t:	cbz	x20,.fin
	ldr	x0,[x21,THL_STATE]
	cmp	x0,2				; 0 pret, 1 endormi, 2 bloque
	b.hi	.st
	ldr	x1,[x19,PR_STATE]
	cbnz	x1,.st
	cmp	x0,x22
	csel	x22,x0,x22,lo
.st:	ldr	x23,[x21,THL_RUNTIME]
	ldr	x24,[x21,THL_TID]
	adr	x1,g_tcur			; ce temps-ci, retenu
	ldv	x2,g_tcount
	cmp	x2,MAXT
	b.hs	.delta
	add	x3,x1,x2,lsl 4
	stp	x24,x23,[x3]
	add	x2,x2,1
	stv	x2,g_tcount
.delta:	adr	x1,g_tprev			; celui d'il y a une periode
	ldv	x2,g_tprevn
.fd:	cbz	x2,.new
	ldp	x3,x4,[x1],16
	sub	x2,x2,1
	cmp	x3,x24
	b.ne	.fd
	subs	x4,x23,x4
	csel	x4,xzr,x4,lo
	b	.add
.new:	mov	x4,0				; (nouveau : compte a sa premiere periode)
	ldv	x5,g_dt
	cbz	x5,.add
	mov	x4,x23
	cmp	x4,x5
	csel	x4,x5,x4,hi
.add:	ldr	x0,[x19,PR_CPU]
	add	x0,x0,x4
	str	x0,[x19,PR_CPU]
	ldr	x0,[x21,THL_CPU]
	cmp	x0,CORES
	b.hs	.nt
	add	x1,x19,PR_CORE
	ldr	w2,[x1,x0,lsl 2]
	add	w2,w2,w4
	str	w2,[x1,x0,lsl 2]
.nt:	add	x21,x21,64
	sub	x20,x20,1
	b	.t
.fin:	str	x22,[x19,PR_RUN]
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; g_owner : pour chaque coeur, la fiche qui l'a le plus occupe (0 : aucune)
core_owners:
	mov	x0,0
.c:	adr	x1,g_owner
	str	xzr,[x1,x0,lsl 3]
	mov	w2,0				; le meilleur temps
	ldv	x3,g_nproc
	adr	x4,g_recs
.p:	cbz	x3,.n
	add	x5,x4,PR_CORE
	ldr	w5,[x5,x0,lsl 2]
	cmp	w5,w2
	b.ls	.s
	mov	w2,w5
	str	x4,[x1,x0,lsl 3]
.s:	add	x4,x4,PR_SIZE
	sub	x3,x3,1
	b	.p
.n:	add	x0,x0,1
	cmp	x0,CORES
	b.lo	.c
	ret

; le reseau : compteurs de netd, interfaces
read_net:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,g_nst
	bl	net_stats
	cmp	x0,0
	cset	x1,eq
	stv	x1,g_netok
	b.ne	.out
	adr	x19,g_nst
	ldv	x20,g_dt
	ldr	x0,[x19,NST_RXB]
	ldv	x1,g_lastrx
	stv	x0,g_lastrx
	sub	x0,x0,x1
	cbz	x1,.r0
	mov	x2,1000
	mul	x0,x0,x2
	cbz	x20,.r0
	udiv	x0,x0,x20
	b	.r1
.r0:	mov	x0,0
.r1:	stv	x0,g_rxrate
	mov	x2,x0
	adr	x0,h_rx
	adr	x1,n_rx
	bl	push_hist
	ldr	x0,[x19,NST_TXB]
	ldv	x1,g_lasttx
	stv	x0,g_lasttx
	sub	x0,x0,x1
	cbz	x1,.t0
	mov	x2,1000
	mul	x0,x0,x2
	cbz	x20,.t0
	udiv	x0,x0,x20
	b	.t1
.t0:	mov	x0,0
.t1:	stv	x0,g_txrate
	mov	x2,x0
	adr	x0,h_tx
	adr	x1,n_tx
	bl	push_hist
	adr	x0,g_ifs
	mov	x1,MAXIF
	bl	net_ifaces
	cmp	x0,0
	csel	x0,x0,xzr,gt
	stv	x0,g_nif
.out:	ldp	x19,x20,[sp],16
	epilogue

; g_order selon g_sort, g_desc (insertion : la liste est courte)
sort_rows:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x19,g_order
	ldv	x20,g_nproc
	mov	x0,0
.i:	cmp	x0,x20
	b.hs	.is
	strh	w0,[x19,x0,lsl 1]
	add	x0,x0,1
	b	.i
.is:	mov	x21,1
.o:	cmp	x21,x20
	b.hs	.out
	mov	x22,x21
.m:	cbz	x22,.nx
	sub	x0,x22,1
	ldrh	w0,[x19,x0,lsl 1]
	ldrh	w1,[x19,x22,lsl 1]
	bl	cmp_rows
	cmp	x0,0
	b.le	.nx
	sub	x0,x22,1
	ldrh	w1,[x19,x0,lsl 1]
	ldrh	w2,[x19,x22,lsl 1]
	strh	w2,[x19,x0,lsl 1]
	strh	w1,[x19,x22,lsl 1]
	sub	x22,x22,1
	b	.m
.nx:	add	x21,x21,1
	b	.o
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = fiches (rangs) -> x0 > 0 : la seconde doit passer devant
cmp_rows:
	adr	x2,g_recs
	mov	x3,PR_SIZE
	madd	x0,x0,x3,x2
	madd	x1,x1,x3,x2
	ldv	x4,g_sort
	cmp	x4,0
	b.eq	.name
	adr	x5,sort_field
	ldrb	w5,[x5,x4]
	ldr	x6,[x0,x5]
	ldr	x7,[x1,x5]
	cmp	x6,x7
	b.eq	.name
	cset	x8,hi				; 1 : a > b
	ldv	x4,g_desc
	cbnz	x4,.d
	mov	x0,-1
	cmp	x8,1
	csneg	x0,x0,x0,ne
	ret
.d:	mov	x0,1
	cmp	x8,1
	csneg	x0,x0,x0,ne
	ret
.name:	add	x0,x0,PR_NAME			; par nom, sans la casse
	add	x1,x1,PR_NAME
	mov	x2,0
.c:	ldrb	w3,[x0,x2]
	ldrb	w4,[x1,x2]
	sub	w5,w3,'A'
	cmp	w5,26
	b.hs	.c1
	add	w3,w3,32
.c1:	sub	w5,w4,'A'
	cmp	w5,26
	b.hs	.c2
	add	w4,w4,32
.c2:	cmp	w3,w4
	b.ne	.df
	cbz	w3,.eq
	add	x2,x2,1
	b	.c
.df:	sub	w0,w3,w4
	sxtw	x0,w0
	ldv	x4,g_sort
	cbnz	x4,.ret
	ldv	x4,g_desc
	cbz	x4,.ret
	neg	x0,x0
.ret:	ret
.eq:	mov	x0,0
	ret

; le message du bas selon l'onglet
foot_text:
	prologue
	str	x19,[sp,-16]!
	adr	x19,g_foot
	ldv	x0,g_tab
	cmp	x0,1
	b.eq	.mem
	cmp	x0,2
	b.eq	.cpu
	cmp	x0,3
	b.eq	.net
	mov	x1,x19				; « 28 process »
	ldv	x0,g_nproc
	bl	fmt_dec
	adr	x1,s_nproc
	bl	tr_x1
	bl	str_cat
	b	.set
.mem:	mov	x0,x19				; « RAM geree 960 Mo - pages de 4 Ko »
	adr	x1,s_ram
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	adr	x0,g_info_sys
	ldr	x0,[x0,16]
	bl	fmt_kb
	adr	x1,s_pages
	bl	tr_x1
	bl	str_cat
	b	.set
.cpu:	mov	x0,x19				; temps de marche, commutations...
	adr	x1,s_up
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	adr	x0,g_info_sys
	ldr	x0,[x0,24]
	bl	fmt_uptime
	adr	x1,s_sw
	bl	str_cat
	mov	x1,x0
	ldv	x0,g_swrate
	bl	fmt_group
	adr	x1,s_sw2
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	ldv	x0,g_nproc
	bl	fmt_dec
	adr	x1,s_np
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	adr	x0,g_info_sys
	ldr	x0,[x0]
	bl	fmt_dec
	adr	x1,s_nc
	bl	tr_x1
	bl	str_cat
	b	.set
.net:	mov	x0,x19
	adr	x1,s_netdown
	bl	tr_x1
	ldv	x2,g_nif
	cbz	x2,.nb
	adr	x1,s_netfr			; « 1234 trames, 3 retransmissions »
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	adr	x2,g_nst
	ldr	x0,[x2,NST_STACK]
	bl	fmt_group
	adr	x1,s_netrt
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	adr	x2,g_nst
	ldr	x0,[x2,NST_RTX]
	ldr	x3,[x2,NST_FRTX]
	add	x0,x0,x3
	bl	fmt_group
	b	.set
.nb:	bl	str_cat
.set:	ldv	x0,g_info
	mov	x1,x19
	bl	ui_w_set_text
	ldr	x19,[sp],16
	epilogue

; x0 = ms, x1 = tampon -> x0 = fin : « 00:03:12 » (ou « 2 j 00:03:12 »)
fmt_uptime:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x2,1000
	udiv	x19,x0,x2			; secondes
	mov	x0,x1
	movl	x2,86400
	udiv	x3,x19,x2
	msub	x19,x3,x2,x19
	cbz	x3,.h
	mov	x1,x0
	mov	x0,x3
	bl	fmt_dec
	adr	x1,s_day
	bl	str_cat
.h:	mov	x2,3600
	udiv	x1,x19,x2
	msub	x19,x1,x2,x19
	bl	two
	mov	w1,':'
	strb	w1,[x0],1
	mov	x2,60
	udiv	x1,x19,x2
	msub	x19,x1,x2,x19
	bl	two
	mov	w1,':'
	strb	w1,[x0],1
	mov	x1,x19
	bl	two
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = 0..99 -> x0 suit
two:
	mov	x6,10
	udiv	x7,x1,x6
	msub	x8,x7,x6,x1
	add	w7,w7,'0'
	add	w8,w8,'0'
	strb	w7,[x0],1
	strb	w8,[x0],1
	strb	wzr,[x0]
	ret

; --- les vues dessinees --------------------------------------------------------
; x0 = vue, x1 = largeur, x2 = hauteur
paint:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x20,x2
	mov	x0,0
	mov	x1,0
	mov	x2,x19
	mov	x3,x20
	tok	w4,TJ_FACE
	bl	ui_fill
	mov	x0,x19
	mov	x1,x20
	ldv	x2,g_tab
	cmp	x2,1
	b.eq	.m
	cmp	x2,2
	b.eq	.c
	cmp	x2,3
	b.ne	.out
	bl	paint_net
	b	.out
.m:	bl	paint_mem
	b	.out
.c:	bl	paint_cpu
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0, x1, x2 = x, y, largeur ; x3 = libelle, x4 = valeur, x5 = dessous (0), w6 = marque (0)
tile:
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
	mov	x24,x5
	mov	w25,w6
	tok	w5,TJ_LIGNE
	mov	w6,w5
	mov	x3,62
	mov	x4,6
	bl	ui_rbox
	tok	w5,TJ_SURFACE
	mov	w6,w5
	add	x0,x19,1
	add	x1,x20,1
	sub	x2,x21,2
	mov	x3,60
	mov	x4,5
	bl	ui_rbox
	mov	x0,x22
	add	x1,x19,10
	add	x2,x20,17
	tok	w3,TJ_TEXTE2
	mov	x4,12
	adr	x5,ui_font
	sub	x6,x21,16
	bl	ui_text_fit
	mov	x0,x23
	add	x1,x19,10
	add	x2,x20,37
	tok	w3,TJ_TEXTE1
	mov	x4,15
	adr	x5,ui_font_b
	sub	x6,x21,16
	bl	ui_text_fit
	cbz	x24,.m
	mov	x0,x24
	add	x1,x19,10
	add	x2,x20,53
	tok	w3,TJ_TEXTE3
	mov	x4,12
	adr	x5,ui_font
	sub	x6,x21,16
	bl	ui_text_fit
.m:	cbz	w25,.out
	add	x0,x19,x21
	sub	x0,x0,20
	add	x1,x20,9
	mov	x2,10
	mov	x3,10
	mov	x4,2
	mov	w5,w25
	mov	w6,w25
	bl	ui_rbox
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0..x3 = boite : un fond de courbe, quatre traits
plot_box:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	tok	w5,TJ_LIGNE
	mov	w6,w5
	mov	x4,5
	bl	ui_rbox
	tok	w5,TJ_SURFACE
	mov	w6,w5
	add	x0,x19,1
	add	x1,x20,1
	sub	x2,x21,2
	sub	x3,x22,2
	mov	x4,4
	bl	ui_rbox
	mov	x23,1
.l:	mul	x1,x22,x23
	lsr	x1,x1,2
	add	x1,x1,x20
	add	x0,x19,4
	sub	x2,x21,8
	mov	x3,1
	tok	w4,TJ_LIGNE
	bl	ui_fill
	add	x23,x23,1
	cmp	x23,4
	b.lo	.l
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0..x3 = boite, x4 = historique, x5 = nombre, x6 = haut, w7 = couleur, bit 32 de x7 : aire
plot:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,32
	cmp	x5,2
	b.lo	.out
	cmp	x6,0
	b.le	.out
	mov	x19,x0
	mov	x20,x1
	sub	x21,x2,12
	sub	x22,x3,12
	mov	x23,x4
	mov	x24,x5
	mov	x25,x6
	mov	x26,x7
	tok	w0,TJ_SURFACE
	mov	w1,w26
	mov	x2,46
	bl	ui_mix
	str	x0,[sp,16]
	mov	x27,0
.l:	ldr	w0,[x23,x27,lsl 2]
	cmp	x0,x25
	csel	x0,x25,x0,hi
	mul	x0,x0,x22
	udiv	x0,x0,x25
	add	x28,x20,6
	add	x28,x28,x22
	sub	x28,x28,x0			; yy
	mov	x0,HIST
	sub	x0,x0,x24
	add	x0,x0,x27
	mul	x0,x0,x21
	mov	x1,HIST-1
	udiv	x0,x0,x1
	add	x0,x0,x19
	add	x0,x0,6				; xx
	str	x0,[sp,24]
	tbz	x26,32,.line
	sub	x0,x0,1
	mov	x1,x28
	mov	x2,HIST-1
	udiv	x2,x21,x2
	add	x2,x2,1
	add	x3,x20,6
	add	x3,x3,x22
	sub	x3,x3,x28
	ldr	x4,[sp,16]
	bl	ui_fill
.line:	cbz	x27,.nx
	ldp	x0,x1,[sp]
	lsl	x0,x0,4
	lsl	x1,x1,4
	ldr	x2,[sp,24]
	lsl	x2,x2,4
	lsl	x3,x28,4
	mov	x4,2*16
	mov	w5,w26
	bl	ui_vp_line
.nx:	ldr	x0,[sp,24]
	stp	x0,x28,[sp]
	add	x27,x27,1
	cmp	x27,x24
	b.lo	.l
.out:	add	sp,sp,32
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = bord droit, x2 = ligne de base, w3 = couleur, x4 = px, x5 = police
text_r:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	w22,w3
	mov	x23,x4
	mov	x24,x5
	mov	x1,x4
	mov	x2,x5
	bl	ui_text_width
	sub	x1,x20,x0
	mov	x0,x19
	mov	x2,x21
	mov	w3,w22
	mov	x4,x23
	mov	x5,x24
	mov	x6,0
	bl	ui_text
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = x, x2 = ligne de base : un titre de section
sect:
	tok	w3,TJ_TEXTE2
	mov	x4,12
	adr	x5,ui_font_b
	mov	x6,0
	b	ui_text

; x0, x1 = largeur, hauteur de la vue
paint_mem:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,96
	mov	x19,x0
	mov	x20,x1
	adr	x0,g_info_sys
	ldr	x21,[x0,16]			; total
	ldr	x22,[x0,8]			; libre
	subs	x23,x21,x22			; utilise
	csel	x23,xzr,x23,lo
	ldv	x24,g_apps
	subs	x25,x23,x24			; systeme
	csel	x25,xzr,x25,lo
	sub	x26,x19,30			; largeur d'une tuile
	lsr	x26,x26,2
	; « 37 % de 960 Mo »
	mov	x1,sp
	mov	x0,100
	mul	x0,x23,x0
	cbz	x21,.z
	udiv	x0,x0,x21
.z:	bl	fmt_dec
	adr	x1,s_pctof
	bl	tr_x1
	bl	str_cat
	mov	x1,x0
	mov	x0,x21
	bl	fmt_kb
	mov	x0,x23
	add	x1,sp,48
	bl	fmt_kb
	mov	x0,0
	mov	x1,0
	mov	x2,x26
	adr	x3,s_used
	bl	tr_x3
	add	x4,sp,48
	mov	x5,sp
	mov	x6,0
	bl	tile
	mov	x0,x22
	add	x1,sp,48
	bl	fmt_kb
	add	x0,x26,10
	mov	x1,0
	mov	x2,x26
	adr	x3,s_free
	bl	tr_x3
	add	x4,sp,48
	mov	x5,0
	mov	x6,0
	bl	tile
	mov	x0,x24
	add	x1,sp,48
	bl	fmt_kb
	add	x0,x26,10
	lsl	x0,x0,1
	mov	x1,0
	mov	x2,x26
	adr	x3,s_apps
	bl	tr_x3
	add	x4,sp,48
	mov	x5,0
	mov	x6,0
	bl	tile
	mov	x0,x25
	add	x1,sp,48
	bl	fmt_kb
	add	x0,x26,10
	mov	x1,3
	mul	x0,x0,x1
	mov	x1,0
	sub	x2,x19,x0
	adr	x3,s_sys
	bl	tr_x3
	add	x4,sp,48
	adr	x5,s_sys2
	bl	tr_x5
	mov	x6,0
	bl	tile
	; la derniere minute
	adr	x0,s_mem60
	bl	tr_x0
	mov	x1,2
	mov	x2,86
	bl	sect
	sub	x27,x20,96+100			; hauteur du graphe
	cmp	x27,50
	mov	x0,50
	csel	x27,x0,x27,lt
	mov	x0,0
	mov	x1,96
	mov	x2,x19
	mov	x3,x27
	bl	plot_box
	mov	x0,0
	mov	x1,96
	mov	x2,x19
	mov	x3,x27
	adr	x4,h_mem
	ldv	x5,n_mem
	mov	x6,1000
	tok	w7,TJ_ACCENT
	mov	x8,1
	orr	x7,x7,x8,lsl 32
	bl	plot
	add	x27,x27,96+26			; ce qui l'utilise
	adr	x0,s_what
	bl	tr_x0
	mov	x1,2
	mov	x2,x27
	sub	x2,x2,8
	bl	sect
	; les quatre plus gros, le systeme, le reste
	adr	x0,g_seg_kb
	adr	x1,g_seg_nm
	str	x25,[x0]
	adr	x2,s_sys
	bl	tr_x2
	str	x2,[x1]
	bl	top_apps
	mov	x26,x0
	mov	x28,x1
	subs	x0,x24,x28			; les autres applications
	b.ls	.seg
	adr	x1,g_seg_kb
	str	x0,[x1,x26,lsl 3]
	adr	x1,g_seg_nm
	adr	x2,s_others
	bl	tr_x2
	str	x2,[x1,x26,lsl 3]
	add	x26,x26,1
.seg:	tok	w5,TJ_CREUX
	mov	w6,w5
	mov	x0,0
	mov	x1,x27
	mov	x2,x19
	mov	x3,18
	mov	x4,4
	bl	ui_rbox
	mov	x25,1				; x de la bande
	mov	x28,0
.bar:	cmp	x28,x26
	b.hs	.bard
	cbz	x21,.bard
	adr	x0,g_seg_kb
	ldr	x3,[x0,x28,lsl 3]
	sub	x2,x19,2
	mul	x2,x3,x2
	udiv	x2,x2,x21
	cbnz	x2,.w
	cbz	x3,.w
	mov	x2,1
.w:	sub	x4,x19,1
	sub	x4,x4,x25
	cmp	x2,x4
	csel	x2,x4,x2,gt
	cmp	x2,0
	b.le	.nb
	mov	x0,x25
	add	x1,x27,1
	mov	x3,16
	adr	x4,seg_cols
	ldr	w4,[x4,x28,lsl 2]
	str	x2,[sp,-16]!
	bl	ui_fill
	ldr	x2,[sp],16
	add	x25,x25,x2
.nb:	add	x28,x28,1
	b	.bar
.bard:	add	x27,x27,26			; la legende
	mov	x25,2
	mov	x28,0
.leg:	cmp	x28,x26
	b.hs	.out
	adr	x0,g_seg_nm
	ldr	x1,[x0,x28,lsl 3]
	mov	x0,sp
	bl	str_cat
	adr	x1,s_2sp
	bl	str_cat
	mov	x1,x0
	adr	x0,g_seg_kb
	ldr	x0,[x0,x28,lsl 3]
	bl	fmt_kb
	mov	x0,sp
	mov	x1,12
	adr	x2,ui_font
	bl	ui_text_width
	add	x24,x0,30			; place de l'entree
	add	x0,x25,x24
	cmp	x0,x19
	b.ls	.put
	cmp	x25,2
	b.ls	.put
	mov	x25,2
	add	x27,x27,20
.put:	mov	x0,x25
	add	x1,x27,4
	mov	x2,10
	mov	x3,10
	mov	x4,2
	adr	x5,seg_cols
	ldr	w5,[x5,x28,lsl 2]
	mov	w6,w5
	bl	ui_rbox
	mov	x0,sp
	add	x1,x25,16
	add	x2,x27,14
	tok	w3,TJ_TEXTE1
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	add	x25,x25,x24
	add	x28,x28,1
	b	.leg
.out:	add	sp,sp,96
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; g_seg_* a partir du segment 1 : les quatre plus gros process qui ne sont pas du
; systeme -> x0 = segments, x1 = Ko montres
top_apps:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,1				; segment
	mov	x20,0				; Ko
	adr	x21,g_taken
	strb	wzr,[x21]
	mov	x0,0
.c:	strb	wzr,[x21,x0]
	add	x0,x0,1
	cmp	x0,MAXP
	b.lo	.c
.k:	cmp	x19,5
	b.hs	.out
	mov	x22,-1				; le plus gros qui reste
	mov	x23,0
	ldv	x2,g_nproc
	adr	x3,g_recs
	mov	x4,0
.p:	cmp	x4,x2
	b.hs	.pd
	ldrb	w5,[x21,x4]
	cbnz	w5,.pn
	ldr	x5,[x3,PR_KIND]
	cmp	x5,2
	b.eq	.pn
	ldr	x5,[x3,PR_MEM]
	cmp	x5,x23
	b.ls	.pn
	mov	x23,x5
	mov	x22,x4
.pn:	add	x3,x3,PR_SIZE
	add	x4,x4,1
	b	.p
.pd:	tbnz	x22,63,.out
	mov	w0,1
	strb	w0,[x21,x22]
	adr	x0,g_seg_kb
	str	x23,[x0,x19,lsl 3]
	add	x20,x20,x23
	adr	x0,g_recs
	mov	x1,PR_SIZE
	madd	x0,x22,x1,x0
	add	x0,x0,PR_NAME
	adr	x1,g_seg_nm
	str	x0,[x1,x19,lsl 3]
	add	x19,x19,1
	b	.k
.out:	mov	x0,x19
	mov	x1,x20
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = largeur, hauteur de la vue
paint_cpu:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	stp	x27,x28,[sp,-16]!
	sub	sp,sp,64
	sub	x19,x0,10			; largeur d'un panneau
	lsr	x19,x19,1
	sub	x20,x1,10			; hauteur
	lsr	x20,x20,1
	mov	x21,0				; coeur
.c:	cmp	x21,CORES
	b.hs	.out
	and	x0,x21,1
	add	x1,x19,10
	mul	x22,x0,x1			; x
	lsr	x0,x21,1
	add	x1,x20,10
	mul	x23,x0,x1			; y
	mov	x0,sp				; « Coeur 0 »
	adr	x1,s_core
	bl	tr_x1
	bl	str_cat
	add	w1,w21,'0'
	strb	w1,[x0],1
	strb	wzr,[x0]
	mov	x0,sp
	add	x1,x22,2
	add	x2,x23,15
	tok	w3,TJ_TEXTE1
	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x0,sp				; ce qui l'occupe
	mov	x1,14
	adr	x2,ui_font_b
	bl	ui_text_width
	add	x24,x0,x22
	add	x24,x24,10
	adr	x0,g_owner
	ldr	x0,[x0,x21,lsl 3]
	adr	x1,s_idle
	bl	tr_x1
	cbz	x0,.w
	add	x1,x0,PR_NAME
.w:	mov	x0,x1
	mov	x1,x24
	add	x2,x23,15
	tok	w3,TJ_TEXTE2
	mov	x4,12
	adr	x5,ui_font
	add	x6,x22,x19
	sub	x6,x6,x24
	sub	x6,x6,60
	bl	ui_text_fit
	adr	x0,h_core			; « 37 % »
	mov	x1,HIST*4
	madd	x25,x21,x1,x0
	adr	x1,n_core
	ldr	x26,[x1,x21,lsl 3]
	mov	x0,0
	cbz	x26,.pc
	sub	x0,x26,1
	ldr	w0,[x25,x0,lsl 2]
.pc:	mov	x1,sp
	bl	fmt_dec
	adr	x1,s_pct
	bl	str_cat
	mov	x0,sp
	add	x1,x22,x19
	sub	x1,x1,2
	add	x2,x23,15
	tok	w3,TJ_TEXTE1
	mov	x4,14
	adr	x5,ui_font_b
	bl	text_r
	mov	x0,x22
	add	x1,x23,24
	mov	x2,x19
	sub	x3,x20,24
	bl	plot_box
	mov	x0,x22
	add	x1,x23,24
	mov	x2,x19
	sub	x3,x20,24
	mov	x4,x25
	mov	x5,x26
	mov	x6,100
	adr	x7,core_cols
	ldr	w7,[x7,x21,lsl 2]
	mov	x8,1
	orr	x7,x7,x8,lsl 32
	bl	plot
	add	x21,x21,1
	b	.c
.out:	add	sp,sp,64
	ldp	x27,x28,[sp],16
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = largeur, hauteur de la vue
paint_net:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	sub	sp,sp,128
	mov	x19,x0
	mov	x20,x1
	ldv	x0,g_netok
	cbnz	x0,.ok
	adr	x0,s_nonet
	bl	tr_x0
	mov	x1,2
	mov	x2,24
	tok	w3,TJ_TEXTE2
	mov	x4,14
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	b	.out
.ok:	sub	x21,x19,30			; largeur d'une tuile
	lsr	x21,x21,2
	adr	x22,g_nst
	ldv	x0,g_rxrate
	mov	x1,sp
	bl	fmt_bps
	mov	x0,0
	mov	x1,0
	mov	x2,x21
	adr	x3,s_rxing
	bl	tr_x3
	mov	x4,sp
	mov	x5,0
	tok	w6,TJ_ACCENT
	bl	tile
	ldv	x0,g_txrate
	mov	x1,sp
	bl	fmt_bps
	add	x0,x21,10
	mov	x1,0
	mov	x2,x21
	adr	x3,s_txing
	bl	tr_x3
	mov	x4,sp
	mov	x5,0
	movl	w6,C_SENT
	bl	tile
	ldr	x0,[x22,NST_RXB]
	mov	x1,sp
	bl	fmt_bytes
	add	x0,x21,10
	lsl	x0,x0,1
	mov	x1,0
	mov	x2,x21
	adr	x3,s_rx
	bl	tr_x3
	mov	x4,sp
	adr	x5,s_since
	bl	tr_x5
	mov	x6,0
	bl	tile
	ldr	x0,[x22,NST_TXB]
	mov	x1,sp
	bl	fmt_bytes
	add	x0,x21,10
	mov	x1,3
	mul	x0,x0,x1
	mov	x1,0
	sub	x2,x19,x0
	adr	x3,s_tx
	bl	tr_x3
	mov	x4,sp
	adr	x5,s_since
	bl	tr_x5
	mov	x6,0
	bl	tile
	; le haut du graphe : le plus grand debit de la minute (64 Ko/s au moins)
	movl	x23,65536
	adr	x0,h_rx
	ldv	x1,n_rx
	adr	x2,h_tx
.t:	cbz	x1,.td
	sub	x1,x1,1
	ldr	w3,[x0,x1,lsl 2]
	cmp	x3,x23
	csel	x23,x3,x23,hi
	ldr	w3,[x2,x1,lsl 2]
	cmp	x3,x23
	csel	x23,x3,x23,hi
	b	.t
.td:	adr	x0,s_lastmin
	bl	tr_x0
	mov	x1,2
	mov	x2,86
	bl	sect
	mov	x0,x23
	adr	x1,g_row
	bl	fmt_bps
	mov	x0,sp
	adr	x1,s_top
	bl	tr_x1
	bl	str_cat
	adr	x1,g_row
	bl	str_cat
	mov	x0,sp
	sub	x1,x19,2
	mov	x2,86
	tok	w3,TJ_TEXTE3
	mov	x4,12
	adr	x5,ui_font
	bl	text_r
	ldv	x24,g_nif			; sous le graphe : les interfaces
	mov	x0,22
	mul	x0,x24,x0
	add	x0,x0,44
	sub	x25,x20,96
	sub	x25,x25,x0			; hauteur du graphe
	cmp	x25,70
	mov	x0,70
	csel	x25,x0,x25,lt
	mov	x0,0
	mov	x1,96
	mov	x2,x19
	mov	x3,x25
	bl	plot_box
	mov	x0,0
	mov	x1,96
	mov	x2,x19
	mov	x3,x25
	adr	x4,h_rx
	ldv	x5,n_rx
	mov	x6,x23
	tok	w7,TJ_ACCENT
	mov	x8,1
	orr	x7,x7,x8,lsl 32
	bl	plot
	mov	x0,0
	mov	x1,96
	mov	x2,x19
	mov	x3,x25
	adr	x4,h_tx
	ldv	x5,n_tx
	mov	x6,x23
	movl	w7,C_SENT
	bl	plot
	add	x25,x25,96+24
	adr	x0,s_ifaces
	bl	tr_x0
	mov	x1,2
	mov	x2,x25
	bl	sect
	add	x25,x25,20
	adr	x23,g_ifs
.i:	cbz	x24,.out
	cmp	x25,x20
	b.hs	.out
	mov	x0,x23
	mov	x1,2
	mov	x2,x25
	tok	w3,TJ_TEXTE1
	mov	x4,13
	adr	x5,ui_font_b
	mov	x6,70
	bl	ui_text_fit
	mov	x0,sp				; type, lien, debit, adresse, SSID
	ldr	w1,[x23,NIF_E_TYPE]
	adr	x2,s_ifeth
	cmp	w1,NIF_ETHERNET
	b.eq	.ty
	adr	x2,s_ifwifi
	cmp	w1,NIF_WIFI
	b.eq	.ty
	adr	x2,s_ifusb
.ty:	mov	x1,x2
	bl	str_cat
	ldr	w2,[x23,NIF_E_LINK]
	adr	x1,s_ifdown
	bl	tr_x1
	cbz	w2,.lk
	adr	x1,s_ifup
	bl	tr_x1
.lk:	bl	str_cat
	ldr	w1,[x23,NIF_E_SPEED]
	cbz	w1,.nsp
	mov	x2,x0
	mov	x0,x1
	mov	x1,x2
	bl	fmt_dec
	adr	x1,s_mbs
	bl	str_cat
.nsp:	ldr	w2,[x23,NIF_E_IP]
	cbz	w2,.nip
	adr	x1,s_2sp
	bl	str_cat
	mov	x1,x0
	ldr	w0,[x23,NIF_E_IP]
	bl	fmt_ip
.nip:	add	x2,x23,NIF_E_SSID
	ldrb	w1,[x2]
	cbz	w1,.nss
	adr	x1,s_2sp
	bl	str_cat
	add	x1,x23,NIF_E_SSID
	bl	str_cat
.nss:	mov	x0,sp
	mov	x1,76
	mov	x2,x25
	tok	w3,TJ_TEXTE2
	mov	x4,12
	adr	x5,ui_font
	sub	x6,x19,80
	bl	ui_text_fit
	add	x23,x23,NIF_ENT
	add	x25,x25,22
	sub	x24,x24,1
	b	.i
.out:	add	sp,sp,128
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

C_SENT		= 0x00D08A30

include '../../inc/tr.inc'

segment readable writeable
kit_data

s_title		db 'Gestionnaire de t',0xC3,0xA2,'ches',0
s_tabs		db 'Processus|M',0xC3,0xA9,'moire|Processeur|R',0xC3,0xA9,'seau',0
s_raise		db 'Au premier plan',0
s_kill		db 'Terminer',0
s_ksys		db 'Ce programme fait partie du syst',0xC3,0xA8,'me : le terminer peut bloquer la machine. Le terminer quand m',0xC3,0xAA,'me ?',0
s_c_name	db 'T',0xC3,0xA2,'che',0
s_c_kind	db 'Genre',0
s_c_state	db 0xC3,0x89,'tat',0
s_c_cpu		db 'CPU',0
s_c_mem		db 'M',0xC3,0xA9,'moire',0
s_c_thr		db 'Threads',0
s_c_heap	db 'Tas',0
s_c_pid		db 'PID',0
tb_kind_s:
s_k_app		db 'Application',0
s_k_prog	db 'Programme',0
s_k_sys		db 'Syst',0xC3,0xA8,'me',0
s_r_act		db 'Actif',0
s_r_idle	db 'En veille',0
s_r_wait	db 'En attente',0
s_r_end		db 'Fin de vie',0
s_nproc		db ' process',0
s_ram		db 'M',0xC3,0xA9,'moire g',0xC3,0xA9,'r',0xC3,0xA9,'e : ',0
s_pages		db '  -  pages de 4 Ko',0
s_up		db 'En marche depuis ',0
s_sw		db '  -  ',0
s_sw2		db ' commutations/s  -  ',0
s_np		db ' process  -  ',0
s_nc		db ' coeurs',0
s_day		db ' j ',0
s_netdown	db 'Pas d''interface r',0xC3,0xA9,'seau',0
s_netfr		db 'Trames trait',0xC3,0xA9,'es : ',0
s_netrt		db '  -  retransmissions TCP : ',0
s_nonet		db 'Le service r',0xC3,0xA9,'seau ne r',0xC3,0xA9,'pond pas.',0
s_ko		db ' Ko',0
s_mo		db ' Mo',0
s_go		db ' Go',0
s_pct		db ' %',0
s_pctof		db ' % de ',0
s_used		db 'Utilis',0xC3,0xA9,'e',0
s_free		db 'Libre',0
s_apps		db 'Programmes',0
s_sys		db 'Syst',0xC3,0xA8,'me',0
s_sys2		db 'noyau et caches',0
s_mem60		db 'M',0xC3,0x89,'MOIRE UTILIS',0xC3,0x89,'E, LA DERNI',0xC3,0x88,'RE MINUTE',0
s_what		db 'CE QUI L''UTILISE',0
s_others	db 'les autres',0
s_2sp		db '  ',0
s_core		db 'Coeur ',0
s_idle		db 'libre',0
s_octets	db ' o',0
s_ps		db '/s',0
s_rxing		db 'R',0xC3,0xA9,'ception',0
s_txing		db 0xC3,0x89,'mission',0
s_rx		db 'Re',0xC3,0xA7,'ues',0
s_tx		db 0xC3,0x89,'mises',0
s_since		db 'depuis le d',0xC3,0xA9,'marrage',0
s_lastmin	db 'LA DERNI',0xC3,0x88,'RE MINUTE',0
s_top		db 'haut : ',0
s_ifaces	db 'INTERFACES',0
s_ifeth		db 'Ethernet',0
s_ifwifi	db 'Wi-Fi',0
s_ifusb		db 'USB',0
s_ifup		db ', reli',0xC3,0xA9,'e, ',0
s_ifdown	db ', non reli',0xC3,0xA9,'e',0
s_mbs		db ' Mb/s',0
		align 4
; largeur, drapeaux, titre (depuis col_table)
col_table:
	dw	130, 0, s_c_name-col_table
	dw	84, 0, s_c_kind-col_table
	dw	76, 0, s_c_state-col_table
	dw	62, ULC_RIGHT, s_c_cpu-col_table
	dw	88, ULC_RIGHT, s_c_mem-col_table
	dw	84, ULC_RIGHT, s_c_thr-col_table
	dw	72, ULC_RIGHT, s_c_heap-col_table
	dw	52, ULC_RIGHT, s_c_pid-col_table
	dw	0
sort_field	db 0, PR_KIND, PR_RUN, PR_CPU, PR_MEM, PR_THREADS, PR_HEAP, PR_PID
		align 4
core_cols	dw 0x003D8BE0, 0x00E05A55, 0x0045B858, 0x00E8A33A
seg_cols	dw 0x005E6FB8, 0x003D86DA, 0x004A9FB0, 0x005E9F58, 0x00B5A040, 0x00A0A0A8
		align 8
tb_base:
tb_kind		dd s_k_app-tb_base, s_k_prog-tb_base, s_k_sys-tb_base
tb_run		dd s_r_act-tb_base, s_r_idle-tb_base, s_r_wait-tb_base, s_r_end-tb_base
g_tok		dd 0
g_self		dd 0
g_seg		dd 0
g_list		dd 0
g_view		dd 0
g_info		dd 0
g_braise	dd 0
g_bkill		dd 0
g_tab		dd 0
g_next		dd 0
g_last		dd 0
g_dt		dd 0
g_sort		dd 3				; (le CPU, le plus gros d'abord)
g_desc		dd 1
g_sel		dd 0
g_nproc		dd 0
g_apps		dd 0
g_lastsw	dd 0
g_swrate	dd 0
g_netok		dd 0
g_lastrx	dd 0
g_lasttx	dd 0
g_rxrate	dd 0
g_txrate	dd 0
g_nif		dd 0
g_tcount	dd 0
g_tprevn	dd 0
n_mem		dd 0
n_rx		dd 0
n_tx		dd 0
n_core		rd 0
		dd 0, 0, 0, 0
g_owner		dd 0, 0, 0, 0
g_seg_kb	rb 7*8
g_seg_nm	rb 7*8
g_info_sys	rb 80
g_vm		rb 64
g_taken		rb MAXP
		align 8
g_raw		rb 64*MAXP
g_recs		rb PR_SIZE*MAXP
g_order		rb 2*MAXP
		align 8
g_tbuf		rb 64*64
g_tprev		rb 16*MAXT
g_tcur		rb 16*MAXT
g_wins		rb WE_PER_MSG*WE_SIZE
g_ifs		rb NIF_ENT*MAXIF
g_nst		rb NST_SIZE
g_row		rb 256
g_foot		rb 160
		align 4
h_mem		rb HIST*4
h_rx		rb HIST*4
h_tx		rb HIST*4
h_core		rb HIST*4*CORES
ui_data
