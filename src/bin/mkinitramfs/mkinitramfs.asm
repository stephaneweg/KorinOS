; mkinitramfs - les modules de demarrage (docs/MICRO-NOYAU.md, section 4),
; fabriques sous ARMPI : « mkinitramfs [liste] » (par defaut
; SD0:/etc/initramfs.txt : un fichier par ligne, # : commentaire).
; Pour chaque fichier : l'enveloppe de module (64 octets : « ARMPIMOD »,
; taille en-tete compris, nom = le nom du fichier sur 48 octets) puis le
; contenu, ecrite a la racine de SD0: (<nom>.mod : le nom coupe au premier
; point ; s'il est deja pris, les points deviennent des « _ »). Puis la
; ligne « initramfs » de SD0:/config.txt est refaite (l'adresse gardee).
; Le firmware charge ces fichiers au prochain demarrage.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'

MOD_MAX		= 16
NOM_MAX		= 48			; nom dans l'en-tete (0 compris)
FICHIER_MAX	= 4*1024*1024		; contenu d'un module
LISTE_MAX	= 4096
CONFIG_MAX	= 16384
E_NOENT		= -2

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; SD0:/lib/appkit.so

start:
	mov	x19,x0				; argument : la liste
	bl	kits_open
	adr	x20,p_liste
	cbz	x19,.l
	ldrb	w0,[x19]
	cbz	w0,.l
	mov	x20,x19
.l:	mov	x0,x20				; la liste
	adr	x1,liste
	mov	x2,LISTE_MAX-1
	bl	lire_fichier
	tbnz	x0,63,.sans_liste
	adr	x1,liste
	strb	wzr,[x1,x0]
	movl	x0,FICHIER_MAX + 64		; tampon d'un module
	bl	malloc
	cbz	x0,.ko
	adr	x1,tampon
	str	x0,[x1]
	adr	x19,liste			; une ligne par fichier
.ligne:	ldrb	w0,[x19]
	cbz	w0,.config
	mov	x20,x19				; fin de ligne -> 0
.f:	ldrb	w0,[x20]
	cbz	w0,.coupe
	cmp	w0,10
	b.eq	.nl
	cmp	w0,13
	b.eq	.nl
	add	x20,x20,1
	b	.f
.nl:	strb	wzr,[x20],1
.coupe:	mov	x0,x19				; espaces autour
	bl	nettoyer
	ldrb	w1,[x0]
	cbz	w1,.suiv
	cmp	w1,'#'
	b.eq	.suiv
	bl	module
	tbnz	x0,63,.ko
.suiv:	mov	x19,x20
	b	.ligne
.config:
	adr	x0,mods_n
	ldr	x0,[x0]
	cbz	x0,.vide
	bl	config
	tbnz	x0,63,.ko_config
	adr	x0,m_fini
	bl	lang_puts
	mov	x0,0
	sys	SYS_EXIT
.sans_liste:
	adr	x0,m_sans_liste
	bl	lang_puts
	mov	x0,x20
	bl	puts
	adr	x0,m_nl
	bl	puts
	b	.ko
.vide:	adr	x0,m_vide
	bl	lang_puts
	b	.ko
.ko_config:
	adr	x0,m_config_ko
	bl	lang_puts
.ko:	mov	x0,1
	sys	SYS_EXIT

; x0 = chemin d'un fichier : son module a la racine de SD0: -> x0 = 0, < 0
module:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	adr	x0,mods_n
	ldr	x0,[x0]
	cmp	x0,MOD_MAX
	b.hs	.trop
	mov	x0,x19				; le nom : apres le dernier « / »
	mov	x20,x19
.d:	ldrb	w1,[x0],1
	cbz	w1,.nom
	cmp	w1,'/'
	b.ne	.d
	mov	x20,x0
	b	.d
.nom:	adr	x0,tampon			; en-tete : « ARMPIMOD », taille, nom
	ldr	x21,[x0]
	mov	x0,x21
	mov	x1,64
	bl	zero
	adr	x0,s_magic
	ldr	x0,[x0]
	str	x0,[x21]
	add	x0,x21,16
	mov	x1,x20
	mov	x2,NOM_MAX-1
.cn:	ldrb	w3,[x1],1
	cbz	w3,.cnf
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.cn
.cnf:	mov	x0,x19				; le contenu, apres l'en-tete
	add	x1,x21,64
	movl	x2,FICHIER_MAX
	bl	lire_fichier
	tbnz	x0,63,.absent
	add	x22,x0,64			; taille totale
	str	x22,[x21,8]
	mov	x0,x20				; le fichier : SD0:/<nom>.mod
	bl	nom_mod
	adr	x0,chemin
	adr	x1,s_racine
	bl	str_cat
	adr	x1,nom_fichier
	bl	str_cat
	adr	x0,chemin
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.ecr_ko
	mov	x20,x0
	mov	x1,x21
	mov	x2,x22
	sys	SYS_WRITE
	mov	x21,x0
	mov	x0,x20
	sys	SYS_CLOSE
	cmp	x21,x22
	b.ne	.ecr_ko
	adr	x0,m_mod			; « mkinitramfs : SD0:/sys/svc/sd -> sd.mod (4 Ko) »
	bl	lang_puts
	mov	x0,x19
	bl	puts
	adr	x0,m_fleche
	bl	puts
	adr	x0,nom_fichier
	bl	puts
	adr	x0,m_par
	bl	puts
	add	x0,x22,1023
	lsr	x0,x0,10
	bl	print_dec
	adr	x0,m_ko_par
	bl	lang_puts
	adr	x0,mods_n			; retenu pour config.txt
	ldr	x1,[x0]
	add	x2,x1,1
	str	x2,[x0]
	adr	x0,mods
	add	x0,x0,x1,lsl 6
	adr	x1,nom_fichier
	mov	x2,64
	bl	copie
	mov	x0,0
	b	.out
.absent:
	adr	x0,m_absent
	b	.err
.ecr_ko:
	adr	x0,m_ecr_ko
	b	.err
.trop:	adr	x0,m_trop
.err:	bl	lang_puts
	mov	x0,x19
	bl	puts
	adr	x0,m_nl
	bl	puts
	mov	x0,-1
.out:	adr	x1,chemin
	strb	wzr,[x1]
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = nom du module -> nom_fichier = « <nom>.mod » : coupe au premier
; point ; deja pris par un module precedent : les points deviennent des « _ »
nom_mod:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	adr	x0,nom_fichier			; coupe au premier point
	mov	x1,x19
	mov	x2,40
.c:	ldrb	w3,[x1],1
	cbz	w3,.f
	cmp	w3,'.'
	b.eq	.f
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.c
.f:	strb	wzr,[x0]
	bl	pris
	cbz	x0,.ext
	adr	x0,nom_fichier			; pris : le nom entier, « . » -> « _ »
	mov	x1,x19
	mov	x2,40
.c2:	ldrb	w3,[x1],1
	cbz	w3,.f2
	cmp	w3,'.'
	mov	w4,'_'
	csel	w3,w4,w3,eq
	strb	w3,[x0],1
	subs	x2,x2,1
	b.ne	.c2
.f2:	strb	wzr,[x0]
.ext:	adr	x0,nom_fichier
	bl	str_len
	adr	x1,nom_fichier
	add	x0,x1,x0
	adr	x1,s_mod
	bl	str_cat
	ldp	x19,x20,[sp],16
	epilogue

; -> x0 = 1 si « nom_fichier.mod » est deja le fichier d'un module precedent
pris:
	prologue
	stp	x19,x20,[sp,-16]!
	sub	sp,sp,64
	mov	x0,sp
	adr	x1,nom_fichier
	bl	str_cat
	adr	x1,s_mod
	bl	str_cat
	adr	x19,mods
	adr	x0,mods_n
	ldr	x20,[x0]
.s:	cbz	x20,.non
	mov	x0,x19
	mov	x1,sp
	bl	str_eq
	cbnz	x0,.oui
	add	x19,x19,64
	sub	x20,x20,1
	b	.s
.oui:	mov	x0,1
	b	.out
.non:	mov	x0,0
.out:	add	sp,sp,64
	ldp	x19,x20,[sp],16
	epilogue

; SD0:/config.txt : la ligne « initramfs ... <adresse> » refaite avec les
; modules ecrits (l'adresse gardee ; sans ligne : ajoutee avec 0x02000000)
; -> x0 = 0, < 0 si erreur
config:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	adr	x0,p_config
	adr	x1,config_in
	mov	x2,CONFIG_MAX-1
	bl	lire_fichier
	tbnz	x0,63,.out
	adr	x1,config_in
	strb	wzr,[x1,x0]
	adr	x21,config_out			; recopie, ligne par ligne
	adr	x19,config_in
	mov	x22,0				; ligne initramfs trouvee
.ligne:	ldrb	w0,[x19]
	cbz	w0,.fin
	mov	x20,x19				; fin de ligne (CR LF compris)
.e:	ldrb	w0,[x20]
	cbz	w0,.l1
	add	x20,x20,1
	cmp	w0,10
	b.ne	.e
.l1:	mov	x0,x19				; « initramfs » en debut de ligne ?
	adr	x1,s_initramfs
	bl	commence
	cbz	x0,.copie
	cbnz	x22,.saute			; (une seule)
	mov	x22,1
	mov	x0,x19				; l'adresse : le dernier mot de la ligne
	mov	x1,x20
	bl	adresse
	mov	x1,x0
	mov	x0,x21
	bl	ligne_initramfs
	mov	x21,x0
	b	.saute
.copie:	ldrb	w0,[x19],1			; la ligne telle quelle
	strb	w0,[x21],1
	cmp	x19,x20
	b.lo	.copie
	b	.ligne
.saute:	mov	x19,x20
	b	.ligne
.fin:	cbnz	x22,.ecrire			; pas de ligne : a la fin
	adr	x1,s_adresse
	mov	x0,x21
	bl	ligne_initramfs
	mov	x21,x0
.ecrire:
	adr	x0,p_config
	mov	x1,O_WRITE or O_CREATE or O_TRUNC
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	adr	x1,config_out
	sub	x2,x21,x1
	sys	SYS_WRITE
	mov	x20,x0
	mov	x0,x19
	sys	SYS_CLOSE
	mov	x0,x20
	tbnz	x0,63,.out
	adr	x0,m_config
	bl	lang_puts
	mov	x0,0
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = destination, x1 = adresse (chaine, 0 final) : « initramfs a.mod,b.mod
; <adresse> » CR LF -> x0 = fin
ligne_initramfs:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x21,x1
	adr	x1,s_initramfs
	bl	str_cat
	mov	w1,' '
	strb	w1,[x0],1
	adr	x19,mods
	adr	x1,mods_n
	ldr	x20,[x1]
.m:	mov	x1,x19
	bl	str_cat
	add	x19,x19,64
	subs	x20,x20,1
	b.eq	.adr
	mov	w1,','
	strb	w1,[x0],1
	b	.m
.adr:	mov	w1,' '
	strb	w1,[x0],1
	mov	x1,x21
	bl	str_cat
	mov	w1,13
	strb	w1,[x0],1
	mov	w1,10
	strb	w1,[x0],1
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = debut de ligne, x1 = fin : le dernier mot (l'adresse) copie dans
; adresse_lue -> x0 = adresse_lue (« 0x02000000 » si la ligne n'en a pas)
adresse:
	sub	x2,x1,1				; sans CR LF ni espaces de fin
.fin:	cmp	x2,x0
	b.lo	.defaut
	ldrb	w3,[x2]
	cmp	w3,' '
	b.hi	.mot
	sub	x2,x2,1
	b	.fin
.mot:	mov	x4,x2				; debut du mot
.deb:	cmp	x4,x0
	b.ls	.copie
	ldrb	w3,[x4,-1]
	cmp	w3,' '
	b.ls	.copie
	sub	x4,x4,1
	b	.deb
.copie:	ldrb	w3,[x4]				; un nombre (« 0x... ») ?
	cmp	w3,'0'
	b.ne	.defaut
	adr	x5,adresse_lue
	mov	x6,x5
.c:	cmp	x4,x2
	b.hi	.f
	ldrb	w3,[x4],1
	strb	w3,[x6],1
	b	.c
.f:	strb	wzr,[x6]
	mov	x0,x5
	ret
.defaut:
	adr	x0,s_adresse
	ret

; x0 = texte, x1 = mot -> x0 = 1 si le texte commence par le mot suivi d'un
; espace
commence:
.c:	ldrb	w2,[x1],1
	cbz	w2,.fin
	ldrb	w3,[x0],1
	cmp	w2,w3
	b.eq	.c
	mov	x0,0
	ret
.fin:	ldrb	w3,[x0]
	cmp	w3,' '
	cset	x0,eq
	ret

; x0 = chemin, x1 = tampon, x2 = taille -> x0 = octets lus, < 0 si erreur
lire_fichier:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x20,x1
	mov	x21,x2
	mov	x1,O_READ
	sys	SYS_OPEN
	tbnz	x0,63,.out
	mov	x19,x0
	mov	x22,0				; lus
.r:	cmp	x22,x21
	b.hs	.fin
	mov	x0,x19
	add	x1,x20,x22
	sub	x2,x21,x22
	sys	SYS_READ
	cmp	x0,0
	b.le	.fin
	add	x22,x22,x0
	b	.r
.fin:	mov	x0,x19
	sys	SYS_CLOSE
	mov	x0,x22
.out:	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x0 = chaine : sans espaces (ni tabulations, ni CR) devant ni derriere -> x0
nettoyer:
.d:	ldrb	w1,[x0]
	cmp	w1,' '
	b.eq	.av
	cmp	w1,9
	b.ne	.der
.av:	add	x0,x0,1
	b	.d
.der:	mov	x1,x0
.e:	ldrb	w2,[x1]
	cbz	w2,.r
	add	x1,x1,1
	b	.e
.r:	cmp	x1,x0
	b.ls	.ok
	ldrb	w2,[x1,-1]
	cmp	w2,' '
	b.hi	.ok
	sub	x1,x1,1
	strb	wzr,[x1]
	b	.r
.ok:	ret

; x0 = destination, x1 = octets
zero:
	cbz	x1,.f
	strb	wzr,[x0],1
	sub	x1,x1,1
	b	zero
.f:	ret

; x0 = destination, x1 = source, x2 = taille (0 compris)
copie:
	sub	x2,x2,1
.c:	cbz	x2,.f
	ldrb	w3,[x1],1
	strb	w3,[x0],1
	cbz	w3,.r
	sub	x2,x2,1
	b	.c
.f:	strb	wzr,[x0]
.r:	ret

s_magic		db 'ARMPIMOD'
p_liste		db 'SD0:/etc/initramfs.txt',0
p_config	db 'SD0:/config.txt',0
s_racine	db 'SD0:/',0
s_mod		db '.mod',0
s_initramfs	db 'initramfs',0
s_adresse	db '0x02000000',0
m_mod		db 'mkinitramfs : ',0
m_fleche	db ' -> ',0
m_par		db ' (',0
m_ko_par	db ' Ko)',10,0
m_nl		db 10,0
m_absent	db 'mkinitramfs : fichier illisible : ',0
m_ecr_ko	db 'mkinitramfs : ecriture impossible : ',0
m_trop		db 'mkinitramfs : trop de modules, ignore : ',0
m_sans_liste	db 'mkinitramfs : liste illisible : ',0
m_vide		db 'mkinitramfs : liste vide, config.txt inchange',10,0
m_config	db 'mkinitramfs : SD0:/config.txt a jour (ligne initramfs)',10,0
m_config_ko	db 'mkinitramfs : SD0:/config.txt illisible ou non ecrit',10,0
m_fini		db 'mkinitramfs : pret pour le prochain demarrage',10,0
		align 8

segment readable writeable
kit_data
		align 8
tampon		dd 0
mods_n		dd 0
mods		rb MOD_MAX * 64			; noms des fichiers ecrits
chemin		rb 128
nom_fichier	rb 64
adresse_lue	rb 32
liste		rb LISTE_MAX
config_in	rb CONFIG_MAX
config_out	rb CONFIG_MAX + 1024
