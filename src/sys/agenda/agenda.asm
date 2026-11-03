; ============================================================================
;  agenda - l'applet du bureau (apps/agenda d'Onyx), lance par la session
;  (SD0:/etc/session) : les prochains rendez-vous du Calendrier, aujourd'hui
;  d'abord, sur le fond d'ecran, sous toutes les fenetres. Il lit les fichiers
;  que le Calendrier ecrit (src/apps/calendar/agendafiles.h, ceux d'Onyx) :
;    SD0:/apps/calendar.app/agenda.txt		« AAAAMMJJ|[H:MM ]titre » (Latin-1)
;    SD0:/apps/calendar.app/reminders.txt	« AAAAMMJJHHMM|texte » : les rappels,
;						envoyes a notifyd quand leur minute vient
;  Service « agenda » : le Calendrier sait qu'il n'a pas a envoyer les rappels
;  lui-meme ; op 1 : ses fichiers ont change, relire. Relu aussi a chaque minute.
;  Un clic sur une ligne ouvre le Calendrier a ce jour. Sa place est fixe, en haut
;  a gauche, sous la barre de menus.
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'

CW		= 344
HDR		= 34				; le titre, son trait
ROW		= 24
NROWS		= 6
CH		= HDR + 4 + NROWS * ROW + 8
MAXEV		= 64
NOTE		= 152				; titre converti en UTF-8
EV_KEY		= 0
EV_NOTE		= 8
EV_SIZE		= EV_NOTE + NOTE
NOTE_X		= 112
FBUF		= 16384
ALPHA		= 0x28000000			; le fond laisse voir un peu le bureau

segment readable executable
kit_use '../../kits/appkit/appkit.abi'
kit_use '../../kits/uikit/uikit.abi'
include '../../inc/notify.inc'
include '../../inc/applet.inc'

start:
	bl	kits_open
	adr	x0,s_svc
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit			; (un autre agenda tourne)
	stv	x0,g_svc
	adr	x0,ui_win
	bl	ui_init
	bl	ui_jetons
	stv	x0,g_tok
	bl	reload
	bl	reminders
	mov	x0,APPLET_X
	mov	x1,APPLET_Y
	mov	x2,CW
	mov	x3,CH
	adr	x4,s_title
	bl	tr_x4
	mov	x5,WF_FRAMELESS or WF_BACK or WF_NOFOCUS or WF_NOLIST or WF_ALPHA
	bl	ui_window_ex
	tbnz	x0,63,quit
	stv	x0,g_win
	bl	draw
.loop:	ldv	x0,g_svc			; le Calendrier a enregistre ?
	adr	x1,msg
	mov	x2,0
	sys	SYS_SVC_NEXT
	cmp	x0,0
	b.le	.ev
	mov	x19,x0
	adr	x1,msg
	str	xzr,[x1]
	sys	SYS_SVC_REPLY
	mov	x0,x19
	bl	refresh
.ev:	mov	x0,1000
	bl	ui_event
	cbz	x0,.idle
	cmp	x0,WEV_MOUSE
	b.ne	.loop
	bl	on_mouse
	b	.loop
.idle:	mov	x0,2				; une nouvelle minute ?
	sys	SYS_CLOCK_GET
	mov	x1,60
	udiv	x0,x0,x1
	ldv	x1,g_minute
	cmp	x0,x1
	b.eq	.loop
	stv	x0,g_minute
	bl	refresh
	bl	reminders
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

; relit agenda.txt ; redessine s'il change
refresh:
	prologue
	bl	reload
	cbz	x0,.out
	bl	draw
.out:	epilogue

; ---------------------------------------------------------------------------
;  Dates
; ---------------------------------------------------------------------------

; x0, x1, x2 = annee, mois, jour -> x0 = jours depuis 1970 (H. Hinnant)
days_from_civil:
	cmp	x1,2
	cset	x3,ls
	sub	x0,x0,x3			; y -= m <= 2
	mov	x4,400
	udiv	x5,x0,x4			; era
	msub	x6,x5,x4,x0			; yoe
	mov	x3,-3
	cmp	x1,2
	b.hi	.m
	mov	x3,9
.m:	add	x3,x3,x1
	mov	x4,153
	mul	x3,x3,x4
	add	x3,x3,2
	mov	x4,5
	udiv	x3,x3,x4
	add	x3,x3,x2
	sub	x3,x3,1				; doy
	mov	x4,365
	mul	x7,x6,x4
	add	x7,x7,x6,lsr 2
	mov	x4,100
	udiv	x4,x6,x4
	sub	x7,x7,x4
	add	x7,x7,x3			; doe
	movl	x4,146097
	mul	x0,x5,x4
	add	x0,x0,x7
	movl	x4,719468
	sub	x0,x0,x4
	ret

; x0 = jours depuis 1970 -> x0, x1, x2 = annee, mois, jour
civil_from_days:
	movl	x1,719468
	add	x0,x0,x1
	movl	x1,146097
	udiv	x2,x0,x1			; era
	msub	x3,x2,x1,x0			; doe
	movl	x1,1460
	udiv	x4,x3,x1
	sub	x4,x3,x4
	movl	x1,36524
	udiv	x5,x3,x1
	add	x4,x4,x5
	movl	x1,146096
	udiv	x5,x3,x1
	sub	x4,x4,x5
	mov	x1,365
	udiv	x4,x4,x1			; yoe
	mov	x1,400
	mul	x5,x2,x1
	add	x5,x5,x4			; y
	mov	x1,365
	mul	x6,x4,x1
	add	x6,x6,x4,lsr 2
	mov	x1,100
	udiv	x7,x4,x1
	sub	x6,x6,x7
	sub	x3,x3,x6			; doy
	mov	x1,5
	mul	x6,x3,x1
	add	x6,x6,2
	mov	x1,153
	udiv	x6,x6,x1			; mp
	mul	x7,x6,x1
	add	x7,x7,2
	mov	x1,5
	udiv	x7,x7,x1
	sub	x2,x3,x7
	add	x2,x2,1				; d
	mov	x1,-9
	cmp	x6,10
	b.hs	.m
	mov	x1,3
.m:	add	x1,x1,x6			; m
	cmp	x1,2
	cinc	x5,x5,ls
	mov	x0,x5
	ret

; -> x0 = aujourd'hui : AAAAMMJJ
today_key:
	prologue
	mov	x0,2
	sys	SYS_CLOCK_GET
	movl	x1,86400
	udiv	x0,x0,x1
	bl	civil_from_days
	movl	x3,10000
	mul	x0,x0,x3
	mov	x3,100
	mul	x1,x1,x3
	add	x0,x0,x1
	add	x0,x0,x2
	epilogue

; x0 = texte, x1 = chiffres -> x0 = valeur (-1 : pas un chiffre)
digits:
	mov	x2,0
	mov	x3,10
.c:	cbz	x1,.ok
	ldrb	w4,[x0],1
	sub	w4,w4,'0'
	cmp	w4,9
	b.hi	.no
	mul	x2,x2,x3
	add	x2,x2,x4
	sub	x1,x1,1
	b	.c
.ok:	mov	x0,x2
	ret
.no:	mov	x0,-1
	ret

; x0 = destination, x1 = source Latin-1 (ou UTF-8), x2 = fin de la source, x3 =
; place : en UTF-8, termine par 0
to_utf8:
	sub	x3,x3,3
	add	x3,x3,x0			; x3 : limite
.c:	cmp	x1,x2
	b.hs	.end
	cmp	x0,x3
	b.hs	.end
	ldrb	w4,[x1],1
	cmp	w4,13
	b.eq	.c
	cmp	w4,0x80
	b.hs	.hi
	strb	w4,[x0],1
	b	.c
.hi:	and	w5,w4,0xE0			; (deja de l'UTF-8 : garde)
	cmp	w5,0xC0
	b.ne	.enc
	cmp	x1,x2
	b.hs	.enc
	ldrb	w5,[x1]
	and	w6,w5,0xC0
	cmp	w6,0x80
	b.ne	.enc
	add	x1,x1,1
	strb	w4,[x0],1
	strb	w5,[x0],1
	b	.c
.enc:	lsr	w5,w4,6
	orr	w5,w5,0xC0
	strb	w5,[x0],1
	and	w5,w4,0x3F
	orr	w5,w5,0x80
	strb	w5,[x0],1
	b	.c
.end:	strb	wzr,[x0]
	ret

; ---------------------------------------------------------------------------
;  Fichiers
; ---------------------------------------------------------------------------

; relit agenda.txt -> x0 = 1 si la liste change (ou le jour)
reload:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,s_agenda
	bl	read_file
	mov	x19,x0				; octets
	bl	today_key
	mov	x23,x0
	adr	x1,fbuf				; empreinte
	mov	x2,0
	mov	x3,x19
	movl	w4,16777619
	movl	w5,2166136261
.h:	cbz	x3,.hd
	ldrb	w6,[x1,x2]
	eor	w5,w5,w6
	mul	w5,w5,w4
	add	x2,x2,1
	sub	x3,x3,1
	b	.h
.hd:	ldv	x6,g_sig
	ldv	x7,g_today
	cmp	x6,x5
	b.ne	.parse
	cmp	x7,x23
	b.ne	.parse
	mov	x0,0
	b	.out
.parse:	stv	x5,g_sig
	stv	x23,g_today
	stv	xzr,g_nev
	adr	x20,fbuf
	add	x21,x20,x19			; fin
.line:	cmp	x20,x21
	b.hs	.sort
	mov	x22,x20				; debut de ligne
.e:	cmp	x20,x21
	b.hs	.le
	ldrb	w0,[x20]
	cmp	w0,10
	b.eq	.le
	add	x20,x20,1
	b	.e
.le:	mov	x24,x20				; fin de ligne
	add	x20,x20,1
	sub	x0,x24,x22
	cmp	x0,10
	b.lo	.line
	ldrb	w0,[x22,8]
	cmp	w0,'|'
	b.ne	.line
	mov	x0,x22
	mov	x1,8
	bl	digits
	tbnz	x0,63,.line
	cmp	x0,x23				; (les jours passes : non)
	b.lo	.line
	ldv	x1,g_nev
	cmp	x1,MAXEV
	b.hs	.line
	mov	x9,x0
	mov	x2,EV_SIZE
	mul	x2,x1,x2
	adr	x3,g_ev
	add	x3,x3,x2
	str	x9,[x3,EV_KEY]
	add	x1,x1,1
	stv	x1,g_nev
	add	x0,x3,EV_NOTE
	add	x1,x22,9
	mov	x2,x24
	mov	x3,NOTE
	bl	to_utf8
	b	.line
.sort:	mov	x19,1				; par date (insertion)
.s:	ldv	x0,g_nev
	cmp	x19,x0
	b.hs	.done
	mov	x20,x19
.j:	cbz	x20,.sn
	mov	x2,EV_SIZE
	mul	x0,x20,x2
	adr	x1,g_ev
	add	x0,x0,x1			; g_ev[j]
	sub	x1,x0,x2			; g_ev[j-1]
	ldr	x3,[x0]
	ldr	x4,[x1]
	cmp	x4,x3
	b.ls	.sn
	stp	x0,x1,[sp,-16]!
	adr	x2,tmp_ev
	mov	x3,x0
	mov	x0,x2
	mov	x1,x3
	mov	x2,EV_SIZE
	bl	mem_move
	ldp	x0,x1,[sp]
	mov	x2,EV_SIZE
	bl	mem_move
	ldp	x0,x1,[sp],16
	mov	x0,x1
	adr	x1,tmp_ev
	mov	x2,EV_SIZE
	bl	mem_move
	sub	x20,x20,1
	b	.j
.sn:	add	x19,x19,1
	b	.s
.done:	mov	x0,1
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; les rappels dont la minute vient de passer : des notifications
reminders:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x0,2
	sys	SYS_CLOCK_GET
	mov	x1,60
	udiv	x23,x0,x1			; minutes maintenant
	ldv	x24,g_last
	stv	x23,g_last
	tbnz	x24,63,.out			; (au lancement : le passe reste passe)
	cmp	x23,x24
	b.eq	.out
	adr	x0,s_remind
	bl	read_file
	adr	x20,fbuf
	add	x21,x20,x0
.line:	cmp	x20,x21
	b.hs	.out
	mov	x22,x20
.e:	cmp	x20,x21
	b.hs	.le
	ldrb	w0,[x20]
	cmp	w0,10
	b.eq	.le
	add	x20,x20,1
	b	.e
.le:	mov	x25,x20
	add	x20,x20,1
	sub	x0,x25,x22
	cmp	x0,14
	b.lo	.line
	ldrb	w0,[x22,12]
	cmp	w0,'|'
	b.ne	.line
	mov	x0,x22				; AAAA
	mov	x1,4
	bl	digits
	tbnz	x0,63,.line
	mov	x19,x0
	add	x0,x22,4			; MM
	mov	x1,2
	bl	digits
	mov	x26,x0
	add	x0,x22,6			; JJ
	mov	x1,2
	bl	digits
	mov	x2,x0
	mov	x0,x19
	mov	x1,x26
	bl	days_from_civil
	movl	x1,1440
	mul	x19,x0,x1
	add	x0,x22,8			; HH
	mov	x1,2
	bl	digits
	mov	x1,60
	mul	x0,x0,x1
	add	x19,x19,x0
	add	x0,x22,10			; MM
	mov	x1,2
	bl	digits
	add	x19,x19,x0
	cmp	x19,x24
	b.ls	.line
	cmp	x19,x23
	b.hi	.line
	adr	x0,textbuf
	add	x1,x22,13
	mov	x2,x25
	mov	x3,300
	bl	to_utf8
	adr	x0,s_cal
	bl	tr_x0
	adr	x1,textbuf
	bl	notifier
	b	.line
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Souris
; ---------------------------------------------------------------------------

on_mouse:
	prologue
	adr	x9,ui_ev
	ldr	w0,[x9,EV_KIND]
	ldrsw	x1,[x9,EV_X]
	ldrsw	x2,[x9,EV_Y]
	cmp	w2,HDR+4
	mov	x3,-1				; ligne
	b.lt	.r
	sub	x3,x2,HDR+4
	mov	x4,ROW
	udiv	x3,x3,x4
	ldv	x4,g_nev
	cmp	x3,x4
	mov	x4,-1
	csel	x3,x4,x3,hs
	cmp	x3,NROWS
	csel	x3,x4,x3,hs
.r:	cmp	w0,1
	b.eq	.press
	cmp	w0,2
	b.eq	.out
	ldv	x4,g_hot
	cmp	x3,x4
	b.eq	.out
	stv	x3,g_hot
	bl	draw
	b	.out
.press:	tbnz	x3,63,.out
	mov	x0,EV_SIZE
	mul	x0,x3,x0
	adr	x1,g_ev
	ldr	x0,[x1,x0]			; le Calendrier, a ce jour
	adr	x1,textbuf
	bl	fmt_dec
	strb	wzr,[x0]
	adr	x0,p_calendar
	adr	x1,textbuf
	sys	SYS_SPAWN
.out:	epilogue

; ---------------------------------------------------------------------------
;  Dessin
; ---------------------------------------------------------------------------

; x0 = jours, -> x0 = 0 lundi
wday_of:
	add	x0,x0,3
	mov	x1,7
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	ret

; x0 = cle AAAAMMJJ, x1 = destination : « Mar 29 sept. » (l'annee si ce n'est
; pas celle-ci)
fmt_date:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x1
	movl	x1,10000
	udiv	x20,x0,x1			; a
	msub	x0,x20,x1,x0
	mov	x1,100
	udiv	x21,x0,x1			; m
	msub	x22,x21,x1,x0			; j
	mov	x0,x20
	mov	x1,x21
	mov	x2,x22
	bl	days_from_civil
	bl	wday_of
	adr	x1,s_days
	add	x1,x1,x0,lsl 2
	bl	tr_x1
	mov	x0,x19
	bl	str_cat
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x0
	mov	x0,x22
	bl	fmt_dec
	mov	w1,' '
	strb	w1,[x0],1
	adr	x1,s_months
	sub	x2,x21,1
	lsl	x2,x2,3
	add	x1,x1,x2
	bl	tr_x1
	bl	str_cat
	ldv	x1,g_today
	movl	x2,10000
	udiv	x1,x1,x2
	cmp	x1,x20
	b.eq	.nul
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x0
	mov	x0,x20
	bl	fmt_dec
.nul:	strb	wzr,[x0]
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte, x1 = x, x2 = y (ligne de base), w3, x4 = police, x5 = largeur max
text13:
	mov	x6,x5
	mov	x5,x4
	mov	x4,13
	b	ui_text_fit

draw:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	ldv	x21,g_tok
	mov	x0,0				; (tout transparent)
	mov	x1,0
	mov	x2,CW
	mov	x3,CH
	movl	w4,0xFF000000
	bl	ui_fill
	ldr	w5,[x21,TJ_SURFACE]		; la carte
	and	w5,w5,0xFFFFFF
	movl	w0,ALPHA
	orr	w5,w5,w0
	mov	w6,w5
	mov	x0,0
	mov	x1,0
	mov	x2,CW
	mov	x3,CH
	mov	x4,10
	bl	ui_rbox
	mov	x0,12				; une petite page de calendrier
	mov	x1,9
	mov	x2,16
	mov	x3,17
	mov	x4,3
	movl	w5,0x00FFFFFF
	movl	w6,0x00E8E8E8
	bl	ui_rbox
	mov	x0,12
	mov	x1,9
	mov	x2,16
	mov	x3,6
	mov	x4,3
	movl	w5,0x00D23A30
	mov	w6,w5
	bl	ui_rbox
	adr	x0,s_title
	bl	tr_x0
	mov	x1,36
	mov	x2,23
	ldr	w3,[x21,TJ_TEXTE1]
	mov	x4,14
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	mov	x19,x0				; (n)
	ldv	x0,g_nev
	cbz	x0,.line
	adr	x20,textbuf
	mov	w1,'('
	strb	w1,[x20],1
	mov	x1,x20
	bl	fmt_dec
	mov	w1,')'
	strb	w1,[x0],1
	strb	wzr,[x0]
	adr	x0,textbuf
	add	x1,x19,6
	mov	x2,23
	ldr	w3,[x21,TJ_TEXTE2]
	mov	x4,13
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
.line:	mov	x0,10
	mov	x1,33
	mov	x2,CW-20
	mov	x3,1
	ldr	w4,[x21,TJ_LIGNE_FORTE]
	bl	ui_fill
	ldv	x0,g_nev
	cbnz	x0,.rows
	adr	x0,s_none
	bl	tr_x0
	mov	x1,14
	mov	x2,HDR+24
	ldr	w3,[x21,TJ_TEXTE2]
	adr	x4,ui_font
	mov	x5,CW-28
	bl	text13
	b	.show
.rows:	mov	x19,0
.r:	ldv	x0,g_nev
	cmp	x19,x0
	b.hs	.more
	cmp	x19,NROWS
	b.hs	.more
	mov	x0,ROW
	mul	x22,x19,x0
	add	x22,x22,HDR+4			; y
	mov	x0,EV_SIZE
	mul	x0,x19,x0
	adr	x1,g_ev
	add	x20,x0,x1			; evenement
	ldv	x0,g_hot
	cmp	x0,x19
	b.ne	.nh
	ldr	w0,[x21,TJ_SURFACE]
	ldr	w1,[x21,TJ_TEXTE1]
	mov	x2,28
	bl	ui_mix
	mov	w5,w0
	mov	w6,w0
	mov	x0,6
	mov	x1,x22
	mov	x2,CW-12
	mov	x3,ROW
	mov	x4,6
	bl	ui_rbox
.nh:	ldr	x0,[x20,EV_KEY]
	ldv	x1,g_today
	cmp	x0,x1
	b.ne	.day
	mov	x0,12				; aujourd'hui : une pastille
	add	x1,x22,3
	mov	x2,NOTE_X-20
	mov	x3,ROW-6
	mov	x4,6
	ldr	w5,[x21,TJ_ACCENT]
	mov	w6,w5
	bl	ui_rbox
	ldr	w0,[x21,TJ_ACCENT]
	bl	ui_ink
	mov	w3,w0
	adr	x0,s_today
	bl	tr_x0
	mov	x1,20
	add	x2,x22,17
	mov	x4,12
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	adr	x4,ui_font_b
	b	.note
.day:	adr	x1,textbuf
	bl	fmt_date
	adr	x0,textbuf
	mov	x1,14
	add	x2,x22,17
	ldr	w3,[x21,TJ_TEXTE2]
	mov	x4,12
	adr	x5,ui_font
	mov	x6,NOTE_X-18
	bl	ui_text_fit
	adr	x4,ui_font
.note:	add	x0,x20,EV_NOTE
	mov	x1,NOTE_X
	add	x2,x22,17
	ldr	w3,[x21,TJ_TEXTE1]
	mov	x5,CW-NOTE_X-14
	bl	text13
	add	x19,x19,1
	b	.r
.more:	ldv	x0,g_nev
	cmp	x0,NROWS
	b.ls	.show
	adr	x0,s_dots
	mov	x1,CW-24
	mov	x2,CH-2
	ldr	w3,[x21,TJ_TEXTE2]
	adr	x4,ui_font
	mov	x5,20
	bl	text13
.show:	bl	ui_update_all
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

s_svc		db 'agenda',0
s_title		db 'Prochains rendez-vous',0
s_none		db 'Aucun rendez-vous ',0xC3,0xA0,' venir.',0
s_today		db 'Aujourd',0x27,'hui',0
s_dots		db 0xE2,0x80,0xA6,0
s_cal		db 'Calendrier',0
s_agenda	db 'SD0:/apps/calendar.app/agenda.txt',0
s_remind	db 'SD0:/apps/calendar.app/reminders.txt',0
p_calendar	db 'SD0:/apps/calendar.app/main',0
s_days		db 'Lun',0,'Mar',0,'Mer',0,'Jeu',0,'Ven',0,'Sam',0,'Dim',0
s_months	db 'janv.',0,0,0,'f',0xC3,0xA9,'vr.',0,0,'mars',0,0,0,0,'avr.',0,0,0,0
		db 'mai',0,0,0,0,0,'juin',0,0,0,0,'juil.',0,0,0,'ao',0xC3,0xBB,'t',0,0,0
		db 'sept.',0,0,0,'oct.',0,0,0,0,'nov.',0,0,0,0,'d',0xC3,0xA9,'c.',0,0,0
		align 8

include '../../inc/tr.inc'

segment readable writeable
kit_data
		align 8
g_svc		dd -1
g_tok		dd 0
g_win		dd 0
g_nev		dd 0
g_today		dd 0
g_sig		dd 0
g_hot		dd -1
g_last		dd -1				; minutes : derniere vue
g_minute	dd 0
msg		rb WS_MSG
textbuf		rb 320
tmp_ev		rb EV_SIZE
		align 8
g_ev		rb MAXEV * EV_SIZE
fbuf		rb FBUF
ui_data
