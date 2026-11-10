; exec.inc - vfs : le bit d'execution (docs/FICHIERS.md, « Le bit d'execution » ;
; etude docs/TERMINAL-ET-FORK.md 5.4). FAT32 et exFAT n'ont pas de bit x :
;  * un fichier est executable par son chemin : sous SD0:/bin, SD0:/sys, un
;    SD0:/apps/<nom>.app/main, ou sous un dossier de SD0:/etc/exec.txt (une
;    ligne par dossier ; absent : exd_defaut, les outils de /gcc, /dotnet, les
;    greffons de Koton) ; lu une fois, a la premiere question ;
;  * sinon par l'index de son volume : <VOL>:/.exec, un chemin relatif par
;    ligne, lu a la premiere question sur ce volume puis tenu en memoire
;    (IDX_CAP octets, jamais rendus : la place resert si le volume revient) ;
;    RAM0: (ramfs) en memoire seulement ; casse ignoree, sauf sur ramfs ;
;  * V_STATX : V_STAT, et ATTR_EXEC pour un fichier executable ; V_CHMOD (a =
;    mode) : un bit x ajoute le chemin a l'index, aucun l'en retire (rien pour
;    un repertoire ni un chemin des regles) ; V_UNLINK retire le chemin (et ce
;    qu'il contient) ; V_RENAME deplace les entrees (appel direct au service
;    s'il y en a, sinon transmise comme avant) ;
;  * V_EXEC : un fichier non executable : E_PERM ; un script (ni ELF ni MZ) :
;    l'interprete de sa ligne #! (avec son argument), sinon /bin/sh, est
;    ouvert a sa place : ATTR_SCRIPT, donnees : nom du script, 0, interprete
;    resolu, 0, argument, 0, script resolu (« SD0:/... »), 0. AppKit et
;    libposix ajoutent l'argument et le script devant les arguments.
; Tout passe par msg2 : msg tient la requete en cours (et la reponse de stat).

IDX_CAP		= 32768			; octets de l'index d'un volume
EXD_MAX		= 16			; dossiers de exec.txt
EXD_TAMPON	= 4096
TETE		= 128			; octets lus en tete d'un programme
IARG_MAX	= 128
E_PERM_X	= -1			; E_PERM : pas executable

; x0 = entree d'un volume -> x0 = resultat de la requete msg2 a son service
appel2:
	prologue
	ldr	x0,[x0,VOL_CANAL]
	adr	x1,msg2
	sys	SYS_SVC_CALL
	epilogue

; x0 = op, x1 = a, x2 = b, x3 = c : msg2 prepare (d = 0, sans donnees)
msg2_op:
	adr	x4,msg2
	str	x0,[x4]
	stp	x1,x2,[x4,MSG_A]
	stp	x3,xzr,[x4,MSG_C]
	str	wzr,[x4,MSG_LEN]
	ret

; x0 = msg2, x1 = chemin relatif : dans les donnees (MSG_LEN) ; ne touche que x2-x4
msg2_chemin:
	add	x2,x0,MSG_DATA
	mov	x3,0
.c:	ldrb	w4,[x1,x3]
	strb	w4,[x2,x3]
	add	x3,x3,1
	cbz	w4,.f
	cmp	x3,FS_DONNEES-1
	b.lo	.c
	strb	wzr,[x2,x3]
.f:	str	w3,[x0,MSG_LEN]
	ret

; x0 = entree du volume, x1 = chemin relatif, x2 = tampon, x3 = octets au plus
; -> x0 = octets lus, ou erreur (E_NOENT : pas de fichier)
vol_lire_fichier:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x0,FS_OUVRIR
	mov	x1,O_READ
	mov	x2,0
	mov	x3,0
	bl	msg2_op
	adr	x0,msg2
	mov	x1,x20
	bl	msg2_chemin
	mov	x0,x19
	bl	appel2
	tbnz	x0,63,.out
	adr	x23,msg2
	ldr	x24,[x23,MSG_B]			; numero
	mov	x20,0				; position
.lit:	sub	x3,x22,x20
	cbz	x3,.fin
	cmp	x3,FS_DONNEES
	mov	x4,FS_DONNEES
	csel	x3,x4,x3,hi
	mov	x0,FS_LIRE
	mov	x1,x24
	mov	x2,x20
	bl	msg2_op
	mov	x0,x19
	bl	appel2
	cmp	x0,0
	b.le	.fin
	sub	x1,x22,x20			; (jamais plus que demande)
	cmp	x0,x1
	csel	x0,x1,x0,hi
	add	x1,x23,MSG_DATA
	add	x2,x21,x20
	mov	x3,0
.cp:	ldrb	w4,[x1,x3]
	strb	w4,[x2,x3]
	add	x3,x3,1
	cmp	x3,x0
	b.lo	.cp
	add	x20,x20,x0
	b	.lit
.fin:	mov	x0,FS_FERMER
	mov	x1,x24
	mov	x2,0
	mov	x3,0
	bl	msg2_op
	mov	x0,x19
	bl	appel2
	mov	x0,x20
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree du volume, x1 = chemin relatif, x2 = tampon, x3 = octets : le
; fichier est recrit -> x0 = 0 ou erreur
vol_ecrire_fichier:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3
	mov	x0,FS_OUVRIR
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	mov	x2,0
	mov	x3,0
	bl	msg2_op
	adr	x0,msg2
	mov	x1,x20
	bl	msg2_chemin
	mov	x0,x19
	bl	appel2
	tbnz	x0,63,.out
	adr	x23,msg2
	ldr	x24,[x23,MSG_B]			; numero
	mov	x20,0				; position
.ecr:	sub	x3,x22,x20
	cbz	x3,.fin
	cmp	x3,FS_DONNEES
	mov	x4,FS_DONNEES
	csel	x3,x4,x3,hi
	mov	x0,FS_ECRIRE
	mov	x1,x24
	mov	x2,x20
	bl	msg2_op
	str	w3,[x23,MSG_LEN]
	add	x1,x23,MSG_DATA
	add	x2,x21,x20
	mov	x4,0
.cp:	ldrb	w5,[x2,x4]
	strb	w5,[x1,x4]
	add	x4,x4,1
	cmp	x4,x3
	b.lo	.cp
	mov	x0,x19
	bl	appel2
	cmp	x0,0
	b.le	.err
	add	x20,x20,x0
	b	.ecr
.err:	mov	x20,E_NOSPC
	b	.ferme
.fin:	mov	x20,0
.ferme:	mov	x0,FS_FERMER
	mov	x1,x24
	mov	x2,0
	mov	x3,0
	bl	msg2_op
	mov	x0,x19
	bl	appel2
	mov	x0,x20
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chemin, x1 = prefixe (minuscules) -> x0 = 1 si le chemin commence par
; le prefixe (casse ignoree), x1 = la suite ; sinon x0 = 0 (ne touche que x0-x4)
prefixe_ci:
.c:	ldrb	w3,[x1],1
	cbz	w3,.oui
	ldrb	w2,[x0],1
	sub	w4,w2,'A'
	cmp	w4,25
	b.hi	.l
	add	w2,w2,32
.l:	cmp	w2,w3
	b.eq	.c
	mov	x0,0
	ret
.oui:	mov	x1,x0
	mov	x0,1
	ret

; x0 = chemin interne -> x0 = 1 s'il est executable par son chemin (regles)
exec_regle:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x1,sp_bin			; SD0:/bin/...
	bl	prefixe_ci
	cbnz	x0,.suite
	mov	x0,x19
	adr	x1,chemin_sys			; SD0:/sys/...
	bl	prefixe_ci
	cbnz	x0,.suite
	mov	x0,x19
	adr	x1,sp_apps			; SD0:/apps/<nom>.app/main
	bl	prefixe_ci
	cbz	x0,.exd
	mov	x2,x1
.a:	ldrb	w3,[x2]
	cbz	w3,.exd
	cmp	w3,'/'
	b.eq	.sl
	add	x2,x2,1
	b	.a
.sl:	sub	x3,x2,x1
	cmp	x3,5
	b.lo	.exd
	sub	x0,x2,4
	adr	x1,sp_main
	bl	prefixe_ci
	cbz	x0,.exd
	ldrb	w3,[x1]
	cbz	w3,.oui
	b	.exd
.suite:	ldrb	w3,[x1]				; (un fichier dedans, pas le dossier)
	cbnz	w3,.oui
.exd:	bl	exd_charger			; les dossiers de exec.txt
	adr	x20,exd_v
.e:	ldrb	w0,[x20]
	cbz	w0,.non
	mov	x0,x19
	mov	x1,x20
	bl	prefixe_ci
	cbz	x0,.n
	ldrb	w3,[x1]
	cmp	w3,'/'
	b.ne	.n
	ldrb	w3,[x1,1]
	cbnz	w3,.oui
.n:	add	x20,x20,PATH_MAX
	adr	x0,exd_v
	add	x0,x0,EXD_MAX * PATH_MAX
	cmp	x20,x0
	b.lo	.e
.non:	mov	x0,0
	b	.out
.oui:	mov	x0,1
.out:	ldp	x19,x20,[sp],16
	epilogue

; SD0:/etc/exec.txt dans exd_v (une fois ; SD0: absent : les valeurs par
; defaut, le fichier sera lu plus tard)
exd_charger:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,exd_lu
	ldr	x1,[x0]
	cbnz	x1,.out
	adr	x0,s_sd0
	bl	volume_de
	cbz	x0,.defaut
	adr	x1,s_exectxt
	adr	x2,exd_tampon
	mov	x3,EXD_TAMPON-1
	bl	vol_lire_fichier
	adr	x1,exd_lu
	mov	x2,1
	str	x2,[x1]
	tbnz	x0,63,.defaut			; (absent : les valeurs par defaut)
	adr	x1,exd_tampon
	strb	wzr,[x1,x0]
	mov	x0,x1
	bl	exd_analyser
	b	.out
.defaut:
	adr	x0,exd_defaut
	bl	exd_analyser
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = texte de exec.txt (« # » : commentaire ; un dossier par ligne, « SD0:/a »
; ou « /a » : chemin virtuel) -> exd_v (chemins internes, minuscules)
exd_analyser:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x20,exd_v
	mov	x0,x20				; table vide
	mov	x1,0
.z:	strb	wzr,[x0,x1]
	add	x1,x1,PATH_MAX
	cmp	x1,EXD_MAX * PATH_MAX
	b.lo	.z
	mov	x21,0				; entrees
.ligne:	ldrb	w0,[x19]
	cbz	w0,.out
	cmp	w0,' '
	b.eq	.sp
	cmp	w0,9
	b.eq	.sp
	cmp	w0,10
	b.eq	.sp
	cmp	w0,13
	b.eq	.sp
	cmp	w0,'#'
	b.eq	.saute
	adr	x2,exd_ligne			; le mot, jusqu'a la fin de la ligne
	mov	x3,0
.m:	ldrb	w0,[x19]
	cbz	w0,.mf
	cmp	w0,10
	b.eq	.mf
	cmp	w0,13
	b.eq	.mf
	cmp	x3,PATH_MAX-1
	b.hs	.mn
	strb	w0,[x2,x3]
	add	x3,x3,1
.mn:	add	x19,x19,1
	b	.m
.mf:	cbz	x3,.ligne			; (blancs de fin retires)
	sub	x4,x3,1
	ldrb	w0,[x2,x4]
	cmp	w0,' '
	b.eq	.blanc
	cmp	w0,9
	b.ne	.mt
.blanc:	mov	x3,x4
	b	.mf
.mt:	strb	wzr,[x2,x3]
	cmp	x21,EXD_MAX
	b.hs	.ligne
	mov	x0,0				; resolu comme un chemin de programme
	adr	x1,exd_ligne			; (pid 0 : dossier /sd0)
	mov	x2,PATH_MAX
	madd	x2,x21,x2,x20
	mov	x22,x2
	bl	resoudre
	cbnz	x0,.vide
	mov	x0,x22				; en minuscules
.lc:	ldrb	w1,[x0]
	cbz	w1,.lcf
	sub	w2,w1,'A'
	cmp	w2,25
	b.hi	.lcn
	add	w1,w1,32
	strb	w1,[x0]
.lcn:	add	x0,x0,1
	b	.lc
.lcf:	add	x21,x21,1
	b	.ligne
.vide:	strb	wzr,[x22]
	b	.ligne
.sp:	add	x19,x19,1
	b	.ligne
.saute:	ldrb	w0,[x19]
	cbz	w0,.out
	add	x19,x19,1
	cmp	w0,10
	b.ne	.saute
	b	.ligne
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree du volume -> x0 = 1 si son service est ramfs (memoire)
vol_ramfs:
	add	x0,x0,VOL_FS
	adr	x1,s_ramfs
.c:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	cmp	w2,w3
	b.ne	.non
	cbnz	w2,.c
	mov	x0,1
	ret
.non:	mov	x0,0
	ret

; x0 = entree du volume -> x0 = 0 : son index est en memoire, ou erreur
idx_charger:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x0,[x19,VOL_IDXOK]
	cbnz	x0,.ok
	ldr	x0,[x19,VOL_IDX]
	cbnz	x0,.buf
	mov	x0,IDX_CAP
	bl	malloc
	cbz	x0,.nomem
	str	x0,[x19,VOL_IDX]
.buf:	str	xzr,[x19,VOL_IDXN]
	mov	x0,x19
	bl	vol_ramfs
	cbnz	x0,.marque
	mov	x0,x19
	adr	x1,s_dotexec
	ldr	x2,[x19,VOL_IDX]
	mov	x3,IDX_CAP-2
	bl	vol_lire_fichier
	cmn	x0,2				; E_NOENT : vide
	b.eq	.marque
	tbnz	x0,63,.out
	ldr	x1,[x19,VOL_IDX]		; nettoye : sans \r, ni ligne vide, ni
	mov	x2,0				; « / » en tete ; \n final
	mov	x3,0				; (x3 : ecriture)
	mov	x5,1				; 1 : debut de ligne
.n:	cmp	x2,x0
	b.hs	.nf
	ldrb	w4,[x1,x2]
	add	x2,x2,1
	cmp	w4,13
	b.eq	.n
	cmp	w4,10
	b.eq	.nl
	cbz	x5,.nc
	cmp	w4,'/'
	b.eq	.n
	cmp	w4,' '
	b.eq	.n
.nc:	mov	x5,0
	strb	w4,[x1,x3]
	add	x3,x3,1
	b	.n
.nl:	cbnz	x5,.n				; (ligne vide)
	strb	w4,[x1,x3]
	add	x3,x3,1
	mov	x5,1
	b	.n
.nf:	cbnz	x5,.fin
	mov	w4,10
	strb	w4,[x1,x3]
	add	x3,x3,1
.fin:	str	x3,[x19,VOL_IDXN]
.marque:
	mov	x0,1
	str	x0,[x19,VOL_IDXOK]
.ok:	mov	x0,0
	b	.out
.nomem:	mov	x0,E_NOMEM
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree du volume (index charge), x1 = chemin relatif, x2 = 1 : aussi
; les lignes sous ce chemin (« chemin/... ») -> x0 = debut de la premiere
; ligne trouvee (-1 : aucune), x1 = sa longueur (sans \n)
idx_cherche:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	bl	vol_ramfs
	cmp	x0,0
	cset	x22,eq				; x22 = 1 : casse ignoree
	ldr	x23,[x19,VOL_IDX]
	ldr	x24,[x19,VOL_IDXN]
	mov	x0,0				; debut de la ligne
.ligne:	cmp	x0,x24
	b.hs	.non
	mov	x1,0
.c:	ldrb	w2,[x20,x1]
	add	x3,x0,x1
	cmp	x3,x24
	mov	w4,10
	b.hs	.b
	ldrb	w4,[x23,x3]
.b:	cbz	w2,.fin_nom
	cmp	w4,10
	b.eq	.suiv
	cbz	x22,.cmp
	sub	w5,w2,'A'
	cmp	w5,25
	b.hi	.l1
	add	w2,w2,32
.l1:	sub	w5,w4,'A'
	cmp	w5,25
	b.hi	.cmp
	add	w4,w4,32
.cmp:	cmp	w2,w4
	b.ne	.suiv
	add	x1,x1,1
	b	.c
.fin_nom:
	cmp	w4,10				; ligne = chemin
	b.eq	.trouve
	cbz	x21,.suiv
	cmp	w4,'/'				; ligne = chemin/...
	b.ne	.suiv
	cbz	x1,.suiv			; (chemin vide : la racine, jamais)
.trouve:
	add	x1,x0,x1			; longueur de la ligne
.lg:	cmp	x1,x24
	b.hs	.lgf
	ldrb	w4,[x23,x1]
	cmp	w4,10
	b.eq	.lgf
	add	x1,x1,1
	b	.lg
.lgf:	sub	x1,x1,x0
	b	.out
.suiv:	add	x3,x0,x1			; ligne suivante
.s:	cmp	x3,x24
	b.hs	.non
	ldrb	w4,[x23,x3]
	add	x3,x3,1
	cmp	w4,10
	b.ne	.s
	mov	x0,x3
	b	.ligne
.non:	mov	x0,-1
	mov	x1,0
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree du volume, x1 = chemin relatif, x2 = 1 : aussi ce qu'il contient
; -> x0 = lignes retirees
idx_retirer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x22,0
.r:	mov	x0,x19
	mov	x1,x20
	mov	x2,x21
	bl	idx_cherche
	tbnz	x0,63,.out
	ldr	x3,[x19,VOL_IDX]		; [debut, debut + longueur + 1[ retire
	ldr	x4,[x19,VOL_IDXN]
	add	x5,x0,x1
	add	x5,x5,1
	cmp	x5,x4
	csel	x5,x4,x5,hi
	sub	x6,x4,x5			; ce qui suit
	sub	x7,x5,x0
	sub	x4,x4,x7
	str	x4,[x19,VOL_IDXN]
	add	x0,x3,x0
	add	x1,x3,x5
	mov	x2,x6
	cbz	x2,.cpt
	bl	mem_move
.cpt:	add	x22,x22,1
	b	.r
.out:	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree du volume, x1 = chemin relatif -> x0 = 1 ajoute, 0 deja la,
; E_NOSPC (index plein)
idx_ajouter:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x2,0
	bl	idx_cherche
	tbz	x0,63,.deja
	mov	x0,0				; longueur
.l:	ldrb	w1,[x20,x0]
	cbz	w1,.lf
	add	x0,x0,1
	b	.l
.lf:	cbz	x0,.deja
	ldr	x2,[x19,VOL_IDXN]
	add	x3,x2,x0
	add	x3,x3,1
	cmp	x3,IDX_CAP
	b.hi	.plein
	ldr	x4,[x19,VOL_IDX]
	add	x4,x4,x2
	mov	x5,0
.c:	ldrb	w1,[x20,x5]
	strb	w1,[x4,x5]
	add	x5,x5,1
	cmp	x5,x0
	b.lo	.c
	mov	w1,10
	strb	w1,[x4,x5]
	str	x3,[x19,VOL_IDXN]
	mov	x0,1
	b	.out
.deja:	mov	x0,0
	b	.out
.plein:	mov	x0,E_NOSPC
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree du volume, x1 = ancien chemin relatif, x2 = nouveau -> x0 =
; entrees deplacees (l'ancien chemin et ce qu'il contient ; une entree du
; nouveau chemin est d'abord retiree : il est remplace)
idx_renommer:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	mov	x21,x2
	mov	x24,0
	bl	vol_ramfs			; meme chemin, a la casse pres : rien
	cbnz	x0,.diff
	mov	x0,x20
	mov	x1,x21
.ci:	ldrb	w2,[x0],1
	ldrb	w3,[x1],1
	orr	w4,w2,0x20
	orr	w5,w3,0x20
	cmp	w4,w5
	b.ne	.diff
	cbnz	w2,.ci
	b	.out
.diff:	mov	x0,x19				; la cible remplacee
	mov	x1,x21
	mov	x2,0
	bl	idx_retirer
	ldr	x22,[x19,VOL_IDXN]		; (borne : une ligne fait au moins 2 o)
.boucle:
	cbz	x22,.out
	sub	x22,x22,1
	mov	x0,x19
	mov	x1,x20
	mov	x2,1
	bl	idx_cherche
	tbnz	x0,63,.out
	mov	x23,x0				; la suite de la ligne apres l'ancien
	mov	x2,0				; chemin, dans exd_ligne
.la:	ldrb	w3,[x20,x2]
	cbz	w3,.suite
	add	x2,x2,1
	b	.la
.suite:	ldr	x3,[x19,VOL_IDX]
	add	x3,x3,x23
	adr	x4,exd_ligne			; nouveau + suite
	mov	x5,0
.nv:	ldrb	w6,[x21,x5]
	cbz	w6,.nvf
	strb	w6,[x4,x5]
	add	x5,x5,1
	cmp	x5,PATH_MAX-1
	b.hs	.out
	b	.nv
.nvf:	cmp	x2,x1
	b.hs	.fin
	ldrb	w6,[x3,x2]
	strb	w6,[x4,x5]
	add	x2,x2,1
	add	x5,x5,1
	cmp	x5,PATH_MAX-1
	b.hs	.out
	b	.nvf
.fin:	strb	wzr,[x4,x5]
	ldr	x3,[x19,VOL_IDX]		; la ligne retiree...
	ldr	x4,[x19,VOL_IDXN]
	add	x5,x23,x1
	add	x5,x5,1
	cmp	x5,x4
	csel	x5,x4,x5,hi
	sub	x6,x4,x5
	sub	x7,x5,x23
	sub	x4,x4,x7
	str	x4,[x19,VOL_IDXN]
	add	x0,x3,x23
	add	x1,x3,x5
	mov	x2,x6
	cbz	x2,.aj
	bl	mem_move
.aj:	mov	x0,x19				; ... et la nouvelle ajoutee
	adr	x1,exd_ligne
	bl	idx_ajouter
	add	x24,x24,1
	b	.boucle
.out:	mov	x0,x24
	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = entree du volume : son index recrit dans <VOL>:/.exec (ramfs : rien)
; -> x0 = 0 ou erreur
idx_ecrire:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	bl	vol_ramfs
	cbnz	x0,.rien
	mov	x0,x19
	adr	x1,s_dotexec
	ldr	x2,[x19,VOL_IDX]
	ldr	x3,[x19,VOL_IDXN]
	bl	vol_ecrire_fichier
	b	.out
.rien:	mov	x0,0
.out:	ldr	x19,[sp],16
	epilogue

; x0 = chemin interne -> x0 = 1 s'il est executable (regles, puis index)
est_executable:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	exec_regle
	cbnz	x0,.out
	add	x0,x19,1
	bl	volume_de
	cbz	x0,.out
	mov	x20,x0
	bl	idx_charger
	cbnz	x0,.non
	add	x0,x19,1
	bl	volume_de
	mov	x0,x20
	mov	x2,0
	bl	idx_cherche
	cmp	x0,0
	cset	x0,ge
	b	.out
.non:	mov	x0,0
.out:	ldp	x19,x20,[sp],16
	epilogue

; resolu va etre efface (V_UNLINK) : ses entrees, et celles de ce qu'il
; contient, quittent l'index
idx_oublier:
	prologue
	str	x19,[sp,-16]!
	adr	x0,resolu
	add	x0,x0,1
	bl	volume_de
	cbz	x0,.out
	mov	x19,x0
	bl	idx_charger
	cbnz	x0,.out
	adr	x0,resolu
	add	x0,x0,1
	bl	volume_de
	mov	x0,x19
	mov	x2,1
	bl	idx_retirer
	cbz	x0,.out
	mov	x0,x19
	bl	idx_ecrire
.out:	ldr	x19,[sp],16
	epilogue

; (V_CHMOD) resolu, x0 = mode : un bit x l'ajoute a l'index, aucun l'en retire
; (repertoire, chemin des regles : rien) -> x0 = 0 ou erreur
idx_chmod:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x20,x0
	adr	x0,resolu
	bl	exec_regle
	cbnz	x0,.ok
	adr	x0,resolu
	add	x0,x0,1
	bl	volume_de
	cbz	x0,.noent
	mov	x19,x0
	bl	idx_charger
	cbnz	x0,.out
	adr	x0,resolu
	add	x0,x0,1
	bl	volume_de
	mov	x0,x19
	mov	x2,0x49				; 0111
	tst	x20,x2
	b.eq	.retire
	bl	idx_ajouter
	tbnz	x0,63,.out
	cbz	x0,.ok
	b	.ecrit
.retire:
	mov	x2,0
	bl	idx_retirer
	cbz	x0,.ok
.ecrit:	mov	x0,x19
	bl	idx_ecrire
	b	.out
.ok:	mov	x0,0
	b	.out
.noent:	mov	x0,E_NOENT
.out:	ldp	x19,x20,[sp],16
	epilogue

; resolu (existe, fichier) : ses premiers octets -> x0 = 0 : un programme
; (ELF, MZ) ; 1 : un script, interp et iarg remplis (ligne #!, sinon
; « /bin/sh ») ; < 0 : erreur
script_voir:
	prologue
	stp	x19,x20,[sp,-16]!
	adr	x0,resolu
	add	x0,x0,1
	bl	volume_de
	cbz	x0,.noent
	adr	x2,tete
	mov	x3,TETE
	bl	vol_lire_fichier
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x20,tete
	cmp	x19,4
	b.lo	.pas_elf
	ldr	w0,[x20]
	movl	w1,0x464C457F			; 7F E L F
	cmp	w0,w1
	b.eq	.bin
.pas_elf:
	cmp	x19,2
	b.lo	.sh
	ldrh	w0,[x20]
	movl	w1,'M' or ('Z' shl 8)
	cmp	w0,w1
	b.eq	.bin
	movl	w1,'#' or ('!' shl 8)
	cmp	w0,w1
	b.ne	.sh
	mov	x2,2
.sp1:	cmp	x2,x19
	b.hs	.sh
	ldrb	w3,[x20,x2]
	cmp	w3,' '
	b.eq	.s1
	cmp	w3,9
	b.ne	.i0
.s1:	add	x2,x2,1
	b	.sp1
.i0:	adr	x4,interp
	mov	x5,0
.i:	cmp	x2,x19
	b.hs	.ifin
	ldrb	w3,[x20,x2]
	cmp	w3,' '
	b.ls	.ifin				; (blanc, \t, \r, \n, 0)
	cmp	x5,PATH_MAX-1
	b.hs	.ifin
	strb	w3,[x4,x5]
	add	x5,x5,1
	add	x2,x2,1
	b	.i
.ifin:	strb	wzr,[x4,x5]
	cbz	x5,.sh
.sp2:	cmp	x2,x19				; l'argument : le reste de la ligne
	b.hs	.a0
	ldrb	w3,[x20,x2]
	cmp	w3,' '
	b.eq	.s2
	cmp	w3,9
	b.ne	.a0
.s2:	add	x2,x2,1
	b	.sp2
.a0:	adr	x4,iarg
	mov	x5,0
.a:	cmp	x2,x19
	b.hs	.afin
	ldrb	w3,[x20,x2]
	cmp	w3,10
	b.eq	.afin
	cmp	w3,13
	b.eq	.afin
	cbz	w3,.afin
	cmp	x5,IARG_MAX-1
	b.hs	.afin
	strb	w3,[x4,x5]
	add	x5,x5,1
	add	x2,x2,1
	b	.a
.afin:	cbz	x5,.af
	sub	x6,x5,1
	ldrb	w3,[x4,x6]
	cmp	w3,' '
	b.eq	.tr
	cmp	w3,9
	b.ne	.af
.tr:	mov	x5,x6
	b	.afin
.af:	strb	wzr,[x4,x5]
	mov	x0,1
	b	.out
.sh:	adr	x0,interp			; sans #! : /bin/sh
	adr	x1,s_binsh
.c:	ldrb	w2,[x1],1
	strb	w2,[x0],1
	cbnz	w2,.c
	adr	x0,iarg
	strb	wzr,[x0]
	mov	x0,1
	b	.out
.bin:	mov	x0,0
	b	.out
.noent:	mov	x0,E_NOENT
.out:	ldp	x19,x20,[sp],16
	epilogue

s_sd0		db 'sd0',0
s_exectxt	db 'etc/exec.txt',0
s_dotexec	db '.exec',0
s_ramfs		db 'ramfs',0
s_binsh		db '/bin/sh',0
; dossiers d'executables sans SD0:/etc/exec.txt (le meme que celui de la carte)
exd_defaut	db '/gcc/bin',10,'/gcc/libexec',10,'/gcc/aarch64-onyx-elf/bin',10
		db '/dotnet',10,'/apps/koton.app/plugins',10,0
		align 8

macro exec_data {
		align 8
msg2		rb MSG_TAILLE			; requetes de vfs a un volume (index, tete)
exd_lu		rb 8				; 1 : exec.txt lu
exd_v		rb EXD_MAX * PATH_MAX		; dossiers d'executables (internes, minuscules)
exd_ligne	rb PATH_MAX
exd_tampon	rb EXD_TAMPON
tete		rb TETE
interp		rb PATH_MAX			; interprete d'un script (#!)
iarg		rb IARG_MAX			; son argument
script_res	rb PATH_MAX			; le script, resolu
}
