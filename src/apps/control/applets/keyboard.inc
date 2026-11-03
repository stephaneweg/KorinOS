; applets/keyboard.inc - applet Clavier et langue du panneau de configuration.
; La disposition du clavier : celles de SD0:/res/keyb (<NOM>.txt) ; Appliquer
; la change tout de suite (appel keymap) et l'ecrit dans SD0:/etc/options.txt
; (Â« keyb=NOM Â», lu au demarrage). La langue (lang=fr ou en dans
; SD0:/etc/system.ini) : AppKit la lit et traduit tout programme a son demarrage.
; Le fuseau : une region (liste deroulante), puis sa ville dans une liste sous
; la carte ; les fuseaux de SD0:/res/tz.txt (« nom regle », tools/tz/mktz.py).

CL_N	= 16
CL_TZBUF	= 16384			; SD0:/res/tz.txt (un peu plus de 12 Ko)
CL_TZ_MAX	= 512

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
.fu:	bl	cl_tz_lit			; le fuseau : sa region
	adr	x0,cl_s_fuseau
	bl	fm_ligne
	adr	x0,cl_fuseau_cur
	bl	cl_region_de
	mov	x21,x0
	adr	x19,cl_regs
	mov	x20,0
.f:	ldr	w0,[x19,x20,lsl 2]
	cbz	w0,.liste
	add	x1,x19,x0			; « Africa », puis son libelle
.n:	ldrb	w2,[x1],1
	cbnz	w2,.n
	cmp	x20,x21
	cset	x2,eq
	mov	x0,3
	bl	fm_item
	add	x20,x20,1
	b	.f
.liste:	adr	x0,fm_w				; puis sa ville, dans la liste
	ldr	x0,[x0,3*8]
	adr	x1,cl_region_on
	mov	x2,0
	bl	ui_w_on
	mov	x0,0				; (placee par cl_dessin)
	mov	x1,0
	mov	x2,100
	mov	x3,100
	mov	x4,0
	bl	ui_list
	adr	x1,cl_liste
	str	x0,[x1]
	adr	x1,fm_change
	mov	x2,0
	bl	ui_w_on
	mov	x0,x21
	bl	cl_remplit
	adr	x0,cl_dessin
	adr	x1,ap_dessin
	str	x0,[x1]
	adr	x0,cl_fermer
	adr	x1,ap_fermer
	str	x0,[x1]
	ldr	x21,[sp],16
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
.fu:	adr	x0,cl_liste			; le fuseau (la ville choisie) :
	ldr	x0,[x0]				; options.txt, puis NTP
	bl	ui_w_get_value
	tbnz	x0,63,.kb
	mov	x1,x0
	adr	x0,cl_liste
	ldr	x0,[x0]
	bl	ui_list_get			; (sa donnee : le nom du fuseau)
	cbz	x0,.kb
	mov	x2,x1
	adr	x0,p_options
	adr	x1,cl_k_fuseau
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

; SD0:/res/tz.txt -> cl_tz (noms, termines par un 0), cl_ntz
cl_tz_lit:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,cl_ntz
	str	xzr,[x0]
	adr	x0,cl_p_tz
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,cl_tzbuf
	mov	x2,CL_TZBUF-1
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	b.le	.out
	adr	x1,cl_tzbuf
	strb	wzr,[x1,x20]
	adr	x2,cl_tz
	mov	x3,0
.l:	ldrb	w4,[x1]				; une ligne : « nom regle »
	cbz	w4,.done
	mov	x5,x1
	cmp	w4,'#'
	cset	x6,eq
	mov	x7,0				; son premier espace
.e:	ldrb	w4,[x1]
	cbz	w4,.eol
	cmp	w4,10
	b.eq	.nl
	cmp	w4,' '
	b.ne	.e1
	cbnz	x7,.e1
	mov	x7,x1
.e1:	add	x1,x1,1
	b	.e
.nl:	strb	wzr,[x1],1
.eol:	cbnz	x6,.l				; (commentaire)
	cbz	x7,.l
	strb	wzr,[x7]
	cmp	x3,CL_TZ_MAX
	b.hs	.l
	str	x5,[x2,x3,lsl 3]
	add	x3,x3,1
	b	.l
.done:	adr	x0,cl_ntz
	str	x3,[x0]
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = fuseau -> x0 = sa region (cl_regs : le nom avant « / »), -1 si aucune
cl_region_de:
	adr	x1,cl_regs
	mov	x2,0
.r:	ldr	w3,[x1,x2,lsl 2]
	cbz	w3,.no
	add	x3,x1,x3
	mov	x4,x0
.c:	ldrb	w5,[x3],1
	cbz	w5,.end
	ldrb	w6,[x4],1
	cmp	w5,w6
	b.eq	.c
	b	.next
.end:	ldrb	w6,[x4]
	cbz	w6,.yes
	cmp	w6,'/'
	b.eq	.yes
.next:	add	x2,x2,1
	b	.r
.yes:	mov	x0,x2
	ret
.no:	mov	x0,-1
	ret

; x0 = region : la liste montre ses villes (« Argentina / Buenos Aires »),
; le fuseau actuel choisi s'il en est
cl_remplit:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	str	x23,[sp,-16]!
	mov	x19,x0
	adr	x0,cl_liste
	ldr	x0,[x0]
	bl	ui_list_clear
	mov	x23,-1				; l'element choisi
	tbnz	x19,63,.sel
	mov	x20,0
.z:	adr	x0,cl_ntz
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.sel
	adr	x0,cl_tz
	ldr	x21,[x0,x20,lsl 3]
	mov	x0,x21
	bl	cl_region_de
	cmp	x0,x19
	b.ne	.next
	mov	x1,x21				; le libelle : apres la region
.s:	ldrb	w2,[x1],1
	cbz	w2,.whole
	cmp	w2,'/'
	b.ne	.s
	b	.lab
.whole:	mov	x1,x21				; (UTC)
.lab:	adr	x0,cl_lbl
	mov	x3,60
.c:	ldrb	w2,[x1],1
	cbz	w2,.end
	cmp	x3,4
	b.lo	.end
	cmp	w2,'_'
	b.ne	.sl
	mov	w2,' '
	b	.put
.sl:	cmp	w2,'/'
	b.ne	.put
	mov	w4,' '
	strb	w4,[x0],1
	strb	w2,[x0],1
	sub	x3,x3,2
	mov	w2,' '
.put:	strb	w2,[x0],1
	sub	x3,x3,1
	b	.c
.end:	strb	wzr,[x0]
	adr	x0,cl_liste
	ldr	x0,[x0]
	adr	x1,cl_lbl
	mov	x2,x21
	bl	ui_list_add
	mov	x22,x0
	mov	x0,x21
	adr	x1,cl_fuseau_cur
	bl	str_eq
	cbz	x0,.next
	mov	x23,x22
.next:	add	x20,x20,1
	b	.z
.sel:	adr	x0,cl_liste
	ldr	x0,[x0]
	mov	x1,x23
	bl	ui_w_set_value
	ldr	x23,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (ui_w_on) une autre region : ses villes
cl_region_on:
	prologue
	stp	x0,x1,[sp,-16]!
	bl	fm_change
	ldp	x0,x1,[sp],16
	cmp	x1,UN_CHANGE
	b.ne	.out
	bl	ui_w_get_value
	bl	cl_remplit
	adr	x0,cl_liste
	ldr	x0,[x0]
	bl	ui_w_draw
.out:	epilogue

; (ap_dessin) la liste des villes sous la carte, jusqu'a Appliquer
cl_dessin:
	prologue
	bl	fm_haut
	adr	x1,fm_n
	ldr	x1,[x1]
	mov	x2,FM_H
	madd	x2,x1,x2,x0
	add	x2,x2,36			; (sous la note)
	adr	x0,ui_w
	ldp	x3,x4,[x0]
	sub	x4,x4,BTN_H+24
	sub	x4,x4,x2
	mov	x5,60
	cmp	x4,x5
	csel	x4,x5,x4,lt
	sub	x3,x3,SIDE_W+2*AP_MARGE
	mov	x1,SIDE_W+AP_MARGE
	adr	x0,cl_liste
	ldr	x0,[x0]
	bl	ui_w_move
	bl	fm_dessin
	epilogue

; (ap_fermer) la liste, puis les lignes
cl_fermer:
	prologue
	adr	x0,cl_liste
	ldr	x0,[x0]
	bl	ui_w_destroy
	adr	x0,cl_liste
	str	xzr,[x0]
	bl	fm_fermer
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
cl_regs		dw cl_r1-cl_regs, cl_r2-cl_regs, cl_r3-cl_regs, cl_r4-cl_regs, cl_r5-cl_regs, cl_r6-cl_regs
		dw cl_r7-cl_regs, cl_r8-cl_regs, cl_r9-cl_regs, cl_r10-cl_regs, cl_r11-cl_regs, 0
cl_r1		db 'Africa',0,'Afrique',0
cl_r2		db 'America',0,'Am',0xC3,0xA9,'rique',0
cl_r3		db 'Antarctica',0,'Antarctique',0
cl_r4		db 'Arctic',0,'Arctique',0
cl_r5		db 'Asia',0,'Asie',0
cl_r6		db 'Atlantic',0,'Atlantique',0
cl_r7		db 'Australia',0,'Australie',0
cl_r8		db 'Europe',0,'Europe',0
cl_r9		db 'Indian',0,'Oc',0xC3,0xA9,'an Indien',0
cl_r10		db 'Pacific',0,'Pacifique',0
cl_r11		db 'UTC',0,'UTC',0
cl_fz_paris	db 'Europe/Paris',0
cl_p_tz		db 'SD0:/res/tz.txt',0
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
cl_liste	rd 1				; les villes de la region
cl_ntz		rd 1
cl_tz		rd CL_TZ_MAX			; noms des fuseaux, dans cl_tzbuf
cl_lbl		rb 64
cl_tzbuf	rb CL_TZBUF
}
