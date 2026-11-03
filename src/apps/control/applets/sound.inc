; applets/sound.inc - applet Son du panneau de configuration (docs/SON.md
; 4.4), par AudioKit (service sound) :
;  * la sortie principale preferee (ou automatique : USB, HDMI, prise jack),
;    retenue par sound dans SD0:/etc/sound.txt ;
;  * le volume de depart de la sortie principale (retenu de meme) ;
;  * le micro I2S (INMP441) au demarrage : la ligne SD0:/sys/svc/i2smic de
;    SD0:/etc/services.txt, mise ou retiree du commentaire ;
;  * un son d'essai (la 440 Hz, 0,6 s) en appliquant.

SN_I2S		= 1			; valeurs des cases
SN_TEST		= 2

sn_ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	bl	ak_state
	mov	x19,x0
	adr	x0,sn_s_pref			; sortie preferee
	mov	x1,R_TITRE
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	applet_row
	mov	x21,1				; « automatique » cochee si aucune preferee
	cbz	x19,.auto
	add	x20,x19,SS_OUTS
	mov	x22,SS_OUT_MAX
.p:	ldr	w0,[x20,SO_FLAGS]
	tbz	w0,1,.pn
	mov	x21,0
.pn:	add	x20,x20,64
	subs	x22,x22,1
	b.ne	.p
.auto:	adr	x0,sn_s_auto
	mov	x1,R_CHOIX
	mov	x2,0
	mov	x3,x21
	mov	x4,0
	bl	applet_row
	cbz	x19,.vol
	add	x20,x19,SS_OUTS
	mov	x22,0
.o:	cmp	x22,SS_OUT_MAX
	b.hs	.vol
	ldr	w0,[x20,SO_ID]
	cbz	w0,.on
	adr	x0,sn_names			; nom garde (la ligne pointe dessus)
	add	x0,x0,x22,lsl 5
	add	x1,x20,SO_NAME
	mov	x2,32
	bl	str_copy_n
	adr	x0,sn_names
	add	x0,x0,x22,lsl 5
	mov	x1,R_CHOIX
	mov	x2,0
	ldr	w3,[x20,SO_FLAGS]
	ubfx	x3,x3,1,1
	ldr	w4,[x20,SO_ID]
	bl	applet_row
.on:	add	x20,x20,64
	add	x22,x22,1
	b	.o
.vol:	adr	x0,sn_s_vol			; volume de depart de la principale
	mov	x1,R_TITRE
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	applet_row
	mov	x21,100				; volume actuel
	cbz	x19,.v
	ldr	x0,[x19,SS_MAIN]
	add	x20,x19,SS_OUTS
	mov	x22,SS_OUT_MAX
.m:	ldr	w1,[x20,SO_ID]
	cmp	x1,x0
	b.ne	.mn
	ldr	w21,[x20,SO_VOLUME]
.mn:	add	x20,x20,64
	subs	x22,x22,1
	b.ne	.m
.v:	mov	x22,1				; 25, 50, 75, 100 % : le plus proche coche
.vl:	cmp	x22,4
	b.hi	.mic
	mov	x0,25
	mul	x4,x22,x0
	sub	x0,x4,x21
	cmp	x0,0
	cneg	x0,x0,lt
	cmp	x0,12
	cset	x3,le
	cmp	x22,4				; (100 % : aussi au-dela)
	b.ne	.vr
	cmp	x21,87
	cset	x3,hi
.vr:	adr	x0,sn_s_pc
	sub	x1,x22,1
	add	x0,x0,x1,lsl 3
	mov	x1,R_CHOIX
	mov	x2,1
	bl	applet_row
	add	x22,x22,1
	b	.vl
.mic:	adr	x0,sn_s_mic			; micro I2S, son d'essai
	mov	x1,R_TITRE
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	applet_row
	bl	sn_i2s_on
	mov	x3,x0
	adr	x0,sn_s_i2s
	mov	x1,R_CASE
	mov	x2,2
	mov	x4,SN_I2S
	bl	applet_row
	adr	x0,sn_s_test
	mov	x1,R_CASE
	mov	x2,3
	mov	x3,1
	mov	x4,SN_TEST
	bl	applet_row
	adr	x0,sn_s_note
	mov	x1,R_NOTE
	mov	x2,0
	mov	x3,0
	mov	x4,0
	bl	applet_row
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = valeur d'une case -> x0 = 1 si cochee
sn_case:
	mov	x1,0
	adr	x2,nrows
	ldr	x2,[x2]
	adr	x3,rows
.r:	cmp	x1,x2
	b.hs	.non
	ldrb	w4,[x3,8]
	cmp	w4,R_CASE
	b.ne	.n
	ldr	x4,[x3,16]
	cmp	x4,x0
	b.ne	.n
	ldrb	w0,[x3,10]
	ret
.n:	add	x1,x1,1
	add	x3,x3,ROW_SIZE
	b	.r
.non:	mov	x0,0
	ret

sn_appliquer:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x0,0				; preferee (0 : automatique)
	bl	applet_choice
	tbnz	x0,63,.vol
	bl	ak_main
.vol:	mov	x0,1				; volume de la principale
	bl	applet_choice
	tbnz	x0,63,.mic
	mov	x19,x0
	bl	ak_state
	cbz	x0,.mic
	ldr	x1,[x0,SS_MAIN]
	add	x2,x0,SS_OUTS
	mov	x3,SS_OUT_MAX
.m:	ldr	w4,[x2,SO_ID]
	cmp	x4,x1
	b.ne	.mn
	cbz	x4,.mn
	mov	x0,x4
	mov	x1,x19
	ldr	w2,[x2,SO_MUTE]
	bl	ak_out_volume
	b	.mic
.mn:	add	x2,x2,64
	subs	x3,x3,1
	b.ne	.m
.mic:	mov	x0,SN_I2S			; micro I2S : services.txt
	bl	sn_case
	bl	sn_i2s_set
	adr	x1,ap_msg
	adr	x2,sn_s_fait
	str	x2,[x1]
	tbz	x0,63,.test
	adr	x2,sn_s_err
	str	x2,[x1]
.test:	mov	x0,SN_TEST
	bl	sn_case
	cbz	x0,.out
	bl	sn_beep
.out:	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 1 si SD0:/etc/services.txt lance i2smic (ligne non commentee)
sn_i2s_on:
	prologue
	bl	sn_read_services
	cmp	x0,0
	b.le	.non
	adr	x0,ap_file
.ligne:	mov	x1,x0				; debut de ligne : espaces
.sp:	ldrb	w2,[x1]
	cmp	w2,' '
	b.eq	.s1
	cmp	w2,9
	b.ne	.cmp
.s1:	add	x1,x1,1
	b	.sp
.cmp:	adr	x3,sn_p_i2s
	mov	x4,0
.c:	ldrb	w5,[x3,x4]
	cbz	w5,.oui
	ldrb	w6,[x1,x4]
	cmp	w5,w6
	b.ne	.suiv
	add	x4,x4,1
	b	.c
.suiv:	ldrb	w2,[x0],1
	cbz	w2,.non
	cmp	w2,10
	b.ne	.suiv
	b	.ligne
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	epilogue

; -> SD0:/etc/services.txt dans ap_file (termine par 0) ; x0 = octets (< 0 : erreur)
sn_read_services:
	prologue
	str	x19,[sp,-16]!
	adr	x0,sn_p_services
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,ap_file
	mov	x2,AP_FILE_MAX-1
	sys	SYS_READ
	mov	x1,x0
	mov	x0,x19
	mov	x19,x1
	sys	SYS_CLOSE
	mov	x0,x19
	cmp	x0,0
	csel	x1,x0,xzr,gt
	adr	x2,ap_file
	strb	wzr,[x2,x1]
.out:	ldr	x19,[sp],16
	epilogue

; x0 = 1 : la ligne i2smic active (sinon en commentaire, « # ») ; ajoutee a la
; fin si elle manque -> x0 = 0, < 0 si erreur
sn_i2s_set:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	sn_read_services
	tbnz	x0,63,.out
	adr	x20,ap_file			; lecture
	adr	x21,ap_out			; ecriture
	mov	x22,0				; ligne trouvee
.ligne:	ldrb	w0,[x20]
	cbz	w0,.fin
	mov	x1,x20				; « [#] SD0:/sys/svc/i2smic » ?
.sp:	ldrb	w2,[x1]
	cmp	w2,' '
	b.eq	.s1
	cmp	w2,'#'
	b.eq	.s1
	cmp	w2,9
	b.ne	.cmp
.s1:	add	x1,x1,1
	b	.sp
.cmp:	adr	x3,sn_p_i2s
	mov	x4,0
.c:	ldrb	w5,[x3,x4]
	cbz	w5,.found
	ldrb	w6,[x1,x4]
	cmp	w5,w6
	b.ne	.copy
	add	x4,x4,1
	b	.c
.found:	mov	x22,1				; la ligne : refaite
	mov	x20,x1
	cbnz	x19,.act
	mov	w2,'#'
	strb	w2,[x21],1
	mov	w2,' '
	strb	w2,[x21],1
.act:
.copy:	ldrb	w2,[x20],1			; le reste de la ligne
	strb	w2,[x21],1
	cbz	w2,.fin0
	cmp	w2,10
	b.ne	.copy
	b	.ligne
.fin0:	sub	x21,x21,1
.fin:	cbnz	x22,.write
	cbz	x19,.write			; absente et voulue : ajoutee
	adr	x0,ap_out			; (fichier sans fin de ligne au bout)
	cmp	x21,x0
	b.eq	.add
	ldurb	w2,[x21,-1]
	cmp	w2,10
	b.eq	.add
	mov	w2,10
	strb	w2,[x21],1
.add:	adr	x1,sn_p_i2s
.a:	ldrb	w2,[x1],1
	cbz	w2,.al
	strb	w2,[x21],1
	b	.a
.al:	mov	w2,13
	strb	w2,[x21],1
	mov	w2,10
	strb	w2,[x21],1
.write:	strb	wzr,[x21]
	adr	x0,sn_p_services
	adr	x1,ap_out
	bl	file_write
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; Son d'essai : la 440 Hz (triangle), 0,6 s, mono 44,1 kHz
sn_beep:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	movl	x0,44100
	mov	x1,1
	mov	x2,16
	mov	x3,0
	bl	ak_open
	tbnz	x0,63,.out
	mov	x19,x0
	mov	x20,26				; 26 morceaux de 1024 trames
	mov	w21,0				; phase (32 bits)
	movl	w22,42852281			; 440 x 2^32 / 44100
.blk:	adr	x0,ap_out
	mov	x1,1024
.s:	lsr	w2,w21,16			; triangle de -8192 a 8191
	mov	w3,65535
	sub	w3,w3,w2
	cmp	w2,32768
	csel	w2,w2,w3,lo
	sub	w2,w2,16384
	asr	w2,w2,1
	strh	w2,[x0],2
	add	w21,w21,w22
	subs	x1,x1,1
	b.ne	.s
	mov	x0,x19
	adr	x1,ap_out
	mov	x2,2048
	bl	ak_write_all
	subs	x20,x20,1
	b.ne	.blk
	mov	x0,x19
	bl	ak_drain
	mov	x0,x19
	bl	ak_close
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

sn_titre	db 'Son',0
sn_s_pref	db 'Sortie principale pr',0xC3,0xA9,'f',0xC3,0xA9,'r',0xC3,0xA9,'e',0
sn_s_auto	db 'Automatique',0
sn_s_vol	db 'Volume principal',0
sn_s_pc		db '25 %',0,0,0,0
		db '50 %',0,0,0,0
		db '75 %',0,0,0,0
		db '100 %',0,0,0
sn_s_mic	db 'Micro et essai',0
sn_s_i2s	db 'Micro I2S (INMP441)',0
sn_s_test	db 'Son d',0x27,'essai en appliquant',0
sn_s_note	db 'Micro : au red',0xC3,0xA9,'marrage.',0
sn_s_fait	db 'Enregistr',0xC3,0xA9,'.',0
sn_s_err	db 'SD0:/etc/services.txt non modifi',0xC3,0xA9,0
sn_p_services	db 'SD0:/etc/services.txt',0
sn_p_i2s	db 'SD0:/sys/svc/i2smic',0
	align 4

macro sn_data {
		align 8
sn_names	rb SS_OUT_MAX * 32
}
