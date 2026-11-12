; ramfs - service de fichiers en memoire (docs/MICRO-NOYAU.md, section 4) :
; un volume RAM0: (monte par vfs : « ramfs <volume> ram:<taille> »), une
; arborescence de repertoires et de fichiers, perdue a l'arret. Il
; s'enregistre sous « fs.<volume> » et repond au protocole de inc/fs.inc.
; Un fichier ouvert : une entree de la table « ouverts » (son numero est
; rendu a l'ouverture) : son noeud et sa position ; un ou plusieurs
; proprietaires (apres un fork : SVF_NAISSANCES, avis SVC_OP_FORK) ; liberee
; avec son dernier proprietaire (avis du noyau, appel d'un process, ou fin
; d'un proprietaire : SVF_FINS). Proprietaires, acces et tampons partages
; (ops 14-16) : inc/fsowner.inc (docs/FICHIERS.md, 4.2).
; Position : b = FS_POS_COUR (-1) dans les ops 10, 11, 15, 16 lit ou ecrit a
; la position du fichier ouvert et l'avance (partagee par ses proprietaires) ;
; une position explicite n'y touche pas ; d rendu = position apres l'op.
; Op 26 (FS_POSITION) la change.
; Le contenu d'un fichier est une region du noyau (VM_MAP, a la demande), rendue
; quand le fichier grandit, est vide ou efface : le volume compte ce qui existe
; (GCC y cree et y efface des fichiers a chaque compilation). Limite : NOEUDS
; noeuds pour tout le volume (fichiers et repertoires) ; 4096 depuis le
; 12/11/2026 (256 avant : pip y pose setuptools, plus de 1000 fichiers). La
; table (128 o par noeud, 512 Ko) est la derniere du segment de donnees : les
; autres restent a portee de adr (+-1 Mo depuis le code).
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

NOEUDS	= 4096
N_NOM	= 0				; nom (64 octets)
N_PARENT = 64				; noeud parent
N_ATTR	= 72				; 0 : libre, ATTR_DIR, ATTR_FICHIER
N_TAILLE = 80
N_DONNEES = 88				; adresse du contenu
N_CAPA	= 96				; place reservee
N_MTIME	= 104				; derniere ecriture (secondes UTC)
N_SIZE	= 128
NOM_MAX	= 63
OUV_MAX	= 64				; fichiers ouverts
OV_NOEUD = 0				; numero du noeud
OV_PROPRIO = 8				; pid qui l'a ouvert (0 : entree libre)
OV_POS	= 16				; position (partagee apres un fork)
OV_SIZE	= 32
OV_SHIFT = 5
E_MFILE	= -24

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	mov	x19,x0				; « <volume> ram:<taille> »
	bl	kits_open
	cbz	x19,quit
	adr	x0,nom_svc			; « fs.<volume> »
	adr	x1,s_fs
	bl	str_cat
	mov	x1,x19
.v:	ldrb	w2,[x1],1
	cmp	w2,' '
	b.eq	.vf
	cbz	w2,.vf
	strb	w2,[x0],1
	b	.v
.vf:	strb	wzr,[x0]
	cbz	w2,.taille			; (sans source : taille par defaut)
.src:	ldrb	w2,[x1],1			; « ram:<taille>[K|M] »
	cbz	w2,.taille
	cmp	w2,':'
	b.ne	.src
	mov	x0,x1
	str	x1,[sp,-16]!
	bl	parse_dec
	ldr	x1,[sp],16
	mov	x3,x0
.u:	ldrb	w2,[x1],1			; unite
	cbz	w2,.set
	orr	w2,w2,0x20
	cmp	w2,'k'
	b.ne	.m
	lsl	x3,x3,10
	b	.set
.m:	cmp	w2,'m'
	b.ne	.u
	lsl	x3,x3,20
.set:	cbz	x3,.taille
	adr	x0,limite
	str	x3,[x0]
.taille:
	adr	x0,noeuds			; racine
	mov	x1,ATTR_DIR
	str	x1,[x0,N_ATTR]
	adr	x0,nom_svc
	mov	x1,SVF_FINS or SVF_NAISSANCES	; prevenu des fins de process, des fork
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	adr	x1,canal
	str	x0,[x1]
.loop:	adr	x0,canal
	ldr	x0,[x0]
	adr	x1,msg
	mov	x2,60000
	sys	SYS_SVC_NEXT
	tbnz	x0,63,quit
	cbz	x0,.loop
	mov	x19,x0
	adr	x20,msg
	ldr	x0,[x20]
	movl	x1,SVC_OP_FIN
	cmp	x0,x1
	b.eq	.fin
	movl	x1,SVC_OP_FORK
	cmp	x0,x1
	b.eq	.fork
	movl	x1,SVC_OP_NAISSANCE		; (avis sans reponse : rien a faire)
	cmp	x0,x1
	b.eq	.loop
	cmp	x0,FS_FERMER
	b.eq	.fermer
	adr	x1,ops				; aiguillage
	mov	x2,0
.o:	add	x3,x1,x2,lsl 3
	ldr	w3,[x3]
	cbz	w3,.inval
	cmp	x3,x0
	b.eq	.go
	add	x2,x2,1
	b	.o
.go:	add	x1,x1,x2,lsl 3
	ldrsw	x3,[x1,4]
	add	x3,x1,x3
	mov	x0,x20
	blr	x3
	b	.reply
.inval:	mov	x0,E_INVAL
.reply:	str	x0,[x20]
	mov	x0,x19
	adr	x1,msg
	sys	SYS_SVC_REPLY
	b	.loop
.fermer:
	mov	x0,x20
	bl	op_fermer
	ldr	w1,[x20,MSG_PID]		; avis du noyau (pid 0) : sans reponse
	cbz	w1,.loop
	b	.reply
.fin:	ldr	x0,[x20,MSG_A]			; fin d'un process : ses fichiers
	bl	fin_pid
	b	.loop
.fork:	ldp	x0,x1,[x20,MSG_A]		; fork : le fils tient les fichiers
	bl	fo_own_fork			; du pere
	b	.loop
quit:	mov	x0,1				; (sur une erreur : init2 relance)
	sys	SYS_EXIT

; ---------------------------------------------------------------------------
;  Noeuds
; ---------------------------------------------------------------------------

; x0 = numero -> x0 = noeud (ne touche que x9, x10)
noeud:
	adr	x9,noeuds
	mov	x10,N_SIZE
	madd	x0,x0,x10,x9
	ret

; x0 = parent, x1 = nom (termine par 0 ou « / »), x2 = sa longueur
; -> x0 = numero de l'enfant de ce nom (-1 : aucun)
enfant:
	mov	x9,0
	adr	x10,noeuds
.e:	cmp	x9,NOEUDS
	b.hs	.none
	cbz	x9,.n				; (la racine n'est l'enfant de personne)
	ldr	x3,[x10,N_ATTR]
	cbz	x3,.n
	ldr	x3,[x10,N_PARENT]
	cmp	x3,x0
	b.ne	.n
	mov	x4,0
.c:	cmp	x4,x2
	b.hs	.end
	ldrb	w5,[x10,x4]
	ldrb	w6,[x1,x4]
	cmp	w5,w6
	b.ne	.n
	add	x4,x4,1
	b	.c
.end:	ldrb	w5,[x10,x4]
	cbnz	w5,.n
	mov	x0,x9
	ret
.n:	add	x9,x9,1
	add	x10,x10,N_SIZE
	b	.e
.none:	mov	x0,-1
	ret

; x0 = chemin relatif (« a/b », « » : racine) -> x0 = noeud (-1 : absent),
; x1 = parent du dernier composant (-1 : chemin intermediaire absent),
; x2 = dernier composant, x3 = sa longueur
chercher:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0				; composant courant
	mov	x20,0				; noeud courant : racine
	mov	x21,-1				; son parent
	mov	x22,0
	ldrb	w0,[x19]
	cbz	w0,.found
.comp:	mov	x22,0				; longueur du composant
.l:	ldrb	w0,[x19,x22]
	cbz	w0,.last
	cmp	w0,'/'
	b.eq	.mid
	add	x22,x22,1
	b	.l
.mid:	cbz	x22,.skip			; « // »
	mov	x0,x20
	mov	x1,x19
	mov	x2,x22
	bl	enfant
	tbnz	x0,63,.lost
	mov	x20,x0
.skip:	add	x19,x19,x22
	add	x19,x19,1
	b	.comp
.last:	cbz	x22,.found			; (« a/ »)
	mov	x21,x20
	mov	x0,x20
	mov	x1,x19
	mov	x2,x22
	bl	enfant
	mov	x20,x0
	b	.found
.lost:	mov	x20,-1
	mov	x21,-1
.found:	mov	x0,x20
	mov	x1,x21
	mov	x2,x19
	mov	x3,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = parent, x1 = nom, x2 = longueur, x3 = attribut -> x0 = numero, < 0
; si erreur
creer:
	cbz	x2,.inval
	cmp	x2,NOM_MAX
	b.hi	.inval
	adr	x9,noeuds
	mov	x10,1
.f:	cmp	x10,NOEUDS
	b.hs	.plein
	mov	x12,N_SIZE
	madd	x11,x10,x12,x9
	ldr	x12,[x11,N_ATTR]
	cbz	x12,.got
	add	x10,x10,1
	b	.f
.got:	str	x0,[x11,N_PARENT]
	str	x3,[x11,N_ATTR]
	stp	xzr,xzr,[x11,N_TAILLE]
	str	xzr,[x11,N_CAPA]
	mov	x12,x0
	mov	x0,0
	sys	SYS_CLOCK_GET
	str	x0,[x11,N_MTIME]
	mov	x0,x12
	mov	x12,0
.c:	ldrb	w13,[x1,x12]
	strb	w13,[x11,x12]
	add	x12,x12,1
	cmp	x12,x2
	b.lo	.c
	strb	wzr,[x11,x12]
	mov	x0,x10
	ret
.plein:	mov	x0,E_NOSPC
	ret
.inval:	mov	x0,E_INVAL
	ret

; x0 = noeud (adresse), x1 = taille voulue -> x0 = 0, ou E_NOSPC
agrandir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	ldr	x0,[x19,N_CAPA]
	cmp	x20,x0
	b.ls	.ok
	lsl	x0,x0,1				; nouvelle place : le double, 4 Ko au moins
	cmp	x0,x20
	csel	x0,x20,x0,lo
	add	x0,x0,0xFFF			; (en pages : celles d'une region)
	and	x20,x0,not 0xFFF
	adr	x1,utilise			; dans la taille du volume ?
	ldr	x2,[x1]
	ldr	x3,[x19,N_CAPA]
	sub	x2,x2,x3
	add	x21,x2,x20
	adr	x3,limite
	ldr	x3,[x3]
	cmp	x21,x3
	b.hi	.nospc
	mov	x0,0
	mov	x1,x20
	mov	x2,PROT_READ or PROT_WRITE
	mov	x3,0
	sys	SYS_VM_MAP
	tbnz	x0,63,.nospc
	mov	x22,x0
	ldr	x1,[x19,N_DONNEES]		; l'ancien contenu
	cbz	x1,.met
	ldr	x2,[x19,N_TAILLE]
	bl	mem_move
	mov	x0,x19
	bl	liberer
.met:	str	x22,[x19,N_DONNEES]
	str	x20,[x19,N_CAPA]
	adr	x1,utilise
	str	x21,[x1]
.ok:	mov	x0,0
	b	.out
.nospc:	mov	x0,E_NOSPC
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = noeud (adresse) : son contenu rendu au noyau, le volume mis a jour
liberer:
	ldr	x2,[x0,N_DONNEES]
	cbz	x2,.ret
	ldr	x1,[x0,N_CAPA]
	stp	xzr,xzr,[x0,N_DONNEES]
	adr	x3,utilise
	ldr	x4,[x3]
	sub	x4,x4,x1
	str	x4,[x3]
	mov	x0,x2
	sys	SYS_VM_UNMAP
.ret:	ret

; ---------------------------------------------------------------------------
;  Operations (x0 = message -> x0 = resultat)
; ---------------------------------------------------------------------------

; 1 ouvrir(a drapeaux, chemin) -> b numero, c attributs, d taille
op_ouvrir:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	bl	open_extras_take		; (attributs demandes par vfs)
	add	x0,x19,MSG_DATA
	bl	chercher
	tbz	x0,63,.existe
	ldr	x4,[x19,MSG_A]			; absent : creer ?
	tst	x4,O_CREATE
	b.eq	.noent
	tbnz	x1,63,.noent
	mov	x20,x1
	mov	x0,x20				; le parent est un repertoire ?
	bl	noeud
	ldr	x4,[x0,N_ATTR]
	cmp	x4,ATTR_DIR
	b.ne	.notdir
	mov	x0,x20
	mov	x1,x2
	mov	x2,x3
	mov	x3,ATTR_FICHIER
	bl	creer
	tbnz	x0,63,.out
.existe:
	mov	x20,x0
	bl	noeud
	mov	x21,x0
	bl	ouvert_libre			; une entree (prise a la fin)
	cbz	x0,.mfile
	mov	x22,x0
	ldr	x4,[x21,N_ATTR]
	ldr	x5,[x19,MSG_A]
	cmp	x4,ATTR_DIR
	b.ne	.fic
	tst	x5,O_WRITE			; repertoire : lecture seule
	b.ne	.isdir
	b	.rend
.fic:	tst	x5,O_TRUNC
	b.eq	.rend
	str	xzr,[x21,N_TAILLE]
	mov	x0,x21
	bl	liberer
	mov	x0,x21
	bl	dater
.rend:	mov	x0,x19
	bl	proprietaire
	mov	x1,x0
	adr	x0,ouverts			; numero de l'entree
	sub	x0,x22,x0
	lsr	x0,x0,OV_SHIFT
	stp	x0,x1,[sp,-16]!
	bl	fo_own_clear
	ldp	x0,x1,[sp]
	bl	fo_own_add			; son proprietaire
	mov	x2,x0
	ldp	x0,x1,[sp],16
	cbz	x2,.mfile
	stp	x20,x1,[x22,OV_NOEUD]
	str	x0,[x19,MSG_B]
	ldr	x2,[x19,MSG_A]			; O_APPEND : ecrit a la fin reelle
	tst	x2,O_APPEND
	cset	w2,ne
	adr	x1,ajouts
	strb	w2,[x1,x0]
	ldr	x1,[x21,N_TAILLE]		; position : 0, la fin avec O_APPEND
	cmp	w2,0
	csel	x1,x1,xzr,ne
	str	x1,[x22,OV_POS]
	ldr	x0,[x21,N_ATTR]
	ldr	x1,[x21,N_TAILLE]
	stp	x0,x1,[x19,MSG_C]
	mov	x0,x19				; a : notre canal (vfs transmet)
	bl	open_extras_put
	mov	x0,0
	b	.out
.notdir:
	mov	x0,E_NOTDIR
	b	.out
.mfile:	mov	x0,E_MFILE
	b	.out
.isdir:	mov	x0,E_ISDIR
	b	.out
.noent:	mov	x0,E_NOENT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; 2 stat(chemin) -> b taille, c attributs
op_stat:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	add	x0,x19,MSG_DATA
	bl	chercher
	tbnz	x0,63,.noent
	bl	noeud
	ldr	x1,[x0,N_TAILLE]
	ldr	x2,[x0,N_ATTR]
	stp	x1,x2,[x19,MSG_B]
	ldr	x1,[x0,N_MTIME]
	str	x1,[x19,MSG_D]
	mov	x0,0
	b	.out
.noent:	mov	x0,E_NOENT
.out:	ldr	x19,[sp],16
	epilogue

; x0 = noeud : ecrit maintenant
dater:
	mov	x9,x0
	mov	x0,0
	sys	SYS_CLOCK_GET
	str	x0,[x9,N_MTIME]
	ret

; 5 utime(b secondes UTC ; donnees : chemin)
op_utime:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	add	x0,x19,MSG_DATA
	bl	chercher
	tbnz	x0,63,.noent
	bl	noeud
	ldr	x1,[x19,MSG_B]
	str	x1,[x0,N_MTIME]
	mov	x0,0
	b	.out
.noent:	mov	x0,E_NOENT
.out:	ldr	x19,[sp],16
	epilogue

; 6 rename(donnees : ancien, 0, nouveau, 0) : le noeud change de parent et de
; nom ; un fichier a l'arrivee est remplace
op_rename:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	add	x20,x0,MSG_DATA
	mov	x0,x20
	bl	chercher
	tbnz	x0,63,.noent
	cbz	x0,.inval			; la racine
	mov	x19,x0
.f:	ldrb	w0,[x20],1
	cbnz	w0,.f
	mov	x0,x20
	bl	chercher
	tbnz	x1,63,.noent
	mov	x21,x0
	mov	x22,x1
	cbz	x3,.inval
	cmp	x3,NOM_MAX
	b.hi	.inval
	mov	x0,x22
	bl	noeud
	ldr	x4,[x0,N_ATTR]
	cmp	x4,ATTR_DIR
	b.ne	.notdir
	mov	x4,x22				; un repertoire : pas dans lui-meme
.p:	cbz	x4,.cible
	cmp	x4,x19
	b.eq	.inval
	mov	x0,x4
	bl	noeud
	ldr	x4,[x0,N_PARENT]
	b	.p
.cible:	tbnz	x21,63,.pose
	cmp	x21,x19
	b.eq	.fini
	mov	x0,x21
	bl	noeud
	ldr	x4,[x0,N_ATTR]
	cmp	x4,ATTR_DIR
	b.eq	.exist
	str	xzr,[x0,N_ATTR]
	stp	x2,x3,[sp,-16]!
	bl	liberer
	ldp	x2,x3,[sp],16
.pose:	mov	x0,x19
	bl	noeud
	str	x22,[x0,N_PARENT]
	mov	x4,0
.c:	ldrb	w5,[x2,x4]
	strb	w5,[x0,x4]
	add	x4,x4,1
	cmp	x4,x3
	b.lo	.c
	strb	wzr,[x0,x4]
.fini:	mov	x0,0
	b	.out
.exist:	mov	x0,E_EXIST
	b	.out
.notdir:
	mov	x0,E_NOTDIR
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.noent:	mov	x0,E_NOENT
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; 3 mkdir(chemin)
op_mkdir:
	prologue
	str	x19,[sp,-16]!
	add	x0,x0,MSG_DATA
	bl	chercher
	tbz	x0,63,.exist
	tbnz	x1,63,.noent
	mov	x19,x1
	mov	x0,x1
	bl	noeud
	ldr	x4,[x0,N_ATTR]
	cmp	x4,ATTR_DIR
	b.ne	.notdir
	mov	x0,x19
	mov	x1,x2
	mov	x2,x3
	mov	x3,ATTR_DIR
	bl	creer
	tbnz	x0,63,.out
	mov	x0,0
	b	.out
.exist:	mov	x0,E_EXIST
	b	.out
.notdir:
	mov	x0,E_NOTDIR
	b	.out
.noent:	mov	x0,E_NOENT
.out:	ldr	x19,[sp],16
	epilogue

; 4 unlink(chemin) : fichier, ou repertoire vide
op_unlink:
	prologue
	str	x19,[sp,-16]!
	add	x0,x0,MSG_DATA
	bl	chercher
	tbnz	x0,63,.noent
	cbz	x0,.inval			; la racine
	mov	x19,x0
	bl	noeud
	ldr	x1,[x0,N_ATTR]
	cmp	x1,ATTR_DIR
	b.ne	.del
	adr	x2,noeuds			; un repertoire : vide ?
	mov	x3,0
.e:	cmp	x3,NOEUDS
	b.hs	.del
	ldr	x4,[x2,N_ATTR]
	cbz	x4,.en
	ldr	x4,[x2,N_PARENT]
	cmp	x4,x19
	b.eq	.notempty
.en:	add	x2,x2,N_SIZE
	add	x3,x3,1
	b	.e
.del:	str	xzr,[x0,N_ATTR]
	bl	liberer
	mov	x0,0
	b	.out
.notempty:
	mov	x0,E_NOTEMPTY
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.noent:	mov	x0,E_NOENT
.out:	ldr	x19,[sp],16
	epilogue

; -> x0 = entree libre de « ouverts » (0 : aucune)
ouvert_libre:
	adr	x0,ouverts
	mov	x1,OUV_MAX
.f:	ldr	x2,[x0,OV_PROPRIO]
	cbz	x2,.got
	add	x0,x0,OV_SIZE
	subs	x1,x1,1
	b.ne	.f
	mov	x0,0
.got:	ret

; x0 = message -> x0 = entree ouverte (numero en a) si l'appelant y a droit,
; E_INVAL (numero libre, noeud efface) ou E_BADF (pas un de ses
; proprietaires) ; x1 = son noeud (adresse)
ouvert_de:
	str	x30,[sp,-16]!
	mov	x4,x0
	ldr	x0,[x4,MSG_A]
	cmp	x0,OUV_MAX
	b.hs	.inval
	adr	x5,ouverts
	add	x5,x5,x0,lsl OV_SHIFT
	ldr	x1,[x5,OV_PROPRIO]
	cbz	x1,.inval
	ldr	x1,[x4,MSG_A]
	mov	x0,x4
	bl	acces
	cbnz	x0,.out
	ldr	x0,[x5,OV_NOEUD]
	bl	noeud
	ldr	x1,[x0,N_ATTR]
	cbz	x1,.inval
	mov	x1,x0
	mov	x0,x5
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldr	x30,[sp],16
	ret

; x0 = message -> x0 = noeud de fichier (adresse) de l'entree en a, ou erreur ;
; x1 = l'entree ouverte
fichier_de:
	str	x30,[sp,-16]!
	bl	ouvert_de
	tbnz	x0,63,.out
	mov	x2,x0
	mov	x0,x1
	mov	x1,x2
	ldr	x2,[x0,N_ATTR]
	cmp	x2,ATTR_FICHIER
	b.eq	.out
	mov	x0,E_INVAL
.out:	ldr	x30,[sp],16
	ret

; 12 fermer(a numero) -> 0 (avis du noyau : ferme pour tous ; appel d'un
; proprietaire : il le quitte, ferme avec le dernier)
op_fermer:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	ldr	x20,[x19,MSG_A]			; (noeud efface depuis : l'entree
	cmp	x20,OUV_MAX			;  est liberee quand meme)
	b.hs	.inval
	adr	x5,ouverts
	add	x5,x5,x20,lsl OV_SHIFT
	ldr	x1,[x5,OV_PROPRIO]
	cbz	x1,.inval
	mov	x0,x19
	mov	x1,x20
	bl	acces
	cbnz	x0,.out
	mov	x0,x19
	mov	x1,x20
	bl	fo_own_close
	cbz	x0,.out
	mov	x0,x20
	bl	ouvert_liberer
	mov	x0,0
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = numero : l'entree ouverte est liberee (ne touche que x0, x1)
ouvert_liberer:
	adr	x1,ouverts
	add	x1,x1,x0,lsl OV_SHIFT
	stp	xzr,xzr,[x1]
	ret

; x0 = pid termine : il quitte ses fichiers (fermes s'il etait le dernier),
; son tampon est rendu
fin_pid:
	prologue
	str	x19,[sp,-16]!
	mov	x19,x0
	adr	x1,ouvert_liberer
	bl	fo_own_fin
	mov	x0,x19
	bl	tampons_fin
	ldr	x19,[sp],16
	epilogue

; x0 = noeud de fichier, x1 = position, x2 = octets voulus, x3 = destination
; -> x0 = octets lus (bornes par la taille)
lire_noeud:
	prologue
	ldr	x5,[x0,N_TAILLE]
	cmp	x1,x5
	b.hs	.fin
	sub	x5,x5,x1			; reste dans le fichier
	cmp	x2,x5
	csel	x2,x5,x2,hi
	ldr	x4,[x0,N_DONNEES]
	add	x1,x4,x1
	mov	x0,x3
	str	x2,[sp,-16]!
	bl	mem_move
	ldr	x0,[sp],16
	epilogue
.fin:	mov	x0,0
	epilogue

; x0 = noeud de fichier, x1 = position, x2 = octets, x3 = source
; -> x0 = octets ecrits, ou E_NOSPC ; x1 = nouvelle taille
ecrire_noeud:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x20,x0
	mov	x22,x1				; position
	mov	x23,x2				; octets
	mov	x24,x3
	add	x21,x22,x23			; fin de l'ecriture
	mov	x1,x21
	bl	agrandir
	tbnz	x0,63,.out
	ldr	x3,[x20,N_TAILLE]		; un trou avant ? (a zero)
	cmp	x22,x3
	b.ls	.cp
	ldr	x0,[x20,N_DONNEES]
.z:	strb	wzr,[x0,x3]
	add	x3,x3,1
	cmp	x3,x22
	b.lo	.z
.cp:	ldr	x0,[x20,N_DONNEES]
	add	x0,x0,x22
	mov	x1,x24
	mov	x2,x23
	bl	mem_move
	ldr	x1,[x20,N_TAILLE]
	cmp	x21,x1
	csel	x1,x21,x1,hi
	str	x1,[x20,N_TAILLE]
	mov	x0,x20
	bl	dater
	mov	x0,x23
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; 10 lire(a numero, b position (FS_POS_COUR : celle du fichier), c octets)
; -> octets (donnees), d position apres
op_lire:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	fichier_de
	tbnz	x0,63,.out
	mov	x20,x1
	bl	position_lire
	ldr	x2,[x19,MSG_C]			; octets
	cmp	x2,FS_DONNEES
	mov	x3,FS_DONNEES
	csel	x2,x3,x2,hi
	add	x3,x19,MSG_DATA
	str	x1,[sp,-16]!
	bl	lire_noeud
	ldr	x1,[sp],16
	str	w0,[x19,MSG_LEN]
	bl	position_apres
.out:	ldp	x19,x20,[sp],16
	epilogue

; x19 = message, x20 = entree ouverte -> x1 = position de l'op : b, ou celle
; du fichier (b = FS_POS_COUR) ; ne touche que x1
position_lire:
	ldr	x1,[x19,MSG_B]
	cmn	x1,1
	b.ne	.r
	ldr	x1,[x20,OV_POS]
.r:	ret

; x0 = octets transferes (ou erreur), x1 = position de l'op, x19 = message,
; x20 = entree : d = position apres ; celle du fichier avancee (b =
; FS_POS_COUR) ; garde x0
position_apres:
	tbnz	x0,63,.r
	add	x1,x1,x0
	str	x1,[x19,MSG_D]
	ldr	x2,[x19,MSG_B]
	cmn	x2,1
	b.ne	.r
	str	x1,[x20,OV_POS]
.r:	ret

; 11 ecrire(a numero, b position (FS_POS_COUR : celle du fichier), c octets,
; donnees) -> octets, b taille, d position apres
op_ecrire:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	bl	fichier_de
	tbnz	x0,63,.out
	mov	x20,x1
	ldr	x2,[x19,MSG_C]
	cmp	x2,FS_DONNEES
	b.hi	.inval
	bl	position_lire
	bl	position_ecrire
	mov	x21,x1
	add	x3,x19,MSG_DATA
	bl	ecrire_noeud
	tbnz	x0,63,.out
	stp	x0,x1,[sp,-16]!
	mov	x1,x21
	bl	position_apres			; (avant b : il y lit FS_POS_COUR)
	ldp	x0,x1,[sp],16
	str	x1,[x19,MSG_B]
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = noeud, x1 = position, x19 = message d'ecriture -> x1 = la fin du fichier
; si l'entree (numero en a) est en ajout ; ne touche que x1, x4, x5
position_ecrire:
	ldr	x4,[x19,MSG_A]
	adr	x5,ajouts
	ldrb	w5,[x5,x4]
	cbz	w5,.r
	ldr	x1,[x0,N_TAILLE]
.r:	ret

; 15 lire_t(a numero, b position, c octets) -> octets, au debut du tampon
op_lire_t:
	mov	x4,0
	b	rw_t
; 16 ecrire_t(a numero, b position, c octets du tampon) -> octets, b taille
op_ecrire_t:
	mov	x4,1
rw_t:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x22,x4
	bl	fichier_de
	tbnz	x0,63,.out
	mov	x21,x0
	mov	x20,x1
	mov	x0,x19				; le tampon de l'appelant
	bl	tampon_de
	cbz	x0,.inval
	mov	x3,x0
	ldr	x2,[x19,MSG_C]
	movl	x4,FS_TAMPON_TAILLE
	cmp	x2,x4
	b.hi	.inval
	bl	position_lire
	mov	x0,x21
	cbnz	x22,.ecr
	str	x1,[sp,-16]!
	bl	lire_noeud
	ldr	x1,[sp],16
	bl	position_apres
	b	.out
.ecr:	bl	position_ecrire
	str	x1,[sp,-16]!
	bl	ecrire_noeud
	ldr	x2,[sp],16
	tbnz	x0,63,.out
	stp	x0,x1,[sp,-16]!
	mov	x1,x2
	bl	position_apres			; (avant b : il y lit FS_POS_COUR)
	ldp	x0,x1,[sp],16
	str	x1,[x19,MSG_B]
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; 26 position(a numero, b decalage, c origine 0 debut, 1 position, 2 fin)
; -> nouvelle position (aussi en b) ; E_INVAL si elle serait negative
op_position:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	fichier_de
	tbnz	x0,63,.out
	mov	x20,x1
	ldp	x1,x2,[x19,MSG_B]
	cmp	x2,2
	b.hi	.inval
	mov	x3,0
	cmp	x2,1
	b.ne	.fin
	ldr	x3,[x20,OV_POS]
.fin:	cmp	x2,2
	b.ne	.pose
	ldr	x3,[x0,N_TAILLE]
.pose:	adds	x1,x3,x1
	b.mi	.inval
	str	x1,[x20,OV_POS]
	str	x1,[x19,MSG_B]
	mov	x0,x1
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; 13 lire_rep(a numero, b rang) -> 1 (donnees : nom[256], taille, attributs)
op_lire_rep:
	prologue
	stp	x19,x20,[sp,-16]!
	str	x21,[sp,-16]!
	mov	x19,x0
	bl	ouvert_de
	tbnz	x0,63,.out
	ldr	x21,[x0,OV_NOEUD]		; le repertoire
	ldr	x20,[x19,MSG_B]			; rang voulu
	adr	x2,noeuds
	mov	x3,1
.e:	cmp	x3,NOEUDS
	b.hs	.fin
	add	x2,x2,N_SIZE
	ldr	x4,[x2,N_ATTR]
	cbz	x4,.en
	ldr	x4,[x2,N_PARENT]
	cmp	x4,x21
	b.ne	.en
	cbz	x20,.got
	sub	x20,x20,1
.en:	add	x3,x3,1
	b	.e
.got:	add	x0,x19,MSG_DATA			; nom[256], taille, attributs
	mov	x1,0
.c:	ldrb	w4,[x2,x1]
	strb	w4,[x0,x1]
	add	x1,x1,1
	cmp	x1,NOM_MAX+1
	b.lo	.c
	ldr	x4,[x2,N_TAILLE]
	ldr	x5,[x2,N_ATTR]
	stp	x4,x5,[x0,256]
	mov	x0,272
	str	w0,[x19,MSG_LEN]
	mov	x0,1
	b	.out
.fin:	mov	x0,0
.out:	ldr	x21,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; 19 info -> a octets du volume, b octets libres, c 0, d FS_T_RAM
op_info:
	adr	x1,limite
	ldr	x1,[x1]
	adr	x2,utilise
	ldr	x2,[x2]
	subs	x2,x1,x2
	csel	x2,xzr,x2,lo
	stp	x1,x2,[x0,MSG_A]
	mov	x1,FS_T_RAM
	stp	xzr,x1,[x0,MSG_C]
	strb	wzr,[x0,MSG_DATA]
	mov	x0,0
	ret

include '../inc/fsowner.inc'

		align 8
ops:	dw FS_OUVRIR, op_ouvrir - ops - 0
	dw FS_STAT, op_stat - ops - 8
	dw FS_MKDIR, op_mkdir - ops - 16
	dw FS_UNLINK, op_unlink - ops - 24
	dw FS_LIRE, op_lire - ops - 32
	dw FS_ECRIRE, op_ecrire - ops - 40
	dw FS_LIRE_REP, op_lire_rep - ops - 48
	dw FS_TAMPON, op_tampon - ops - 56
	dw FS_LIRE_T, op_lire_t - ops - 64
	dw FS_ECRIRE_T, op_ecrire_t - ops - 72
	dw FS_UTIME, op_utime - ops - 80
	dw FS_RENAME, op_rename - ops - 88
	dw FS_INFO, op_info - ops - 96
	dw FS_POSITION, op_position - ops - 104
	dw 0, 0
s_fs	db 'fs.',0
	align 8

segment readable writeable
limite		dd 16 shl 20			; taille du volume (« ram:16M »)
utilise		dd 0
canal		dd 0
kit_data
		align 8
nom_svc		rb 32
msg		rb MSG_TAILLE
ouverts		rb OUV_MAX * OV_SIZE
ajouts		rb OUV_MAX			; par entree : ouverte en O_APPEND
fsowner_data
		align 8
noeuds		rb NOEUDS * N_SIZE		; (la derniere : voir l'en-tete)
