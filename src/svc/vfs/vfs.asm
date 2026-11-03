; vfs - le service des volumes (docs/NOYAU-HYBRIDE.md, section 4), lance par
; SD0:/etc/services.txt. Il tient la table des volumes montes en espace
; utilisateur (RAM0:...) : pour chacun, le service de fichiers qui le sert.
;  * Les programmes (AppKit) lui envoient leurs chemins (ops 30-36) ; il les
;    transmet au service du volume avec le chemin relatif (« a/b ») ; a
;    l'ouverture, il ajoute le canal de ce service a la reponse (AppKit lui
;    enverra lire / ecrire). Le relais du noyau (ops 1-4) n'existe plus
;    (docs/FICHIERS.md, etape 5).
;  * Monter : un volume, une source (« ram:16M »), un service (« ramfs ») ;
;    vfs lance SD0:/sys/svc/<service> « <volume> <source> », qui s'enregistre
;    sous « fs.<volume> ». Au demarrage : SD0:/etc/mounts.txt
;    (« RAM0 = ram:16M, ramfs ») ; ensuite : la requete 20 (monter).
;  * Un service de fichiers qui s'arrete : son volume disparait.
;  * Mode services (lance par init, initramfs) : un volume en arguments
;    (« SD0 blk.sd0p1 fatfs ») est monte d'abord ; un service de demarrage
;    est pris dans initramfs (mod_lancer), les autres sur le disque ; les
;    montages de mounts.txt se font dans un thread, pendant que vfs sert
;    deja SD0: (leurs services se chargent depuis SD0:).
;  * Chemins des programmes (docs/FICHIERS.md, etape 1 ; path.inc) :
;    ops 30-36 (ouvrir, stat, mkdir, unlink, chdir, getcwd, exec) au nom du
;    pid appelant, avec son dossier courant ; avis de naissance (0xFFFE) :
;    le fils recoit le dossier du pere ; avis de fin : il est oublie.
;  * Tubes et entrees / sorties standard (docs/FICHIERS.md, etape 4 ;
;    pipes.inc) : ops 37-41 ; l'avis de naissance porte les entrees /
;    sorties que le pere donne au fils. Une lecture d'un tube vide peut
;    attendre : sa reponse est differee (svc_next a echeance courte).
;  * Lancement depuis le thread principal (monter : un service hors de
;    initramfs) : resolution locale (ops_chemins sur msg, sauve) puis
;    f_spawn_file d'AppKit : vfs ne s'envoie jamais de requete a lui-meme.
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../inc/fs.inc'

VOL_MAX	= 8
VOL_NOM	= 0				; nom en minuscules (16 octets)
VOL_CANAL = 16
VOL_PID	= 24
VOL_SRC	= 32				; source (� blk.usb0p1 �, � ram:16M � : 24 o)
VOL_FS	= 56				; service (� fatfs � : 16 o)
VOL_SIZE = 72
VE_TAILLE = 56				; VFS_VOLUMES : nom 16, source 24, service 16

segment readable executable
kit_use '../../kits/appkit/appkit.abi'		; AppKit (initramfs en mode services)
include '../../inc/applist.inc'		; ini_read, ini_field

start:
	mov	x19,x0				; (mode services : « SD0 blk.sd0p1 fat »)
	bl	kits_open
	adr	x0,s_vfs
	mov	x1,SVF_FINS or SVF_NAISSANCES
	sys	SYS_SVC_REGISTER
	tbnz	x0,63,quit
	adr	x1,canal
	str	x0,[x1]
	sys	SYS_GETPID
	adr	x1,own_pid
	str	x0,[x1]
	cbz	x19,.montages			; d'abord le volume donne (SD0:)
	mov	x0,x19
	bl	monter_ligne
	mov	x20,x0
	adr	x0,m_vfs
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_fleche
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_nl
	bl	puts
.montages:					; mounts.txt : dans un thread (ses
	mov	x0,16384			; services se chargent depuis un
	bl	malloc				; volume que vfs doit servir)
	cbz	x0,.loop
	add	x1,x0,16384
	adr	x0,montages_thread
	mov	x2,0
	sys	SYS_THREAD_CREATE
.loop:	bl	waits_tick			; lectures de tubes trop longues
	bl	waits_any
	mov	x2,60000
	cmp	x0,0
	mov	x1,500
	csel	x2,x1,x2,ne
	adr	x0,canal
	ldr	x0,[x0]
	adr	x1,msg
	sys	SYS_SVC_NEXT
	tbnz	x0,63,quit
	cbz	x0,.loop
	mov	x19,x0				; numero de la requete
	adr	x20,msg
	ldr	x0,[x20]
	movl	x1,SVC_OP_FIN
	cmp	x0,x1
	b.eq	.fin
	movl	x1,SVC_OP_NAISSANCE
	cmp	x0,x1
	b.eq	.naissance
	cmp	x0,V_PIPE			; tubes, entrees / sorties
	b.lo	.chemins
	cmp	x0,V_STDIO
	b.hi	.chemins
	adr	x1,current_req
	str	x19,[x1]
	bl	ops_pipes
	cbnz	x1,.loop			; reponse differee
	str	x0,[x20]
	b	.reply
.chemins:
	cmp	x0,V_OUVRIR			; chemins des programmes
	b.lo	.autres
	cmp	x0,V_EXEC
	b.hi	.autres
	bl	ops_chemins
	str	x0,[x20]
	b	.reply
.autres:
	cmp	x0,V_FILE_END
	b.eq	.bouts
	cmp	x0,V_END_FILE
	b.eq	.bouts
	cmp	x0,V_UTIME
	b.eq	.utime
	cmp	x0,V_RENAME
	b.eq	.utime
	cmp	x0,VFS_MONTER
	b.eq	.monter
	cmp	x0,VFS_DEMONTER
	b.eq	.demonter
	cmp	x0,VFS_PATHMAP
	b.eq	.pathmap
	cmp	x0,VFS_PATHQ
	b.eq	.pathq
	cmp	x0,VFS_VOLUMES
	b.ne	.inval
	bl	lister_volumes
	str	x0,[x20]
	b	.reply
.bouts:	bl	ops_bouts
	str	x0,[x20]
	b	.reply
.utime:	bl	ops_chemins
	str	x0,[x20]
	b	.reply
.pathmap:
	bl	chemin_ajouter
	str	x0,[x20]
	b	.reply
.pathq:	strb	wzr,[x20,MSG_TAILLE-1]
	add	x0,x20,MSG_DATA
	bl	chemin_virtuel
	cmp	x0,0
	cset	x0,ne
	str	x0,[x20]
	b	.reply
.demonter:					; donnees : � vol � (minuscules)
	add	x0,x20,MSG_DATA
	bl	volume_de
	mov	x1,E_NOENT
	cbz	x0,.d_rep
	ldr	x0,[x0,VOL_PID]			; son service s'arrete : le volume
	sys	SYS_KILL			; part a l'avis de fin
	mov	x1,0
.d_rep:	str	x1,[x20]
	b	.reply
.monter:
	add	x0,x20,MSG_DATA			; « volume source service »
	bl	monter_ligne
	str	x0,[x20]
	b	.reply
.inval:	mov	x0,E_INVAL
	str	x0,[x20]
.reply:	mov	x0,x19
	adr	x1,msg
	sys	SYS_SVC_REPLY
	b	.loop
.fin:	ldr	x0,[x20,MSG_A]			; un process termine : un service de
	bl	oublier_pid			; fichiers ? son volume disparait
	ldr	x0,[x20,MSG_A]			; son dossier courant est oublie
	bl	oublier_dossier
	ldr	x0,[x20,MSG_A]			; ses bouts de tubes sont fermes
	bl	ends_pid_gone
	b	.loop
.naissance:					; le fils recoit le dossier du pere
	ldp	x0,x1,[x20,MSG_A]
	bl	naissance
	ldp	x0,x1,[x20,MSG_A]		; et ses entrees / sorties
	ldp	x2,x3,[x20,MSG_C]
	ldr	x4,[x20,MSG_DATA]
	bl	stdio_birth
	b	.loop
quit:	mov	x0,1				; (sur une erreur : init2 relance)
	sys	SYS_EXIT

; (thread) les montages permanents de SD0:/etc/mounts.txt
montages_thread:
	adr	x0,p_mounts
	adr	x1,montage_ligne
	bl	ini_read
	sys	SYS_THREAD_EXIT

; x0 = « vol/reste » ou « vol » -> x0 = entree du volume (0 : aucun),
; x1 = reste (sans « / » initial)
volume_de:
	adr	x2,volumes
	mov	x3,0
.v:	cmp	x3,VOL_MAX
	b.hs	.none
	ldr	x4,[x2,VOL_CANAL]
	tbnz	x4,63,.n
	ldrb	w4,[x2]
	cbz	w4,.n
	mov	x5,0
.c:	ldrb	w4,[x2,x5]
	ldrb	w6,[x0,x5]
	cbz	w4,.fin_nom
	cmp	w4,w6
	b.ne	.n
	add	x5,x5,1
	b	.c
.fin_nom:
	cmp	w6,'/'
	b.eq	.sep
	cbnz	w6,.n
	add	x1,x0,x5			; « vol » seul : racine
	mov	x0,x2
	ret
.sep:	add	x1,x0,x5
	add	x1,x1,1
	mov	x0,x2
	ret
.n:	add	x2,x2,VOL_SIZE
	add	x3,x3,1
	b	.v
.none:	mov	x0,0
	ret

; (ini_read de mounts.txt) x0 = volume (minuscules), x1 = « source, service »
montage_ligne:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	mov	x0,x1
	bl	ini_field			; x0 = source, x1 = service
	cbz	x1,.out
	mov	x20,x0
	mov	x0,x1
	bl	ini_field
	mov	x2,x0
	mov	x0,x19
	mov	x1,x20
	mov	x3,0				; (thread des montages)
	bl	monter
	mov	x20,x0				; au journal : « vfs : ram0 -> 0 »
	adr	x0,m_vfs
	bl	puts
	mov	x0,x19
	bl	puts
	adr	x0,m_fleche
	bl	puts
	mov	x0,x20
	bl	print_dec
	adr	x0,m_nl
	bl	puts
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = « volume source service » (modifiee) -> x0 = 0 ou erreur
monter_ligne:
	prologue
	stp	x19,x20,[sp,-16]!
	mov	x19,x0
	bl	mot_suivant
	cbz	x0,.inval
	mov	x20,x0
	bl	mot_suivant
	cbz	x0,.inval
	mov	x2,x0
	mov	x0,x19
	mov	x1,x20
	mov	x3,1				; (thread principal)
	bl	monter
	b	.out
.inval:	mov	x0,E_INVAL
.out:	ldp	x19,x20,[sp],16
	epilogue

; x0 = mot -> coupe au premier espace ; x0 = mot suivant (0 : aucun)
mot_suivant:
.c:	ldrb	w1,[x0]
	cbz	w1,.none
	cmp	w1,' '
	b.eq	.cut
	add	x0,x0,1
	b	.c
.cut:	strb	wzr,[x0],1
.sp:	ldrb	w1,[x0]
	cmp	w1,' '
	b.ne	.got
	add	x0,x0,1
	b	.sp
.got:	cbz	w1,.none
	ret
.none:	mov	x0,0
	ret

; x0 = volume (« RAM0 » ou « ram0 », « : » final permis), x1 = source,
; x2 = service : lance SD0:/sys/svc/<service> « <volume> <source> » et
; l'inscrit ; x3 = 1 : appele par le thread principal (lancement local)
; -> x0 = 0 ou erreur
monter:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	stp	x23,x24,[sp,-16]!
	mov	x23,x3
	mov	x20,x1
	mov	x21,x2
	adr	x1,nom_vol			; nom en minuscules, sans « : »
	mov	x2,0
.n:	ldrb	w3,[x0,x2]
	cbz	w3,.nf
	cmp	w3,':'
	b.eq	.nf
	cmp	x2,14
	b.hs	.nf
	sub	w4,w3,'A'
	cmp	w4,25
	b.hi	.lc
	add	w3,w3,32
.lc:	strb	w3,[x1,x2]
	add	x2,x2,1
	b	.n
.nf:	strb	wzr,[x1,x2]
	cbz	x2,.inval
	adr	x0,nom_vol			; deja monte ?
	bl	volume_de
	cbnz	x0,.exist
	adr	x0,chemin			; SD0:/sys/svc/<service>
	adr	x1,p_svc
	bl	str_cat
	mov	x1,x21
	bl	str_cat
	adr	x0,args				; « <volume> <source> »
	adr	x1,nom_vol
	bl	str_cat
	adr	x1,s_esp
	bl	str_cat
	mov	x1,x20
	bl	str_cat
	mov	x0,x21			; un module de initramfs (services
	adr	x1,args				; de demarrage), sinon le disque
	sys	SYS_MOD_LANCER
	mov	x22,x0
	tbz	x0,63,.lance
	adr	x0,chemin
	adr	x1,args
	cbz	x23,.thread
	bl	spawn_local			; (le thread principal sert les
	b	.spawned			; requetes : pas de requete a soi)
.thread:
	sys	SYS_SPAWN
.spawned:
	mov	x22,x0
.lance:
	adr	x0,chemin			; (tampons rendus vides)
	strb	wzr,[x0]
	adr	x0,args
	strb	wzr,[x0]
	tbnz	x22,63,.err
	adr	x0,nom_svc			; « fs.<volume> »
	adr	x1,s_fs
	bl	str_cat
	adr	x1,nom_vol
	bl	str_cat
	adr	x0,nom_svc
	mov	x1,3000
	sys	SYS_SVC_OPEN
	adr	x1,nom_svc
	strb	wzr,[x1]
	tbnz	x0,63,.err
	mov	x19,x0
	adr	x0,volumes			; place libre
	mov	x1,0
.p:	cmp	x1,VOL_MAX
	b.hs	.plein
	ldrb	w2,[x0]
	cbz	w2,.place
	add	x0,x0,VOL_SIZE
	add	x1,x1,1
	b	.p
.place:	str	x19,[x0,VOL_CANAL]
	str	x22,[x0,VOL_PID]
	adr	x1,nom_vol
	mov	x2,0
.cp:	ldrb	w3,[x1,x2]
	strb	w3,[x0,x2]
	add	x2,x2,1
	cbnz	w3,.cp
	add	x1,x0,VOL_SRC			; source et service (pour VFS_VOLUMES)
	mov	x2,x20
	mov	x3,23
	bl	copie_bornee
	add	x1,x0,VOL_FS
	mov	x2,x21
	mov	x3,15
	bl	copie_bornee
	mov	x0,0
	b	.out
.plein:	mov	x0,E_NOMEM
	b	.out
.exist:	mov	x0,E_EXIST
	b	.out
.inval:	mov	x0,E_INVAL
	b	.out
.err:	mov	x0,E_NOENT
.out:	ldp	x23,x24,[sp],16
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; (thread principal) x0 = chemin, x1 = arguments : lance le programme sans
; requete a vfs lui-meme : V_EXEC resolu ici (ops_chemins, au nom de vfs ;
; msg est sauve, il peut tenir la requete en cours), puis f_spawn_file
; d'AppKit (lecture directe au service FS, spawn_image) -> pid ou erreur
spawn_local:
	prologue
	stp	x19,x20,[sp,-16]!
	stp	x21,x22,[sp,-16]!
	mov	x19,x0
	mov	x20,x1
	adr	x21,msg
	adr	x0,msg_save
	mov	x1,x21
	mov	x2,MSG_TAILLE
	bl	mem_move
	mov	x0,V_EXEC
	str	x0,[x21]
	adr	x0,own_pid
	ldr	x0,[x0]
	str	w0,[x21,MSG_PID]
	add	x0,x21,MSG_DATA
	strb	wzr,[x0]
	mov	x1,x19
	bl	str_cat
	bl	ops_chemins
	mov	x22,x0
	tbnz	x0,63,.rest
	mov	x0,x21
	mov	x1,x20
	mov	x2,0				; (entrees / sorties de vfs)
	bl	f_spawn_file
	mov	x22,x0
.rest:	mov	x0,x21
	adr	x1,msg_save
	mov	x2,MSG_TAILLE
	bl	mem_move
	mov	x0,x22
	ldp	x21,x22,[sp],16
	ldp	x19,x20,[sp],16
	epilogue

; x1 = destination, x2 = source, x3 = octets au plus (sans le 0) ; x0 garde
copie_bornee:
.c:	cbz	x3,.f
	ldrb	w4,[x2],1
	cbz	w4,.f
	strb	w4,[x1],1
	sub	x3,x3,1
	b	.c
.f:	strb	wzr,[x1]
	ret

; (msg) VFS_VOLUMES : les volumes montes -> b = nombre ; donnees : par
; volume, nom (16 o, minuscules), source (24 o), service (16 o)
lister_volumes:
	adr	x1,volumes
	adr	x5,msg
	add	x6,x5,MSG_DATA
	mov	x2,0				; volume
	mov	x7,0				; nombre
.v:	cmp	x2,VOL_MAX
	b.hs	.fin
	ldrb	w3,[x1]
	cbz	w3,.n
	mov	x3,0				; nom (16), source et service (40)
.c1:	ldrb	w4,[x1,x3]
	strb	w4,[x6,x3]
	add	x3,x3,1
	cmp	x3,16
	b.lo	.c1
	mov	x3,0
	add	x8,x1,VOL_SRC
	add	x9,x6,16
.c2:	ldrb	w4,[x8,x3]
	strb	w4,[x9,x3]
	add	x3,x3,1
	cmp	x3,40
	b.lo	.c2
	add	x6,x6,VE_TAILLE
	add	x7,x7,1
.n:	add	x1,x1,VOL_SIZE
	add	x2,x2,1
	b	.v
.fin:	str	x7,[x5,MSG_B]
	mov	x0,0
	ret

; x0 = pid termine : retire le volume qu'il servait
oublier_pid:
	adr	x1,volumes
	mov	x2,0
.v:	cmp	x2,VOL_MAX
	b.hs	.out
	ldrb	w3,[x1]
	cbz	w3,.n
	ldr	x3,[x1,VOL_PID]
	cmp	x3,x0
	b.ne	.n
	strb	wzr,[x1]
.n:	add	x1,x1,VOL_SIZE
	add	x2,x2,1
	b	.v
.out:	ret

include 'path.inc'
include 'pipes.inc'

s_vfs		db 'vfs',0
s_fs		db 'fs.',0
s_esp		db ' ',0
p_svc		db 'SD0:/sys/svc/',0
p_mounts	db 'SD0:/etc/mounts.txt',0
m_vfs		db 'vfs : montage ',0
m_fleche	db ' -> ',0
m_nl		db 10,0
		align 8

segment readable writeable
kit_data
applis_data
chemins_data
		align 8
canal		dd 0
own_pid		dd 0
volumes		rb VOL_MAX * VOL_SIZE
nom_vol		rb 16
nom_svc		rb 32
chemin		rb 64
args		rb 96
msg		rb MSG_TAILLE
pipes_data
