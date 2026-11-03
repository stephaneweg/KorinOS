; ============================================================================
;  fmt_edit.inc - FM Tracker : audio, annulation, curseur, blocs, lecture,
;  commandes du morceau et des motifs
;
;  Rien ici ne dessine : les fonctions notent ce qui a change (macro mark) et
;  flush (fmt_view.inc) repeint depuis la boucle principale. Un dialogue ouvert
;  (g_in_dlg) laisse donc la lecture continuer sans toucher a sa fenetre.
; ============================================================================

; ---- audio -----------------------------------------------------------------

; x0 = canal : son instrument au synthe
upload:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	ins_ptr
	add	x1,x0,INS_P
	adr	x0,g_akins
	bl	ak_fm_from_fms
	ldv	x0,g_fm
	mov	x1,x19
	adr	x2,g_akins
	bl	ak_fm_instrument
	ldr	x19,[sp],16
	epilogue

upload_all:
	prologue
	str	x19,[sp,-16]!
	mov	x19,0
.l:	mov	x0,x19
	bl	upload
	add	x19,x19,1
	cmp	x19,FMS_CH
	b.lo	.l
	ldr	x19,[sp],16
	epilogue

; x0 = voix, x1 = octet de note
note_on:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	cmp	x19,FMS_CH
	b.hs	.s
	adr	x0,g_vu
	mov	x1,100
	str	x1,[x0,x19,lsl 3]
	adr	x0,g_snd
	strb	w20,[x0,x19]
	mark	D_PIANO or D_HEAD
.s:	mov	x0,x20
	bl	note_mhz
	mov	x2,x0
	ldv	x0,g_fm
	mov	x1,x19
	mov	x3,AKW_FM
	mov	x4,220
	bl	ak_fm_start
	ldp	x19,x20,[sp],16
	epilogue

; x0 = voix (-1 : toutes)
note_off:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	tbz	x19,63,.one
	adr	x0,g_snd
	str	xzr,[x0]
	mark	D_PIANO or D_HEAD
	b	.stop
.one:	cmp	x19,FMS_CH
	b.hs	.stop
	adr	x0,g_snd
	ldrb	w1,[x0,x19]
	cbz	w1,.stop
	strb	wzr,[x0,x19]
	mark	D_PIANO or D_HEAD
.stop:	ldv	x0,g_fm
	mov	x1,x19
	bl	ak_fm_stop
	ldr	x19,[sp],16
	epilogue

; x0 = canal -> x0 = 1 s'il est reduit au silence (muet, ou un autre en solo)
silenced:
	mov	x4,x0
	ldv	x0,g_pat
	lsl	x0,x0,5
	adr	x1,g_pats
	add	x0,x0,x1
	add	x0,x0,PT_MUTE
	ldrb	w1,[x0,x4]
	cbnz	w1,.y
	ldv	x1,g_solo
	tbnz	x1,63,.n
	cmp	x1,x4
	b.ne	.y
.n:	mov	x0,0
	ret
.y:	mov	x0,1
	ret

; x0 = canal, x1 = note : entendre la note saisie
preview:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldv	x0,g_playing
	cbnz	x0,.out
	mov	x0,x19
	bl	silenced
	cbnz	x0,.out
	mov	x0,x19
	mov	x1,x20
	bl	note_on
	stv	x19,g_pv_voice
	sys	SYS_TIME_MS
	add	x0,x0,PREVIEW_MS
	stv	x0,g_pv_end
.out:	ldp	x19,x20,[sp],16
	epilogue

; ---- etat ------------------------------------------------------------------

; x0 = texte, x1 = suite (0 : aucune) : barre d'etat
set_status:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x1
	mov	x1,x0
	adr	x0,g_status
	strb	wzr,[x0]
	bl	str_cat
	cbz	x19,.m
	mov	x1,x19
	bl	str_cat
.m:	mark	D_STATUS
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte original : sa traduction en barre d'etat
status_tr:
	prologue
	bl	ui_tr
	mov	x1,0
	bl	set_status
	epilogue

set_modified:
	ldv	x0,g_modified
	cbnz	x0,.r
	mov	x0,1
	stv	x0,g_modified
	mark	D_TITLE
.r:	ret

; ---- annulation : les notes du motif avant chaque changement ----------------

; x0 = pile, x1 = &compte : empile le motif courant
snap_push:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x21,[x20]
	cmp	x21,UNDO_MAX
	b.lo	.room
	mov	x0,x19				; plein : le plus ancien s'en va
	add	x1,x19,SN_SIZE
	movl	x2,(UNDO_MAX-1)*SN_SIZE
	bl	mem_move
	sub	x21,x21,1
.room:	mov	x0,SN_SIZE
	mul	x0,x21,x0
	add	x22,x19,x0
	add	x21,x21,1
	str	x21,[x20]
	bl	cur_pat
	mov	x19,x0
	ldv	x0,g_pat
	str	x0,[x22,SN_PAT]
	ldr	x0,[x19,PT_ROWS]
	str	x0,[x22,SN_ROWS]
	ldr	x0,[x19,PT_SPEED]
	str	x0,[x22,SN_SPEED]
	ldr	x0,[x19,PT_MUTE]
	str	x0,[x22,SN_MUTE]
	add	x0,x22,SN_N
	ldr	x1,[x19,PT_N]
	mov	x2,CELLS
	bl	mem_move
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; les piles sont videes (la liste des motifs a change)
undo_clear:
	stv	xzr,g_nundo
	stv	xzr,g_nredo
	ret

undo_push:
	prologue
	stv	xzr,g_nredo
	adr	x0,g_undo
	adr	x1,g_nundo
	bl	snap_push
	bl	set_modified
	epilogue

; x0 = pile source, x1 = &compte, x2 = pile cible, x3 = &compte, x4 = message
; si la source est vide
snap_back:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	ldr	x0,[x20]
	cbnz	x0,.go
	mov	x0,x4
	bl	status_tr
	b	.out
.go:	sub	x0,x0,1
	str	x0,[x20]
	mov	x1,SN_SIZE
	mul	x0,x0,x1
	add	x19,x19,x0			; la photo
	ldr	x0,[x19,SN_PAT]
	ldv	x1,g_npat
	cmp	x0,x1
	b.hs	.out
	stv	x0,g_pat
	mov	x0,x21
	mov	x1,x22
	bl	snap_push
	bl	cur_pat
	ldr	x1,[x19,SN_ROWS]
	str	x1,[x0,PT_ROWS]
	ldr	x1,[x19,SN_SPEED]
	str	x1,[x0,PT_SPEED]
	ldr	x1,[x19,SN_MUTE]
	str	x1,[x0,PT_MUTE]
	ldr	x0,[x0,PT_N]
	add	x1,x19,SN_N
	mov	x2,CELLS
	bl	mem_move
	bl	set_modified
	stv	xzr,g_sel
	bl	refresh_pattern
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

op_undo:
	adr	x0,g_undo
	adr	x1,g_nundo
	adr	x2,g_redo
	adr	x3,g_nredo
	adr	x4,s_no_undo
	b	snap_back

op_redo:
	adr	x0,g_redo
	adr	x1,g_nredo
	adr	x2,g_undo
	adr	x3,g_nundo
	adr	x4,s_no_redo
	b	snap_back

; ---- la vue ----------------------------------------------------------------

; g_top borne par la longueur du motif
sync_scroll:
	prologue
	bl	cur_pat
	ldr	x0,[x0,PT_ROWS]
	ldv	x1,g_vis
	sub	x0,x0,x1
	cmp	x0,0
	csel	x0,xzr,x0,lt
	ldv	x1,g_top
	cmp	x1,x0
	csel	x1,x0,x1,gt
	cmp	x1,0
	csel	x1,xzr,x1,lt
	stv	x1,g_top
	mark	D_SCROLL or D_GRID
	epilogue

; x0 = ligne, x1 = 1 : centree
ensure_visible:
	ldv	x2,g_vis
	ldv	x3,g_top
	cbz	x1,.m
	lsr	x4,x2,1
	sub	x3,x0,x4
	b	.st
.m:	cmp	x0,x3
	b.ge	.h
	mov	x3,x0
	b	.st
.h:	add	x4,x3,x2
	cmp	x0,x4
	b.lt	.st
	sub	x3,x0,x2
	add	x3,x3,1
.st:	stv	x3,g_top
	b	sync_scroll

; le motif courant a change : numeros, curseur, vue
refresh_pattern:
	prologue
	bl	cur_pat
	ldr	x0,[x0,PT_ROWS]
	ldv	x1,g_row
	cmp	x1,x0
	b.lo	.r
	sub	x1,x0,1
	stv	x1,g_row
.r:	ldv	x1,g_ar
	cmp	x1,x0
	b.lo	.a
	stv	xzr,g_sel
.a:	mark	D_BOXES or D_SONG
	bl	list_show
	bl	sync_scroll
	epilogue

; -> x0 = r0, x1 = r1, x2 = c0, x3 = c1 : le bloc (la case du curseur sans bloc)
sel_rect:
	ldv	x0,g_row
	ldv	x2,g_ch
	mov	x1,x0
	mov	x3,x2
	ldv	x4,g_sel
	cbz	x4,.r
	ldv	x4,g_ar
	cmp	x4,x0
	b.ge	.a1
	mov	x0,x4
	b	.c
.a1:	mov	x1,x4
.c:	ldv	x4,g_ac
	cmp	x4,x2
	b.ge	.a2
	mov	x2,x4
	ret
.a2:	mov	x3,x4
.r:	ret

; x0 = ligne, x1 = canal
move_to:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	cur_pat
	ldr	x0,[x0,PT_ROWS]
	cmp	x19,0
	csel	x19,xzr,x19,lt
	cmp	x19,x0
	b.lt	.r
	sub	x19,x0,1
.r:	mov	x0,FMS_CH-1
	cmp	x20,0
	csel	x20,x0,x20,lt
	cmp	x20,FMS_CH
	csel	x20,xzr,x20,ge
	stv	x19,g_row
	stv	x20,g_ch
	mov	x0,x19
	mov	x1,0
	bl	ensure_visible
	mark	D_GRID or D_HEAD or D_LCD
	ldp	x19,x20,[sp],16
	epilogue

; x0 = octet, x1 = lignes a descendre
set_cell:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	undo_push
	bl	cur_pat
	ldr	x0,[x0,PT_N]
	ldv	x1,g_ch
	lsl	x1,x1,8
	ldv	x2,g_row
	add	x1,x1,x2
	strb	w19,[x0,x1]
	stv	xzr,g_sel
	ldv	x0,g_row
	add	x0,x0,x20
	ldv	x1,g_ch
	bl	move_to
	ldp	x19,x20,[sp],16
	epilogue

; x0 = note (1..7), x1 = diese, x2 = octave : note tapee ou cliquee
enter_note:
	prologue
	str	x19,[sp,-16]!
	bl	make_note
	mov	x19,x0
	ldv	x0,g_ch
	mov	x1,x19
	bl	preview
	mov	x0,x19
	ldv	x1,g_step
	bl	set_cell
	ldr	x19,[sp],16
	epilogue

; ---- le bloc ---------------------------------------------------------------

; efface le bloc (sans annulation : voir op_clear_block)
block_zero:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	sel_rect
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	bl	cur_pat
	ldr	x0,[x0,PT_N]
.c:	cmp	x21,x22
	b.gt	.e
	lsl	x1,x21,8
	add	x1,x1,x0
	mov	x2,x19
.r:	cmp	x2,x20
	b.gt	.n
	strb	wzr,[x1,x2]
	add	x2,x2,1
	b	.r
.n:	add	x21,x21,1
	b	.c
.e:	mark	D_GRID
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

op_clear_block:
	prologue
	bl	undo_push
	bl	block_zero
	epilogue

op_copy:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	sel_rect
	mov	x19,x0
	mov	x21,x2
	sub	x4,x1,x0
	add	x4,x4,1				; lignes
	sub	x5,x3,x2
	add	x5,x5,1				; canaux
	stv	x4,g_clip_r
	stv	x5,g_clip_c
	bl	cur_pat
	ldr	x0,[x0,PT_N]
	ldv	x4,g_clip_r
	ldv	x5,g_clip_c
	adr	x6,g_clip
	mov	x1,0				; c
.c:	cmp	x1,x5
	b.hs	.st
	add	x2,x21,x1
	lsl	x2,x2,8
	add	x2,x2,x19
	add	x2,x2,x0			; source
	lsl	x3,x1,8
	add	x3,x3,x6			; dest
	mov	x7,0
.r:	ldrb	w8,[x2,x7]
	strb	w8,[x3,x7]
	add	x7,x7,1
	cmp	x7,x4
	b.lo	.r
	add	x1,x1,1
	b	.c
.st:	adr	x0,s_copied
	bl	ui_tr
	mov	x1,x0
	adr	x0,g_tmp
	ldv	x2,g_clip_r
	ldv	x3,g_clip_c
	bl	fmt
	adr	x0,g_tmp
	mov	x1,0
	bl	set_status
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

op_cut:
	prologue
	bl	op_copy
	bl	op_clear_block
	epilogue

op_paste:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x0,g_clip_r
	cbnz	x0,.go
	adr	x0,s_no_paste
	bl	status_tr
	b	.out
.go:	bl	sel_rect
	mov	x19,x0				; r0
	mov	x20,x2				; c0
	bl	undo_push
	bl	cur_pat
	ldr	x21,[x0,PT_N]
	ldr	x22,[x0,PT_ROWS]
	ldv	x4,g_clip_r
	ldv	x5,g_clip_c
	adr	x6,g_clip
	mov	x1,0
.c:	cmp	x1,x5
	b.hs	.e
	add	x2,x20,x1
	cmp	x2,FMS_CH
	b.hs	.e
	lsl	x2,x2,8
	add	x2,x2,x21
	lsl	x3,x1,8
	add	x3,x3,x6
	mov	x7,0
.r:	cmp	x7,x4
	b.hs	.n
	add	x8,x19,x7
	cmp	x8,x22
	b.hs	.n
	ldrb	w9,[x3,x7]
	strb	w9,[x2,x8]
	add	x7,x7,1
	b	.r
.n:	add	x1,x1,1
	b	.c
.e:	mark	D_GRID
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

op_select_all:
	prologue
	bl	cur_pat
	ldr	x0,[x0,PT_ROWS]
	sub	x0,x0,1
	stv	x0,g_row
	mov	x0,FMS_CH-1
	stv	x0,g_ch
	stv	xzr,g_ar
	stv	xzr,g_ac
	mov	x0,1
	stv	x0,g_sel
	mark	D_GRID or D_HEAD or D_LCD
	epilogue

; x0 = demi-tons : les notes du bloc (celle du curseur sans bloc)
transpose:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x25,x0
	bl	sel_rect
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	bl	cur_pat
	ldr	x24,[x0,PT_N]
	mov	x23,x21				; une note dans le bloc ?
.fc:	cmp	x23,x22
	b.gt	.out
	lsl	x1,x23,8
	add	x1,x1,x24
	mov	x2,x19
.fr:	cmp	x2,x20
	b.gt	.fn
	ldrb	w3,[x1,x2]
	tbnz	w3,7,.fs
	tst	w3,7
	b.ne	.found
.fs:	add	x2,x2,1
	b	.fr
.fn:	add	x23,x23,1
	b	.fc
.found:	bl	undo_push
	mov	x23,x21
.c:	cmp	x23,x22
	b.gt	.done
	lsl	x26,x23,8
	add	x26,x26,x24
	add	x26,x26,x19
	mov	x21,x19
.r:	cmp	x21,x20
	b.gt	.n
	ldrb	w0,[x26]
	mov	x1,x25
	bl	note_transpose
	strb	w0,[x26],1
	add	x21,x21,1
	b	.r
.n:	add	x23,x23,1
	b	.c
.done:	ldv	x0,g_sel
	cbnz	x0,.m
	ldv	x0,g_ch
	lsl	x1,x0,8
	ldv	x2,g_row
	add	x1,x1,x2
	ldrb	w1,[x24,x1]
	bl	preview
.m:	mark	D_GRID
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

op_up1:
	mov	x0,1
	b	transpose
op_down1:
	mov	x0,-1
	b	transpose
op_up12:
	mov	x0,12
	b	transpose
op_down12:
	mov	x0,-12
	b	transpose

op_insert_row:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	undo_push
	bl	cur_pat
	ldr	x19,[x0,PT_ROWS]
	ldr	x0,[x0,PT_N]
	ldv	x1,g_ch
	lsl	x1,x1,8
	add	x0,x0,x1
	ldv	x20,g_row
	sub	x1,x19,1
.l:	cmp	x1,x20
	b.le	.e
	sub	x2,x1,1
	ldrb	w3,[x0,x2]
	strb	w3,[x0,x1]
	mov	x1,x2
	b	.l
.e:	strb	wzr,[x0,x20]
	mark	D_GRID
	ldp	x19,x20,[sp],16
	epilogue

op_delete_row:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	undo_push
	bl	cur_pat
	ldr	x19,[x0,PT_ROWS]
	ldr	x0,[x0,PT_N]
	ldv	x1,g_ch
	lsl	x1,x1,8
	add	x0,x0,x1
	ldv	x20,g_row
	sub	x19,x19,1
.l:	cmp	x20,x19
	b.ge	.e
	add	x2,x20,1
	ldrb	w3,[x0,x2]
	strb	w3,[x0,x20]
	mov	x20,x2
	b	.l
.e:	strb	wzr,[x0,x19]
	mark	D_GRID
	ldp	x19,x20,[sp],16
	epilogue

op_clear_channel:
	prologue
	bl	undo_push
	bl	cur_pat
	ldr	x1,[x0,PT_ROWS]
	ldr	x0,[x0,PT_N]
	ldv	x2,g_ch
	lsl	x2,x2,8
	add	x0,x0,x2
	mov	x2,0
.l:	strb	wzr,[x0,x2]
	add	x2,x2,1
	cmp	x2,x1
	b.lo	.l
	mark	D_GRID
	epilogue

; ---- motifs ----------------------------------------------------------------

; x0 = 1 : copie du motif courant -> un motif apres lui
add_pattern:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	ldv	x20,g_npat
	cmp	x20,FMS_MAXPAT
	b.lo	.go
	adr	x0,s_max_pat
	bl	status_tr
	b	.out
.go:	bl	op_stop
	bl	undo_clear
	ldv	x21,g_pat
	mov	x0,x20				; la case libre est a npat
	bl	pat_ptr
	ldr	x22,[x0,PT_N]
.sh:	add	x1,x21,1			; les suivants reculent
	cmp	x20,x1
	b.ls	.put
	mov	x0,x20
	bl	pat_ptr
	sub	x1,x0,PT_SIZE
	ldp	x2,x3,[x1]
	stp	x2,x3,[x0]
	ldp	x2,x3,[x1,16]
	stp	x2,x3,[x0,16]
	sub	x20,x20,1
	b	.sh
.put:	add	x0,x21,1
	bl	pat_ptr
	str	x22,[x0,PT_N]
	mov	x20,x0
	mov	x0,x21
	bl	pat_ptr
	mov	x21,x0
	mov	x0,x20
	ldr	x1,[x21,PT_ROWS]
	ldr	x2,[x21,PT_SPEED]
	bl	pattern_clear
	cbz	x19,.done
	ldr	x0,[x21,PT_MUTE]
	str	x0,[x20,PT_MUTE]
	ldr	x0,[x20,PT_N]
	ldr	x1,[x21,PT_N]
	mov	x2,CELLS
	bl	mem_move
.done:	ldv	x0,g_npat
	add	x0,x0,1
	stv	x0,g_npat
	ldv	x0,g_pat
	add	x0,x0,1
	stv	x0,g_pat
	stv	xzr,g_row
	stv	xzr,g_top
	stv	xzr,g_sel
	bl	set_modified
	bl	refresh_pattern
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

op_new_pattern:
	mov	x0,0
	b	add_pattern
op_dup_pattern:
	mov	x0,1
	b	add_pattern

op_delete_pattern:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x0,g_npat
	cmp	x0,1
	b.hi	.ask
	adr	x0,s_keep_pat
	bl	status_tr
	b	.out
.ask:	bl	op_stop
	adr	x0,s_pattern
	bl	ui_tr
	mov	x19,x0
	adr	x0,s_del_pat
	bl	ui_tr
	mov	x1,x0
	mov	x0,x19
	mov	x2,UM_YESNO
	bl	ui_message
	cbz	x0,.out
	bl	undo_clear
	ldv	x19,g_pat
	mov	x0,x19
	bl	pat_ptr
	ldr	x20,[x0,PT_N]			; sa case passe a la fin
.mv:	ldv	x0,g_npat
	add	x1,x19,1
	cmp	x1,x0
	b.hs	.last
	mov	x0,x19
	bl	pat_ptr
	add	x1,x0,PT_SIZE
	ldp	x2,x3,[x1]
	stp	x2,x3,[x0]
	ldp	x2,x3,[x1,16]
	stp	x2,x3,[x0,16]
	add	x19,x19,1
	b	.mv
.last:	mov	x0,x19
	bl	pat_ptr
	str	x20,[x0,PT_N]
	ldv	x0,g_npat
	sub	x0,x0,1
	stv	x0,g_npat
	ldv	x1,g_pat
	cmp	x1,x0
	b.lo	.k
	sub	x1,x0,1
	stv	x1,g_pat
.k:	stv	xzr,g_sel
	bl	set_modified
	bl	refresh_pattern
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = -1 / +1 : plus tot / plus tard dans le morceau
move_pattern:
	prologue
	stp	x19,x20,[sp,-16]!
	ldv	x19,g_pat
	add	x20,x19,x0
	tbnz	x20,63,.out
	ldv	x0,g_npat
	cmp	x20,x0
	b.hs	.out
	bl	op_stop
	bl	undo_clear
	mov	x0,x19
	bl	pat_ptr
	mov	x19,x0
	mov	x0,x20
	bl	pat_ptr
	ldp	x2,x3,[x0]
	ldp	x4,x5,[x19]
	stp	x4,x5,[x0]
	stp	x2,x3,[x19]
	ldp	x2,x3,[x0,16]
	ldp	x4,x5,[x19,16]
	stp	x4,x5,[x0,16]
	stp	x2,x3,[x19,16]
	stv	x20,g_pat
	bl	set_modified
	bl	refresh_pattern
.out:	ldp	x19,x20,[sp],16
	epilogue

op_pattern_up:
	mov	x0,-1
	b	move_pattern
op_pattern_down:
	mov	x0,1
	b	move_pattern

op_prev_pattern:
	prologue
	ldv	x0,g_pat
	cbz	x0,.o
	sub	x0,x0,1
	stv	x0,g_pat
	stv	xzr,g_sel
	bl	refresh_pattern
.o:	epilogue

op_next_pattern:
	prologue
	ldv	x0,g_pat
	add	x0,x0,1
	ldv	x1,g_npat
	cmp	x0,x1
	b.hs	.o
	stv	x0,g_pat
	stv	xzr,g_sel
	bl	refresh_pattern
.o:	epilogue

; x0 = canal : muet dans ce motif (ou plus)
mute_channel:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	cur_pat
	add	x0,x0,PT_MUTE
	ldrb	w1,[x0,x19]
	eor	w1,w1,1
	strb	w1,[x0,x19]
	str	x1,[sp,8]
	bl	set_modified
	ldr	x1,[sp,8]
	cbz	x1,.m
	mov	x0,x19
	bl	note_off
.m:	mark	D_GRID or D_HEAD
	ldr	x19,[sp],16
	epilogue

op_mute:
	ldv	x0,g_ch
	b	mute_channel

; x0 = canal : solo (ou plus de solo)
solo_channel:
	prologue
	str	x19,[sp,-16]!
	ldv	x1,g_solo
	mov	x2,-1
	cmp	x1,x0
	csel	x0,x2,x0,eq
	stv	x0,g_solo
	ldv	x0,g_playing
	cbz	x0,.d
	mov	x19,0				; ceux qui se taisent maintenant
.l:	mov	x0,x19
	bl	silenced
	cbz	x0,.n
	mov	x0,x19
	bl	note_off
.n:	add	x19,x19,1
	cmp	x19,FMS_CH
	b.lo	.l
.d:	mark	D_GRID or D_HEAD
	ldr	x19,[sp],16
	epilogue

op_solo:
	ldv	x0,g_ch
	b	solo_channel

op_instrument:
	ldv	x0,g_ch
	b	edit_instrument

; ---- lecture ---------------------------------------------------------------

; la ligne g_play_row du motif g_play_pat
play_row:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	ldv	x0,g_play_pat
	bl	pat_ptr
	mov	x20,x0
	ldr	x21,[x20,PT_N]
	ldv	x0,g_play_row
	add	x21,x21,x0
	mov	x19,0
.l:	ldrb	w22,[x21]
	add	x21,x21,FMS_MAXROWS
	tbnz	w22,7,.off
	tst	w22,7
	b.eq	.n
	add	x0,x20,PT_MUTE
	ldrb	w0,[x0,x19]
	cbnz	w0,.off
	ldv	x0,g_solo
	tbnz	x0,63,.on
	cmp	x0,x19
	b.ne	.off
.on:	mov	x0,x19
	mov	x1,x22
	bl	note_on
	b	.n
.off:	mov	x0,x19
	bl	note_off
.n:	add	x19,x19,1
	cmp	x19,FMS_CH
	b.lo	.l
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

op_stop:
	prologue
	ldv	x0,g_playing
	cbz	x0,.o
	stv	xzr,g_playing
	mov	x0,-1
	bl	note_off
	mark	D_SONG or D_BOXES
	adr	x0,s_stopped
	bl	status_tr
.o:	epilogue

; x0 = motif, x1 = ligne
start_play:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	upload_all
	mov	x0,1
	stv	x0,g_playing
	stv	x19,g_play_pat
	stv	x20,g_play_row
	stv	x20,g_shown_row
	sys	SYS_TIME_MS
	stv	x0,g_next_tick
	mark	D_BOXES
	adr	x0,s_playing
	ldv	x1,g_loop
	cbz	x1,.s
	adr	x0,s_playing_loop
.s:	bl	status_tr
	ldp	x19,x20,[sp],16
	epilogue

op_play:
	prologue
	ldv	x0,g_playing
	cbnz	x0,.st
	ldv	x0,g_pat
	ldv	x1,g_row
	bl	start_play
	epilogue
.st:	bl	op_stop
	epilogue

op_play_start:
	prologue
	bl	op_stop
	mov	x0,0
	ldv	x1,g_loop
	cbz	x1,.s
	ldv	x0,g_pat
.s:	mov	x1,0
	bl	start_play
	epilogue

op_loop:
	prologue
	ldv	x0,g_loop
	eor	x0,x0,1
	stv	x0,g_loop
	mov	x1,0
	cbz	x0,.s
	mov	x1,UMS_CHECKED
.s:	mov	x0,ID_LOOP
	bl	ui_menu_state
	adr	x0,s_loop_off
	ldv	x1,g_loop
	cbz	x1,.m
	adr	x0,s_loop_on
.m:	bl	status_tr
	mark	D_BOXES
	epilogue

op_follow:
	prologue
	ldv	x0,g_follow
	eor	x0,x0,1
	stv	x0,g_follow
	mov	x1,0
	cbz	x0,.s
	mov	x1,UMS_CHECKED
.s:	mov	x0,ID_FOLLOW
	bl	ui_menu_state
	mark	D_BOXES
	epilogue

; a chaque tour de boucle : fin des notes d'essai, vu-metres, lecture
tick:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sys	SYS_TIME_MS
	mov	x19,x0				; maintenant
	ldv	x0,g_pv_voice
	tbnz	x0,63,.vu
	ldv	x1,g_pv_end
	cmp	x19,x1
	b.lo	.vu
	bl	note_off
	mov	x0,-1
	stv	x0,g_pv_voice
.vu:	ldv	x20,g_vu_tick
	sub	x20,x19,x20			; ms
	cmp	x20,30
	b.lo	.play
	stv	x19,g_vu_tick
	mov	x0,200
	cmp	x20,x0
	csel	x20,x0,x20,hi
	mov	x0,10
	udiv	x20,x20,x0			; centiemes
	adr	x2,g_vu
	adr	x3,g_snd
	mov	x4,0				; canal
	mov	x5,0				; 1 : a repeindre
.v:	ldr	x6,[x2,x4,lsl 3]
	ldrb	w7,[x3,x4]
	mov	x0,35				; plancher tant que la note sonne
	mov	x1,2
	cbnz	w7,.f
	mov	x0,0
	mov	x1,6
.f:	cmp	x6,x0
	b.le	.vn
	mul	x1,x1,x20
	sub	x6,x6,x1
	cmp	x6,x0
	csel	x6,x0,x6,lt
	str	x6,[x2,x4,lsl 3]
	mov	x5,1
.vn:	add	x4,x4,1
	cmp	x4,FMS_CH
	b.lo	.v
	cbz	x5,.play
	mark	D_HEAD
.play:	ldv	x0,g_playing
	cbz	x0,.out
	mov	x20,0				; garde : 8 lignes au plus d'un coup
.more:	ldv	x0,g_playing
	cbz	x0,.out
	ldv	x0,g_next_tick
	cmp	x19,x0
	b.lo	.resync
	cmp	x20,8
	b.hs	.resync
	add	x20,x20,1
	ldv	x0,g_play_pat
	bl	pat_ptr
	ldr	x1,[x0,PT_ROWS]
	ldv	x2,g_play_row
	cmp	x2,x1
	b.lo	.row
	stv	xzr,g_play_row			; fin du motif
	ldv	x0,g_loop
	cbnz	x0,.row
	ldv	x0,g_play_pat
	add	x0,x0,1
	ldv	x1,g_npat
	cmp	x0,x1
	csel	x0,xzr,x0,hs
	stv	x0,g_play_pat
.row:	bl	play_row
	ldv	x0,g_follow
	cbz	x0,.nf
	ldv	x0,g_play_pat			; suivre : le motif joue, sa ligne au centre
	ldv	x1,g_pat
	cmp	x0,x1
	b.eq	.same
	stv	x0,g_pat
	stv	xzr,g_sel
	bl	refresh_pattern
.same:	ldv	x0,g_play_row
	mov	x1,1
	bl	ensure_visible
.nf:	ldv	x0,g_play_pat
	bl	pat_ptr
	ldr	x0,[x0,PT_SPEED]
	mov	x1,ROW_MS
	mul	x0,x0,x1
	ldv	x1,g_next_tick
	add	x1,x1,x0
	stv	x1,g_next_tick
	ldv	x0,g_play_row
	stv	x0,g_shown_row
	add	x0,x0,1
	stv	x0,g_play_row
	mark	D_SONG
	b	.more
.resync:
	ldv	x0,g_next_tick			; en retard : on recale
	sub	x0,x19,x0
	cmp	x0,500
	b.le	.out
	tbnz	x0,63,.out
	stv	x19,g_next_tick
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---- commandes du morceau --------------------------------------------------

; x0 = chemin -> x0 = dernier composant
base_name:
	mov	x1,x0
.n:	ldrb	w2,[x1],1
	cbz	w2,.r
	cmp	w2,'/'
	b.ne	.n
	mov	x0,x1
	b	.n
.r:	ret

; la barre d'etat : le fichier, modifie ou non
title_status:
	prologue
	str	x19,[sp,-16]!
	adr	x19,g_path
	ldrb	w0,[x19]
	cbnz	w0,.p
	adr	x0,s_untitled
	bl	ui_tr
	mov	x19,x0
.p:	mov	x1,0
	ldv	x0,g_modified
	cbz	x0,.s
	adr	x0,s_modif
	bl	ui_tr
	mov	x1,x0
.s:	mov	x0,x19
	bl	set_status
	mark	D_TITLE or D_SIDE
	ldr	x19,[sp],16
	epilogue

; -> x0 = 0 si l'on renonce (le morceau modifie est garde ou non : question)
confirm_discard:
	prologue
	str	x19,[sp,-16]!
	ldv	x0,g_modified
	cbz	x0,.yes
	bl	op_stop
	adr	x0,s_app
	bl	ui_tr
	mov	x19,x0
	adr	x0,s_save_first
	bl	ui_tr
	mov	x1,x0
	mov	x0,x19
	mov	x2,UM_YESNOCANCEL
	bl	ui_message
	cbz	x0,.out
	cmp	x0,1
	b.ne	.yes
	bl	op_save
	ldv	x0,g_modified
	cmp	x0,0
	cset	x0,eq
	b	.out
.yes:	mov	x0,1
.out:	ldr	x19,[sp],16
	epilogue

after_load:
	prologue
	stv	xzr,g_pat
	stv	xzr,g_row
	stv	xzr,g_ch
	stv	xzr,g_top
	stv	xzr,g_sel
	mov	x0,-1
	stv	x0,g_solo
	bl	undo_clear
	bl	upload_all
	stv	xzr,g_modified
	bl	refresh_pattern
	bl	title_status
	epilogue

; le nouveau morceau du debut : les instruments PIANO.FMI si on les trouve
song_defaults:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	song_new
	adr	x0,s_piano_fmi
	adr	x1,g_ins_tmp
	bl	read_fmi
	cbz	x0,.out
	mov	x19,0
.c:	mov	x0,x19
	bl	ins_ptr
	adr	x1,g_ins_tmp
	mov	x2,INS_SIZE
	bl	mem_move
	add	x19,x19,1
	cmp	x19,FMS_CH
	b.lo	.c
.out:	ldp	x19,x20,[sp],16
	epilogue

op_new:
	prologue
	bl	confirm_discard
	cbz	x0,.o
	bl	op_stop
	bl	song_defaults
	adr	x0,g_path
	strb	wzr,[x0]
	bl	after_load
.o:	epilogue

; x0 = chemin -> x0 = 1 si le morceau est charge
load_song:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x1,g_file
	movl	x2,FILE_MAX
	bl	read_file
	cmp	x0,0
	b.gt	.parse
	adr	x0,s_cant_open
	bl	ui_tr
	mov	x1,x0
	adr	x0,g_tmp
	strb	wzr,[x0]
	bl	str_cat
	adr	x1,s_space
	bl	str_cat
	mov	x1,x19
	bl	str_cat
	adr	x0,g_tmp
	mov	x1,0
	bl	set_status
	mov	x0,0
	b	.out
.parse:	mov	x20,x0
	bl	op_stop
	adr	x0,g_file
	mov	x1,x20
	bl	fms_parse
	cbnz	x0,.ok
	adr	x0,s_app
	bl	ui_tr
	mov	x20,x0
	adr	x0,s_not_fms
	bl	ui_tr
	mov	x1,x0
	mov	x0,x20
	mov	x2,UM_OK
	bl	ui_message
	mov	x0,0
	b	.out
.ok:	adr	x0,g_path
	mov	x1,x19
	mov	x2,256
	bl	str_copy
	bl	after_load
	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = dossier de depart des dialogues
song_dir:
	prologue
	adr	x0,g_path
	ldrb	w1,[x0]
	cbz	w1,.def
	adr	x0,g_dirbuf
	adr	x1,g_path
	mov	x2,256
	bl	str_copy
	adr	x0,g_dirbuf
	bl	path_parent
	adr	x0,g_dirbuf
	epilogue
.def:	adr	x0,s_song_dir
	epilogue

op_open:
	prologue
	bl	confirm_discard
	cbz	x0,.o
	bl	op_stop
	bl	song_dir
	adr	x1,g_dlgpath
	mov	x2,256
	bl	ui_file_open
	cbz	x0,.o
	adr	x0,g_dlgpath
	bl	load_song
.o:	epilogue

; x0 = chemin -> x0 = 0 / < 0
write_song:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,g_file
	bl	fms_write
	mov	x2,x0
	mov	x0,x19
	adr	x1,g_file
	bl	write_file
	ldr	x19,[sp],16
	epilogue

; x0 = chemin (tampon de 256 octets), x1 = extension : ajoutee s'il n'en a pas
ensure_ext:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	bl	str_len
	cmp	x0,4
	b.ls	.add
	sub	x1,x0,4
	ldrb	w1,[x19,x1]
	cmp	w1,'.'
	b.eq	.out
.add:	add	x0,x19,x0
	mov	x1,x20
	mov	x2,5
	bl	str_copy
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = message (original) : " <message> <chemin> " en barre d'etat
status_path:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	bl	ui_tr
	mov	x1,x0
	adr	x0,g_tmp
	strb	wzr,[x0]
	bl	str_cat
	adr	x1,s_space
	bl	str_cat
	mov	x1,x19
	bl	str_cat
	adr	x0,g_tmp
	mov	x1,0
	bl	set_status
	ldr	x19,[sp],16
	epilogue

op_save_as:
	prologue
	bl	op_stop
	bl	song_dir
	adr	x1,s_default_fms
	adr	x2,g_path
	ldrb	w3,[x2]
	cbz	w3,.d
	stp	x0,xzr,[sp,-16]!
	mov	x0,x2
	bl	base_name
	mov	x1,x0
	ldp	x0,x2,[sp],16
.d:	adr	x2,g_dlgpath
	mov	x3,256
	bl	ui_file_save
	cbz	x0,.o
	adr	x0,g_dlgpath
	adr	x1,s_ext_fms
	bl	ensure_ext
	adr	x0,g_dlgpath
	bl	write_song
	tbz	x0,63,.ok
	adr	x0,s_cant_save
	adr	x1,g_dlgpath
	bl	status_path
	b	.o
.ok:	adr	x0,g_path
	adr	x1,g_dlgpath
	mov	x2,256
	bl	str_copy
	stv	xzr,g_modified
	bl	title_status
.o:	epilogue

op_save:
	prologue
	adr	x0,g_path
	ldrb	w0,[x0]
	cbnz	w0,.go
	bl	op_save_as
	epilogue
.go:	adr	x0,g_path
	bl	write_song
	tbz	x0,63,.ok
	adr	x0,s_cant_save
	adr	x1,g_path
	bl	status_path
	epilogue
.ok:	stv	xzr,g_modified
	bl	title_status
	epilogue

; Fichier > Exporter en WAV : le morceau tel qu'AudioKit le joue (ak_dec_open
; lit le FM Song, enregistre pour l'occasion dans un fichier du moment)
op_export_wav:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	sub	sp,sp,AKI_SIZE
	bl	op_stop
	adr	x0,g_tmp			; le nom propose
	adr	x1,s_default_fms
	adr	x2,g_path
	ldrb	w3,[x2]
	cbz	w3,.nm
	mov	x0,x2
	bl	base_name
	mov	x1,x0
	adr	x0,g_tmp
.nm:	mov	x2,64
	bl	str_copy
	adr	x0,g_tmp
	bl	str_len
	cmp	x0,4
	b.ls	.ext
	adr	x1,g_tmp
	sub	x2,x0,4
	ldrb	w3,[x1,x2]
	cmp	w3,'.'
	b.ne	.ext
	mov	x0,x2
.ext:	adr	x1,g_tmp
	add	x0,x1,x0
	adr	x1,s_ext_wav
	mov	x2,5
	bl	str_copy
	bl	song_dir
	adr	x1,g_tmp
	adr	x2,g_dlgpath
	mov	x3,256
	bl	ui_file_save
	cbz	x0,.out
	adr	x0,g_dlgpath
	adr	x1,s_ext_wav
	bl	ensure_ext
	adr	x0,s_tmp_dir
	bl	f_mkdir
	adr	x0,s_tmp_fms
	bl	write_song
	tbnz	x0,63,.bad
	adr	x0,s_exporting
	adr	x1,g_dlgpath
	bl	status_path
	bl	flush
	adr	x0,s_tmp_fms
	bl	ak_dec_open
	tbnz	x0,63,.badt
	mov	x19,x0				; decodeur
	mov	x1,sp
	bl	ak_dec_info
	adr	x0,g_dlgpath
	mov	x1,AKF_WAV
	ldr	x2,[sp,AKI_RATE]
	ldr	x3,[sp,AKI_CHANNELS]
	mov	x4,0
	bl	ak_enc_open
	tbnz	x0,63,.badc
	mov	x20,x0				; encodeur
.rd:	mov	x0,x19
	adr	x1,g_pcm
	mov	x2,PCM_FRAMES
	bl	ak_dec_read
	cmp	x0,0
	b.le	.end
	mov	x2,x0
	mov	x0,x20
	adr	x1,g_pcm
	bl	ak_enc_write
	tbz	x0,63,.rd
	mov	x21,x0
	mov	x0,x20
	bl	ak_enc_close
	b	.badc
.end:	mov	x0,x20
	bl	ak_enc_close
	mov	x21,x0
	mov	x0,x19
	bl	ak_dec_close
	adr	x0,s_tmp_fms
	bl	f_unlink
	tbnz	x21,63,.bad
	adr	x0,s_exported
	adr	x1,g_dlgpath
	bl	status_path
	b	.out
.badc:	mov	x0,x19
	bl	ak_dec_close
.badt:	adr	x0,s_tmp_fms
	bl	f_unlink
.bad:	adr	x0,s_cant_export
	adr	x1,g_dlgpath
	bl	status_path
.out:	add	sp,sp,AKI_SIZE
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = id d'une commande (menu, raccourci, bouton)
on_command:
	macro cmd id*, fn* {
	cmp	x0,id
	b.eq	fn
	}
	cmd	ID_NEW,op_new
	cmd	ID_OPEN,op_open
	cmd	ID_SAVE,op_save
	cmd	ID_SAVEAS,op_save_as
	cmd	ID_EXPORT,op_export_wav
	cmd	ID_INFO,op_info
	cmd	ID_UNDO,op_undo
	cmd	ID_REDO,op_redo
	cmd	ID_CUT,op_cut
	cmd	ID_COPY,op_copy
	cmd	ID_PASTE,op_paste
	cmd	ID_ALL,op_select_all
	cmd	ID_INSROW,op_insert_row
	cmd	ID_DELROW,op_delete_row
	cmd	ID_CLRCH,op_clear_channel
	cmd	ID_UP1,op_up1
	cmd	ID_DOWN1,op_down1
	cmd	ID_UP12,op_up12
	cmd	ID_DOWN12,op_down12
	cmd	ID_PREV,op_prev_pattern
	cmd	ID_NEXT,op_next_pattern
	cmd	ID_NEWPAT,op_new_pattern
	cmd	ID_DUPPAT,op_dup_pattern
	cmd	ID_PATUP,op_pattern_up
	cmd	ID_PATDOWN,op_pattern_down
	cmd	ID_DELPAT,op_delete_pattern
	cmd	ID_INSTR,op_instrument
	cmd	ID_MUTE,op_mute
	cmd	ID_SOLO,op_solo
	cmd	ID_PLAY,op_play
	cmd	ID_START,op_play_start
	cmd	ID_LOOP,op_loop
	cmd	ID_FOLLOW,op_follow
	cmd	ID_STOP,op_stop
	ret
