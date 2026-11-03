; ============================================================================
;  inc/applist.inc - le catalogue des applications, pour la session du bureau
;  (dock, barre de menus) : les paquets SD0:/apps/<nom>.app (main :
;  l'executable ; app.txt : le manifeste, cles d'Onyx « name » et
;  « category »), et la lecture des fichiers « cle = valeur ».
;
;	segment readable executable	... include '../../inc/applist.inc'
;	segment readable writeable	... applis_data
;	bl applis_load ; ap_n applications, ca_n categories
; ============================================================================

AP_MAX		= 128
AP_NOM		= 32			; nom (paquet sans .app)
AP_LIB		= 40			; libelle UTF-8
CA_MAX		= 24
CA_NOM		= 24
INI_MAX		= 4096

; x0 = chemin, x1 = fonction (x0 = cle en minuscules, x1 = valeur ; les deux
; sans espaces autour) : appelee pour chaque ligne « cle = valeur »
; -> x0 = 0 si le fichier manque
ini_read:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x1
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.none
	mov	x19,x0
	adr	x1,ini_buf
	mov	x2,INI_MAX-1
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	csel	x20,xzr,x20,lt
	adr	x19,ini_buf
	strb	wzr,[x19,x20]
.line:	ldrb	w0,[x19]
	cbz	w0,.done
	mov	x20,x19				; fin de ligne -> 0 ; « # » : commentaire
.eol:	ldrb	w0,[x20]
	cbz	w0,.cut
	cmp	w0,10
	b.eq	.cut
	cmp	w0,'#'
	b.ne	.e1
	strb	wzr,[x20]
.e1:	add	x20,x20,1
	b	.eol
.cut:	ldrb	w22,[x20]
	strb	wzr,[x20]
	mov	x0,x19				; cle = valeur ?
.eq:	ldrb	w1,[x0]
	cbz	w1,.next
	cmp	w1,'='
	b.eq	.split
	add	x0,x0,1
	b	.eq
.split:	strb	wzr,[x0]
	add	x1,x0,1
	str	x1,[sp,-16]!
	mov	x0,x19
	bl	ini_trim
	mov	x2,x0				; cle en minuscules (ASCII)
.lc:	ldrb	w3,[x2]
	cbz	w3,.lcd
	sub	w4,w3,'A'
	cmp	w4,25
	b.hi	.lc1
	add	w3,w3,32
	strb	w3,[x2]
.lc1:	add	x2,x2,1
	b	.lc
.lcd:	str	x0,[sp,8]
	ldr	x0,[sp]
	bl	ini_trim
	mov	x1,x0
	ldr	x0,[sp,8]
	add	sp,sp,16
	blr	x21
.next:	cbz	w22,.done
	add	x19,x20,1
	b	.line
.done:	mov	x0,1
	b	.out
.none:	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine -> x0 sans espaces (ni tabulations, ni CR) devant ni derriere
ini_trim:
	ldrb	w1,[x0]
	cmp	w1,' '
	b.eq	.s
	cmp	w1,9
	b.ne	.end
.s:	add	x0,x0,1
	b	ini_trim
.end:	mov	x1,x0
.f:	ldrb	w2,[x1]
	cbz	w2,.back
	add	x1,x1,1
	b	.f
.back:	cmp	x1,x0
	b.ls	.ret
	ldurb	w2,[x1,-1]
	cmp	w2,' '
	b.ls	.cut
	b	.ret
.cut:	sub	x1,x1,1
	strb	wzr,[x1]
	b	.back
.ret:	ret

; x0 = valeur « a, b, c » -> x0 = premier champ (sans espaces), x1 = la suite
; (0 : aucune)
ini_field:
	prologue
	mov	x1,x0
.c:	ldrb	w2,[x1]
	cbz	w2,.last
	cmp	w2,','
	b.eq	.cut
	add	x1,x1,1
	b	.c
.cut:	strb	wzr,[x1]
	add	x1,x1,1
	str	x1,[sp,-16]!
	bl	ini_trim
	ldr	x1,[sp],16
	b	.out
.last:	bl	ini_trim
	mov	x1,0
.out:	epilogue

; x0 = nom -> x0 = application (-1 : inconnue)
app_find:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.a:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.none
	adr	x1,ap_nom
	add	x1,x1,x20,lsl 5
	mov	x0,x19
	bl	str_eq
	cbnz	x0,.found
	add	x20,x20,1
	b	.a
.found:	mov	x0,x20
	b	.out
.none:	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom -> x0 = categorie (ajoutee si nouvelle ; -1 : plus de place)
cat_find:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,0
.c:	adr	x0,ca_n
	ldr	x0,[x0]
	cmp	x20,x0
	b.hs	.add
	adr	x1,ca_nom
	mov	x2,CA_NOM
	madd	x1,x20,x2,x1
	mov	x0,x19
	bl	str_eq
	cbnz	x0,.found
	add	x20,x20,1
	b	.c
.add:	cmp	x20,CA_MAX
	b.hs	.none
	adr	x0,ca_nom
	mov	x2,CA_NOM
	madd	x0,x20,x2,x0
	mov	x1,x19
	mov	x2,CA_NOM
	bl	str_copy_n
	add	x0,x20,1
	adr	x1,ca_n
	str	x0,[x1]
.found:	mov	x0,x20
	b	.out
.none:	mov	x0,-1
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = source, x2 = taille (NUL compris) : copie bornee
str_copy_n:
	sub	x2,x2,1
.c:	cbz	x2,.end
	ldrb	w3,[x1],1
	cbz	w3,.end
	strb	w3,[x0],1
	sub	x2,x2,1
	b	.c
.end:	strb	wzr,[x0]
	ret

; catalogue : les paquets de SD0:/apps et leurs manifestes ; sans manifeste
; (ou sans categorie) : « Autres », libelle = nom
applis_load:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,96
	adr	x0,ap_n
	str	xzr,[x0]
	adr	x0,ca_n
	str	xzr,[x0]
	adr	x0,ap_dir
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
.next:	adr	x0,ap_n
	ldr	x20,[x0]
	cmp	x20,AP_MAX
	b.hs	.close
	mov	x0,x19
	adr	x1,ap_dirent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.close
	adr	x0,ap_dirent
	adr	x1,ap_ext
	bl	str_ends
	cbz	x0,.next
	adr	x0,ap_dirent
	bl	str_len
	sub	x2,x0,4
	cmp	x2,AP_NOM-1
	mov	x3,AP_NOM-1
	csel	x2,x3,x2,gt
	adr	x0,ap_dirent			; nom sans .app
	strb	wzr,[x0,x2]
	adr	x0,ap_nom
	add	x0,x0,x20,lsl 5
	adr	x1,ap_dirent
	mov	x2,AP_NOM
	bl	str_copy_n
	adr	x0,ap_cat
	mov	w1,0xFF				; (pas encore de categorie)
	strb	w1,[x0,x20]
	adr	x0,ap_lib			; libelle : le nom, sauf manifeste
	mov	x2,AP_LIB
	madd	x0,x20,x2,x0
	adr	x1,ap_dirent
	bl	str_copy_n
	adr	x0,ap_cur			; manifeste : SD0:/apps/<nom>.app/app.txt
	str	x20,[x0]
	mov	x0,sp
	strb	wzr,[x0]
	adr	x1,ap_path
	bl	str_cat
	adr	x1,ap_dirent
	bl	str_cat
	adr	x1,ap_man
	bl	str_cat
	mov	x0,sp
	adr	x1,manifest_line
	bl	ini_read
	add	x20,x20,1
	adr	x0,ap_n
	str	x20,[x0]
	b	.next
.close:	mov	x0,x19
	sys	SYS_CLOSE
	mov	x19,0				; sans categorie : « Autres »
.o:	adr	x0,ap_n
	ldr	x0,[x0]
	cmp	x19,x0
	b.hs	.out
	adr	x0,ap_cat
	ldrb	w0,[x0,x19]
	cmp	w0,0xFF
	b.ne	.on
	adr	x0,ap_autres
	bl	cat_find
	adr	x1,ap_cat
	strb	w0,[x1,x19]
.on:	add	x19,x19,1
	b	.o
.out:	add	sp,sp,96
	ldp	x19,x20,[sp],16
	epilogue

; (ini_read d'un manifeste, application ap_cur) x0 = cle, x1 = valeur
manifest_line:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x1,k_name
	bl	str_eq
	cbnz	x0,.name
	mov	x0,x19
	adr	x1,k_category
	bl	str_eq
	cbz	x0,.out
	mov	x0,x20				; category = la categorie
	bl	cat_find
	tbnz	x0,63,.out
	adr	x1,ap_cur
	ldr	x1,[x1]
	adr	x2,ap_cat
	strb	w0,[x2,x1]
	b	.out
.name:
if defined lang_tr				; (les services qui n'ont que ini_read : sans AppKit)
	mov	x0,x20				; le libelle traduit ; la categorie reste la
	bl	lang_tr				; cle : lang_tr a l'affichage
	mov	x20,x0
end if
	adr	x1,ap_cur			; name = le libelle
	ldr	x1,[x1]
	adr	x0,ap_lib
	mov	x2,AP_LIB
	madd	x0,x1,x2,x0
	mov	x1,x20
	bl	str_copy_n
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = application : la lance (SD0:/apps/<nom>.app/main)
app_launch:
	prologue
	sub	sp,sp,64
	mov	x1,x0
	mov	x0,sp
	strb	wzr,[x0]
	str	x1,[sp,56]
	adr	x1,ap_path
	bl	str_cat
	ldr	x1,[sp,56]
	adr	x2,ap_nom
	add	x1,x2,x1,lsl 5
	bl	str_cat
	adr	x1,ap_main
	bl	str_cat
	mov	x0,sp
	mov	x1,0
	sys	SYS_SPAWN
	add	sp,sp,64
	epilogue

ap_dir		db 'SD0:/apps',0
ap_path		db 'SD0:/apps/',0
ap_ext		db '.app',0
ap_main		db '.app/main',0
ap_man		db '.app/app.txt',0
k_name		db 'name',0
k_category	db 'category',0
ap_autres	db 'Autres',0
		align 4

macro applis_data {
		align 8
ap_n		dd 0
ca_n		dd 0
ap_cur		dd 0				; application dont on lit le manifeste
ap_nom		rb AP_MAX * AP_NOM
ap_lib		rb AP_MAX * AP_LIB
ap_cat		rb AP_MAX
ca_nom		rb CA_MAX * CA_NOM
ap_dirent	rb DIRENT_BYTES
ini_buf		rb INI_MAX
		align 8
}
