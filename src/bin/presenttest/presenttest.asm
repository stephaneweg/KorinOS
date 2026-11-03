; ============================================================================
;  presenttest - essai du present asynchrone (ws_ext.inc, docs/ABI.md) :
;  temps d'un WS_OP_UPDATE (requete, attend Elegant) et d'un ui_update (sans
;  l'attendre), images par WEV_FRAME, attente de ui_event bornee.
;  Sortie : les temps, puis « presenttest : PASS / FAIL ».
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

PT_W		= 400
PT_H		= 300
PT_CALLS	= 2000			; mises a jour par phase
PT_FRAME_MS	= 2000			; duree de la phase WEV_FRAME
PT_WAIT_MS	= 100			; attente de ui_event

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'

start:
	bl	kits_open
	adr	x0,ui_win
	bl	ui_init
	mov	x0,PT_W
	mov	x1,PT_H
	adr	x2,s_title
	bl	ui_window
	tbnz	x0,63,.nowin
	mov	x19,0				; echecs
	bl	ui_update_all

	; --- ancien chemin : une requete par mise a jour ------------------
	sys	SYS_TIME_MS
	mov	x20,x0
	mov	x21,PT_CALLS
.old:	adr	x9,msg
	mov	x0,WS_OP_UPDATE
	adr	x1,ui_win
	ldr	x1,[x1]
	stp	x0,x1,[x9,WM_OP]
	stp	xzr,xzr,[x9,WM_B]
	mov	x0,PT_W
	str	x0,[x9,WM_D]
	mov	x0,PT_H
	str	x0,[x9,WM_DATA]
	mov	x0,x9
	sys	SYS_WS_CALL
	subs	x21,x21,1
	b.ne	.old
	sys	SYS_TIME_MS
	sub	x0,x0,x20
	adr	x1,m_old
	bl	report

	; --- ui_update : sans requete -------------------------------------
	sys	SYS_TIME_MS
	mov	x20,x0
	mov	x21,PT_CALLS
.new:	mov	x0,0
	mov	x1,0
	mov	x2,PT_W
	mov	x3,PT_H
	bl	ui_update
	subs	x21,x21,1
	b.ne	.new
	sys	SYS_TIME_MS
	sub	x0,x0,x20
	adr	x1,m_new
	bl	report

	; --- images cadencees par WEV_FRAME --------------------------------
	sys	SYS_TIME_MS
	mov	x20,x0
	mov	x21,0				; images
.frame:	bl	ui_frame_request
	cbz	x0,.lost
	mov	x0,0
	mov	x1,0
	mov	x2,PT_W
	mov	x3,PT_H
	lsl	w4,w21,8			; une image differente
	bl	ui_fill
	mov	x0,0
	mov	x1,0
	mov	x2,PT_W
	mov	x3,PT_H
	bl	ui_update
.wait:	mov	x0,500
	bl	ui_event
	cbz	x0,.lost			; aucun evenement : perdu
	cmp	w0,WEV_FRAME
	b.ne	.wait
	add	x21,x21,1
	sys	SYS_TIME_MS
	sub	x0,x0,x20
	cmp	x0,PT_FRAME_MS
	b.lo	.frame
	adr	x0,m_frames
	bl	puts
	mov	x0,x21
	bl	print_dec
	adr	x0,m_in
	bl	puts
	sys	SYS_TIME_MS
	sub	x0,x0,x20
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	cmp	x21,10
	b.hs	.bound
	add	x19,x19,1
	b	.bound
.lost:	adr	x0,m_lost
	bl	puts
	add	x19,x19,1

	; --- attente bornee : ni trop courte, ni par sondage rate ---------
.bound:	mov	x21,0
.drain:	mov	x0,0				; (les evenements en file)
	bl	ui_event
	cbz	x0,.empty
	add	x21,x21,1
	cmp	x21,256
	b.lo	.drain
.empty:	sys	SYS_TIME_MS
	mov	x20,x0
	mov	x0,PT_WAIT_MS
	bl	ui_event
	mov	x21,x0
	sys	SYS_TIME_MS
	sub	x20,x0,x20
	adr	x0,m_wait
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_ms
	bl	puts
	cbnz	x21,.ok				; (un evenement arrive entre-temps)
	cmp	x20,PT_WAIT_MS-1
	b.hs	.ok
	add	x19,x19,1
.ok:	adr	x0,m_pass
	cbz	x19,.end
	adr	x0,m_fail
.end:	bl	puts
	bl	ui_close
	mov	x0,x19
	sys	SYS_EXIT
.nowin:	adr	x0,m_nowin
	bl	puts
	mov	x0,1
	sys	SYS_EXIT

; x0 = ms pour PT_CALLS appels, x1 = libelle : les affiche, en us par appel
report:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x0,x1
	bl	puts
	mov	x0,1000
	mul	x0,x19,x0
	mov	x1,PT_CALLS
	udiv	x0,x0,x1
	bl	print_dec
	adr	x0,m_us
	bl	puts
	ldp	x19,x20,[sp],16
	epilogue

segment readable writeable
kit_data
s_title		db 'presenttest',0
m_old		db 'presenttest : WS_OP_UPDATE ',0
m_new		db 'presenttest : ui_update ',0
m_us		db ' us par appel',10,0
m_frames	db 'presenttest : WEV_FRAME ',0
m_in		db ' images en ',0
m_ms		db ' ms',10,0
m_wait		db 'presenttest : ui_event(100) rendu en ',0
m_lost		db 'presenttest : WEV_FRAME perdu',10,0
m_pass		db 'presenttest : PASS',10,0
m_fail		db 'presenttest : FAIL',10,0
m_nowin		db 'presenttest : pas de fen',0xC3,0xAA,'tre (Elegant ?)',10,0
		align 8
msg		rb WS_MSG
ui_data
