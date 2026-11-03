; ============================================================================
;  elegant/desktop.inc - mode « desktop » (shell=desktop) : le bureau Elegant.
;  Comme dans Onyx, le bureau est fait de programmes ordinaires, lances au
;  demarrage de la session (SD0:/etc/session, une ligne « chemin [arguments] »
;  par programme) : le fond d'ecran (voronoy), la barre de menus (menubar),
;  le dock (dock). Le serveur ne fait que le fond uni d'attente.
;  D'autres modes (PocketUI, console de jeu) auront leur propre table.
; ============================================================================

COL_DESK	= 0x002B3A4E			; avant le fond d'ecran

		align 8
mode_bureau:	dd str_mode_bureau, 0, bu_init, bu_back, bu_none, bu_no
		dd bu_no, bu_none, bu_none

str_mode_bureau	db 'desktop',0
path_session	db 'SD0:/etc/session',0
session_def	db 'SD0:/sys/voronoy',0		; sans SD0:/etc/session
		db 'SD0:/sys/menubar',0
		db 'SD0:/sys/panel',0
		db 0
		align 4

bu_no:	mov	x0,0
bu_none:
	ret

; fond uni dans le clip
bu_back:
	prologue
	adr	x4,clip_r
	ldp	x0,x1,[x4]
	ldp	x2,x3,[x4,16]
	sub	x2,x2,x0
	sub	x3,x3,x1
	movl	w4,COL_DESK
	bl	bb_fill
	epilogue

; lance la session : chaque ligne de SD0:/etc/session (# : commentaire)
bu_init:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x0,path_session
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.default
	mov	x19,x0
	adr	x1,ini_buf
	mov	x2,INI_MAX-1
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	b.le	.default
	adr	x19,ini_buf
	strb	wzr,[x19,x20]
.line:	ldrb	w0,[x19]			; debut de ligne
	cbz	w0,.out
	mov	x20,x19				; fin de ligne -> 0
.eol:	ldrb	w0,[x20]
	cbz	w0,.cut
	cmp	w0,10
	b.eq	.cut
	cmp	w0,13
	b.eq	.cut
	add	x20,x20,1
	b	.eol
.cut:	ldrb	w21,[x20]			; (le separateur, pour continuer)
	strb	wzr,[x20]
	mov	x0,x19
	bl	session_run
	cbz	w21,.out
	add	x19,x20,1
	b	.line
.default:
	adr	x19,session_def
.d:	ldrb	w0,[x19]
	cbz	w0,.out
	mov	x0,x19
	bl	session_run
	mov	x0,x19
	bl	str_len
	add	x19,x19,x0
	add	x19,x19,1
	b	.d
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = ligne « chemin [arguments] » (modifiee) : lance le programme
session_run:
	prologue
	ldrb	w1,[x0]
	cbz	w1,.out				; vide
	cmp	w1,'#'
	b.eq	.out				; commentaire
	mov	x1,0				; arguments apres le premier espace
	mov	x2,x0
.sp:	ldrb	w3,[x2]
	cbz	w3,.go
	cmp	w3,' '
	b.eq	.split
	add	x2,x2,1
	b	.sp
.split:	strb	wzr,[x2]
	add	x1,x2,1
.go:	sys	SYS_SPAWN
.out:	epilogue
