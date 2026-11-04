; ============================================================================
;  kits/uikit_assoc.inc - UIKit : l'association des fichiers (fileassoc.h de
;  SystemKit d'Onyx)
;
;  Chaque application dit dans son manifeste (SD0:/apps/<nom>.app/app.txt) les
;  extensions qu'elle ouvre : « opens = rtf docx odt ». Le choix par defaut
;  d'une extension est dans SD0:/etc/fileassoc.ini, « ext = app » une par
;  ligne (# : commentaire), qui l'emporte : « Ouvrir avec... » l'ecrit quand
;  « Toujours » est coche, et il se modifie a la main. Sans ligne, la premiere
;  application qui declare l'extension ; sans application, l'Editeur (le texte
;  et les fichiers inconnus).
;
;	ui_assoc_app (chemin, tampon, taille)	le programme qui ouvre le fichier
;	ui_assoc_open (chemin)			l'ouvrir : un dossier dans Fichiers,
;						un paquet .app ou un programme lance
;	ui_assoc_set (extension, app)		le choix par defaut d'une extension
;	ui_open_with (chemin)			le dialogue « Ouvrir avec »
;	ui_arg (ligne d'arguments)		le premier argument, sans guillemets
;
;  Un chemin avec des espaces est passe entre guillemets (comme posix_spawn) :
;  ui_arg les retire pour un programme en assembleur.
; ============================================================================

UK_FA_BUF	= 4096			; fileassoc.ini, app.txt
UK_FA_NAME	= 32			; nom d'une application (paquet sans .app)
UK_FA_LABEL	= 48			; son libelle (name = ...)
UK_FA_ESZ	= 96			; entree du dialogue : nom, libelle, drapeaux
UK_FA_FLAGS	= UK_FA_NAME+UK_FA_LABEL
UK_FA_MAX	= 160			; applications montrees
UK_OW_W		= 440			; dialogue « Ouvrir avec »
UK_OW_LIST	= 240			; hauteur de la liste

; ---------------------------------------------------------------------------
;  Lecture des manifestes
; ---------------------------------------------------------------------------

; x0 = chemin -> x0 = 1 : son extension en minuscules dans uk_fa_ext ; 0 :
; aucune (pas de point, point en tete du nom, plus de 15 octets) (feuille)
uk_fa_ext:
	mov	x2,x0				; debut du nom
	mov	x3,0				; apres le dernier point
	mov	x4,x0
.c:	ldrb	w1,[x4],1
	cbz	w1,.e
	cmp	w1,'/'
	b.eq	.sl
	cmp	w1,':'
	b.eq	.sl
	cmp	w1,'.'
	b.ne	.c
	sub	x5,x4,1				; « .profil » : pas d'extension
	cmp	x5,x2
	b.eq	.c
	mov	x3,x4
	b	.c
.sl:	mov	x2,x4
	mov	x3,0
	b	.c
.e:	cbz	x3,.no
	adr	x5,uk_fa_ext_b
	mov	x6,0
.cp:	ldrb	w1,[x3,x6]
	cbz	w1,.end
	cmp	x6,15
	b.hs	.no
	sub	w7,w1,'A'
	cmp	w7,25
	b.hi	.lo
	add	w1,w1,32
.lo:	strb	w1,[x5,x6]
	add	x6,x6,1
	b	.cp
.end:	cbz	x6,.no
	strb	wzr,[x5,x6]
	mov	x0,1
	ret
.no:	adr	x5,uk_fa_ext_b
	strb	wzr,[x5]
	mov	x0,0
	ret

; x0 = chemin -> x0 = octets lus dans uk_fa_buf (termine par 0 ; 0 : absent)
uk_fa_read:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x1,uk_fa_buf
	strb	wzr,[x1]
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.none
	mov	x19,x0
	adr	x1,uk_fa_buf
	mov	x2,UK_FA_BUF-1
	sys	SYS_READ
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	cmp	x20,0
	csel	x20,xzr,x20,lt
	adr	x1,uk_fa_buf
	strb	wzr,[x1,x20]
	mov	x0,x20
	b	.out
.none:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = curseur dans un texte « cle = valeur » -> x0 = ligne suivante (0 : fin
; du texte), x1, x2 = cle (debut, fin), x3, x4 = valeur (debut, fin), sans
; blancs autour ; ligne vide, sans « = » ou commentaire : cle vide (feuille)
uk_fa_kv:
	ldrb	w5,[x0]
	cbz	w5,.end
	mov	x6,x0				; fin de la ligne
.eol:	ldrb	w5,[x6]
	cbz	w5,.got
	cmp	w5,10
	b.eq	.got
	add	x6,x6,1
	b	.eol
.got:	mov	x7,x6				; ligne suivante
	cbz	w5,.lb0
	add	x7,x7,1
.lb0:	mov	x1,x0
.lb:	cmp	x1,x6
	b.hs	.empty
	ldrb	w5,[x1]
	cmp	w5,' '
	b.eq	.lb1
	cmp	w5,9
	b.ne	.lk
.lb1:	add	x1,x1,1
	b	.lb
.lk:	cmp	w5,'#'
	b.eq	.empty
	cmp	w5,';'
	b.eq	.empty
	mov	x3,x1				; le « = »
.fe:	cmp	x3,x6
	b.hs	.empty
	ldrb	w5,[x3]
	cmp	w5,'='
	b.eq	.eq
	add	x3,x3,1
	b	.fe
.eq:	mov	x2,x3				; fin de la cle
.kt:	cmp	x2,x1
	b.ls	.v
	sub	x8,x2,1
	ldrb	w5,[x8]
	cmp	w5,' '
	b.hi	.v
	mov	x2,x8
	b	.kt
.v:	add	x3,x3,1				; debut de la valeur
.vs:	cmp	x3,x6
	b.hs	.ve
	ldrb	w5,[x3]
	cmp	w5,' '
	b.hi	.ve
	add	x3,x3,1
	b	.vs
.ve:	mov	x4,x3				; fin de la valeur : # (commentaire),
.vh:	cmp	x4,x6				; blancs, CR
	b.hs	.vt
	ldrb	w5,[x4]
	cmp	w5,'#'
	b.eq	.vt
	add	x4,x4,1
	b	.vh
.vt:	cmp	x4,x3
	b.ls	.ret
	sub	x8,x4,1
	ldrb	w5,[x8]
	cmp	w5,' '
	b.hi	.ret
	mov	x4,x8
	b	.vt
.ret:	mov	x0,x7
	ret
.empty:	mov	x1,x0
	mov	x2,x0
	mov	x3,x0
	mov	x4,x0
	mov	x0,x7
	ret
.end:	mov	x0,0
	ret

; x0, x1 = texte (debut, fin), x2 = mot en minuscules (termine par 0) -> x0 =
; 1 s'ils sont egaux, casse ASCII ignoree (feuille)
uk_fa_eq:
.c:	cmp	x0,x1
	b.hs	.end
	ldrb	w3,[x0],1
	ldrb	w4,[x2],1
	cbz	w4,.no
	sub	w5,w3,'A'
	cmp	w5,25
	b.hi	.l
	add	w3,w3,32
.l:	cmp	w3,w4
	b.eq	.c
.no:	mov	x0,0
	ret
.end:	ldrb	w4,[x2]
	cmp	w4,0
	cset	x0,eq
	ret

; x0, x1 = valeur (debut, fin), x2 = mot -> x0 = 1 si l'un de ses mots
; (separes par des blancs ou des virgules) est celui-la
uk_fa_has:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
.sk:	cmp	x19,x20
	b.hs	.no
	ldrb	w3,[x19]
	cmp	w3,','
	b.eq	.sk1
	cmp	w3,' '
	b.hi	.w
.sk1:	add	x19,x19,1
	b	.sk
.w:	mov	x1,x19				; fin du mot
.we:	cmp	x1,x20
	b.hs	.cmp
	ldrb	w3,[x1]
	cmp	w3,','
	b.eq	.cmp
	cmp	w3,' '
	b.ls	.cmp
	add	x1,x1,1
	b	.we
.cmp:	mov	x0,x19
	mov	x19,x1
	mov	x2,x21
	bl	uk_fa_eq
	cbz	x0,.sk
	mov	x0,1
	b	.out
.no:	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = cle voulue (minuscules) -> x0 = 1 si uk_fa_buf l'a, x1, x2 = sa
; valeur (debut, fin)
uk_fa_get:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x20,uk_fa_buf
.l:	mov	x0,x20
	bl	uk_fa_kv
	cbz	x0,.no
	mov	x20,x0
	stp	x3,x4,[sp,-16]!
	mov	x0,x1
	mov	x1,x2
	mov	x2,x19
	bl	uk_fa_eq
	ldp	x1,x2,[sp],16
	cbz	x0,.l
	b	.out
.no:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = source -> x0 = fin (sur le 0) (feuille)
uk_fa_cat:
.c:	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,.c
	sub	x0,x0,1
	ret

; x0 = nom d'application, x1 = suite (« /main ») -> x0 = uk_fa_path
; « SD0:/apps/<nom>.app<suite> »
uk_fa_apppath:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x0,uk_fa_path
	adr	x1,uk_fa_s_apps
	bl	uk_fa_cat
	mov	x1,'/'
	strb	w1,[x0],1
	mov	x1,x19
	bl	uk_fa_cat
	adr	x1,uk_fa_s_dotapp
	bl	uk_fa_cat
	mov	x1,x20
	bl	uk_fa_cat
	adr	x0,uk_fa_path
	ldp	x19,x20,[sp],16
	epilogue

; x0 = fonction (x0 = uk_fa_name ; le manifeste dans uk_fa_buf -> x0 = 0 :
; suivante) : appelee pour chaque application de SD0:/apps -> x0 = ce qu'elle
; a rendu d'autre que 0 (la parcours s'arrete), 0 a la fin
uk_fa_scan:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,uk_fa_s_apps
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.none
	mov	x20,x0
.rd:	mov	x0,x20
	adr	x1,uk_fa_ent
	sys	SYS_READDIR
	cmp	x0,1
	b.ne	.end
	adr	x0,uk_fa_ent			; « <nom>.app », nom court
	bl	uk_strlen
	cmp	x0,4
	b.ls	.rd
	cmp	x0,UK_FA_NAME+3
	b.hi	.rd
	sub	x2,x0,4
	adr	x1,uk_fa_ent
	add	x0,x1,x2
	adr	x3,uk_fa_s_dotapp
	mov	x1,0
.suf:	ldrb	w4,[x0,x1]
	ldrb	w5,[x3,x1]
	cbz	w5,.isapp
	orr	w4,w4,0x20
	cmp	w4,w5
	b.ne	.rd
	add	x1,x1,1
	b	.suf
.isapp:	adr	x0,uk_fa_name
	adr	x1,uk_fa_ent
	bl	uk_memcpy
	strb	wzr,[x0]
	adr	x0,uk_fa_name
	adr	x1,uk_fa_s_apptxt
	bl	uk_fa_apppath
	bl	uk_fa_read
	cbz	x0,.rd
	adr	x0,uk_fa_name
	blr	x19
	cbz	x0,.rd
	mov	x19,x0
	mov	x0,x20
	sys	SYS_CLOSE
	mov	x0,x19
	b	.out
.end:	mov	x0,x20
	sys	SYS_CLOSE
.none:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; fonction de uk_fa_scan : 1 si le manifeste ouvre uk_fa_ext_b
uk_fa_cb_opens:
	prologue
	adr	x0,uk_fa_s_opens
	bl	uk_fa_get
	cbz	x0,.out
	mov	x0,x1
	mov	x1,x2
	adr	x2,uk_fa_ext_b
	bl	uk_fa_has
.out:	epilogue

; x0 = nom d'application -> x0 = 1 si son paquet a un main
uk_fa_present:
	prologue
	sub	sp,sp,16
	adr	x1,uk_fa_s_main
	bl	uk_fa_apppath
	mov	x1,sp
	sys	SYS_STAT
	mvn	x0,x0
	lsr	x0,x0,63			; (>= 0 : present)
	add	sp,sp,16
	epilogue

; x0 = chemin -> x0 = 1 : uk_fa_name est l'application qui l'ouvre ; 0 :
; aucune, uk_fa_name est l'Editeur. uk_fa_ext_b : son extension
uk_fa_find:
	prologue
	stp	x19,x20,[sp,-16]!
	bl	uk_fa_ext
	cbz	x0,.none
	adr	x0,uk_fa_s_ini			; le choix par defaut
	bl	uk_fa_read
	cbz	x0,.scan
	adr	x0,uk_fa_ext_b
	bl	uk_fa_get
	cbz	x0,.scan
	sub	x2,x2,x1
	cbz	x2,.none			; « ext = » : rien ne l'ouvre
	cmp	x2,UK_FA_NAME-1
	b.hs	.scan
	adr	x0,uk_fa_name
	bl	uk_memcpy
	strb	wzr,[x0]
	adr	x0,uk_fa_name
	bl	uk_fa_present
	cbnz	x0,.yes				; (une application retiree : les manifestes)
.scan:	adr	x0,uk_fa_cb_opens
	bl	uk_fa_scan
	cbz	x0,.none
.yes:	mov	x0,1
	b	.out
.none:	adr	x0,uk_fa_name
	adr	x1,uk_fa_s_editor
	bl	uk_fa_cat
	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = programme, x1 = chemin (0 : sans argument) -> x0 = pid (< 0 : erreur).
; Un chemin avec des espaces ou des guillemets : entre guillemets, \" et \\.
uk_fa_spawn:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	cbz	x1,.go
	adr	x20,uk_fa_arg
	mov	x2,x1				; a citer ?
.q:	ldrb	w3,[x2],1
	cbz	w3,.plain
	cmp	w3,' '
	b.eq	.quote
	cmp	w3,'"'
	b.ne	.q
.quote:	mov	x0,x20
	mov	w3,'"'
	strb	w3,[x0],1
	mov	x4,x20				; (borne : la place du " et du 0)
	add	x4,x4,UK_PATH_MAX*2-4
.c:	ldrb	w3,[x1],1
	cbz	w3,.qe
	cmp	x0,x4
	b.hs	.qe
	cmp	w3,'"'
	b.eq	.esc
	cmp	w3,'\'
	b.ne	.put
.esc:	mov	w5,'\'
	strb	w5,[x0],1
.put:	strb	w3,[x0],1
	b	.c
.qe:	mov	w3,'"'
	strb	w3,[x0],1
	strb	wzr,[x0]
	mov	x1,x20
	b	.go
.plain:	mov	x0,x20				; (copie : le tampon de l'appelant peut
	bl	uk_fa_cat			; etre uk_fa_path)
	mov	x1,x20
.go:	mov	x0,x19
	sys	SYS_SPAWN
	ldp	x19,x20,[sp],16
	epilogue

; ---------------------------------------------------------------------------
;  Fonctions du kit
; ---------------------------------------------------------------------------

; x0 = chemin d'un fichier, x1 = tampon, x2 = taille -> x0 = 1 : le main de
; l'application qui l'ouvre dans le tampon ; 0 : aucune, celui de l'Editeur
ui_assoc_app:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	mov	x21,x2
	bl	uk_fa_find
	mov	x19,x0
	adr	x0,uk_fa_name
	adr	x1,uk_fa_s_main
	bl	uk_fa_apppath
	cbz	x21,.out
	mov	x22,x0
	bl	uk_strlen
	sub	x2,x21,1
	cmp	x0,x2
	csel	x2,x0,x2,lo
	mov	x0,x20
	mov	x1,x22
	bl	uk_memcpy
	strb	wzr,[x0]
.out:	mov	x0,x19
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin -> x0 = pid du programme lance (< 0 : erreur). Un dossier
; s'ouvre dans Fichiers, un paquet .app et un programme (ELF) se lancent, un
; fichier s'ouvre dans son application (ui_assoc_app).
ui_assoc_open:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,16
	mov	x19,x0
	mov	x1,sp
	sys	SYS_STAT
	tbnz	x0,63,.out
	ldr	x0,[sp,8]
	tst	x0,ATTR_DIR
	b.eq	.file
	mov	x0,x19				; un dossier : .app ?
	bl	uk_fa_ext
	cbz	x0,.dir
	adr	x0,uk_fa_ext_b
	adr	x1,uk_fa_s_app
	bl	uk_str_eq
	cbz	x0,.dir
	adr	x0,uk_fa_path			; son main
	strb	wzr,[x0]
	mov	x1,x19
	bl	uk_fa_cat
	adr	x1,uk_fa_s_main
	bl	uk_fa_cat
	adr	x0,uk_fa_path
	mov	x1,0
	bl	uk_fa_spawn
	b	.out
.dir:	adr	x0,uk_fa_s_files
	mov	x1,x19
	bl	uk_fa_spawn
	b	.out
.file:	mov	x0,x19				; un programme ? (en-tete ELF)
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.assoc
	mov	x20,x0
	str	wzr,[sp]
	mov	x1,sp
	mov	x2,4
	sys	SYS_READ
	mov	x0,x20
	sys	SYS_CLOSE
	ldr	w0,[sp]
	movl	w1,0x464C457F			; 7F 'E' 'L' 'F'
	cmp	w0,w1
	b.ne	.assoc
	mov	x0,x19
	mov	x1,0
	bl	uk_fa_spawn
	b	.out
.assoc:	mov	x0,x19
	bl	uk_fa_find
	adr	x0,uk_fa_name
	adr	x1,uk_fa_s_main
	bl	uk_fa_apppath
	mov	x1,x19
	bl	uk_fa_spawn
.out:	add	sp,sp,16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = extension (« docx », « .docx »), x1 = application (nom du paquet sans
; .app ; 0 ou vide : retirer le choix) -> x0 = 1 si SD0:/etc/fileassoc.ini est
; ecrit. Les autres lignes restent telles quelles.
ui_assoc_set:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x1
	ldrb	w1,[x0]				; l'extension, en minuscules
	cmp	w1,'.'
	b.ne	.e0
	add	x0,x0,1
.e0:	adr	x2,uk_fa_ext_b
	mov	x3,0
.e:	ldrb	w1,[x0,x3]
	cmp	x3,16
	b.hs	.fail
	sub	w4,w1,'A'
	cmp	w4,25
	b.hi	.e1
	add	w1,w1,32
.e1:	strb	w1,[x2,x3]
	add	x3,x3,1
	cbnz	w1,.e
	cmp	x3,1
	b.ls	.fail
	adr	x0,uk_fa_s_ini
	bl	uk_fa_read
	mov	x20,x0
	add	x0,x0,256			; le nouveau texte (en-tete, ligne)
	bl	uk_alloc
	cbz	x0,.fail
	mov	x21,x0
	mov	x22,x0				; fin
	cbnz	x20,.copy
	adr	x1,uk_fa_s_inihead		; un nouveau fichier : son en-tete
	mov	x0,x22
	bl	uk_fa_cat
	mov	x22,x0
.copy:	adr	x23,uk_fa_buf
.l:	mov	x0,x23
	bl	uk_fa_kv
	cbz	x0,.add
	mov	x24,x0
	mov	x0,x1				; la ligne de cette extension : retiree
	mov	x1,x2
	adr	x2,uk_fa_ext_b
	bl	uk_fa_eq
	cbnz	x0,.skip
	mov	x0,x22
	mov	x1,x23
	sub	x2,x24,x23
	bl	uk_memcpy
	mov	x22,x0
.skip:	mov	x23,x24
	b	.l
.add:	cbz	x19,.write
	ldrb	w0,[x19]
	cbz	w0,.write
	cmp	x22,x21				; (la derniere ligne finie)
	b.eq	.a1
	sub	x1,x22,1
	ldrb	w0,[x1]
	cmp	w0,10
	b.eq	.a1
	mov	w0,10
	strb	w0,[x22],1
.a1:	mov	x0,x22				; « ext = app »
	adr	x1,uk_fa_ext_b
	bl	uk_fa_cat
	adr	x1,uk_fa_s_eq
	bl	uk_fa_cat
	mov	x23,x0				; (uk_strlen ne garde pas x1, x2)
	mov	x0,x19
	bl	uk_strlen
	cmp	x0,UK_FA_NAME-1
	mov	x1,UK_FA_NAME-1
	csel	x2,x0,x1,lo
	mov	x0,x23
	mov	x1,x19
	bl	uk_memcpy
	mov	w1,10
	strb	w1,[x0],1
	mov	x22,x0
.write:	adr	x0,uk_fa_s_ini
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.wfail
	mov	x23,x0
	mov	x1,x21
	sub	x2,x22,x21
	sys	SYS_WRITE
	mov	x24,x0
	mov	x0,x23
	sys	SYS_CLOSE
	mov	x0,x21
	bl	uk_free
	sub	x2,x22,x21
	cmp	x24,x2
	cset	x0,eq
	b	.out
.wfail:	mov	x0,x21
	bl	uk_free
.fail:	mov	x0,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = ligne d'arguments (0 : aucune) -> x0 = la meme, son premier argument
; sans guillemets (\" et \\ rendus) quand il en a ; sinon telle quelle (feuille)
ui_arg:
	cbz	x0,.ret
	ldrb	w1,[x0]
	cmp	w1,'"'
	b.ne	.ret
	add	x1,x0,1
	mov	x2,x0
.c:	ldrb	w3,[x1],1
	cbz	w3,.end
	cmp	w3,'"'
	b.eq	.end
	cmp	w3,'\'
	b.ne	.put
	ldrb	w4,[x1]
	cmp	w4,'"'
	b.eq	.esc
	cmp	w4,'\'
	b.ne	.put
.esc:	mov	w3,w4
	add	x1,x1,1
.put:	strb	w3,[x2],1
	b	.c
.end:	strb	wzr,[x2]
.ret:	ret

; ---------------------------------------------------------------------------
;  Ouvrir avec
; ---------------------------------------------------------------------------

; fonction de uk_fa_scan : une entree de plus dans uk_fa_tab (nom, libelle,
; drapeau 1 : le manifeste ouvre l'extension)
uk_fa_cb_all:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,uk_fa_tn
	ldr	x1,[x0]
	cmp	x1,UK_FA_MAX
	b.hs	.out
	add	x2,x1,1
	str	x2,[x0]
	adr	x0,uk_fa_tab
	ldr	x0,[x0]
	mov	x2,UK_FA_ESZ
	madd	x19,x1,x2,x0
	mov	x0,x19				; le nom
	adr	x1,uk_fa_name
	bl	uk_fa_cat
	adr	x0,uk_fa_s_name			; le libelle (name = ...)
	bl	uk_fa_get
	cbz	x0,.noname
	sub	x2,x2,x1
	cbz	x2,.noname
	cmp	x2,UK_FA_LABEL-1
	b.lo	.cut
	mov	x2,UK_FA_LABEL-1
.cb:	ldrb	w3,[x1,x2]			; (sans couper un caractere UTF-8)
	and	w3,w3,0xC0
	cmp	w3,0x80
	b.ne	.cut
	sub	x2,x2,1
	b	.cb
.cut:	add	x0,x19,UK_FA_NAME
	bl	uk_memcpy
	strb	wzr,[x0]
	b	.flag
.noname:
	add	x0,x19,UK_FA_NAME
	adr	x1,uk_fa_name
	bl	uk_fa_cat
.flag:	adr	x0,uk_fa_name			; l'Editeur ouvre tout
	adr	x1,uk_fa_s_editor
	bl	uk_str_eq
	cbnz	x0,.on
	adr	x0,uk_fa_ext_b
	ldrb	w0,[x0]
	cbz	w0,.out
	bl	uk_fa_cb_opens
	cbz	x0,.out
.on:	mov	w0,1
	strb	w0,[x19,UK_FA_FLAGS]
.out:	mov	x0,0
	ldp	x19,x20,[sp],16
	epilogue

; x0 = liste, x1 = drapeau voulu -> les entrees de uk_fa_tab qui l'ont, par
; ordre de libelle ; la ligne de l'application par defaut dans uk_fa_row
uk_fa_rows:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x21,uk_fa_tab
	ldr	x21,[x21]
.next:	mov	x22,-1				; la plus petite qui reste
	adr	x0,uk_fa_tn
	ldr	x23,[x0]
.e:	cbz	x23,.got
	sub	x23,x23,1
	mov	x0,UK_FA_ESZ
	madd	x24,x23,x0,x21
	ldrb	w0,[x24,UK_FA_FLAGS]
	tbnz	w0,1,.e				; deja dans la liste
	and	w0,w0,1
	cmp	x0,x20
	b.ne	.e
	tbnz	x22,63,.take
	mov	x0,UK_FA_ESZ
	madd	x1,x22,x0,x21
	add	x1,x1,UK_FA_NAME
	add	x0,x24,UK_FA_NAME
	bl	uk_fa_icmp
	tbz	x0,63,.e
.take:	mov	x22,x23
	b	.e
.got:	tbnz	x22,63,.out
	mov	x0,UK_FA_ESZ
	madd	x24,x22,x0,x21
	ldrb	w0,[x24,UK_FA_FLAGS]
	orr	w0,w0,2
	strb	w0,[x24,UK_FA_FLAGS]
	mov	x0,x19
	add	x1,x24,UK_FA_NAME
	mov	x2,x22
	bl	ui_list_add
	mov	x23,x0
	mov	x0,x24				; l'application par defaut ?
	adr	x1,uk_fa_def
	bl	uk_str_eq
	cbz	x0,.next
	adr	x0,uk_fa_row
	str	x23,[x0]
	b	.next
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0, x1 = chaines -> x0 < 0, 0, > 0 (casse ASCII ignoree) (feuille)
uk_fa_icmp:
.c:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	sub	w4,w2,'A'
	cmp	w4,25
	b.hi	.a
	add	w2,w2,32
.a:	sub	w4,w3,'A'
	cmp	w4,25
	b.hi	.b
	add	w3,w3,32
.b:	cmp	w2,w3
	b.ne	.d
	cbnz	w2,.c
	mov	x0,0
	ret
.d:	sub	x0,x2,x3
	ret

; x0 = dialogue, x1 = titre de section -> « #titre » traduit dans la liste
uk_fa_section:
	prologue
	str	x19,[sp,-16]!
	ldr	x19,[x0,DG_LIST]
	mov	x0,x1
	bl	ui_tr
	mov	x1,x0
	adr	x0,uk_fa_arg
	mov	w2,'#'
	strb	w2,[x0],1
	bl	uk_fa_cat
	mov	x0,x19
	adr	x1,uk_fa_arg
	mov	x2,-1
	bl	ui_list_add
	ldr	x19,[sp],16
	epilogue

; fonction du dialogue : un double-clic dans la liste ouvre
uk_ow_fn:
	cmp	x2,UN_ACTIVATE
	b.ne	.ret
	ldr	x3,[x0,DG_LIST]
	cmp	x1,x3
	b.ne	.ret
	mov	x1,1
	b	ui_dialog_close
.ret:	ret

; x0 = chemin d'un fichier -> x0 = pid du programme lance (0 : Annuler, < 0 :
; erreur). Le dialogue « Ouvrir avec » : les applications qui declarent son
; extension (et l'Editeur) d'abord, puis les autres ; « Toujours » en fait le
; choix par defaut (ui_assoc_set).
ui_open_with:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	stp	x25,x26,[sp,-16]!
	mov	x20,x0
	mov	x25,0				; reponse : Annuler
	bl	uk_fa_find			; l'application par defaut
	adr	x0,uk_fa_def
	adr	x1,uk_fa_name
	bl	uk_fa_cat
	adr	x0,uk_fa_row
	mov	x1,-1
	str	x1,[x0]
	mov	x0,UK_FA_MAX*UK_FA_ESZ		; les applications
	bl	uk_alloc
	cbz	x0,.fail
	adr	x1,uk_fa_tab
	str	x0,[x1]
	adr	x1,uk_fa_tn
	str	xzr,[x1]
	adr	x0,uk_fa_cb_all
	bl	uk_fa_scan
	bl	uk_met
	mov	x23,x0
	ldr	x1,[x23,MT_CTL]			; hauteur : texte, nom, liste, case, boutons
	lsl	x1,x1,2
	add	x1,x1,UK_OW_LIST+80
	mov	x0,UK_OW_W
	adr	x2,uk_fa_s_title
	bl	ui_dialog_begin
	cbz	x0,.nodlg
	mov	x19,x0
	mov	x0,UK_DLG_PAD			; « Choisir l'application... »
	mov	x1,UK_DLG_PAD-8
	mov	x2,UK_OW_W-2*UK_DLG_PAD
	ldr	x3,[x23,MT_CTL]
	adr	x4,uk_fa_s_choose
	mov	x5,0
	bl	ui_label
	mov	x0,x20				; le nom du fichier, en gras
	bl	uk_fa_base
	mov	x4,x0
	mov	x0,UK_DLG_PAD
	ldr	x3,[x23,MT_CTL]
	add	x1,x3,UK_DLG_PAD-8
	mov	x2,UK_OW_W-2*UK_DLG_PAD
	mov	x5,ULB_BOLD
	bl	ui_label
	ldr	x1,[x23,MT_CTL]			; la liste
	add	x1,x1,x1
	add	x1,x1,UK_DLG_PAD
	mov	x0,UK_DLG_PAD
	mov	x2,UK_OW_W-2*UK_DLG_PAD
	mov	x3,UK_OW_LIST
	mov	x4,UL_SIDEBAR
	bl	ui_list
	str	x0,[x19,DG_LIST]
	mov	x21,x0
	mov	x0,x19
	adr	x1,uk_fa_s_sugg
	bl	uk_fa_section
	mov	x0,x21
	mov	x1,1
	bl	uk_fa_rows
	mov	x0,x19
	adr	x1,uk_fa_s_other
	bl	uk_fa_section
	mov	x0,x21
	mov	x1,0
	bl	uk_fa_rows
	adr	x0,uk_fa_row			; l'application par defaut choisie
	ldr	x1,[x0]
	tbnz	x1,63,.nosel
	mov	x0,x21
	bl	ui_w_set_value
.nosel:	mov	x22,0				; « Toujours » (avec une extension)
	adr	x0,uk_fa_ext_b
	ldrb	w0,[x0]
	cbz	w0,.nocb
	adr	x0,uk_fa_s_always
	bl	ui_tr
	mov	x1,x0
	adr	x0,uk_fa_arg
	bl	uk_fa_cat
	adr	x1,uk_fa_s_paren
	bl	uk_fa_cat
	adr	x1,uk_fa_ext_b
	bl	uk_fa_cat
	mov	w1,')'
	strb	w1,[x0],1
	strb	wzr,[x0]
	ldr	x1,[x23,MT_CTL]
	add	x1,x1,x1
	add	x1,x1,UK_DLG_PAD+UK_OW_LIST+10
	mov	x0,UK_DLG_PAD
	mov	x2,UK_OW_W-2*UK_DLG_PAD
	ldr	x3,[x23,MT_CTL]
	adr	x4,uk_fa_arg
	mov	x5,0
	bl	ui_checkbox
	mov	x22,x0
.nocb:	mov	x0,x19
	bl	uk_dlg_bar
	mov	x0,x19
	mov	x1,0
	adr	x2,uk_s_bopen
	mov	x3,1
	mov	x4,UB_DEFAULT
	mov	x5,UK_OW_W-16
	bl	ui_dialog_button
	mov	x5,x0
	mov	x0,x19
	mov	x1,1
	adr	x2,uk_s_cancel
	mov	x3,0
	mov	x4,0
	bl	ui_dialog_button
	mov	x0,x21
	bl	ui_w_focus
	mov	x0,x19
	adr	x1,uk_ow_fn
	bl	ui_dialog_run
	cbz	x0,.end
	mov	x0,x21				; l'application choisie
	bl	ui_w_get_value
	tbnz	x0,63,.end
	mov	x1,x0
	mov	x0,x21
	bl	ui_list_get
	tbnz	x1,63,.end			; (une section)
	adr	x0,uk_fa_tab
	ldr	x0,[x0]
	mov	x2,UK_FA_ESZ
	madd	x24,x1,x2,x0
	adr	x0,uk_fa_def			; (son nom : la table part avec le dialogue)
	strb	wzr,[x0]
	mov	x1,x24
	bl	uk_fa_cat
	mov	x25,1
	cbz	x22,.end
	mov	x0,x22
	bl	ui_w_get_value
	cbz	x0,.end
	adr	x0,uk_fa_ext_b			; le choix par defaut
	adr	x1,uk_fa_def
	bl	ui_assoc_set
.end:	mov	x0,x19
	bl	ui_dialog_end
.nodlg:	adr	x0,uk_fa_tab
	ldr	x0,[x0]
	bl	uk_free
	cbz	x25,.fail
	adr	x0,uk_fa_def			; ouvrir le fichier avec elle
	adr	x1,uk_fa_s_main
	bl	uk_fa_apppath
	mov	x1,x20
	bl	uk_fa_spawn
	b	.out
.fail:	mov	x0,0
.out:	ldp	x25,x26,[sp],16
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin -> x0 = son dernier composant (feuille)
uk_fa_base:
	mov	x1,x0
.c:	ldrb	w2,[x1],1
	cbz	w2,.r
	cmp	w2,'/'
	b.eq	.s
	cmp	w2,':'
	b.ne	.c
.s:	mov	x0,x1
	b	.c
.r:	ret

uk_fa_s_apps	db 'SD0:/apps',0
uk_fa_s_dotapp	db '.app',0
uk_fa_s_app	db 'app',0
uk_fa_s_apptxt	db '/app.txt',0
uk_fa_s_main	db '/main',0
uk_fa_s_ini	db 'SD0:/etc/fileassoc.ini',0
uk_fa_s_files	db 'SD0:/apps/files.app/main',0
uk_fa_s_editor	db 'editor',0
uk_fa_s_opens	db 'opens',0
uk_fa_s_name	db 'name',0
uk_fa_s_eq	db ' = ',0
uk_fa_s_paren	db ' (.',0
uk_fa_s_inihead	db '# Associations des fichiers : ',0xC2,0xAB,' extension = application ',0xC2,0xBB,10
		db '# (SD0:/apps/<application>.app ; UIKit, ui_assoc_*).',10,0
uk_fa_s_title	db 'Ouvrir avec',0
uk_fa_s_choose	db 'Choisir l',0x27,'application qui ouvre ce fichier :',0
uk_fa_s_sugg	db 'Applications propos',0xC3,0xA9,'es',0
uk_fa_s_other	db 'Autres applications',0
uk_fa_s_always	db 'Toujours ouvrir ce type de fichier avec elle',0
		align 4
