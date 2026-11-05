; ============================================================================
;  inc/alarms.inc - les alarmes de l'Horloge (le modele d'alarms.h d'Onyx),
;  partagees par l'Horloge (src/apps/clock : elle les edite) et clockd
;  (src/sys/clockd : il les sonne). Pas d'interface ici : le fichier, les
;  dates, la prochaine sonnerie, les sons.
;
;  SD0:/etc/alarms.txt (le format d'Onyx : des lignes « cle = valeur », un bloc
;  [alarm] par alarme, '#' et ';' commencent un commentaire) :
;	snooze = 10		minutes du report (1..30), avant tout bloc
;	[alarm]
;	id     = 1		jamais reutilise tant que le fichier vit
;	time   = 07:00		HH:MM, heure locale (invalide : jamais sonnee)
;	label  = Ecole		UTF-8, AL_LABEL_MAX octets au plus
;	on     = 1
;	days   = mon tue wed thu fri	(vide : une fois, le jour de « date »)
;	date   = 20261105	une fois : le jour, AAAAMMJJ
;	sound  = chimes		chimes | beeps | marimba
;	snooze = 202611050710	AAAAMMJJHHMM : sonne encore a cette minute
;	missed = 202611050700	(seulement si posee) une sonnerie sans reponse
;  L'Horloge et clockd l'ecrivent tous deux, puis publient le sujet
;  korin.clock.alarms (docs/BUS.md) : l'autre le relit.
;
;	segment executable :  include '../../inc/gamesnd.inc' (les sons), puis
;			      include '../../inc/alarms.inc'
;	donnees :	      alarms_data
;  Le programme a kit_use appkit (str_eq, str_cat, fmt_dec) et inclut
;  kits/audiokit/audiokit.inc (AKW_*).
; ============================================================================

AL_MAX		= 20
AL_SIZE		= 128
AL_ID		= 0			; (32 bits)
AL_HH		= 4			; (octets)
AL_MM		= 5
AL_ON		= 6
AL_VALID	= 7
AL_DAYS		= 8			; bit 0 lundi .. bit 6 dimanche ; 0 : une fois
AL_SOUND	= 9			; 0 carillon, 1 bips, 2 marimba
AL_DATE		= 16			; une fois : jours depuis 1970 (-1 : aucun)
AL_SNOOZE	= 24			; minute (depuis 1970, heure locale) du report, -1
AL_MISSED	= 32			; minute de la derniere sonnerie sans reponse, -1
AL_LABEL	= 40
AL_LABEL_MAX	= 87			; octets (sans le 0)
AL_FBUF		= 8192
AL_DAYS_ALL	= 0x7F
AL_WEEKDAYS	= 0x1F
AL_WEEKEND	= 0x60
AL_SNOOZE_DEF	= 10
AL_SOUNDS	= 3

; ---------------------------------------------------------------------------
;  Dates (heure locale ; une minute = jours * 1440 + h * 60 + m)
; ---------------------------------------------------------------------------

; x0, x1, x2 = annee, mois, jour -> x0 = jours depuis 1970 (H. Hinnant)
al_days:
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
al_civil:
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

; x0 = jours -> x0 = 0 lundi .. 6 dimanche (le 1/1/1970 etait un jeudi)
al_wday:
	add	x0,x0,3
	mov	x1,7
	udiv	x2,x0,x1
	msub	x0,x2,x1,x0
	ret

; -> x0 = minute locale maintenant, x1 = secondes locales
al_now:
	mov	x0,2
	mov	x8,SYS_CLOCK_GET
	svc	0
	mov	x1,x0
	mov	x2,60
	udiv	x0,x0,x2
	ret

; x0 = alarme, x1 = depuis (minute) -> x0 = la premiere minute >= x1 ou elle
; sonne d'apres son programme (valide ; « on » et le report non regardes),
; -1 : jamais. Feuille : x0-x10.
al_next:
	ldrb	w2,[x0,AL_VALID]
	cbz	w2,.never
	ldrb	w2,[x0,AL_HH]
	ldrb	w3,[x0,AL_MM]
	mov	x4,60
	madd	x3,x2,x4,x3			; hh * 60 + mm
	ldrb	w4,[x0,AL_DAYS]
	ands	w4,w4,AL_DAYS_ALL
	b.ne	.rep
	ldr	x2,[x0,AL_DATE]			; une fois : son jour
	tbnz	x2,63,.never
	mov	x5,1440
	madd	x0,x2,x5,x3
	cmp	x0,x1
	b.ge	.ret
.never:	mov	x0,-1
.ret:	ret
.rep:	mov	x5,1440
	cmp	x1,0
	csel	x2,x1,xzr,gt
	udiv	x2,x2,x5			; le jour de depart
	mov	x6,0
.k:	add	x7,x2,x6
	add	x8,x7,3				; son jour de la semaine
	mov	x9,7
	udiv	x10,x8,x9
	msub	x8,x10,x9,x8
	lsr	w9,w4,w8
	tbz	w9,0,.nk
	madd	x0,x7,x5,x3
	cmp	x0,x1
	b.ge	.ret
.nk:	add	x6,x6,1
	cmp	x6,8
	b.ls	.k
	b	.never

; x0 = maintenant (minute) -> x0 = la plus proche sonnerie apres maintenant
; des alarmes actives, reports compris (-1 : aucune), x1 = son numero
al_next_all:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	mov	x20,-1				; la meilleure
	mov	x21,-1				; son numero
	mov	x22,0
.i:	adr	x0,al_n
	ldr	x0,[x0]
	cmp	x22,x0
	b.hs	.done
	adr	x23,al_tab
	mov	x0,AL_SIZE
	madd	x23,x22,x0,x23
	ldrb	w0,[x23,AL_ON]
	cbz	w0,.next
	ldrb	w0,[x23,AL_VALID]
	cbz	w0,.next
	mov	x0,x23
	add	x1,x19,1
	bl	al_next
	ldr	x1,[x23,AL_SNOOZE]		; un report a venir compte
	cmp	x1,x19
	b.le	.cmp
	tbnz	x0,63,.snz
	cmp	x1,x0
	b.ge	.cmp
.snz:	mov	x0,x1
.cmp:	tbnz	x0,63,.next
	tbnz	x20,63,.take
	cmp	x0,x20
	b.ge	.next
.take:	mov	x20,x0
	mov	x21,x22
.next:	add	x22,x22,1
	b	.i
.done:	mov	x0,x20
	mov	x1,x21
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = alarme, x1 = maintenant -> x0 = 1 : une alarme d'une fois dont la
; minute est passee (et pas de report a venir) : montree desactivee
al_passed:
	ldrb	w2,[x0,AL_DAYS]
	ands	w2,w2,AL_DAYS_ALL
	b.ne	.no
	ldrb	w2,[x0,AL_VALID]
	cbz	w2,.no
	ldr	x2,[x0,AL_SNOOZE]
	cmp	x2,x1
	b.ge	.no
	ldr	x2,[x0,AL_DATE]
	tbnz	x2,63,.no
	mov	x3,1440
	ldrb	w4,[x0,AL_HH]
	mov	x5,60
	mul	x4,x4,x5
	ldrb	w5,[x0,AL_MM]
	add	x4,x4,x5
	madd	x2,x2,x3,x4
	cmp	x2,x1
	b.ge	.no
	mov	x0,1
	ret
.no:	mov	x0,0
	ret

; x0 = hh, x1 = mm, x2 = maintenant (minute) -> x0 = le jour d'une alarme
; d'une fois : aujourd'hui si hh:mm est encore a venir, sinon demain
al_once_day:
	mov	x3,1440
	udiv	x4,x2,x3			; aujourd'hui
	msub	x5,x4,x3,x2			; minute du jour
	mov	x6,60
	madd	x0,x0,x6,x1
	cmp	x0,x5
	cinc	x0,x4,le
	ret

; x0 = id -> x0 = l'alarme (0 : aucune)
al_find:
	adr	x1,al_n
	ldr	x1,[x1]
	adr	x2,al_tab
.c:	cbz	x1,.no
	ldr	w3,[x2,AL_ID]
	cmp	w3,w0
	b.eq	.yes
	add	x2,x2,AL_SIZE
	sub	x1,x1,1
	b	.c
.yes:	mov	x0,x2
	ret
.no:	mov	x0,0
	ret

; x0 = alarme : vide (07:00, active, une fois, carillon, sans nom ni report)
al_blank:
	str	wzr,[x0,AL_ID]
	mov	w1,7
	strb	w1,[x0,AL_HH]
	strb	wzr,[x0,AL_MM]
	mov	w1,1
	strb	w1,[x0,AL_ON]
	strb	wzr,[x0,AL_VALID]
	strb	wzr,[x0,AL_DAYS]
	strb	wzr,[x0,AL_SOUND]
	mov	x1,-1
	str	x1,[x0,AL_DATE]
	str	x1,[x0,AL_SNOOZE]
	str	x1,[x0,AL_MISSED]
	strb	wzr,[x0,AL_LABEL]
	ret

; x0 = id : retiree de la table -> x0 = 1, 0 : aucune
al_remove:
	prologue
	bl	al_find
	cbz	x0,.out
	adr	x1,al_n
	ldr	x2,[x1]
	sub	x2,x2,1
	str	x2,[x1]
	adr	x3,al_tab			; les suivantes remontent
	mov	x4,AL_SIZE
	madd	x3,x2,x4,x3			; fin (la derniere, maintenant hors table)
	add	x1,x0,AL_SIZE
	sub	x2,x3,x0			; (octets de x0+SIZE a fin+SIZE)
	bl	mem_move
	mov	x0,1
.out:	epilogue

; ---------------------------------------------------------------------------
;  Lecture
; ---------------------------------------------------------------------------

; x0 = texte, x1 = chiffres -> x0 = valeur, -1 : pas des chiffres ou trop long
; (le texte doit finir juste apres)
al_digits:
	mov	x2,0
	mov	x3,10
.c:	cbz	x1,.end
	ldrb	w4,[x0],1
	sub	w4,w4,'0'
	cmp	w4,9
	b.hi	.no
	madd	x2,x2,x3,x4
	sub	x1,x1,1
	b	.c
.end:	ldrb	w4,[x0]
	cbnz	w4,.no
	mov	x0,x2
	ret
.no:	mov	x0,-1
	ret

; x0 = texte « HH:MM » (ou « H:MM ») -> x0 = 1 valide, x1 = hh, x2 = mm
al_parse_hm:
	mov	x1,0
	mov	x3,0				; chiffres
.h:	ldrb	w4,[x0],1
	cmp	w4,':'
	b.eq	.colon
	sub	w4,w4,'0'
	cmp	w4,9
	b.hi	.no
	mov	x5,10
	madd	x1,x1,x5,x4
	add	x3,x3,1
	cmp	x3,2
	b.hi	.no
	b	.h
.colon:	cbz	x3,.no
	cmp	x1,23
	b.hi	.no
	ldrb	w4,[x0]
	ldrb	w5,[x0,1]
	ldrb	w6,[x0,2]
	cbnz	w6,.no
	sub	w4,w4,'0'
	sub	w5,w5,'0'
	cmp	w4,5
	b.hi	.no
	cmp	w5,9
	b.hi	.no
	mov	w6,10
	madd	w2,w4,w6,w5
	mov	x0,1
	ret
.no:	mov	x0,0
	mov	x1,0
	mov	x2,0
	ret

; x0 = « AAAAMMJJ » -> x0 = jours depuis 1970, -1
al_parse_day:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x1,8
	bl	al_digits
	tbnz	x0,63,.out
	movl	x1,10000
	udiv	x20,x0,x1			; annee
	msub	x0,x20,x1,x0
	mov	x1,100
	udiv	x3,x0,x1			; mois
	msub	x2,x3,x1,x0			; jour
	cmp	x20,1970
	b.lo	.no
	sub	x4,x3,1
	cmp	x4,11
	b.hi	.no
	sub	x4,x2,1
	cmp	x4,30
	b.hi	.no
	mov	x0,x20
	mov	x1,x3
	bl	al_days
	b	.out
.no:	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = « AAAAMMJJHHMM » -> x0 = minute, -1
al_parse_stamp:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	str_len
	cmp	x0,12
	b.ne	.no
	add	x0,x19,8			; HHMM
	mov	x1,4
	bl	al_digits
	tbnz	x0,63,.no
	mov	x1,100
	udiv	x20,x0,x1			; hh
	msub	x21,x20,x1,x0			; mm
	cmp	x20,23
	b.hi	.no
	cmp	x21,59
	b.hi	.no
	ldrb	w22,[x19,8]			; le jour seul
	strb	wzr,[x19,8]
	mov	x0,x19
	bl	al_parse_day
	strb	w22,[x19,8]
	tbnz	x0,63,.out
	mov	x1,1440
	mov	x2,60
	madd	x3,x20,x2,x21
	madd	x0,x0,x1,x3
	b	.out
.no:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte « mon tue ... » -> x0 = jours (bit 0 lundi)
al_parse_days:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.w:	ldrb	w1,[x19]
	cbz	w1,.out
	orr	w1,w1,0x20			; (minuscules)
	ldrb	w2,[x19,1]
	orr	w2,w2,0x20
	ldrb	w3,[x19,2]
	orr	w3,w3,0x20
	orr	w1,w1,w2,lsl 8
	orr	w1,w1,w3,lsl 16
	adr	x4,al_day_tok			; les trois lettres
	mov	x5,0
.d:	ldr	w6,[x4,x5,lsl 2]
	cmp	w6,w1
	b.eq	.hit
	add	x5,x5,1
	cmp	x5,7
	b.lo	.d
	b	.skip
.hit:	mov	x6,1
	lsl	x6,x6,x5
	orr	x20,x20,x6
.skip:	ldrb	w1,[x19]			; la fin du mot, puis les separateurs
	cbz	w1,.out
	cmp	w1,' '
	b.eq	.sep
	cmp	w1,','
	b.eq	.sep
	cmp	w1,9
	b.eq	.sep
	add	x19,x19,1
	b	.skip
.sep:	ldrb	w1,[x19]
	cmp	w1,' '
	b.eq	.s1
	cmp	w1,','
	b.eq	.s1
	cmp	w1,9
	b.ne	.w
.s1:	add	x19,x19,1
	b	.sep
.out:	mov	x0,x20
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination (AL_LABEL_MAX + 1), x1 = source : copiee, coupee sur un
; caractere, les tabulations en espaces
al_label_copy:
	mov	x2,0
.c:	ldrb	w3,[x1,x2]
	cbz	w3,.end
	cmp	w3,9
	b.ne	.k
	mov	w3,' '
.k:	and	w4,w3,0xC0			; un caractere commence : tient-il ?
	cmp	w4,0x80
	b.eq	.put
	mov	x5,1
	cmp	w3,0xC0
	b.lo	.fit
	mov	x5,2
	cmp	w3,0xE0
	b.lo	.fit
	mov	x5,3
	cmp	w3,0xF0
	b.lo	.fit
	mov	x5,4
.fit:	add	x5,x5,x2
	cmp	x5,AL_LABEL_MAX
	b.hi	.end
.put:	strb	w3,[x0,x2]
	add	x2,x2,1
	b	.c
.end:	strb	wzr,[x0,x2]
	ret

; x0 = cle, x1 = valeur, x2 = alarme du bloc (0 : aucune), x3 = 1 apres un
; bloc : une ligne « cle = valeur »
al_key:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	cbnz	x21,.blk
	cbnz	x3,.out				; (un autre bloc : ignore)
	adr	x1,al_k_snooze			; avant tout bloc : le report
	bl	str_eq
	cbz	x0,.out
	mov	x0,x20
	bl	parse_dec
	sub	x1,x0,1
	cmp	x1,29
	b.hi	.out
	adr	x1,al_snz
	str	x0,[x1]
	b	.out
.blk:	adr	x22,al_keys			; la cle : son numero
	mov	x3,0
.k:	ldrb	w0,[x22]
	cbz	w0,.out
	mov	x0,x19
	mov	x1,x22
	stp	x3,x3,[sp,-16]!
	bl	str_eq
	ldp	x3,x4,[sp],16
	cbnz	x0,.hit
.z:	ldrb	w0,[x22],1			; la cle suivante
	cbnz	w0,.z
	add	x3,x3,1
	b	.k
.hit:	adr	x0,.jt
	ldrb	w1,[x0,x3]
	adr	x2,.base
	add	x2,x2,x1,lsl 2
	mov	x0,x20
	br	x2
.base:
.id:	bl	parse_dec
	str	w0,[x21,AL_ID]
	b	.out
.time:	bl	al_parse_hm
	strb	w0,[x21,AL_VALID]
	strb	w1,[x21,AL_HH]
	strb	w2,[x21,AL_MM]
	b	.out
.label:	mov	x1,x0
	add	x0,x21,AL_LABEL
	bl	al_label_copy
	b	.out
.on:	ldrb	w1,[x0]
	ldrb	w2,[x0,1]
	cmp	w1,'0'
	cset	w1,ne
	cmp	w2,0
	csinc	w1,w1,wzr,eq			; « 0 » seul : 0, sinon 1
	strb	w1,[x21,AL_ON]
	b	.out
.days:	bl	al_parse_days
	strb	w0,[x21,AL_DAYS]
	b	.out
.date:	bl	al_parse_day
	str	x0,[x21,AL_DATE]
	b	.out
.sound:	mov	x1,0
	adr	x22,al_snd_tok
.sn:	mov	x0,x20
	stp	x1,x1,[sp,-16]!
	mov	x1,x22
	bl	str_eq
	ldp	x1,x2,[sp],16
	cbnz	x0,.sf
	add	x22,x22,8
	add	x1,x1,1
	cmp	x1,AL_SOUNDS
	b.lo	.sn
	mov	x1,0
.sf:	strb	w1,[x21,AL_SOUND]
	b	.out
.snz:	bl	al_parse_stamp
	str	x0,[x21,AL_SNOOZE]
	b	.out
.mis:	bl	al_parse_stamp
	str	x0,[x21,AL_MISSED]
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue
.jt:	db (.id-.base)/4, (.time-.base)/4, (.label-.base)/4, (.on-.base)/4
	db (.days-.base)/4, (.date-.base)/4, (.sound-.base)/4, (.snz-.base)/4
	db (.mis-.base)/4
	align 4

; -> SD0:/etc/alarms.txt lu dans la table (absent : aucune alarme) ; x0 = nombre
al_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	adr	x0,al_path
	mov	x1,O_READ
	sys	SYS_OPEN
	mov	x19,0
	tbnz	x0,63,.parse
	mov	x20,x0
	adr	x1,al_fbuf
	mov	x2,AL_FBUF-1
	sys	SYS_READ
	cmp	x0,0
	csel	x19,x0,xzr,gt
	mov	x0,x20
	sys	SYS_CLOSE
.parse:	adr	x0,al_n
	str	xzr,[x0]
	mov	x1,AL_SNOOZE_DEF
	adr	x0,al_snz
	str	x1,[x0]
	adr	x20,al_fbuf
	add	x21,x20,x19			; fin
	strb	wzr,[x21]
	mov	x22,0				; l'alarme du bloc
	mov	x23,0				; un bloc vu
.line:	cmp	x20,x21
	b.hs	.ids
	ldrb	w0,[x20]			; les blancs du debut
	cmp	w0,' '
	b.eq	.sp
	cmp	w0,9
	b.ne	.ls
.sp:	add	x20,x20,1
	b	.line
.ls:	mov	x24,x20				; la fin de la ligne
.e:	cmp	x24,x21
	b.hs	.le
	ldrb	w0,[x24]
	cmp	w0,10
	b.eq	.le
	add	x24,x24,1
	b	.e
.le:	mov	x25,x24				; sans les blancs de la fin
.t:	cmp	x25,x20
	b.ls	.tt
	ldrb	w0,[x25,-1]
	cmp	w0,13
	b.eq	.tr
	cmp	w0,' '
	b.eq	.tr
	cmp	w0,9
	b.ne	.tt
.tr:	sub	x25,x25,1
	b	.t
.tt:	strb	wzr,[x25]
	ldrb	w0,[x20]
	cbz	w0,.nl
	cmp	w0,'#'
	b.eq	.nl
	cmp	w0,';'
	b.eq	.nl
	cmp	w0,'['
	b.ne	.kv
	mov	x23,1				; un bloc : [alarm] ou un autre
	mov	x22,0
	mov	x0,x20
	adr	x1,al_k_alarm
	bl	str_eq
	cbz	x0,.nl
	adr	x0,al_n
	ldr	x1,[x0]
	cmp	x1,AL_MAX
	b.hs	.nl
	add	x2,x1,1
	str	x2,[x0]
	adr	x22,al_tab
	mov	x2,AL_SIZE
	madd	x22,x1,x2,x22
	mov	x0,x22
	bl	al_blank
	b	.nl
.kv:	mov	x26,x20				; « cle = valeur »
.q:	ldrb	w0,[x26]
	cbz	w0,.nl
	cmp	w0,'='
	b.eq	.eq
	add	x26,x26,1
	b	.q
.eq:	strb	wzr,[x26]
	mov	x0,x26
.kt:	cmp	x0,x20
	b.ls	.kd
	ldrb	w1,[x0,-1]
	cmp	w1,' '
	b.eq	.ks
	cmp	w1,9
	b.ne	.kd
.ks:	sub	x0,x0,1
	strb	wzr,[x0]
	b	.kt
.kd:	add	x1,x26,1
.vs:	ldrb	w2,[x1]
	cmp	w2,' '
	b.eq	.vi
	cmp	w2,9
	b.ne	.vd
.vi:	add	x1,x1,1
	b	.vs
.vd:	mov	x0,x20
	mov	x2,x22
	mov	x3,x23
	bl	al_key
.nl:	add	x20,x24,1
	b	.line
.ids:	bl	al_fix_ids
	adr	x0,al_n
	ldr	x0,[x0]
	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; les numeros : uniques, positifs ; al_next_id au-dessus de tous
al_fix_ids:
	adr	x0,al_n
	ldr	x0,[x0]
	adr	x1,al_tab
	mov	x2,1				; le suivant
	mov	x3,0
.m:	cmp	x3,x0
	b.hs	.dup
	mov	x4,AL_SIZE
	madd	x4,x3,x4,x1
	ldrsw	x5,[x4,AL_ID]
	cmp	x5,x2
	b.lt	.m1
	add	x2,x5,1
.m1:	add	x3,x3,1
	b	.m
.dup:	mov	x3,0				; un numero absent ou deja pris : un neuf
.d:	cmp	x3,x0
	b.hs	.out
	mov	x4,AL_SIZE
	madd	x4,x3,x4,x1
	ldrsw	x5,[x4,AL_ID]
	cmp	x5,0
	b.le	.new
	mov	x6,0
.j:	cmp	x6,x3
	b.hs	.ok
	mov	x7,AL_SIZE
	madd	x7,x6,x7,x1
	ldr	w7,[x7,AL_ID]
	cmp	w7,w5
	b.eq	.new
	add	x6,x6,1
	b	.j
.new:	str	w2,[x4,AL_ID]
	add	x2,x2,1
.ok:	add	x3,x3,1
	b	.d
.out:	adr	x0,al_next_id
	str	x2,[x0]
	ret

; ---------------------------------------------------------------------------
;  Ecriture
; ---------------------------------------------------------------------------

; x0 = destination, x1 = 0..99 -> x0 apres (deux chiffres)
al_2d:
	mov	x2,10
	udiv	x3,x1,x2
	msub	x4,x3,x2,x1
	add	w3,w3,'0'
	add	w4,w4,'0'
	strb	w3,[x0],1
	strb	w4,[x0],1
	strb	wzr,[x0]
	ret

; x0 = destination, x1 = alarme -> x0 apres « HH:MM »
al_fmt_hm:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x1
	ldrb	w1,[x19,AL_HH]
	bl	al_2d
	mov	w1,':'
	strb	w1,[x0],1
	ldrb	w1,[x19,AL_MM]
	bl	al_2d
	ldr	x19,[sp],16
	epilogue

; x0 = destination, x1 = minute -> x0 apres « HH:MM » (l'heure de la minute)
al_fmt_min_hm:
	prologue
	mov	x2,1440
	udiv	x3,x1,x2
	msub	x1,x3,x2,x1
	mov	x2,60
	udiv	x3,x1,x2
	msub	x4,x3,x2,x1
	stp	x4,x4,[sp,-16]!
	mov	x1,x3
	bl	al_2d
	mov	w1,':'
	strb	w1,[x0],1
	ldp	x1,x2,[sp],16
	bl	al_2d
	epilogue

; x0 = destination, x1 = jours (-1 : rien) -> x0 apres « AAAAMMJJ »
al_fmt_day:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	strb	wzr,[x0]
	tbnz	x1,63,.out
	mov	x0,x1
	bl	al_civil
	stp	x1,x2,[sp,-16]!
	mov	x1,x19
	bl	fmt_dec
	mov	x19,x0
	ldp	x1,x2,[sp],16
	str	x2,[sp,-16]!
	mov	x0,x19
	bl	al_2d
	ldr	x1,[sp],16
	bl	al_2d
	mov	x19,x0
.out:	mov	x0,x19
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = minute (-1 : rien) -> x0 apres « AAAAMMJJHHMM »
al_fmt_stamp:
	prologue
	stp	x19,x20,[sp,-16]!
	strb	wzr,[x0]
	tbnz	x1,63,.out
	mov	x20,x1
	mov	x2,1440
	udiv	x1,x1,x2
	bl	al_fmt_day
	mov	x2,1440
	udiv	x3,x20,x2
	msub	x19,x3,x2,x20			; minute du jour
	mov	x2,60
	udiv	x1,x19,x2
	msub	x19,x1,x2,x19
	bl	al_2d
	mov	x1,x19
	bl	al_2d
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = jours (bits) -> x0 apres « mon tue ... »
al_fmt_days:
	mov	x2,0
	mov	x5,0				; un mot deja ecrit
	adr	x3,al_day_tok
.d:	lsr	x4,x1,x2
	tbz	x4,0,.n
	cbz	x5,.w
	mov	w4,' '
	strb	w4,[x0],1
.w:	add	x6,x3,x2,lsl 2
	ldrb	w4,[x6]
	strb	w4,[x0],1
	ldrb	w4,[x6,1]
	strb	w4,[x0],1
	ldrb	w4,[x6,2]
	strb	w4,[x0],1
	mov	x5,1
.n:	add	x2,x2,1
	cmp	x2,7
	b.lo	.d
	strb	wzr,[x0]
	ret

; x0 = fin du texte, x1 = cle -> x0 apres « cle = »
al_put_key:
	prologue
	bl	str_cat
	adr	x1,al_s_eq
	bl	str_cat
	epilogue

; x0 = fin du texte -> x0 apres le saut de ligne
al_put_nl:
	mov	w1,10
	strb	w1,[x0],1
	strb	wzr,[x0]
	ret

; la table -> SD0:/etc/alarms.txt ; x0 = 0, -1 : echec
al_save:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,al_fbuf
	adr	x1,al_s_head
	bl	str_cat
	adr	x1,al_k_snooze
	bl	al_put_key
	mov	x19,x0
	adr	x0,al_snz
	ldr	x0,[x0]
	mov	x1,x19
	bl	fmt_dec
	bl	al_put_nl
	mov	x19,x0
	mov	x20,0
	adr	x21,al_tab
.a:	adr	x0,al_n
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.write
	mov	x0,x19
	bl	al_put_nl
	adr	x1,al_k_alarm
	bl	str_cat
	bl	al_put_nl
	adr	x1,al_keys			; id
	bl	al_put_key
	mov	x1,x0
	ldr	w0,[x21,AL_ID]
	bl	fmt_dec
	bl	al_put_nl
	adr	x1,al_k_time
	bl	al_put_key
	ldrb	w1,[x21,AL_VALID]
	cbz	w1,.nt
	mov	x1,x21
	bl	al_fmt_hm
.nt:	bl	al_put_nl
	adr	x1,al_k_label
	bl	al_put_key
	add	x1,x21,AL_LABEL
	bl	str_cat
	bl	al_put_nl
	adr	x1,al_k_on
	bl	al_put_key
	ldrb	w1,[x21,AL_ON]
	add	w1,w1,'0'
	strb	w1,[x0],1
	bl	al_put_nl
	adr	x1,al_k_days
	bl	al_put_key
	ldrb	w1,[x21,AL_DAYS]
	and	w1,w1,AL_DAYS_ALL
	bl	al_fmt_days
	bl	al_put_nl
	adr	x1,al_k_date
	bl	al_put_key
	ldrb	w1,[x21,AL_DAYS]
	ands	w1,w1,AL_DAYS_ALL
	b.ne	.nd
	ldr	x1,[x21,AL_DATE]
	bl	al_fmt_day
.nd:	bl	al_put_nl
	adr	x1,al_k_sound
	bl	al_put_key
	ldrb	w1,[x21,AL_SOUND]
	cmp	w1,AL_SOUNDS
	csel	w1,w1,wzr,lo
	adr	x2,al_snd_tok
	add	x1,x2,x1,lsl 3
	bl	str_cat
	bl	al_put_nl
	adr	x1,al_k_snz
	bl	al_put_key
	ldr	x1,[x21,AL_SNOOZE]
	bl	al_fmt_stamp
	bl	al_put_nl
	ldr	x1,[x21,AL_MISSED]
	tbnz	x1,63,.nm
	adr	x1,al_k_missed
	bl	al_put_key
	ldr	x1,[x21,AL_MISSED]
	bl	al_fmt_stamp
	bl	al_put_nl
.nm:	mov	x19,x0
	add	x20,x20,1
	add	x21,x21,AL_SIZE
	b	.a
.write:	adr	x0,al_fbuf
	sub	x20,x19,x0			; octets
	adr	x0,al_path
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.fail
	mov	x19,x0
	adr	x1,al_fbuf
	mov	x2,x20
	sys	SYS_WRITE
	mov	x21,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x21,x20
	b.ne	.fail
	mov	x0,0
	b	.out
.fail:	mov	x0,-1
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; les autres sont prevenus : korin.clock.alarms (kit_use buskit)
al_publish:
	prologue
	sys	SYS_TIME_MS
	adr	x1,al_pubval
	bl	fmt_dec
	adr	x0,al_topic
	adr	x1,al_pubval
	mov	x2,-1
	mov	x3,0
	bl	bus_publish
	epilogue

; ---------------------------------------------------------------------------
;  Les sons (ceux d'Onyx : sounds.h), sur les voix FM d'AudioKit (gamesnd.inc)
; ---------------------------------------------------------------------------

; x0 = son 0..2 -> x0 = sa table (gs_seq), x1 = la duree d'un tour (ms)
al_sound:
	cmp	x0,AL_SOUNDS
	csel	x0,x0,xzr,lo
	adr	x1,al_snd_round
	ldr	w1,[x1,x0,lsl 2]
	adr	x2,al_snd_tabs
	ldr	w2,[x2,x0,lsl 2]
	adr	x3,al_snd_chimes
	add	x0,x3,x2
	ret

al_snd_chimes:					; un arpege montant, des cloches
	gs_ent	0, 1047, 900, AKW_SINE, 170
	gs_ent	250, 1319, 900, AKW_SINE, 170
	gs_ent	500, 1568, 900, AKW_SINE, 170
	gs_ent	750, 2093, 1200, AKW_SINE, 170
	gs_end
al_snd_beeps:					; des bips aigus et courts
	gs_ent	0, 2093, 90, AKW_SQUARE, 70
	gs_ent	180, 2093, 90, AKW_SQUARE, 70
	gs_ent	360, 2093, 90, AKW_SQUARE, 70
	gs_ent	540, 2093, 90, AKW_SQUARE, 70
	gs_end
al_snd_marimba:					; une phrase douce, du bois
	gs_ent	0, 784, 400, AKW_TRIANGLE, 200
	gs_ent	200, 1047, 400, AKW_TRIANGLE, 200
	gs_ent	400, 1319, 400, AKW_TRIANGLE, 200
	gs_ent	600, 1047, 400, AKW_TRIANGLE, 200
	gs_ent	1000, 1568, 700, AKW_TRIANGLE, 200
	gs_end
	align 4
al_snd_tabs	dw 0, al_snd_beeps-al_snd_chimes, al_snd_marimba-al_snd_chimes
al_snd_round	dw 2200, 1200, 2000

al_path		db 'SD0:/etc/alarms.txt',0
al_topic	db 'korin.clock.alarms',0
al_s_head	db '# Horloge : les alarmes (ecrites par l',0x27,'Horloge et clockd, sonnees par clockd).',10
		db '# snooze : minutes du report. Un bloc [alarm] par alarme.',10,0
al_s_eq		db ' = ',0
al_k_alarm	db '[alarm]',0
al_k_snooze	db 'snooze',0
al_keys		db 'id',0			; (dans l'ordre de .jt d'al_key)
al_k_time	db 'time',0
al_k_label	db 'label',0
al_k_on		db 'on',0
al_k_days	db 'days',0
al_k_date	db 'date',0
al_k_sound	db 'sound',0
al_k_snz	db 'snooze',0
al_k_missed	db 'missed',0,0
		align 4
al_day_tok	db 'mon',0,'tue',0,'wed',0,'thu',0,'fri',0,'sat',0,'sun',0
al_snd_tok	db 'chimes',0,0,'beeps',0,0,0,'marimba',0
	align 4

macro alarms_data {
		align 8
al_n		dd 0
al_next_id	dd 1
al_snz		dd AL_SNOOZE_DEF
al_pubval	rb 24
		align 8
al_tab		rb AL_MAX * AL_SIZE
al_fbuf		rb AL_FBUF
}
