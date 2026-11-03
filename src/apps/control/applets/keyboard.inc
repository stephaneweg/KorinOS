; applets/keyboard.inc - applet Clavier et langue du panneau de configuration.
; La disposition du clavier : celles de SD0:/res/keyb (<NOM>.txt) ; Appliquer
; la change tout de suite (appel keymap) et l'ecrit dans SD0:/etc/options.txt
; (Â« keyb=NOM Â», lu au demarrage). La langue (lang=fr ou en dans
; SD0:/etc/system.ini) : AppKit la lit et traduit tout programme a son demarrage.

CL_N	= 16

cl_ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	adr	x0,cl_courant			; (sans keyb= : FR integre)
	adr	x1,cl_fr_nom
	mov	x2,16
	bl	str_copy_n
	adr	x0,cl_pays_cur			; (sans pays= : FR ; sans fuseau= :
	adr	x1,cl_fr_nom			;  Europe/Paris)
	mov	x2,4
	bl	str_copy_n
	adr	x0,cl_fuseau_cur
	adr	x1,cl_fz_paris
	mov	x2,32
	bl	str_copy_n
	adr	x0,cl_lang_cur			; (sans lang= : francais)
	adr	x1,cl_v_fr
	mov	x2,4
	bl	str_copy_n
	adr	x0,p_options			; disposition actuelle (sans : FR)
	adr	x1,cl_ligne
	bl	ini_read
	adr	x0,p_systeme			; la langue : celle de tout le systeme (AppKit)
	adr	x1,cl_ligne
	bl	ini_read
	adr	x0,cl_s_sous
	adr	x1,cl_s_note
	bl	fm_debut
	adr	x0,cl_s_disp
	bl	fm_ligne
	adr	x0,cl_p_keyb			; les dispositions de SD0:/res/keyb
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.lang
	mov	x19,x0
	mov	x20,0				; nombre
.d:	cmp	x20,CL_N
	b.hs	.close
	mov	x0,x19
	adr	x1,ap_dirent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.close
	adr	x0,ap_dirent
	adr	x1,cl_s_txt
	bl	str_ends
	cbz	x0,.d
	adr	x0,ap_dirent
	bl	str_len
	sub	x2,x0,4
	cmp	x2,15
	mov	x3,15
	csel	x2,x3,x2,gt
	adr	x0,ap_dirent
	strb	wzr,[x0,x2]
	adr	x0,cl_dispos			; nom sans .txt
	add	x0,x0,x20,lsl 4
	adr	x1,ap_dirent
	mov	x2,16
	bl	str_copy_n
	adr	x0,cl_dispos
	add	x0,x0,x20,lsl 4
	adr	x1,cl_courant
	bl	str_eq
	mov	x2,x0
	adr	x1,cl_dispos
	add	x1,x1,x20,lsl 4
	mov	x0,0
	bl	fm_item
	add	x20,x20,1
	b	.d
.close:	mov	x0,x19
	sys	SYS_CLOSE
.lang:	adr	x0,cl_s_langue			; la langue
	bl	fm_ligne
	adr	x0,cl_lang_cur
	adr	x1,cl_v_fr
	bl	str_eq
	mov	x2,x0
	mov	x0,1
	adr	x1,cl_s_fr
	bl	fm_item
	adr	x0,cl_lang_cur
	adr	x1,cl_v_en
	bl	str_eq
	mov	x2,x0
	mov	x0,1
	adr	x1,cl_s_en
	bl	fm_item
	adr	x0,cl_s_pays			; le pays
	bl	fm_ligne
	adr	x19,cl_pays
	mov	x20,0
.p:	ldr	w0,[x19,x20,lsl 2]		; (decalage du libelle)
	cbz	w0,.fu
	add	x21,x19,x0			; "FR\0Libelle"
	mov	x0,x21
	adr	x1,cl_pays_cur
	bl	str_eq
	mov	x2,x0
	mov	x0,2
	add	x1,x21,3
	bl	fm_item
	add	x20,x20,1
	b	.p
.fu:	adr	x0,cl_s_fuseau			; le fuseau
	bl	fm_ligne
	adr	x19,cl_fuseaux
	mov	x20,0
.f:	ldr	w0,[x19,x20,lsl 2]
	cbz	w0,.fin
	add	x21,x19,x0
	mov	x0,x21
	adr	x1,cl_fuseau_cur
	bl	str_eq
	mov	x2,x0
	mov	x0,3
	mov	x1,x21
	bl	fm_item
	add	x20,x20,1
	b	.f
.fin:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (ini_read) x0 = cle, x1 = valeur : Â« keyb Â»
cl_ligne:
	prologue
	stp	x0,x1,[sp,-16]!
	adr	x1,cl_k_keyb
	bl	str_eq
	ldp	x2,x1,[sp]
	cbz	x0,.pays
	adr	x0,cl_courant
	mov	x2,16
	bl	str_copy_n
	b	.out
.pays:	mov	x0,x2
	adr	x1,cl_k_pays
	bl	str_eq
	ldp	x2,x1,[sp]
	cbz	x0,.fuseau
	adr	x0,cl_pays_cur
	mov	x2,4
	bl	str_copy_n
	adr	x0,cl_pays_cur			; (majuscules)
	ldrb	w1,[x0]
	bic	w1,w1,0x20
	strb	w1,[x0]
	ldrb	w1,[x0,1]
	bic	w1,w1,0x20
	strb	w1,[x0,1]
	b	.out
.fuseau:
	mov	x0,x2
	adr	x1,cl_k_fuseau
	bl	str_eq
	ldp	x2,x1,[sp]
	cbz	x0,.lang
	adr	x0,cl_fuseau_cur
	mov	x2,32
	bl	str_copy_n
	b	.out
.lang:	mov	x0,x2
	adr	x1,cl_k_lang
	bl	str_eq
	ldp	x2,x1,[sp]
	cbz	x0,.out
	adr	x0,cl_lang_cur
	mov	x2,4
	bl	str_copy_n
.out:	add	sp,sp,16
	epilogue

; Appliquer : la disposition tout de suite, et dans SD0:/etc/options.txt
cl_appliquer:
	prologue
	str	x19,[sp,-16]!
	mov	x0,1				; la langue : system.ini (AppKit)
	bl	fm_valeur
	tbnz	x0,63,.pa
	adr	x2,cl_v_fr
	adr	x1,cl_v_en
	cmp	x0,0
	csel	x2,x2,x1,eq
	adr	x0,p_systeme
	adr	x1,cl_k_lang
	bl	ini_set
.pa:	mov	x0,2				; le pays : options.txt et le Wi-Fi
	bl	fm_valeur
	tbnz	x0,63,.fu
	adr	x1,cl_pays
	ldr	w0,[x1,x0,lsl 2]
	add	x19,x1,x0			; "FR"
	adr	x0,p_options
	adr	x1,cl_k_pays
	mov	x2,x19
	bl	ini_set
	mov	x0,x19
	bl	wifi_country			; (sans Wi-Fi : E_NODEV, sans effet)
.fu:	mov	x0,3				; le fuseau : options.txt, puis NTP
	bl	fm_valeur
	tbnz	x0,63,.kb
	adr	x1,cl_fuseaux
	ldr	w0,[x1,x0,lsl 2]
	add	x19,x1,x0
	adr	x0,p_options
	adr	x1,cl_k_fuseau
	mov	x2,x19
	bl	ini_set
	mov	x0,0				; (netd reprend l'heure : demande seule)
	bl	net_ntp_sync
.kb:	mov	x0,0
	bl	fm_valeur
	tbnz	x0,63,.out
	adr	x19,cl_dispos
	add	x19,x19,x0,lsl 4
	mov	x0,x19
	bl	cl_keymap
	adr	x2,cl_s_erreur
	tbnz	x0,63,.msg
	adr	x0,p_options
	adr	x1,cl_k_keyb
	mov	x2,x19
	bl	ini_set
	adr	x2,cl_s_fait
.msg:	adr	x0,ap_msg
	str	x2,[x0]
.out:	ldr	x19,[sp],16
	epilogue

; x0 = disposition (« FR », « US »...) : a HIDManager (service « hid »),
; qui la charge depuis SD0:/res/keyb -> x0 = 0 ou erreur
cl_keymap:
	prologue
	str	x19,[sp,-16]!
	sub	sp,sp,512			; message
	mov	x19,x0
	adr	x0,cl_s_hid
	mov	x1,1000
	sys	SYS_SVC_OPEN
	tbnz	x0,63,.out
	mov	x2,2				; HID_OP_KEYMAP
	str	x2,[sp]
	add	x3,sp,48			; donnees : le nom
	mov	x4,15
.c:	ldrb	w5,[x19],1
	strb	w5,[x3],1
	cbz	w5,.call
	subs	x4,x4,1
	b.ne	.c
	strb	wzr,[x3]
.call:	mov	x1,sp
	sys	SYS_SVC_CALL
.out:	add	sp,sp,512
	ldr	x19,[sp],16
	epilogue

cl_s_hid	db 'hid',0
cl_titre	db 'R',0xC3,0xA9,'gion et langue',0
cl_s_sous	db 'Clavier, langue et heure de cet appareil.',0
cl_s_pays	db 'Pays (Wi-Fi)',0
cl_s_fuseau	db 'Fuseau horaire (NTP)',0
cl_k_pays	db 'pays',0
cl_k_fuseau	db 'fuseau',0
		align 4
cl_pays		dw cl_p1-cl_pays, cl_p2-cl_pays, cl_p3-cl_pays, cl_p4-cl_pays
		dw cl_p5-cl_pays, cl_p6-cl_pays, cl_p7-cl_pays, cl_p8-cl_pays, 0
cl_p1		db 'FR',0,'France',0
cl_p2		db 'BE',0,'Belgique',0
cl_p3		db 'CH',0,'Suisse',0
cl_p4		db 'LU',0,'Luxembourg',0
cl_p5		db 'CA',0,'Canada',0
cl_p6		db 'DE',0,'Allemagne',0
cl_p7		db 'GB',0,'Royaume-Uni',0
cl_p8		db 'US',0,0xC3,0x89,'tats-Unis',0
		align 4
cl_fuseaux	dw cl_fz_paris-cl_fuseaux, cl_fz2-cl_fuseaux, cl_fz3-cl_fuseaux
		dw cl_fz4-cl_fuseaux, cl_fz5-cl_fuseaux, cl_fz6-cl_fuseaux, cl_fz7-cl_fuseaux, 0
cl_fz_paris	db 'Europe/Paris',0
cl_fz2		db 'Europe/London',0
cl_fz3		db 'Europe/Lisbon',0
cl_fz4		db 'Europe/Athens',0
cl_fz5		db 'Europe/Moscow',0
cl_fz6		db 'America/New_York',0
cl_fz7		db 'UTC',0
cl_s_disp	db 'Disposition du clavier',0
cl_s_langue db 'Langue',0
cl_s_fr	db 'Fran',0xC3,0xA7,'ais',0
cl_s_en	db 'English',0
cl_s_note	db 'La traduction est progressive : certaines applications restent en anglais.',0
cl_k_lang	db 'lang',0
cl_v_fr		db 'fr',0
cl_v_en		db 'en',0
cl_s_fait	db 'Disposition appliqu',0xC3,0xA9,'e.',0
cl_s_erreur db 'Disposition illisible.',0
cl_k_keyb	db 'keyb',0
cl_p_keyb	db 'SD0:/res/keyb',0
cl_s_txt	db '.txt',0
cl_fr_nom db 'FR',0
	align 4

macro cl_data {
		align 8
cl_courant	rb 16
cl_pays_cur	rb 4
cl_lang_cur	rb 4
cl_fuseau_cur	rb 32
cl_dispos	rb CL_N * 16
}
