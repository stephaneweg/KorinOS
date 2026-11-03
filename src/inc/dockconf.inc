; ============================================================================
;  inc/dockconf.inc - les reglages du dock (SD0:/etc/dock.ini), communs au dock
;  (sys/dock) et a l'applet Dock du panneau de configuration (dockconf.h
;  d'Onyx : DockLayout, dock_layout_load). A inclure apres applist.inc. Une
;  ligne par reglage :
;
;	drawer   = Accessoires, editor	un tiroir : sa categorie, son application
;					principale (vide : la premiere par nom), dans
;					l'ordre du dock
;	hidden   = Jeux			une categorie sans tiroir
;	launcher = terminal		un lanceur apres les tiroirs (6 au plus)
;	desk     = Principal		un espace de travail, son nom (1 a 6)
;	corbeille = oui | non		(ARMPI)
;	taille    = auto | grande | moyenne | petite (ARMPI)
;
;  Les cles tiroir et lanceur sont lues comme drawer et launcher. Le dock a un
;  tiroir pour CHAQUE categorie des applications de la carte, sauf les
;  cachees et Shell, Settings, Emulators (et leurs noms francais) : celles que
;  le fichier ne nomme pas viennent a la fin, par nom.
;
;	bl applis_load ; bl dock_layout_load	dl_* (ligne : l'ordre du dock)
;	bl apps_sort				ap_ordre : les applications par nom
; ============================================================================

DOCK_MAXLAUNCHERS = 6
DOCK_MAXDESKS	= 6
DL_DESK		= 24			; nom d'un espace (NUL compris)

; -> x0 < 0, 0, > 0 : x0 et x1 compares sans la casse (ASCII)
str_ci_cmp:
	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	sub	w4,w2,'A'
	cmp	w4,25
	b.hi	.b
	add	w2,w2,32
.b:	sub	w4,w3,'A'
	cmp	w4,25
	b.hi	.c
	add	w3,w3,32
.c:	cmp	w2,w3
	b.ne	.diff
	cbnz	w2,str_ci_cmp
.diff:	sub	x0,x2,x3
	ret

; x0 = categorie (texte) -> x0 = 1 si elle peut avoir un tiroir
dock_category_docked:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldrb	w0,[x19]
	cbz	w0,.non
	adr	x20,dl_no_docked
.n:	ldrb	w0,[x20]
	cbz	w0,.oui
	mov	x0,x19
	mov	x1,x20
	bl	str_ci_cmp
	cbz	x0,.non
.sk:	ldrb	w0,[x20],1
	cbnz	w0,.sk
	b	.n
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = categorie (catalogue) -> x0 = sa ligne (-1 : aucune)
dl_find:
	adr	x1,dl_ncats
	ldr	x1,[x1]
	adr	x2,dl_cat
	mov	x3,0
.l:	cmp	x3,x1
	b.hs	.none
	ldrb	w4,[x2,x3]
	cmp	x4,x0
	b.eq	.got
	add	x3,x3,1
	b	.l
.got:	mov	x0,x3
	ret
.none:	mov	x0,-1
	ret

; x0 = categorie -> x0 = sa ligne (nouvelle si elle n'en a pas ; -1 : plus de
; place)
dl_add:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	dl_find
	tbz	x0,63,.out
	adr	x1,dl_ncats
	ldr	x0,[x1]
	cmp	x0,CA_MAX
	b.hs	.full
	add	x2,x0,1
	str	x2,[x1]
	adr	x1,dl_cat
	strb	w19,[x1,x0]
	adr	x1,dl_hidden
	strb	wzr,[x1,x0]
	adr	x1,dl_napps
	strb	wzr,[x1,x0]
	adr	x1,dl_app
	add	x1,x1,x0,lsl 5
	strb	wzr,[x1]
	b	.out
.full:	mov	x0,-1
.out:	ldr	x19,[sp],16
	epilogue

; (ini_read de dock.ini) x0 = cle, x1 = valeur
dl_line:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x1,dl_k_drawer
	bl	str_eq
	cbnz	x0,.drawer
	mov	x0,x19
	adr	x1,dl_k_tiroir
	bl	str_eq
	cbnz	x0,.drawer
	mov	x0,x19
	adr	x1,dl_k_launcher
	bl	str_eq
	cbnz	x0,.launcher
	mov	x0,x19
	adr	x1,dl_k_lanceur
	bl	str_eq
	cbnz	x0,.launcher
	mov	x0,x19
	adr	x1,dl_k_hidden
	bl	str_eq
	cbnz	x0,.hidden
	mov	x0,x19
	adr	x1,dl_k_desk
	bl	str_eq
	cbnz	x0,.desk
	mov	x0,x19
	adr	x1,dl_k_corbeille
	bl	str_eq
	cbnz	x0,.corb
	mov	x0,x19
	adr	x1,dl_k_taille
	bl	str_eq
	cbz	x0,.out
	mov	x19,0				; grande, moyenne, petite ; sinon auto
.ts:	cmp	x19,3
	b.hs	.out
	adr	x1,dl_v_tailles
	add	x1,x1,x19,lsl 3
	mov	x0,x20
	bl	str_eq
	cbnz	x0,.tset
	add	x19,x19,1
	b	.ts
.tset:	adr	x0,dl_taille
	str	x19,[x0]
	b	.out
.corb:	mov	x0,x20
	adr	x1,dl_v_non
	bl	str_eq
	eor	x0,x0,1
	adr	x1,dl_corbeille
	str	x0,[x1]
	b	.out
.desk:	adr	x0,dl_ndesks
	ldr	x1,[x0]
	cmp	x1,DOCK_MAXDESKS
	b.hs	.out
	add	x2,x1,1
	str	x2,[x0]
	mov	x0,DL_DESK
	adr	x2,dl_desk
	madd	x0,x1,x0,x2
	mov	x1,x20
	mov	x2,DL_DESK
	bl	str_copy_n
	b	.out
.launcher:
	mov	x0,x20
	bl	dl_lan_add
	b	.out
.hidden:
	ldrb	w0,[x20]
	cbz	w0,.out
	mov	x0,x20
	bl	cat_find
	tbnz	x0,63,.out
	bl	dl_add
	tbnz	x0,63,.out
	adr	x1,dl_hidden
	mov	w2,1
	strb	w2,[x1,x0]
	b	.out
.drawer:
	mov	x0,x20				; « categorie, application »
	bl	ini_field
	mov	x20,x1
	mov	x19,x0
	bl	dock_category_docked
	cbz	x0,.out
	mov	x0,x19
	bl	cat_find
	tbnz	x0,63,.out
	bl	dl_add
	tbnz	x0,63,.out
	mov	x21,x0
	cbz	x20,.out
	mov	x0,x20
	bl	ini_field
	mov	x1,x0
	adr	x0,dl_app
	add	x0,x0,x21,lsl 5
	ldrb	w2,[x0]				; (la premiere ligne garde sa place)
	cbnz	w2,.out
	mov	x2,AP_NOM
	bl	str_copy_n
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom : un lanceur de plus (6 au plus)
dl_lan_add:
	prologue
	ldrb	w1,[x0]
	cbz	w1,.out
	mov	x1,x0
	adr	x2,dl_nlan
	ldr	x3,[x2]
	cmp	x3,DOCK_MAXLAUNCHERS
	b.hs	.out
	add	x4,x3,1
	str	x4,[x2]
	adr	x0,dl_lan
	add	x0,x0,x3,lsl 5
	mov	x2,AP_NOM
	bl	str_copy_n
.out:	epilogue

; dock.ini fusionne avec le catalogue -> x0 = 0 sans fichier
dock_layout_load:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,dl_ncats
	str	xzr,[x0]
	adr	x0,dl_nlan
	str	xzr,[x0]
	adr	x0,dl_ndesks
	str	xzr,[x0]
	adr	x0,dl_corbeille
	mov	x1,1
	str	x1,[x0]
	adr	x0,dl_taille
	mov	x1,-1
	str	x1,[x0]
	adr	x0,dl_p_dock
	adr	x1,dl_line
	bl	ini_read
	mov	x22,x0
	cbnz	x22,.desks
	adr	x0,dl_d_terminal		; sans fichier : terminal, fichiers
	bl	dl_lan_add
	adr	x0,dl_d_files
	bl	dl_lan_add
.desks:	adr	x19,dl_ndesks			; aucun espace nomme : 4, de 1 a 4
	ldr	x0,[x19]
	cbnz	x0,.cats
	mov	x0,4
	str	x0,[x19]
	mov	x19,0
.dn:	mov	x0,DL_DESK
	adr	x1,dl_desk
	madd	x0,x19,x0,x1
	add	w1,w19,'1'
	strb	w1,[x0]
	strb	wzr,[x0,1]
	add	x19,x19,1
	cmp	x19,4
	b.lo	.dn
.cats:	adr	x0,dl_ncats			; les categories des applications : comptees,
	ldr	x21,[x0]			; les nouvelles ajoutees (x21 : premiere)
	mov	x19,0
.a:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.sort
	adr	x0,ap_cat
	ldrb	w20,[x0,x19]
	adr	x0,ca_nom
	mov	x1,CA_NOM
	madd	x0,x20,x1,x0
	bl	dock_category_docked
	cbz	x0,.an
	mov	x0,x20
	bl	dl_add
	tbnz	x0,63,.an
	adr	x1,dl_napps
	ldrb	w2,[x1,x0]
	add	w2,w2,1
	strb	w2,[x1,x0]
.an:	add	x19,x19,1
	b	.a
.sort:	add	x19,x21,1			; (les nouvelles par nom)
.s:	adr	x0,dl_ncats
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.main
	mov	x20,x19
.j:	cmp	x20,x21
	b.ls	.sn
	sub	x0,x20,1
	bl	dl_nom
	str	x0,[sp,-16]!
	mov	x0,x20
	bl	dl_nom
	mov	x1,x0
	ldr	x0,[sp],16
	bl	str_ci_cmp
	cmp	x0,0
	b.le	.sn
	sub	x0,x20,1
	mov	x1,x20
	bl	dl_swap
	sub	x20,x20,1
	b	.j
.sn:	add	x19,x19,1
	b	.s
.main:	mov	x19,0				; une application principale absente : aucune
.m:	adr	x0,dl_ncats
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.fin
	adr	x20,dl_app
	add	x20,x20,x19,lsl 5
	ldrb	w0,[x20]
	cbz	w0,.mn
	mov	x0,x20
	bl	app_find
	tbz	x0,63,.mn
	strb	wzr,[x20]
.mn:	add	x19,x19,1
	b	.m
.fin:	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = ligne -> x0 = le nom de sa categorie
dl_nom:
	adr	x1,dl_cat
	ldrb	w0,[x1,x0]
	mov	x1,CA_NOM
	adr	x2,ca_nom
	madd	x0,x0,x1,x2
	ret

; x0, x1 = lignes : les echange
dl_swap:
	adr	x2,dl_cat
	ldrb	w3,[x2,x0]
	ldrb	w4,[x2,x1]
	strb	w4,[x2,x0]
	strb	w3,[x2,x1]
	adr	x2,dl_hidden
	ldrb	w3,[x2,x0]
	ldrb	w4,[x2,x1]
	strb	w4,[x2,x0]
	strb	w3,[x2,x1]
	adr	x2,dl_napps
	ldrb	w3,[x2,x0]
	ldrb	w4,[x2,x1]
	strb	w4,[x2,x0]
	strb	w3,[x2,x1]
	adr	x2,dl_app
	add	x3,x2,x0,lsl 5
	add	x4,x2,x1,lsl 5
	mov	x5,AP_NOM/8
.c:	ldr	x6,[x3]
	ldr	x7,[x4]
	str	x7,[x3],8
	str	x6,[x4],8
	subs	x5,x5,1
	b.ne	.c
	ret

; ap_ordre <- les applications par libelle
apps_sort:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x21,ap_ordre
	mov	x19,0
.i:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	strb	w19,[x21,x19]
	mov	x20,x19
.j:	cbz	x20,.n
	sub	x22,x20,1
	ldrb	w0,[x21,x22]
	bl	app_label
	str	x0,[sp,-16]!
	ldrb	w0,[x21,x20]
	bl	app_label
	mov	x1,x0
	ldr	x0,[sp],16
	bl	str_ci_cmp
	cmp	x0,0
	b.le	.n
	ldrb	w0,[x21,x22]
	ldrb	w1,[x21,x20]
	strb	w1,[x21,x22]
	strb	w0,[x21,x20]
	mov	x20,x22
	b	.j
.n:	add	x19,x19,1
	b	.i
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = application -> x0 = son libelle
app_label:
	mov	x1,AP_LIB
	adr	x2,ap_lib
	madd	x0,x0,x1,x2
	ret

; x0 = ligne -> x0 = l'application principale de sa categorie : celle choisie,
; sinon la premiere par nom (-1 : aucune) ; apps_sort fait
dl_main:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x0,dl_app
	add	x0,x0,x19,lsl 5
	ldrb	w1,[x0]
	cbz	w1,.first
	bl	app_find
	tbz	x0,63,.out
.first:	adr	x0,dl_cat
	ldrb	w0,[x0,x19]
	adr	x1,ap_n
	ldr	x1,[x1]
	adr	x2,ap_ordre
	mov	x3,0
.l:	cmp	x3,x1
	b.hs	.none
	ldrb	w4,[x2,x3]
	adr	x5,ap_cat
	ldrb	w5,[x5,x4]
	cmp	x5,x0
	b.eq	.got
	add	x3,x3,1
	b	.l
.got:	mov	x0,x4
	b	.out
.none:	mov	x0,-1
.out:	ldr	x19,[sp],16
	epilogue

dl_p_dock	db 'SD0:/etc/dock.ini',0
dl_k_drawer	db 'drawer',0
dl_k_tiroir	db 'tiroir',0
dl_k_launcher	db 'launcher',0
dl_k_lanceur	db 'lanceur',0
dl_k_hidden	db 'hidden',0
dl_k_desk	db 'desk',0
dl_k_corbeille	db 'corbeille',0
dl_k_taille	db 'taille',0
dl_v_tailles	db 'grande',0,0
		db 'moyenne',0
		db 'petite',0,0
dl_v_non	db 'non',0
dl_d_terminal	db 'terminal',0
dl_d_files	db 'files',0
dl_no_docked	db 'Shell',0,'Settings',0,'Emulators',0
		db 'R',0xC3,0xA9,'glages',0
		db 'Param',0xC3,0xA8,'tres',0
		db 0xC3,0x89,'mulateurs',0,0
		align 4

macro dockconf_data {
		align 8
dl_ncats	dd 0
dl_nlan		dd 0
dl_ndesks	dd 0
dl_corbeille	dd 1
dl_taille	dd -1				; -1 auto, 0 grande, 1 moyenne, 2 petite
dl_cat		rb CA_MAX			; par ligne : categorie (catalogue),
dl_hidden	rb CA_MAX			; 1 : sans tiroir,
dl_napps	rb CA_MAX			; ses applications sur la carte
ap_ordre	rb AP_MAX			; (les applications par libelle)
		align 8
dl_app		rb CA_MAX * AP_NOM		; son application principale (vide : aucune)
dl_lan		rb DOCK_MAXLAUNCHERS * AP_NOM
dl_desk		rb DOCK_MAXDESKS * DL_DESK
}
