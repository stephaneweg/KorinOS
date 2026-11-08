; ============================================================================
;  clockd - le service des alarmes de l'Horloge (clockd d'Onyx), lance par la
;  session du bureau (SD0:/etc/session). Il lit SD0:/etc/alarms.txt (le format
;  d'Onyx : src/inc/alarms.inc), et sonne chaque alarme a sa minute : une
;  notification (notifyd), une carte au-dessus des fenetres (Repeter, Arreter),
;  un son en boucle sur les voix FM d'AudioKit. Sans reponse en 2 minutes : le
;  son s'arrete, « Alarme manquee » est notifiee et notee dans le fichier.
;
;  Sans sondage : il dort (bus_wait) jusqu'a la minute de la prochaine alarme,
;  une heure au plus (l'heure reglee par NTP ou le fuseau) ; le sujet
;  korin.clock.alarms (publie par l'Horloge apres chaque changement) le
;  reveille pour relire le fichier. Sans busd, il le relit toutes les 30 s.
;
;  Comme le Ringer d'Onyx : chaque minute d'alarme sonne une fois, avec 2
;  minutes de retard au plus ; le passe reste passe au lancement ; un recul de
;  l'heure de moins de 2 h (heure d'ete) ne fait rien sonner deux fois. Au
;  premier tour de confiance, les alarmes d'une fois manquees pendant que le
;  Pi etait eteint (12 h au plus) sont notifiees, sans son.
;
;	clockd [--grace N]	N : secondes apres le demarrage avant toute
;				sonnerie (l'heure n'est sure qu'apres NTP ;
;				60 par defaut, 0 pour les essais)
; ============================================================================
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'
include '../../kits/audiokit/audiokit.inc'
include '../../kits/buskit/buskit.inc'

RING_W		= 360
RING_H		= 236
RING_MS		= 120000			; sans reponse : 2 minutes
GRACE_DEF	= 60				; secondes
WAIT_MAX	= 3600000			; une heure au plus sans regarder l'heure
WAIT_NOBUS	= 30000				; sans busd : le fichier relu
MISSED_BACK	= 720				; minutes : 12 heures
QMAX		= 8
R_SNOOZE	= 1				; fins d'une sonnerie
R_STOP		= 2
R_MISSED	= 3
BTN_Y		= 182
BTN_H		= 36

segment readable executable
kit_use '../../kits/appkit/appkit.abi'			; SD0:/lib/appkit.so
kit_use '../../kits/uikit/uikit.abi'			; SD0:/lib/uikit.so
kit_use '../../kits/audiokit/audiokit.abi'		; SD0:/lib/audiokit.so
kit_use '../../kits/buskit/buskit.abi'			; SD0:/lib/buskit.so
include '../../inc/notify.inc'
include '../../inc/gamesnd.inc'
include '../../inc/alarms.inc'

start:
	mov	x19,x0				; arguments
	bl	kits_open
	adr	x0,s_svc			; un seul clockd
	mov	x1,0
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	adr	x0,ui_win
	bl	ui_init
	mov	x0,x19
	bl	parse_args
	adr	x0,al_topic			; l'Horloge a ecrit le fichier
	mov	x1,0
	bl	bus_subscribe
	cmp	x0,0
	csel	x0,x0,xzr,gt
	adr	x1,g_sub
	str	x0,[x1]
	adr	x0,s_clk_topic			; l'heure reglee : la prochaine alarme
	mov	x1,0				; recalculee tout de suite
	bl	bus_subscribe
	bl	reload
	bl	al_now				; le passe reste passe
	adr	x1,g_last
	str	x0,[x1]
.loop:	bl	step
	bl	wait_ms
	mov	x1,x0
	adr	x0,g_ev
	bl	bus_wait
	cmp	x0,WEV_BUS
	b.ne	.nobus
	adr	x0,g_ev
	ldr	w0,[x0,BEV_SUB]
	adr	x1,g_sub
	ldr	x1,[x1]
	cmp	x0,x1
	b.ne	.loop
	bl	reload
	b	.loop
.nobus:	adr	x0,g_sub			; sans busd : relu a chaque reveil
	ldr	x0,[x0]
	cbnz	x0,.loop
	bl	al_load
	b	.loop
quit:	mov	x0,0
	sys	SYS_EXIT

; x0 = arguments : --grace N (secondes)
parse_args:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x1,GRACE_DEF
	cbz	x0,.set
	mov	x19,x0
.s:	ldrb	w2,[x19]
	cbz	w2,.set
	adr	x20,s_grace			; « --grace » ici ?
	mov	x3,0
.c:	ldrb	w4,[x20,x3]
	cbz	w4,.hit
	ldrb	w5,[x19,x3]
	cmp	w4,w5
	b.ne	.n
	add	x3,x3,1
	b	.c
.n:	add	x19,x19,1
	b	.s
.hit:	add	x0,x19,x3
.sp:	ldrb	w2,[x0]
	cmp	w2,' '
	b.ne	.d
	add	x0,x0,1
	b	.sp
.d:	bl	parse_dec
	mov	x1,x0
.set:	mov	x2,1000
	mul	x1,x1,x2
	adr	x0,g_grace
	str	x1,[x0]
	ldp	x19,x20,[sp],16
	epilogue

; le fichier relu ; au journal : combien
reload:
	prologue
	bl	al_load
	adr	x1,g_line
	adr	x2,m_reload
.c:	ldrb	w3,[x2],1
	strb	w3,[x1],1
	cbnz	w3,.c
	sub	x1,x1,1
	bl	fmt_dec
	adr	x1,m_alarms
	bl	str_cat
	adr	x0,g_line
	bl	puts
	epilogue

; ---------------------------------------------------------------------------
;  Quand sonner
; ---------------------------------------------------------------------------

; -> x0 = ms a dormir : jusqu'a la prochaine minute d'alarme
wait_ms:
	prologue
	stp	x19,x20,[sp,-16]!
	sys	SYS_TIME_MS			; avant la confiance : jusqu'a elle
	adr	x1,g_grace
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.trust
	sub	x0,x1,x0
	add	x0,x0,100
	b	.cap
.trust:	bl	al_now
	mov	x19,x0				; minute
	mov	x20,x1				; secondes
	mov	x0,x19
	bl	al_next_all
	tbnz	x0,63,.max
	mov	x1,60
	mul	x0,x0,x1
	sub	x0,x0,x20			; secondes jusqu'a cette minute
	mov	x1,1000
	mul	x0,x0,x1
	add	x0,x0,500			; (juste apres son debut)
	cmp	x0,100
	b.ge	.cap
	mov	x0,100
	b	.cap
.max:	movl	x0,WAIT_MAX
.cap:	movl	x1,WAIT_MAX
	cmp	x0,x1
	csel	x0,x0,x1,lo
	adr	x1,g_sub
	ldr	x1,[x1]
	cbnz	x1,.out
	movl	x1,WAIT_NOBUS
	cmp	x0,x1
	csel	x0,x0,x1,lo
.out:	ldp	x19,x20,[sp],16
	epilogue

; un tour : ce qui doit sonner sonne
step:
	prologue
	stp	x19,x20,[sp,-16]!
	sys	SYS_TIME_MS
	adr	x1,g_grace
	ldr	x1,[x1]
	cmp	x0,x1
	b.hs	.trust
	bl	al_now				; pas encore sur de l'heure : rien
	adr	x1,g_last
	str	x0,[x1]
	b	.out
.trust:	adr	x0,g_mdone			; une fois : les manquees, Pi eteint
	ldr	x1,[x0]
	cbnz	x1,.c
	mov	x1,1
	str	x1,[x0]
	bl	missed_off
.c:	bl	collect
.q:	adr	x2,g_qn
	ldr	x3,[x2]
	cbz	x3,.out
	adr	x4,g_q				; la premiere de la file
	ldp	x19,x20,[x4]
	sub	x3,x3,1
	str	x3,[x2]
	mov	x0,x4
	add	x1,x4,16
	lsl	x2,x3,4
	bl	mem_move
	mov	x0,x19
	mov	x1,x20
	bl	ring
	b	.q
.out:	ldp	x19,x20,[sp],16
	epilogue

; les minutes d'alarme passees depuis le dernier regard : dans la file
collect:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	bl	al_now
	mov	x19,x0
	adr	x0,g_last
	ldr	x20,[x0]
	cmp	x19,x20
	b.eq	.out
	b.gt	.fwd
	sub	x1,x20,x19			; en arriere : plus de 2 h, on repart
	cmp	x1,120
	b.le	.out
	str	x19,[x0]
	b	.out
.fwd:	str	x19,[x0]
	add	x20,x20,1			; depuis : apres le dernier regard,
	sub	x1,x19,2			; 2 minutes au plus en arriere
	cmp	x20,x1
	csel	x20,x1,x20,lt
	adr	x21,al_tab
	mov	x22,0
.i:	adr	x0,al_n
	ldr	x0,[x0]
	cmp	x22,x0
	b.hs	.out
	ldrb	w0,[x21,AL_ON]
	cbz	w0,.next
	ldrb	w0,[x21,AL_VALID]
	cbz	w0,.next
	mov	x0,x21
	mov	x1,x20
	bl	al_next
	tbnz	x0,63,.snz
	cmp	x0,x19
	b.gt	.snz
	mov	x1,x0
	b	.push
.snz:	ldr	x1,[x21,AL_SNOOZE]
	cmp	x1,x20
	b.lt	.next
	cmp	x1,x19
	b.gt	.next
.push:	ldr	w0,[x21,AL_ID]
	bl	q_push
.next:	add	x22,x22,1
	add	x21,x21,AL_SIZE
	b	.i
.out:	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = id, x1 = minute : a la fin de la file (pas deux fois la meme alarme)
q_push:
	adr	x2,g_qn
	ldr	x3,[x2]
	adr	x4,g_q
	mov	x5,0
.c:	cmp	x5,x3
	b.hs	.add
	add	x6,x4,x5,lsl 4
	ldr	x6,[x6]
	cmp	x6,x0
	b.eq	.out
	add	x5,x5,1
	b	.c
.add:	cmp	x3,QMAX
	b.hs	.out
	add	x4,x4,x3,lsl 4
	stp	x0,x1,[x4]
	add	x3,x3,1
	str	x3,[x2]
.out:	ret

; Pi eteint : les alarmes d'une fois passees jusqu'a g_last. Dans les 12
; dernieres heures : notifiees « manquees » (sans son) ; plus vieilles :
; desactivees sans rien dire
missed_off:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	adr	x0,g_last
	ldr	x19,[x0]			; jusqu'a
	mov	x23,0				; change
	adr	x21,al_tab
	mov	x22,0
.i:	adr	x0,al_n
	ldr	x0,[x0]
	cmp	x22,x0
	b.hs	.end
	ldrb	w0,[x21,AL_ON]
	cbz	w0,.next
	ldrb	w0,[x21,AL_VALID]
	cbz	w0,.next
	ldrb	w0,[x21,AL_DAYS]
	ands	w0,w0,AL_DAYS_ALL
	b.ne	.next
	ldr	x0,[x21,AL_DATE]
	tbnz	x0,63,.next
	ldrb	w1,[x21,AL_HH]
	ldrb	w2,[x21,AL_MM]
	mov	x3,60
	madd	x1,x1,x3,x2
	mov	x3,1440
	madd	x20,x0,x3,x1			; sa minute
	ldr	x1,[x21,AL_SNOOZE]
	cmp	x1,x19
	b.gt	.next				; (un report encore a venir)
	cmp	x1,x20
	csel	x20,x1,x20,gt			; (un report passe aussi)
	cmp	x20,x19
	b.gt	.next
	mov	x0,-1
	str	x0,[x21,AL_SNOOZE]
	strb	wzr,[x21,AL_ON]
	mov	x23,1
	sub	x0,x19,MISSED_BACK
	cmp	x20,x0
	b.lt	.next				; (trop vieille : eteinte, sans mot)
	ldr	x0,[x21,AL_MISSED]
	cmp	x0,x20
	b.ge	.next				; (deja dite)
	str	x20,[x21,AL_MISSED]
	mov	x0,x21
	bl	say_missed
.next:	add	x22,x22,1
	add	x21,x21,AL_SIZE
	b	.i
.end:	cbz	x23,.out
	bl	al_save
	bl	al_publish
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = alarme : g_text <- « 07:00 Nom » (« Alarme » sans nom)
ring_words:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	mov	x1,x0
	adr	x0,g_text
	bl	al_fmt_hm
	mov	w1,' '
	strb	w1,[x0],1
	add	x1,x19,AL_LABEL
	ldrb	w2,[x1]
	cbnz	w2,.l
	adr	x1,s_alarm
	bl	tr_x1
.l:	bl	str_cat
	ldr	x19,[sp],16
	epilogue

; x0 = alarme : « Alarme manquee : 07:00 Nom » notifiee, et au journal
say_missed:
	prologue
	bl	ring_words
	adr	x0,s_missed
	bl	tr_x0
	mov	x1,x0
	adr	x0,g_line
	bl	str_cat
	mov	w1,' '
	strb	w1,[x0],1
	adr	x1,g_text
	bl	str_cat
	adr	x0,s_clock
	bl	tr_x0
	adr	x1,g_line
	bl	notifier
	adr	x0,g_log
	adr	x1,m_missed
	bl	str_cat
	adr	x1,g_text
	bl	str_cat
	bl	put_nl
	adr	x0,g_log
	bl	puts
	epilogue

; x0 = fin -> saut de ligne
put_nl:
	mov	w1,10
	strb	w1,[x0],1
	strb	wzr,[x0]
	ret

; ---------------------------------------------------------------------------
;  Sonner
; ---------------------------------------------------------------------------

; x0 = id, x1 = minute : la carte, le son, la notification, jusqu'a la
; reponse (ou 2 minutes) ; puis le fichier
ring:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,g_ring_id
	stp	x19,x20,[x0]
	mov	x0,x19
	bl	al_find
	cbz	x0,.out
	mov	x21,x0
	ldrb	w0,[x21,AL_SOUND]
	adr	x1,g_sound
	str	x0,[x1]
	mov	x0,x21
	bl	ring_words			; « 07:00 Nom »
	adr	x0,g_text			; (l'heure et le nom, gardes pour la carte)
	adr	x1,g_hm
	ldp	x2,x3,[x0]
	stp	x2,x3,[x1]
	strb	wzr,[x1,5]
	add	x1,x0,6
	adr	x0,g_name
	bl	str_cat
	adr	x0,s_clock			; la notification d'abord
	bl	tr_x0
	adr	x1,g_text
	bl	notifier
	adr	x0,g_log
	adr	x1,m_ring
	bl	str_cat
	adr	x1,g_text
	bl	str_cat
	bl	put_nl
	adr	x0,g_log
	bl	puts
	bl	ring_open
	bl	gs_open				; le son
	adr	x0,g_sound
	ldr	x0,[x0]
	bl	al_sound
	adr	x2,g_snd
	stp	x0,x1,[x2]
	bl	gs_seq
	sys	SYS_TIME_MS
	mov	x22,x0				; debut
	mov	x23,x0				; debut du tour
.loop:	mov	x0,20
	bl	ui_event
	cbz	x0,.tick
	cmp	x0,WEV_CLOSE
	b.eq	.stop
	cmp	x0,WEV_THEME
	b.ne	.k
	bl	ring_draw
	b	.tick
.k:	cmp	x0,WEV_KEY
	b.ne	.b
	adr	x1,ui_ev
	ldr	w2,[x1,EV_KIND]
	cbz	w2,.tick
	ldr	w2,[x1,EV_CODE]
	cmp	w2,KEY_ENTER
	b.eq	.snooze
	cmp	w2,KEY_ESC
	b.eq	.stop
	b	.tick
.b:	cmp	x0,WEV_BUS
	b.ne	.w
	adr	x0,ui_ev			; l'Horloge a change le fichier
	ldr	w0,[x0,BEV_SUB]
	adr	x1,g_sub
	ldr	x1,[x1]
	cmp	x0,x1
	b.ne	.tick
	bl	al_load
	b	.tick
.w:	mov	x0,0
	bl	ui_w_event
	cmp	x1,UN_ACTIVATE
	b.ne	.tick
	adr	x2,g_btn
	ldp	x3,x4,[x2]
	cmp	x0,x3
	b.eq	.snooze
	cmp	x0,x4
	b.eq	.stop
.tick:	bl	gs_tick
	sys	SYS_TIME_MS
	mov	x24,x0
	sub	x0,x24,x23			; un tour de plus
	adr	x1,g_snd
	ldp	x2,x3,[x1]
	cmp	x0,x3
	b.lo	.t2
	add	x23,x23,x3
	mov	x0,x2
	bl	gs_seq
.t2:	sub	x0,x24,x22
	movl	x1,RING_MS
	cmp	x0,x1
	b.hs	.missed
	bl	collect				; (d'autres alarmes : apres)
	b	.loop
.snooze:
	mov	x19,R_SNOOZE
	b	.end
.stop:	mov	x19,R_STOP
	b	.end
.missed:
	mov	x19,R_MISSED
.end:	bl	gs_close
	mov	x0,0
	bl	ui_window_close
	bl	al_load				; le fichier tel qu'il est maintenant
	adr	x0,g_ring_id
	ldr	x0,[x0]
	bl	al_find
	cbz	x0,.out
	mov	x21,x0
	mov	x0,-1
	str	x0,[x21,AL_SNOOZE]
	str	x0,[x21,AL_MISSED]
	cmp	x19,R_SNOOZE
	b.ne	.off
	bl	al_now				; repete dans snooze minutes
	adr	x1,al_snz
	ldr	x1,[x1]
	add	x0,x0,x1
	str	x0,[x21,AL_SNOOZE]
	adr	x0,g_log
	adr	x1,m_snooze
	bl	str_cat
	ldr	x1,[x21,AL_SNOOZE]
	bl	al_fmt_min_hm
	bl	put_nl
	adr	x0,g_log
	bl	puts
	b	.save
.off:	ldrb	w0,[x21,AL_DAYS]		; arretee ou manquee : une alarme
	ands	w0,w0,AL_DAYS_ALL		; d'une fois s'eteint
	b.ne	.miss
	strb	wzr,[x21,AL_ON]
.miss:	cmp	x19,R_MISSED
	b.ne	.stopped
	adr	x0,g_ring_min
	ldr	x0,[x0]
	str	x0,[x21,AL_MISSED]
	mov	x0,x21
	bl	say_missed
	b	.save
.stopped:
	adr	x0,m_stop
	bl	puts
.save:	bl	al_save
	bl	al_publish
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; la carte : au-dessus de tout, au tiers haut de l'ecran, deux boutons
ring_open:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,g_screen
	bl	ui_screen
	adr	x2,g_screen
	ldp	x0,x1,[x2]
	sub	x0,x0,RING_W
	asr	x0,x0,1
	sub	x1,x1,RING_H
	mov	x3,3
	sdiv	x1,x1,x3
	ldr	x3,[x2,24]			; (sous la barre de menus)
	cmp	x1,x3
	csel	x1,x3,x1,lt
	mov	x2,RING_W
	mov	x3,RING_H
	adr	x4,s_alarm
	bl	tr_x4
	mov	x5,WF_FRAMELESS or WF_TOP
	bl	ui_window_ex
	tbnz	x0,63,.out
	adr	x0,g_line			; « Repeter dans 10 min »
	adr	x1,s_snooze
	bl	tr_x1
	bl	str_cat
	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x0
	adr	x0,al_snz
	ldr	x0,[x0]
	bl	fmt_dec
	adr	x1,s_min
	bl	str_cat
	mov	x0,20
	mov	x1,BTN_Y
	mov	x2,200
	mov	x3,BTN_H
	adr	x4,g_line
	mov	x5,UB_DEFAULT
	bl	ui_pushbutton
	adr	x1,g_btn
	str	x0,[x1]
	mov	x0,232
	mov	x1,BTN_Y
	mov	x2,RING_W-232-20
	mov	x3,BTN_H
	adr	x4,s_stop
	mov	x5,0
	bl	ui_pushbutton
	adr	x1,g_btn
	str	x0,[x1,8]
	bl	ring_draw
.out:	ldp	x19,x20,[sp],16
	epilogue

; le fond de la carte, l'icone, l'heure, le nom, les boutons
ring_draw:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,0
	mov	x1,0
	mov	x2,RING_W
	mov	x3,RING_H
	jeton	w4,TJ_LIGNE_FORTE
	bl	ui_fill
	mov	x0,1
	mov	x1,1
	mov	x2,RING_W-2
	mov	x3,RING_H-2
	mov	x4,10
	jeton	w5,TJ_SURFACE
	jeton	w6,TJ_SURFACE
	bl	ui_rbox
	adr	x0,s_appname			; l'icone de l'Horloge
	mov	x1,20
	mov	x2,18
	mov	x3,40
	bl	ui_icone
	adr	x0,s_alarm			; « Alarme », puis le nom
	bl	tr_x0
	mov	x1,72
	mov	x2,34
	jeton	w3,TJ_TEXTE2
	mov	x4,13
	adr	x5,ui_font
	mov	x6,RING_W-92
	bl	ui_text_fit
	adr	x0,g_name
	mov	x1,72
	mov	x2,56
	jeton	w3,TJ_TEXTE1
	mov	x4,17
	adr	x5,ui_font_b
	mov	x6,RING_W-92
	bl	ui_text_fit
	adr	x0,g_hm				; l'heure, au milieu
	mov	x1,52
	adr	x2,ui_font_b
	bl	ui_text_width
	mov	x1,RING_W
	sub	x1,x1,x0
	lsr	x1,x1,1
	adr	x0,g_hm
	mov	x2,130
	jeton	w3,TJ_ACCENT
	mov	x4,52
	adr	x5,ui_font_b
	mov	x6,0
	bl	ui_text
	adr	x0,s_keys			; Entree, Echap
	bl	tr_x0
	mov	x19,x0
	mov	x1,12
	adr	x2,ui_font
	bl	ui_text_width
	mov	x1,RING_W
	sub	x1,x1,x0
	lsr	x1,x1,1
	mov	x0,x19
	mov	x2,164
	jeton	w3,TJ_TEXTE2
	mov	x4,12
	adr	x5,ui_font
	mov	x6,0
	bl	ui_text
	mov	x0,0
	bl	ui_w_draw
	bl	ui_update_all
	ldp	x19,x20,[sp],16
	epilogue

s_svc		db 'clockd',0
s_grace		db '--grace',0
s_appname	db 'clock',0
s_clock		db 'Horloge',0
s_alarm		db 'Alarme',0
s_missed	db 'Alarme manqu',0xC3,0xA9,'e :',0
s_snooze	db 'R',0xC3,0xA9,'p',0xC3,0xA9,'ter dans',0
s_min		db ' min',0
s_stop		db 'Arr',0xC3,0xAA,'ter',0
s_keys		db 'Entr',0xC3,0xA9,'e : r',0xC3,0xA9,'p',0xC3,0xA9,'ter ',0xC2,0xB7,' ',0xC3,0x89,'chap : arr',0xC3,0xAA,'ter',0
m_reload	db 'clockd : alarmes lues : ',0
m_alarms	db 10,0
m_ring		db 'clockd : sonne ',0
m_snooze	db 'clockd : repetee a ',0
m_stop		db 'clockd : arretee',10,0
m_missed	db 'clockd : manquee ',0
	align 4

include '../../inc/tr.inc'

s_clk_topic	db 'korin.clock.offset',0
		align 8

segment readable writeable
kit_data
		align 8
g_sub		dd 0				; l'abonnement a korin.clock.alarms
g_grace		dd 0				; ms apres le demarrage
g_last		dd 0				; la derniere minute regardee
g_mdone		dd 0
g_qn		dd 0
g_ring_id	dd 0
g_ring_min	dd 0
g_sound		dd 0
g_snd		dd 0, 0				; table du son, duree d'un tour
g_btn		dd 0, 0				; Repeter, Arreter
g_q		rb QMAX * 16
g_screen	rb 32
g_ev		rb 32
g_hm		rb 16
g_name		rb AL_LABEL_MAX + 8
g_text		rb 160
g_line		rb 320
g_log		rb 320
alarms_data
gs_data
ui_data
